#version 450

out vec4 frag_color;

in vec3 uv;

uniform textureCube u_skyTexture;
uniform sampler u_sky_smp;

uniform u_fs_skyParams {
    vec4 color;        // sRGB
    vec4 sunDirection; // toward the strongest directional light, in the sky's space
    vec4 sunColor;     // its color times intensity, linear; black without one
    vec4 capture;      // x = 1 while the sky draws into its capture
} skyParams;

#include "includes/parameters_fs.glsl"

#ifdef USE_FRAGMENT_HOOKS
    #include "includes/srgb.glsl"

    // linear, as the hooks of the other types
    struct FragmentContext {
        vec3 direction;       // of the cube map, in the sky's space
        vec4 textureColor;    // the cube map sample
        vec4 tint;            // the sky color
        vec3 sunDirection;    // toward the strongest directional light, in the sky's space
        vec3 sunColor;        // its color times intensity; black without one
        vec2 pixel;
        bool capturing;       // drawn into the cube that lights the scene
    };

    #ifdef HOOK_SKY_CAPTURE
        uniform textureCube u_skyCaptureTexture;
        uniform sampler u_skyCapture_smp;

        // the sky as last captured, linear; black while it is captured
        vec4 skyCapture(vec3 direction){
            return sRGBToLinear(textureLod(samplerCube(u_skyCaptureTexture, u_skyCapture_smp), direction, 0.0));
        }
    #endif

    // the shader file's fragment stage
    #include "shader_hooks_fs.glsl"
#endif

void main(){
    loadShaderParameters();

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context;
        context.direction = uv;
        context.textureColor = sRGBToLinear(texture(samplerCube(u_skyTexture, u_sky_smp), uv));
        context.tint = sRGBToLinear(skyParams.color);
        context.sunDirection = skyParams.sunDirection.xyz;
        context.sunColor = skyParams.sunColor.rgb;
        context.pixel = gl_FragCoord.xy;
        context.capturing = skyParams.capture.x > 0.5;
        frag_color = linearTosRGB(getColor(context, context.tint * context.textureColor));
    #else
        frag_color = skyParams.color * texture(samplerCube(u_skyTexture, u_sky_smp), uv);
    #endif
}