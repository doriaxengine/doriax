#version 450

// Screen-space reflections (linear ray-march + binary refinement).
// Reconstructs the view-space position from the G-buffer packed depth
// (u_depthTexture) and reads the view-space surface normal from the G-buffer
// (u_gbufferTexture), reflects the view vector about the normal, marches the
// reflected ray in view space and samples the offscreen scene color where the
// ray hits screen geometry.
// Output: rgb = reflected color, a = reflection coverage/mask in [0,1]. The
// physical reflectance (Fresnel / GGX BRDF) is applied later by the composite.
//
// Orientation: the G-buffer keeps the target's own orientation while the scene
// color was rendered flipped into the offscreen, so on GL the two differ by a Y
// flip (misc.z). This fragment writes the SSR buffer in scene-color (composite)
// space: the G-buffer coordinate for this fragment is the flipped texcoord, and a
// hit's scene color is sampled back in scene-color space.

#ifndef SSR_MAX_STEPS
#define SSR_MAX_STEPS 128
#endif

#define SSR_BINARY_STEPS 6
#define SSR_MAX_ROUGHNESS 0.6 // rougher surfaces are left to the IBL

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;
uniform texture2D u_gbufferTexture;
uniform sampler u_gbuffer_smp;
uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;

uniform u_fs_ssrParams {
    mat4 projection;
    mat4 invProjection;
    vec4 params;     // x = maxDistance, y = thickness, z = intensity, w = maxSteps
    vec4 misc;       // xy = 1/depthTextureSize, z = flip depth<->color Y, w = glossy-blur amount
} ssr;

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"

float sampleDepth(vec2 uv){
    // Explicit LOD keeps the D3D11 ray-march loop dynamic.
    return decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0));
}

// No per-backend depth range here: the G-buffer packs 0.5*z/w + 0.5 taken before
// the API clip-space fixup, so [-1,1] always matches invProjection. Y does need
// the per-backend mapping, see depth_util.glsl.
vec3 reconstructViewPos(vec2 uv, float depth01){
    vec3 ndc = vec3(uv.x * 2.0 - 1.0, depthUVToNDCY(uv.y), depth01 * 2.0 - 1.0);
    vec4 view = ssr.invProjection * vec4(ndc, 1.0);
    return view.xyz / view.w;
}

// project a view-space position to depth-space uv [0,1]; w<=0 => behind camera
bool projectToUV(vec3 viewPos, out vec2 uv){
    vec4 clip = ssr.projection * vec4(viewPos, 1.0);
    if (clip.w <= 0.0) return false;
    vec2 ndc = clip.xy / clip.w;
    uv = vec2(ndc.x * 0.5 + 0.5, ndcYToDepthUV(ndc.y));
    return true;
}

void main(){
    bool flip = ssr.misc.z > 0.5;

    // depth-space coordinate for this (scene-color space) fragment
    vec2 d = v_texcoord;
    if (flip) d.y = 1.0 - d.y;

    float depth01 = sampleDepth(d);
    if (depth01 >= 1.0){ // background
        frag_color = vec4(0.0);
        return;
    }

    vec3 P = reconstructViewPos(d, depth01);

    // View-space surface normal from the G-buffer (octahedral .rg). This is the real
    // geometric/shading normal of the surface rather than one reconstructed from the
    // depth gradient, so reflections follow curvature and detail correctly.
    vec4 g = texture(sampler2D(u_gbufferTexture, u_gbuffer_smp), d);
    vec3 n = octDecode(g.rg);
    if (dot(n, P) > 0.0) n = -n; // orient toward the camera

    float roughness = g.b;
    if (roughness >= SSR_MAX_ROUGHNESS){ // too rough
        frag_color = vec4(0.0);
        return;
    }

    // view-space reflection ray (camera at origin looking down -Z)
    vec3 V = normalize(P);
    vec3 R = normalize(reflect(V, n));

    float maxDistance = ssr.params.x;
    float thickness = ssr.params.y;
    int steps = int(ssr.params.w);
    float stepLen = maxDistance / float(max(steps, 1));

    // Jitter the ray start within one step to break up the visible stepping/banding
    // of the linear march. Scaled by misc.w (the glossy-blur amount) so sharp mirror
    // reflections (blur = 0) are left byte-identical and the dither is only added in
    // glossy mode, where the blur pass smooths it out.
    float jitterAmt = ssr.misc.w;
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float startOffset = stepLen * ign * jitterAmt;

    bool hit = false;
    vec2 hitUV = vec2(0.0);
    float hitTravel = 0.0;

    vec3 prevPos = P;
    vec3 rayPos = P + R * startOffset;
    for (int i = 0; i < SSR_MAX_STEPS; ++i){
        if (i >= steps) break;
        prevPos = rayPos;
        rayPos += R * stepLen;

        if (rayPos.z >= 0.0) break; // ray went behind the camera

        vec2 uv;
        if (!projectToUV(rayPos, uv)) break;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sd = sampleDepth(uv);
        if (sd >= 1.0) continue; // background, keep marching

        vec3 storedPos = reconstructViewPos(uv, sd);
        float delta = storedPos.z - rayPos.z; // >0: ray is behind the stored surface

        if (delta > 0.0 && delta < thickness){
            // binary search between prevPos (in front) and rayPos (behind) to
            // refine the intersection
            vec3 a = prevPos;
            vec3 b = rayPos;
            for (int j = 0; j < SSR_BINARY_STEPS; ++j){
                vec3 mid = (a + b) * 0.5;
                vec2 muv;
                if (!projectToUV(mid, muv)){ break; }
                float md = sampleDepth(muv);
                vec3 ms = reconstructViewPos(muv, md);
                if (ms.z - mid.z > 0.0) b = mid; else a = mid;
                uv = muv;
            }
            hitUV = uv;
            hitTravel = length(((a + b) * 0.5) - P);
            hit = true;
            break;
        }
    }

    if (!hit){
        frag_color = vec4(0.0);
        return;
    }

    // sample the scene color in composite (scene-color) space
    vec2 cuv = hitUV;
    if (flip) cuv.y = 1.0 - cuv.y;
    vec3 reflColor = texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), cuv).rgb;

    // --- confidence / coverage mask ---
    // The mask is COVERAGE only (no Fresnel): the composite applies the physically
    // correct reflectance (the GGX environment BRDF, which includes metal F0) when it
    // blends this reflection over the IBL specular. Baking Fresnel here too would
    // double-count it and under-weight metals.
    //
    // screen-edge fade so reflections vanish smoothly at the borders
    vec2 fadeMin = smoothstep(vec2(0.0), vec2(0.15), hitUV);
    vec2 fadeMax = vec2(1.0) - smoothstep(vec2(0.85), vec2(1.0), hitUV);
    float edgeFade = fadeMin.x * fadeMin.y * fadeMax.x * fadeMax.y;

    // ray-length fade: confidence drops as the ray travels further
    float distFade = 1.0 - smoothstep(0.0, maxDistance, hitTravel);

    // roughness fade: hands rough surfaces over to the IBL
    float roughnessFade = 1.0 - smoothstep(0.5 * SSR_MAX_ROUGHNESS, SSR_MAX_ROUGHNESS, roughness);

    float mask = edgeFade * distFade * roughnessFade;

    frag_color = vec4(reflColor, mask);
}
