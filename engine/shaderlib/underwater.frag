#version 450

// The scene seen by a camera inside a water, fading into it with the distance as
// water.frag fades what is seen through the surface.
//
// Declare values in comments, as "// @param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;
uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;
uniform textureCube u_lambertianEnvTexture;
uniform sampler u_lambertianEnv_smp;

uniform u_fs_underwaterParams {
    mat4 invViewProjection; // logical, unprojects the scene depth
    vec4 shallowColor;      // rgb (linear), w = depth fade
    vec4 deepColor;         // rgb (linear), w = 1 when the scene depth is valid
    vec4 light;             // rgb = light reaching the water besides the sky (linear)
    vec4 envColor;          // rgb = sky tint (linear)
    vec4 eyePos;            // xyz = eye
    vec4 sceneRect;         // xy = view origin, zw = view size, in scene copy uv
} underwater;

#include "includes/srgb.glsl"
#include "includes/depth_util.glsl"

#include "includes/parameters_fs.glsl"

void main(){
    loadShaderParameters();

    // the scene targets keep the top of the view in row 0
    vec2 ndc = vec2(v_texcoord.x * 2.0 - 1.0, 1.0 - v_texcoord.y * 2.0);
    float depthFade = max(underwater.shallowColor.w, 0.001);

    // water crossed before the opaque scene, without the scene depth as if at the depth fade
    float path = depthFade;
    if (underwater.deepColor.w > 0.5){
        float depth = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), vec2(v_texcoord.x, ndcYToDepthUV(ndc.y))));
        path = 1.0e4;
        if (depth < 0.9999){
            vec4 hit = underwater.invViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
            path = length(hit.xyz / hit.w - underwater.eyePos.xyz);
        }
    }

    // the absorption of water.frag
    vec3 tint = clamp(underwater.shallowColor.rgb, vec3(0.001), vec3(0.999));
    vec3 extinction = (2.0 / depthFade) * log(tint) / log(max(tint.r, max(tint.g, tint.b)));
    vec3 transmittance = exp(-extinction * path);

    vec3 ambient = sRGBToLinear(texture(samplerCube(u_lambertianEnvTexture, u_lambertianEnv_smp), vec3(0.0, 1.0, 0.0)).rgb) * underwater.envColor.rgb;
    vec3 scattered = underwater.deepColor.rgb * (underwater.light.rgb + ambient);
    vec3 scene = sRGBToLinear(texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), underwater.sceneRect.xy + v_texcoord * underwater.sceneRect.zw).rgb);

    frag_color = vec4(linearTosRGB(scene * transmittance + scattered * (1.0 - transmittance)), 1.0);
}
