#version 450

// Vertexless fullscreen triangle: drawn with draw(3, 1) and no vertex buffer.
// gl_VertexIndex 0,1,2 -> uv (0,0),(2,0),(0,2) -> clip triangle covering the screen.
//
// A post-process or underwater fork can declare values here too, as in its .frag.
out vec2 v_texcoord;

#include "includes/parameters_vs.glsl"

void main() {
    loadShaderParameters();

    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_texcoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    #ifndef IS_GLSL
        // only GL stores destination row 0 at clip y = -1; everywhere else the
        // texel this fragment writes is at the flipped coordinate
        v_texcoord.y = 1.0 - v_texcoord.y;
    #endif
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}
