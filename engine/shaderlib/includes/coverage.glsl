// Cuts a fragment by the getCoverage() of the mesh's shader, the same in every pass
#include "includes/parameters_fs.glsl"
#include "includes/coverage_helpers.glsl"

struct FragmentContext {
    vec3 worldPosition;
    vec2 uv1;           // as the mesh stores it, 0 without the set
    vec3 eye;
    vec3 forward;
    bool perspective;
    vec2 pixel;         // from a corner every pass shares
};

// the fragment stage of the mesh's shader, mapped in by the shader builder
#include "shader_hooks_fs.glsl"

// discards a removed fragment; returns the alpha factor of a blended one
float applyCoverage(vec3 worldPosition, vec2 uv){
    // shadow maps keeping their silhouette
    if (paramsFS.effect_pass.z < 0.5)
        return 1.0;

    FragmentContext context;
    context.worldPosition = worldPosition;
    context.uv1 = uv;
    context.eye = paramsFS.effect_eye.xyz;
    context.forward = paramsFS.effect_forward;
    context.perspective = paramsFS.effect_eye.w > 0.5;
    context.pixel = abs(gl_FragCoord.xy - paramsFS.effect_pass.xy);

    float coverage = clamp(getCoverage(context), 0.0, 1.0);
    // keeps the inputs alive when the function ignores them, so the stages still pair
    if (worldPosition.x < -3.0e38 || uv.x < -3.0e38)
        coverage = 0.0;

    if (paramsFS.effect_pass.w > 0.5){
        if (coverage <= 0.0)
            discard;
        return coverage;
    }
    if (coverage < coverageDitherThreshold(context.pixel))
        discard;
    return 1.0;
}
