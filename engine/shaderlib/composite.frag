#version 450

// Composites the offscreen scene color with the screen-space reflection buffer
// and writes the result to the real render target.
//
// SSR replaces the environment specular already in the scene color instead of adding
// to it: this pass rebuilds that term from the G-buffer (the sky, or the object's local
// probe blended over it) and swaps it for the reflection by the SSR confidence. Both are
// occluded as the color pass does it, by the material AO in color[3] and by SSAO.
//
// G-buffer albedo.a holds the IBL source as an 8-bit code (renderGBufferPass):
//   0 = no IBL, 1 = sky, 2 = a probe without a slot here (SSR adds on top of both),
//   3 + slot * 63 + (q - 1) = probe in `slot`, blended over the sky by q / 63.
//
// A fullscreen pass fragment at v_texcoord t writes destination texel t and samples
// scene/SSR (composite space) at t, flipped by params.w when the destination is the
// GL swapchain (bottom-up) and the sources are not. The G-buffer is in depth space,
// sampled at the flipped composite coordinate when params.y says the spaces differ.

// Must match SSR_PROBE_SLOTS in RenderSystem.h.
#define SSR_PROBE_SLOTS 4

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;
uniform texture2D u_ssrTexture;
uniform sampler u_ssr_smp;
uniform texture2D u_depthTexture;     // G-buffer color[0]: packed depth
uniform sampler u_depth_smp;
uniform texture2D u_gbufferTexture;   // G-buffer color[1]: oct normal + roughness + metallic
uniform sampler u_gbuffer_smp;
uniform texture2D u_albedoTexture;    // G-buffer color[2]: base color + IBL source code
uniform sampler u_albedo_smp;
uniform textureCube u_GGXEnvTexture;  // prefiltered environment (GGX)
uniform sampler u_GGXEnv_smp;
uniform texture2D u_ssaoTexture;      // screen-space AO the color pass applied (white when off)
uniform sampler u_ssao_smp;
uniform texture2D u_occlusionGBufferTexture; // G-buffer color[3]: material (baked) occlusion
uniform sampler u_occlusionGBuffer_smp;
uniform textureCube u_probeTexture0;  // local reflection probes, by slot
uniform sampler u_probe0_smp;
uniform textureCube u_probeTexture1;
uniform sampler u_probe1_smp;
uniform textureCube u_probeTexture2;
uniform sampler u_probe2_smp;
uniform textureCube u_probeTexture3;
uniform sampler u_probe3_smp;

uniform u_fs_compositeParams {
    mat4 invProjection;
    mat4 invView;
    vec4 params;    // x = intensity, y = flip G-buffer Y, z = debug, w = flip destination Y
    vec4 envColor;  // rgb = env color (linear), w = env rotation (radians)
    vec4 probePosition[SSR_PROBE_SLOTS]; // xyz = capture position
    vec4 probeBoxMin[SSR_PROBE_SLOTS];   // xyz = influence AABB min, w = intensity
    vec4 probeBoxMax[SSR_PROBE_SLOTS];   // xyz = influence AABB max, w = max mip LOD (0 = unfiltered)
} comp;

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"
#include "includes/srgb.glsl"

// Must match IBLEnvironment::SPECULAR_MIPS - 1 and ibl.glsl's ENV_GGX_LODS.
const float ENV_GGX_LODS = 7.0;

// analytic GGX environment BRDF approximation (copy of ibl.glsl envBRDFApprox)
vec2 envBRDFApprox(float NdotV, float roughness){
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 rotateEnv(vec3 dir, float angle){
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
}

// See ssr.frag: the packed depth is backend-independent, so no depth-range branch,
// but Y still needs the per-backend mapping.
vec3 reconstructViewPos(vec2 uv, float depth01){
    vec3 ndc = vec3(uv.x * 2.0 - 1.0, depthUVToNDCY(uv.y), depth01 * 2.0 - 1.0);
    vec4 view = comp.invProjection * vec4(ndc, 1.0);
    return view.xyz / view.w;
}

// as in ibl.glsl, so the term taken out matches the one the color pass added
vec3 boxProjectReflection(vec3 direction, vec3 worldPosition, int slot){
    vec3 safeDir = vec3(
        abs(direction.x) < 0.0001 ? (direction.x < 0.0 ? -0.0001 : 0.0001) : direction.x,
        abs(direction.y) < 0.0001 ? (direction.y < 0.0 ? -0.0001 : 0.0001) : direction.y,
        abs(direction.z) < 0.0001 ? (direction.z < 0.0 ? -0.0001 : 0.0001) : direction.z);
    vec3 t0 = (comp.probeBoxMin[slot].xyz - worldPosition) / safeDir;
    vec3 t1 = (comp.probeBoxMax[slot].xyz - worldPosition) / safeDir;
    vec3 tFar = max(t0, t1);
    float distanceToBox = min(tFar.x, min(tFar.y, tFar.z));
    vec3 hit = worldPosition + safeDir * max(distanceToBox, 0.0);
    return normalize(hit - comp.probePosition[slot].xyz);
}

// samplers cannot be indexed dynamically on GLES/WebGL, so branch on the slot
vec3 probeTap(int slot, vec3 direction, float lod){
    vec4 c;
    if (slot == 0)      c = textureLod(samplerCube(u_probeTexture0, u_probe0_smp), direction, lod);
    else if (slot == 1) c = textureLod(samplerCube(u_probeTexture1, u_probe1_smp), direction, lod);
    else if (slot == 2) c = textureLod(samplerCube(u_probeTexture2, u_probe2_smp), direction, lod);
    else                c = textureLod(samplerCube(u_probeTexture3, u_probe3_smp), direction, lod);
    return sRGBToLinear(c).rgb;
}

vec3 sampleLocalProbe(int slot, vec3 direction, float lod){
    float maxLod = comp.probeBoxMax[slot].w;
    if (maxLod > 0.0){
        return probeTap(slot, direction, min(lod, maxLod));
    }

    // capture not prefiltered yet: the same small angular filter as ibl.glsl
    float roughness = clamp(lod / ENV_GGX_LODS, 0.0, 1.0);
    vec3 helper = abs(direction.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, direction));
    vec3 bitangent = cross(direction, tangent);
    float spread = roughness * roughness * 0.45;
    vec3 color = probeTap(slot, direction, 0.0) * 4.0;
    color += probeTap(slot, normalize(direction + tangent * spread), 0.0);
    color += probeTap(slot, normalize(direction - tangent * spread), 0.0);
    color += probeTap(slot, normalize(direction + bitangent * spread), 0.0);
    color += probeTap(slot, normalize(direction - bitangent * spread), 0.0);
    return color * 0.125;
}

void main(){
    // composite space, flipped when writing to the GL swapchain
    vec2 c = v_texcoord;
    if (comp.params.w > 0.5) c.y = 1.0 - c.y;

    vec3 scene = texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), c).rgb;
    vec4 refl  = texture(sampler2D(u_ssrTexture, u_ssr_smp), c);

    int mode = int(comp.params.z + 0.5);   // debug mode (0 = off)
    float intensity = comp.params.x;

    // nothing reflected here, as on most pixels: the scene is final
    float w = refl.a * intensity;
    if (mode == 0 && w < 1.0 / 1024.0){
        frag_color = vec4(scene, 1.0);
        return;
    }

    // G-buffer is in depth space
    vec2 d = c;
    if (comp.params.y > 0.5) d.y = 1.0 - d.y;

    float depth01 = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), d));

    // background (no geometry): nothing to reflect, the SSR mask is 0 anyway
    if (depth01 >= 1.0){
        if (mode == 0)      frag_color = vec4(scene, 1.0);
        else if (mode == 1) frag_color = vec4(refl.rgb * refl.a, 1.0);
        else                frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 alb = texture(sampler2D(u_albedoTexture, u_albedo_smp), d);
    int iblCode = int(alb.a * 255.0 + 0.5); // see the header

    vec4 g = texture(sampler2D(u_gbufferTexture, u_gbuffer_smp), d);
    vec3 nV = octDecode(g.rg);
    float roughness = g.b;
    float metallic = g.a;
    vec3 albedo = alb.rgb;

    vec3 P = reconstructViewPos(d, depth01);
    if (dot(nV, P) > 0.0) nV = -nV;      // orient toward camera
    vec3 vV = normalize(-P);
    float NdotV = clamp(dot(nV, vV), 0.0, 1.0);

    // GGX environment BRDF weight (FssEss): the physical reflectance of this surface,
    // including metal F0. Applied to both the IBL specular and the SSR reflection.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec2 f_ab = envBRDFApprox(NdotV, roughness);
    vec3 Fr = max(vec3(1.0 - roughness), f0) - f0;
    vec3 k_S = f0 + Fr * pow(1.0 - NdotV, 5.0);
    vec3 FssEss = k_S * f_ab.x + f_ab.y;

    // environment specular the color pass added, left at 0 where it cannot be rebuilt
    vec3 iblSpec = vec3(0.0);
    float ao = 1.0;
    if (iblCode == 1 || iblCode >= 3){
        int slot = -1;
        float probeWeight = 0.0;
        if (iblCode >= 3){
            slot = (iblCode - 3) / 63;
            probeWeight = float((iblCode - 3) % 63 + 1) / 63.0;
        }

        vec3 rV = reflect(-vV, nV);                    // view-space reflection
        vec3 rW = normalize(mat3(comp.invView) * rV);  // -> world space for the env lookup
        float lod = roughness * ENV_GGX_LODS;

        vec3 specularLight = vec3(0.0);
        if (probeWeight < 1.0){
            specularLight = sRGBToLinear(textureLod(samplerCube(u_GGXEnvTexture, u_GGXEnv_smp), rotateEnv(rW, comp.envColor.w), lod)).rgb * comp.envColor.rgb;
        }
        if (slot >= 0){
            vec3 worldPos = (comp.invView * vec4(P, 1.0)).xyz;
            vec3 localDirection = boxProjectReflection(rW, worldPos, slot);
            vec3 localLight = sampleLocalProbe(slot, localDirection, lod) * comp.probeBoxMin[slot].w;
            specularLight = mix(specularLight, localLight, probeWeight);
        }
        iblSpec = specularLight * FssEss;

        ao = texture(sampler2D(u_occlusionGBufferTexture, u_occlusionGBuffer_smp), d).r
           * texture(sampler2D(u_ssaoTexture, u_ssao_smp), d).r;
    }

    // debug visualizations of the SSR G-buffer channels
    if (mode != 0){
        if (mode == 1)      frag_color = vec4(refl.rgb * refl.a, 1.0);          // reflection
        else if (mode == 2) frag_color = vec4(nV * 0.5 + 0.5, 1.0);            // view normal
        else if (mode == 3) frag_color = vec4(vec3(roughness), 1.0);          // roughness
        else if (mode == 4) frag_color = vec4(vec3(metallic), 1.0);           // metallic
        else if (mode == 5) frag_color = vec4(linearTosRGB(albedo), 1.0);     // base color
        else                frag_color = vec4(linearTosRGB(iblSpec), 1.0);    // IBL specular
        return;
    }

    // SSR reflection: un-premultiply, linearize, apply the same BRDF weight
    vec3 reflColorLin = sRGBToLinear(refl.rgb / max(refl.a, 1e-4));
    vec3 ssrSpec = reflColorLin * FssEss;

    // SSR replaces the environment specular by its confidence, scaled by intensity
    vec3 sceneLin = sRGBToLinear(scene);
    vec3 outLin = sceneLin + w * ao * (ssrSpec - iblSpec);

    frag_color = vec4(linearTosRGB(max(outLin, vec3(0.0))), 1.0);
}
