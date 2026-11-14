#version 450

out vec4 g_finalColor;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 v_color;
#endif
#ifdef HOOK_FRAGMENT_POSITION
    in vec3 v_hookPosition;
#endif

#include "includes/srgb.glsl"

#include "includes/parameters_fs.glsl"
#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_color_fs.glsl"
#endif

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

#ifdef USE_FRAGMENT_HOOKS
    FragmentContext getLinesContext(){
        FragmentContext context;
        context.worldPosition = vec3(0.0);
        #ifdef HOOK_FRAGMENT_POSITION
            context.worldPosition = v_hookPosition;
        #endif
        context.uv1 = vec2(0.0);
        context.color = getLineColor();
        context.textureColor = vec4(1.0);
        context.tint = vec4(1.0);
        context.pixel = gl_FragCoord.xy;
        return context;
    }
#endif

void main() {  
    loadShaderParameters();

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context = getLinesContext();
        vec4 color = context.color;
        color.a *= keepHookInputs(context);
        color = getColor(context, color);
    #else
        vec4 color = getLineColor();
    #endif

    g_finalColor = (vec4(linearTosRGB(color.rgb), color.a));
}