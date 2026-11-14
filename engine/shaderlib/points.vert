#version 450

uniform u_vs_pointsParams {
    mat4 mvpMatrix;
    float pointScale;
} pointsParams;

in vec3 a_position;

in float a_pointsize;

#ifdef HAS_TEXTURE
    in float a_pointrotation;
    out float v_pointrotation;
#endif

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 a_color;
    out vec3 v_color;
#endif

#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 a_color;
    out vec4 v_color;
#endif

#ifdef HAS_TEXTURERECT
    in vec4 a_texturerect;
    out vec4 v_texturerect;
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

    #ifdef HAS_TEXTURE
        v_pointrotation = a_pointrotation;
    #endif

    #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
        v_color = a_color;
    #endif

    #ifdef HAS_TEXTURERECT
        v_texturerect = a_texturerect;
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
        gl_Position = pointsParams.mvpMatrix * vec4(position, 1.0);
    #else
        gl_Position = pointsParams.mvpMatrix * vec4(a_position, 1.0);
    #endif

    // gl_Position.w is the eye-space depth for perspective and exactly 1.0 for
    // ortho/UI, so this single expression gives world-space sizing in both cases.
    gl_PointSize = max(a_pointsize * pointsParams.pointScale / gl_Position.w, 1.0);

    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}