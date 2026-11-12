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
// in the vertex stage's order: GL pairs the stages by location
#ifdef USE_FRAGMENT_HOOKS
    in vec3 v_worldPosition;
#endif

uniform u_fs_gbufferMaterial {
    vec4 params;        // x = roughness, y = metallic, z = IBL source code / 255, w = alpha cutoff (-1 = disabled)
    vec4 baseColorFactor;
} gbufferMaterial;

#if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
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
#ifdef HOOK_FRAGMENT_NORMAL
    in vec3 v_hookNormal;
#endif
#ifdef HOOK_FRAGMENT_COLOR
    in vec4 v_hookColor;
#endif

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"
#include "includes/srgb.glsl"
#ifdef HAS_TERRAIN_PBR
    #include "includes/terrain_fs.glsl"
#endif
#include "includes/parameters_fs.glsl"
#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_fs.glsl"

    FragmentContext getGBufferContext(){
        FragmentContext context = getFragmentContext(v_worldPosition);
        #ifdef HOOK_FRAGMENT_UV1
            context.uv1 = v_uv1;
        #endif
        #ifdef HOOK_FRAGMENT_NORMAL
            context.normal = normalize(gl_FrontFacing ? v_hookNormal : -v_hookNormal);
        #endif
        #ifdef HOOK_FRAGMENT_COLOR
            context.color = v_hookColor;
        #endif
        return context;
    }
#endif

void main() {
    loadShaderParameters();
    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context = getGBufferContext();
    #endif
    #ifdef USE_COVERAGE
        applyCoverage(context);
    #endif
    vec4 baseColor = gbufferMaterial.baseColorFactor;
    #if defined(HAS_BASECOLOR_TEXTURE) && !defined(SURFACE_REPLACES_BASECOLOR_MAP)
        vec4 texColor = texture(sampler2D(u_baseColorTexture, u_baseColor_smp), v_uv1);
        baseColor *= sRGBToLinear(texColor);
    #endif
    #ifndef USE_SURFACE
        if (gbufferMaterial.params.w >= 0.0 && baseColor.a < gbufferMaterial.params.w) {
            discard;
        }
    #endif

    float roughness = gbufferMaterial.params.x;
    float metallic  = gbufferMaterial.params.y;
    #if defined(HAS_METALLICROUGHNESS_TEXTURE) && !defined(SURFACE_REPLACES_METALLICROUGHNESS_MAP)
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
    #if defined(HAS_OCCLUSION_TEXTURE) && !defined(SURFACE_REPLACES_OCCLUSION_MAP)
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

    #ifdef USE_SURFACE
        // as the color pass shades it, with the normal turned to world space and back
        Surface surface;
        surface.baseColor = baseColor.rgb;
        surface.alpha = baseColor.a;
        surface.normal = n * mat3(paramsFS.effect_worldToView);
        surface.metallic = metallic;
        surface.roughness = roughness;
        surface.occlusion = occlusion;
        surface.emissive = vec3(0.0);
        getSurface(context, surface);
        if (gbufferMaterial.params.w >= 0.0 && surface.alpha * keepHookInputs(context) < gbufferMaterial.params.w) {
            discard;
        }
        baseColor.rgb = surface.baseColor;
        n = normalize(mat3(paramsFS.effect_worldToView) * surface.normal);
        roughness = surface.roughness;
        metallic = surface.metallic;
        occlusion = surface.occlusion;
    #endif

    frag_gbuffer = vec4(octEncode(n), clamp(roughness, 0.0, 1.0), clamp(metallic, 0.0, 1.0));

    frag_albedo = vec4(baseColor.rgb, gbufferMaterial.params.z);

    frag_occlusion = vec4(occlusion, 0.0, 0.0, 1.0);
}
