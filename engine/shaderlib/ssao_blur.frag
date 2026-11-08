#version 450

// 4x4 blur of the raw SSAO buffer, matching the 4x4 rotation-noise tile.
// Removes the high-frequency noise introduced by the random kernel rotation.
// Taps across a depth edge are left out, so AO stays on its own surface.

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_ssaoTexture;
uniform sampler u_ssao_smp;
uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;

uniform u_fs_ssaoBlurParams {
    vec4 texelSize; // xy = 1.0 / textureSize
    vec4 depthParams; // view z = (x * ndc + y) / (z * ndc + w)
} blurParams;

#include "includes/depth_util.glsl"

float viewDepth(vec2 uv){
    // the cleared background decodes a little past 1.0
    float ndc = min(decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), uv)) * 2.0 - 1.0, 1.0);
    vec4 p = blurParams.depthParams;
    return abs((p.x * ndc + p.y) / (p.z * ndc + p.w));
}

void main(){
    float center = viewDepth(v_texcoord);
    float result = 0.0;
    float weight = 0.0;
    for (int x = -2; x < 2; ++x){
        for (int y = -2; y < 2; ++y){
            vec2 uv = v_texcoord + vec2(float(x), float(y)) * blurParams.texelSize.xy;
            float w = max(1.0 - abs(viewDepth(uv) - center) / (0.05 * center + 0.01), 0.0);
            result += texture(sampler2D(u_ssaoTexture, u_ssao_smp), uv).r * w;
            weight += w;
        }
    }
    frag_color = vec4(vec3(result / weight), 1.0);
}
