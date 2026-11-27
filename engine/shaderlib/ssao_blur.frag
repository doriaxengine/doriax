#version 450

// 4x4 blur of the raw SSAO buffer, as wide as the pattern that turns its slices.
// Taps across a depth edge are left out, so AO stays on its own surface.

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_ssaoTexture;
uniform sampler u_ssao_smp;
uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;

uniform u_fs_ssaoBlurParams {
    vec4 texelSize;   // xy = 1.0 / textureSize, z = intensity
    vec4 depthParams; // view z = (x * ndc + y) / (z * ndc + w)
    vec4 screen;      // xy = 1 / depth size, zw = depth texels per AO texel
} blurParams;

#include "includes/depth_util.glsl"

#define AO_TERM_SCALE 1.5 // as ssao.frag stores it

float viewDepth(ivec2 pixel){
    // the depth texel ssao.frag read here, clamped as the AO taps
    pixel = max(pixel, ivec2(0));
    vec2 uv = (vec2(ivec2((vec2(pixel) + 0.5) * blurParams.screen.zw)) + 0.5) * blurParams.screen.xy;
    // the cleared background decodes a little past 1.0
    float ndc = min(decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0)) * 2.0 - 1.0, 1.0);
    vec4 p = blurParams.depthParams;
    return abs((p.x * ndc + p.y) / (p.z * ndc + p.w));
}

void main(){
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float center = viewDepth(pixel);
    float result = 0.0;
    float weight = 0.0;
    for (int x = -2; x < 2; ++x){
        for (int y = -2; y < 2; ++y){
            vec2 uv = v_texcoord + vec2(float(x), float(y)) * blurParams.texelSize.xy;
            float w = max(1.0 - abs(viewDepth(pixel + ivec2(x, y)) - center) / (0.05 * center + 0.01), 0.0);
            result += textureLod(sampler2D(u_ssaoTexture, u_ssao_smp), uv, 0.0).r * w;
            weight += w;
        }
    }
    float visibility = clamp(result / weight * AO_TERM_SCALE, 1e-5, 1.0);
    frag_color = vec4(vec3(pow(visibility, blurParams.texelSize.z)), 1.0);
}
