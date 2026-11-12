// Fragment hooks of the material's shader file, the same in every pass
#include "includes/parameters_fs.glsl"
#include "includes/coverage_helpers.glsl"
#include "includes/srgb.glsl"

struct FragmentContext {
    vec3 worldPosition;
    vec3 normal;        // world space, facing the viewer; (0, 1, 0) without normals
    vec2 uv1;           // as the mesh stores it, 0 without the set
    vec4 color;         // vertex color times instance color, 1 without either
    vec3 view;          // unit vector to the eye
    vec3 eye;
    vec3 forward;
    bool perspective;
    vec2 pixel;         // from a corner every pass shares
    bool frontFacing;
};

// one light reaching the surface, for getLight()
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct LightContext {
    vec3 direction;       // from the surface to the light
    vec3 color;           // color times intensity times attenuation, linear
    float shadow;         // 1 lit, 0 shadowed
    int type;             // LIGHT_DIRECTIONAL, LIGHT_POINT or LIGHT_SPOT
};

struct Surface {
    vec3 baseColor;     // linear
    float alpha;
    vec3 normal;        // world space
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;      // linear
};

// the fragment stage of the shader file, mapped in by the shader builder
#include "shader_hooks_fs.glsl"

// what every pass knows of a fragment; the pass adds the mesh's data
FragmentContext getFragmentContext(vec3 worldPosition){
    FragmentContext context;
    context.worldPosition = worldPosition;
    context.normal = vec3(0.0, 1.0, 0.0);
    context.uv1 = vec2(0.0);
    context.color = vec4(1.0);
    context.eye = paramsFS.effect_eye.xyz;
    context.forward = paramsFS.effect_forward;
    context.perspective = paramsFS.effect_eye.w > 0.5;
    context.view = context.perspective ? normalize(context.eye - worldPosition) : -context.forward;
    context.pixel = abs(gl_FragCoord.xy - paramsFS.effect_pass.xy);
    context.frontFacing = gl_FrontFacing;
    #ifdef USE_VARYINGS
        loadVaryings();
    #endif
    return context;
}

// always 1; keeps unused inputs alive so the stages pair
float keepHookInputs(FragmentContext context){
    return (context.worldPosition.x < -3.0e38 || context.uv1.x < -3.0e38 || context.normal.x < -3.0e38 ||
        context.color.x < -3.0e38) ? 0.0 : 1.0;
}

#ifdef USE_DEPTH_OFFSET
    // pushed away from the eye; from gl_FragCoord, so every pass agrees
    float getHookDepth(FragmentContext context){
        float offset = max(getDepthOffset(context), 0.0);
        float w = 1.0 / gl_FragCoord.w;
        float z = (gl_FragCoord.z * 2.0 - 1.0) * w - paramsFS.effect_projection.x * offset;
        w -= paramsFS.effect_projection.y * offset;
        return clamp(0.5 * z / w + 0.5, 0.0, 1.0);
    }
#endif

#ifdef USE_COVERAGE
// discards a removed fragment; returns the alpha factor of a blended one
float applyCoverage(FragmentContext context){
    // shadow maps keeping their silhouette
    if (paramsFS.effect_pass.z < 0.5)
        return 1.0;

    float coverage = clamp(getCoverage(context), 0.0, 1.0) * keepHookInputs(context);
    if (paramsFS.effect_pass.w > 0.5){
        if (coverage <= 0.0)
            discard;
        return coverage;
    }
    if (coverage < coverageDitherThreshold(context.pixel))
        discard;
    return 1.0;
}
#endif
