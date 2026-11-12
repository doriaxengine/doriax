#version 450

uniform u_vs_uiParams {
    mat4 mvpMatrix;
} uiParams;

in vec3 a_position;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 a_color;
    out vec3 v_color;
#endif

#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 a_color;
    out vec4 v_color;
#endif

// every UI buffer has UVs, the hooks read them without a texture too
#if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE) || defined(HOOK_UV1)
    in vec2 a_texcoord1;
#endif
#if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
    out vec2 v_uv1;
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

    #if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
        v_uv1 = a_texcoord1;
    #endif

    #if defined(USE_VERTEX_HOOKS) || defined(HOOK_FRAGMENT_POSITION)
        vec3 position = a_position;
        #ifdef USE_VERTEX_OFFSET
            VertexContext context = getVertexContext(position, (paramsVS.effect_modelToWorld * vec4(position, 1.0)).xyz, mat4(1.0));
            position += getModelOffset(context);
        #endif
        #ifdef HOOK_FRAGMENT_POSITION
            v_hookPosition = (paramsVS.effect_modelToWorld * vec4(position, 1.0)).xyz;
        #endif
        gl_Position = uiParams.mvpMatrix * vec4(position, 1.0);
    #else
        gl_Position = uiParams.mvpMatrix * vec4(a_position, 1.0);
    #endif
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}