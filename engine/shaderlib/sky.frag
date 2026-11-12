#version 450

out vec4 frag_color;

in vec3 uv;

uniform textureCube u_skyTexture;
uniform sampler u_sky_smp;

uniform u_fs_skyParams {
    vec4 color; //sRGB
} skyParams;

#include "includes/parameters_fs.glsl"

#ifdef USE_FRAGMENT_HOOKS
    #include "includes/srgb.glsl"

    // linear, as the hooks of the other types
    struct FragmentContext {
        vec3 direction;       // of the cube map, in the sky's space
        vec4 textureColor;    // the cube map sample
        vec4 tint;            // the sky color
        vec2 pixel;
    };

    // the fragment stage of the shader file, mapped in by the shader builder
    #include "shader_hooks_fs.glsl"
#endif

void main(){
    loadShaderParameters();

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context;
        context.direction = uv;
        context.textureColor = sRGBToLinear(texture(samplerCube(u_skyTexture, u_sky_smp), uv));
        context.tint = sRGBToLinear(skyParams.color);
        context.pixel = gl_FragCoord.xy;
        frag_color = linearTosRGB(getColor(context, context.tint * context.textureColor));
    #else
        frag_color = skyParams.color * texture(samplerCube(u_skyTexture, u_sky_smp), uv);
    #endif
}