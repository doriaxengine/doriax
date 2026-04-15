#version 450

out vec4 g_finalColor;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 v_color;
#endif

#include "includes/srgb.glsl"

vec4 getLineColor(){
   vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    #ifdef HAS_VERTEX_COLOR_VEC3
        color.rgb = v_color.rgb;
    #endif
    #ifdef HAS_VERTEX_COLOR_VEC4
        color = v_color;
    #endif

   return color;
}

void main() {  
    vec4 color = getLineColor();

    g_finalColor = (vec4(linearTosRGB(color.rgb), color.a));
}