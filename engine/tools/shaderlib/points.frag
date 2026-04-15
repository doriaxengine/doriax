#version 450

out vec4 g_finalColor;

#ifdef HAS_TEXTURE
    in float v_pointrotation;
#endif

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 v_color;
#endif

#ifdef HAS_TEXTURERECT
    in vec4 v_texturerect;
#endif

#ifdef HAS_TEXTURE
    uniform texture2D u_pointsTexture;
    uniform sampler u_points_smp;
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
    vec4 baseColor = vec4(1.0, 1.0, 1.0, 1.0);
    #ifdef HAS_TEXTURE

        vec2 resultCoord = gl_PointCoord;

        if (v_pointrotation != 0.0){
            resultCoord = vec2(cos(v_pointrotation) * (resultCoord.x - 0.5) + sin(v_pointrotation) * (resultCoord.y - 0.5) + 0.5,
                            cos(v_pointrotation) * (resultCoord.y - 0.5) - sin(v_pointrotation) * (resultCoord.x - 0.5) + 0.5);
        }

        #ifdef HAS_TEXTURERECT
            resultCoord = resultCoord * v_texturerect.zw + v_texturerect.xy;
        #endif

        baseColor *= sRGBToLinear(texture(sampler2D(u_pointsTexture, u_points_smp), resultCoord));
    #endif
    return baseColor * getVertexColor();
}

void main() {  
    vec4 color = getBaseColor();

    g_finalColor = (vec4(linearTosRGB(color.rgb), color.a));
}