#version 450

// Bloom: one half-size level down, 13 taps; the first level keeps what passes the threshold

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;

uniform u_fs_bloomDownParams {
    vec4 params;    // xy = source texel size, z = 1 on the first level
    vec4 threshold; // x = threshold, y = knee width
} bloom;

#include "includes/srgb.glsl"

vec3 readSource(vec2 uv){
    return sRGBToLinear(texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), uv).rgb);
}

// weights a bright box down, so single bright pixels do not flicker
float fireflyWeight(vec3 color){
    return 1.0 / (1.0 + dot(color, vec3(0.2126, 0.7152, 0.0722)));
}

void main(){
    vec2 texel = bloom.params.xy;
    vec2 uv = v_texcoord;

    vec3 a = readSource(uv + texel * vec2(-2.0, -2.0));
    vec3 b = readSource(uv + texel * vec2( 0.0, -2.0));
    vec3 c = readSource(uv + texel * vec2( 2.0, -2.0));
    vec3 d = readSource(uv + texel * vec2(-2.0,  0.0));
    vec3 e = readSource(uv);
    vec3 f = readSource(uv + texel * vec2( 2.0,  0.0));
    vec3 g = readSource(uv + texel * vec2(-2.0,  2.0));
    vec3 h = readSource(uv + texel * vec2( 0.0,  2.0));
    vec3 i = readSource(uv + texel * vec2( 2.0,  2.0));
    vec3 j = readSource(uv + texel * vec2(-1.0, -1.0));
    vec3 k = readSource(uv + texel * vec2( 1.0, -1.0));
    vec3 l = readSource(uv + texel * vec2(-1.0,  1.0));
    vec3 m = readSource(uv + texel * vec2( 1.0,  1.0));

    vec3 color;
    if (bloom.params.z > 0.5){
        // five boxes of four (Karis average)
        vec3 inner = (j + k + l + m) * 0.25;
        vec3 topLeft = (a + b + d + e) * 0.25;
        vec3 topRight = (b + c + e + f) * 0.25;
        vec3 bottomLeft = (d + e + g + h) * 0.25;
        vec3 bottomRight = (e + f + h + i) * 0.25;
        float wInner = fireflyWeight(inner) * 0.5;
        float wTopLeft = fireflyWeight(topLeft) * 0.125;
        float wTopRight = fireflyWeight(topRight) * 0.125;
        float wBottomLeft = fireflyWeight(bottomLeft) * 0.125;
        float wBottomRight = fireflyWeight(bottomRight) * 0.125;
        color = (inner * wInner + topLeft * wTopLeft + topRight * wTopRight + bottomLeft * wBottomLeft + bottomRight * wBottomRight) /
                (wInner + wTopLeft + wTopRight + wBottomLeft + wBottomRight);

        // soft threshold on the brightest channel, so colors keep their hue
        float brightness = max(color.r, max(color.g, color.b));
        float knee = bloom.threshold.y;
        float soft = clamp(brightness - bloom.threshold.x + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 0.0001);
        color *= max(brightness - bloom.threshold.x, soft) / max(brightness, 0.0001);
    }else{
        color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }

    frag_color = vec4(linearTosRGB(color), 1.0);
}
