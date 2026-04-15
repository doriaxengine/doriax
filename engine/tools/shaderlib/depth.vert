#version 450

#define DEPTH_SHADER

uniform u_vs_depthParams {
    mat4 modelMatrix;
    mat4 lightVPMatrix;
} depthParams;

in vec3 a_position;
out vec2 v_projZW;

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
#endif

#if defined(HAS_TEXTURE)
    in vec2 a_texcoord1;
    out vec2 v_uv1;
#endif

#include "includes/skinning.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif

vec4 getPosition(mat4 boneTransform){
    vec3 pos = a_position;

    pos = getMorphPosition(pos);
    pos = getSkinPosition(pos, getBoneTransform());
    #ifdef HAS_TERRAIN
        pos = getTerrainPosition(pos, depthParams.modelMatrix);
    #endif

    return vec4(pos, 1.0);
}

void main() {
    mat4 lightMVPMatrix = depthParams.lightVPMatrix * depthParams.modelMatrix;

    mat4 boneTransform = getBoneTransform();

    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = mat4(i_matrix_col1, i_matrix_col2, i_matrix_col3, i_matrix_col4);
        vec4 pos = instanceMatrix *  getPosition(boneTransform);
    #else
        vec4 pos = getPosition(boneTransform);
    #endif

    #if defined(HAS_TEXTURE)
        v_uv1 = a_texcoord1;
    #endif

    gl_Position = lightMVPMatrix * pos;

    v_projZW = gl_Position.zw;
    #ifndef IS_GLSL
        gl_Position.y = -gl_Position.y;
    #endif
}