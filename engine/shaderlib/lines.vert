#version 450

// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

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


#include "includes/parameters_vs.glsl"

void main() {
    loadShaderParameters();

    #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
        v_color = a_color;
    #endif

    gl_Position = linesParams.mvpMatrix * vec4(a_position, 1.0);
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}