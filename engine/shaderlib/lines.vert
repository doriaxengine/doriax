#version 450

uniform u_vs_linesParams {
    mat4 mvpMatrix;
} linesParams;

in vec3 a_position;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 a_color;
    out vec3 v_color;
#endif

#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 a_color;
    out vec4 v_color;
#endif
#ifdef HOOK_FRAGMENT_POSITION
    out vec3 v_hookPosition;
#endif


#include "includes/parameters_vs.glsl"
#ifdef USE_VERTEX_HOOKS
    #include "includes/hooks_vs.glsl"
#endif

void main() {
    loadShaderParameters();

    #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
        v_color = a_color;
    #endif

    #if defined(USE_VERTEX_HOOKS) || defined(HOOK_FRAGMENT_POSITION)
        vec3 position = a_position;
        #ifdef USE_VERTEX_HOOKS
            VertexContext context = getVertexContext(position, (paramsVS.effect_modelToWorld * vec4(position, 1.0)).xyz, mat4(1.0));
            position += getModelOffset(context);
        #endif
        #ifdef HOOK_FRAGMENT_POSITION
            v_hookPosition = (paramsVS.effect_modelToWorld * vec4(position, 1.0)).xyz;
        #endif
        gl_Position = linesParams.mvpMatrix * vec4(position, 1.0);
    #else
        gl_Position = linesParams.mvpMatrix * vec4(a_position, 1.0);
    #endif
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}