// Fragment hooks of the material's shader file, the same in every pass
#include "includes/parameters_fs.glsl"
#include "includes/coverage_helpers.glsl"

struct FragmentContext {
    vec3 worldPosition;
    vec3 normal;        // world space, facing the viewer; (0, 1, 0) without normals
    vec2 uv1;           // as the mesh stores it, 0 without the set
    vec2 uv2;
    vec4 color;         // vertex color, 1 without it
    vec3 view;          // unit vector to the eye
    vec3 eye;
    vec3 forward;
    bool perspective;
    vec2 pixel;         // from a corner every pass shares
    bool frontFacing;
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

// what every pass knows of a fragment
FragmentContext getFragmentContext(vec3 worldPosition, vec2 uv){
    FragmentContext context;
    context.worldPosition = worldPosition;
    context.normal = vec3(0.0, 1.0, 0.0);
    context.uv1 = uv;
    context.uv2 = vec2(0.0);
    context.color = vec4(1.0);
    context.eye = paramsFS.effect_eye.xyz;
    context.forward = paramsFS.effect_forward;
    context.perspective = paramsFS.effect_eye.w > 0.5;
    context.view = context.perspective ? normalize(context.eye - worldPosition) : -context.forward;
    context.pixel = abs(gl_FragCoord.xy - paramsFS.effect_pass.xy);
    context.frontFacing = gl_FrontFacing;
    return context;
}

// always 1; keeps unused inputs alive so the stages pair
float keepHookInputs(FragmentContext context){
    return (context.worldPosition.x < -3.0e38 || context.uv1.x < -3.0e38) ? 0.0 : 1.0;
}

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
