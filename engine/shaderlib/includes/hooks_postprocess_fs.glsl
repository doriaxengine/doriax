// Fragment hooks of post-process passes
#include "includes/srgb.glsl"
#include "includes/depth_util.glsl"

struct FragmentContext {
    vec2 uv;              // of the view, 0 at the top left
    vec2 pixel;
};

// the previous pass, linear
vec4 sceneColor(vec2 uv){
    return sRGBToLinear(texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), uv));
}

// depth, normals and occlusion in the depth targets' orientation
vec2 getSceneDepthUV(vec2 uv){
    return vec2(uv.x, ndcYToDepthUV(1.0 - uv.y * 2.0));
}

#ifdef HOOK_SCENE_DEPTH
    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;

    // view distance in world units, far plane where empty
    float sceneDepth(vec2 uv){
        float depth = min(decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), getSceneDepthUV(uv))), 1.0) * 2.0 - 1.0;
        vec4 terms = paramsFS.effect_projection;
        return (depth * terms.w - terms.z) / (depth * terms.y - terms.x);
    }
#endif

#ifdef HOOK_SCENE_NORMAL
    #include "includes/octahedral.glsl"

    uniform texture2D u_gbufferTexture;
    uniform sampler u_gbuffer_smp;

    // world space, from the SSR G-buffer
    vec3 sceneNormal(vec2 uv){
        vec2 encoded = texture(sampler2D(u_gbufferTexture, u_gbuffer_smp), getSceneDepthUV(uv)).rg;
        return normalize(octDecode(encoded) * mat3(paramsFS.effect_worldToView));
    }
#endif

#ifdef HOOK_SCENE_OCCLUSION
    uniform texture2D u_ssaoTexture;
    uniform sampler u_ssao_smp;

    // ambient occlusion of SSAO, 1 without it
    float sceneOcclusion(vec2 uv){
        return texture(sampler2D(u_ssaoTexture, u_ssao_smp), getSceneDepthUV(uv)).r;
    }
#endif

// the shader file's fragment stage
#include "shader_hooks_fs.glsl"
