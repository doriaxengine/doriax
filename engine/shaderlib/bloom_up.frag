#version 450

// Bloom: the smaller level spread over this one, or the glow added over the scene

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture; // the smaller level
uniform sampler u_sceneColor_smp;
uniform texture2D u_bloomTexture;      // this level on the way down, or the scene on the last step
uniform sampler u_bloom_smp;

uniform u_fs_bloomUpParams {
    vec4 params; // xy = texel size of the smaller level, z = spread or intensity, w = 1 on the last step
} bloom;

#include "includes/srgb.glsl"

vec3 readSmaller(vec2 uv){
    return sRGBToLinear(texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), uv).rgb);
}

void main(){
    vec2 texel = bloom.params.xy;
    vec2 uv = v_texcoord;
    vec4 base = texture(sampler2D(u_bloomTexture, u_bloom_smp), uv);

    // the glow is blurred already, so one filtered read brings it to the full size
    if (bloom.params.w > 0.5){
        frag_color = vec4(linearTosRGB(sRGBToLinear(base.rgb) + readSmaller(uv) * bloom.params.z), base.a);
        return;
    }

    // 3x3 tent
    vec3 spread = readSmaller(uv) * 4.0;
    spread += (readSmaller(uv + vec2(texel.x, 0.0)) + readSmaller(uv - vec2(texel.x, 0.0)) +
               readSmaller(uv + vec2(0.0, texel.y)) + readSmaller(uv - vec2(0.0, texel.y))) * 2.0;
    spread += readSmaller(uv + texel) + readSmaller(uv - texel) +
              readSmaller(uv + vec2(texel.x, -texel.y)) + readSmaller(uv + vec2(-texel.x, texel.y));
    spread *= 1.0 / 16.0;

    frag_color = vec4(linearTosRGB(mix(sRGBToLinear(base.rgb), spread, bloom.params.z)), 1.0);
}
