#version 450

// G-buffer geometry pass (fragment), MRT output:
//   color[0] = packed view depth (identical to depth.frag, so SSAO/SSR depth is unchanged)
//   color[1] = view-space normal (octahedral .rg) + roughness (.b) + metallic (.a)
//   color[2] = linear base color (.rgb) + IBL source code / 255 (.a): none, sky, or
//              the local reflection probe slot and blend weight (see composite.frag)
//   color[3] = material occlusion (.r, R8), as the color pass applies it
//
// HAS_TERRAIN_PBR evaluates the painted layers here too, so what this writes matches
// what the color pass shades.

layout(location = 0) out vec4 frag_depth;
layout(location = 1) out vec4 frag_gbuffer;
layout(location = 2) out vec4 frag_albedo;
layout(location = 3) out vec4 frag_occlusion;

in vec2 v_projZW;
in vec3 v_normal;

uniform u_fs_gbufferMaterial {
    vec4 params;        // x = roughness, y = metallic, z = IBL source code / 255, w = alpha cutoff (-1 = disabled)
    vec4 baseColorFactor;
} gbufferMaterial;

#if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE)
    in vec2 v_uv1;
#endif
#if defined(HAS_BASECOLOR_TEXTURE)
    uniform texture2D u_baseColorTexture;
    uniform sampler u_baseColor_smp;
#endif
#if defined(HAS_METALLICROUGHNESS_TEXTURE)
    uniform texture2D u_metallicRoughnessTexture;
    uniform sampler u_metallicRoughness_smp;
#endif
#ifdef HAS_OCCLUSION_TEXTURE
    in vec2 v_uvOcclusion;
    uniform texture2D u_occlusionTexture;
    uniform sampler u_occlusion_smp;
#endif

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"
#include "includes/srgb.glsl"
#ifdef HAS_TERRAIN_PBR
    #include "includes/terrain_fs.glsl"
#endif

void main() {
    vec4 baseColor = gbufferMaterial.baseColorFactor;
    #if defined(HAS_BASECOLOR_TEXTURE)
        vec4 texColor = texture(sampler2D(u_baseColorTexture, u_baseColor_smp), v_uv1);
        baseColor *= sRGBToLinear(texColor);
    #endif
    if (gbufferMaterial.params.w >= 0.0 && baseColor.a < gbufferMaterial.params.w) {
        discard;
    }

    float roughness = gbufferMaterial.params.x;
    float metallic  = gbufferMaterial.params.y;
    #if defined(HAS_METALLICROUGHNESS_TEXTURE)
        // glTF convention: green = roughness, blue = metallic
        vec4 mr = texture(sampler2D(u_metallicRoughnessTexture, u_metallicRoughness_smp), v_uv1);
        roughness *= mr.g;
        metallic  *= mr.b;
    #endif

    // Higher precision equivalent of gl_FragCoord.z (see depth.frag).
    frag_depth = encodeDepth(0.5 * v_projZW[0] / v_projZW[1] + 0.5);

    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) n = -n;

    float occlusion = 1.0;
    #ifdef HAS_OCCLUSION_TEXTURE
        occlusion = texture(sampler2D(u_occlusionTexture, u_occlusion_smp), v_uvOcclusion).r;
    #endif

    #ifdef HAS_TERRAIN_PBR
        // the same evaluation the color pass runs, so SSR sees the surface it shades
        TerrainSurface terrainSurface = getTerrainSurface(baseColor, n, roughness, metallic);
        baseColor = terrainSurface.color;
        roughness = terrainSurface.roughness;
        metallic = terrainSurface.metallic;
        n = terrainSurface.normal;
        occlusion = terrainSurface.occlusion;
    #endif

    frag_gbuffer = vec4(octEncode(n), clamp(roughness, 0.0, 1.0), clamp(metallic, 0.0, 1.0));

    frag_albedo = vec4(baseColor.rgb, gbufferMaterial.params.z);

    frag_occlusion = vec4(occlusion, 0.0, 0.0, 1.0);
}
