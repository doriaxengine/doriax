#version 450

// Exposure and a tone curve from the scene brightness to the 0..1 of the screen

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;

uniform u_fs_toneMappingParams {
    vec4 params; // x = exposure, y = curve: 0 none, 1 Reinhard, 2 ACES, 3 AgX
} toneMapping;

#include "includes/srgb.glsl"

// Stephen Hill's fit of the ACES reference and output transforms
vec3 acesFitted(vec3 color){
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    // keeps mid gray about where it was
    color = inputMatrix * (color / 0.6);
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return outputMatrix * (a / b);
}

// AgX with its base look, after Benjamin Wrensch's fit
vec3 agx(vec3 color){
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 x = clamp(log2(max(inset * color, vec3(1e-10))), minEv, maxEv);
    x = (x - minEv) / (maxEv - minEv);
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    // the curve ends in display values, back to linear
    return pow(max(outset * x, vec3(0.0)), vec3(2.2));
}

void main(){
    vec4 scene = texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), v_texcoord);
    vec3 color = sRGBToLinear(scene.rgb) * toneMapping.params.x;

    int curve = int(toneMapping.params.y + 0.5);
    if (curve == 1){
        color = color / (1.0 + color);
    }else if (curve == 2){
        color = acesFitted(color);
    }else if (curve == 3){
        color = agx(color);
    }

    frag_color = vec4(linearTosRGB(clamp(color, 0.0, 1.0)), scene.a);
}
