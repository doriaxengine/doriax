#version 450

// Post-process pass: a passthrough, or the shader file's getColor

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;

#include "includes/parameters_fs.glsl"
#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_postprocess_fs.glsl"
#endif

void main(){
    loadShaderParameters();

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context;
        context.uv = v_texcoord;
        context.pixel = gl_FragCoord.xy;
        frag_color = linearTosRGB(getColor(context, sceneColor(v_texcoord)));
    #else
        frag_color = texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), v_texcoord);
    #endif
}
