#version 450

uniform u_fs_uiParams {
    vec4 color;
} uiParams;

out vec4 g_finalColor;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 v_color;
#endif

#if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE)
    uniform texture2D u_uiTexture;
    uniform sampler u_ui_smp;
    in vec2 v_uv1;
#endif

#include "includes/srgb.glsl"

vec4 getVertexColor(){
   vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    #ifdef HAS_VERTEX_COLOR_VEC3
        color.rgb = v_color.rgb;
    #endif
    #ifdef HAS_VERTEX_COLOR_VEC4
        color = v_color;
    #endif

   return color;
}

vec4 getBaseColor(){
    vec4 baseColor = uiParams.color;
    #ifdef HAS_TEXTURE
        baseColor *= sRGBToLinear(texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1));
    #endif
    #ifdef HAS_FONTATLAS_TEXTURE
        baseColor *= vec4(1.0, 1.0, 1.0, texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1).r);
    #endif
    return baseColor * getVertexColor();
}

void main() {  
    vec4 color = getBaseColor();

    g_finalColor = (vec4(linearTosRGB(color.rgb), color.a));
}