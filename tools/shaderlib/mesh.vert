#version 450

uniform u_vs_pbrParams {
    mat4 modelMatrix;
    mat4 normalMatrix;
    mat4 mvpMatrix;
} pbrParams;

#if defined(HAS_TEXTURERECT) && (defined(HAS_UV_SET1) || defined(HAS_UV_SET2))
    uniform u_vs_spriteParams {
        vec4 textureRect;
    } spriteParams;
#endif

in vec3 a_position;

#ifndef MATERIAL_UNLIT
    out vec3 v_position;
#endif

#ifdef HAS_NORMALS
    in vec3 a_normal;
#endif

#ifdef HAS_TANGENTS
    in vec4 a_tangent;
#endif

#ifdef HAS_NORMALS
#ifdef HAS_TANGENTS
    out mat3 v_tbn;
#else
    out vec3 v_normal;
#endif
#endif

#ifdef HAS_UV_SET1
    #ifndef HAS_TERRAIN
        in vec2 a_texcoord1;
    #endif
    out vec2 v_uv1;
#endif

#ifdef HAS_UV_SET2
    in vec2 a_texcoord2;
    out vec2 v_uv2;
#endif

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 a_color;
    out vec3 v_color;
#endif

#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 a_color;
#endif
#if defined(HAS_VERTEX_COLOR_VEC4) || defined(HAS_INSTANCING)
    out vec4 v_color;
#endif

#ifdef USE_SHADOWS
    uniform u_vs_shadows {
        mat4 lightVPMatrix[MAX_SHADOWSMAP];
        vec4 shadowParams[MAX_SHADOWSMAP]; // normalBias in .x
    };
 
    out vec4 v_lightProjPos[MAX_SHADOWSMAP];
#endif

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
    in vec4 i_color;
    in vec4 i_textureRect;
#endif

#include "includes/skinning.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif

vec4 getPosition(mat4 boneTransform){
    vec3 pos = a_position;

    pos = getMorphPosition(pos);
    pos = getSkinPosition(pos, boneTransform);
    #ifdef HAS_TERRAIN
        pos = getTerrainPosition(pos, pbrParams.modelMatrix);
    #endif

    return vec4(pos, 1.0);
}

#ifdef HAS_NORMALS
vec3 getNormal(mat4 boneTransform, vec4 position){
    vec3 normal = a_normal;

    normal = getMorphNormal(normal);
    normal = getSkinNormal(normal, boneTransform);
    #ifdef HAS_TERRAIN
        normal = getTerrainNormal(normal, position.xyz);
    #endif

    return normalize(normal);
}
#endif

#ifdef HAS_TANGENTS
vec3 getTangent(mat4 boneTransform){
    vec3 tangent = a_tangent.xyz;

    tangent = getMorphTangent(tangent);
    tangent = getSkinTangent(tangent, boneTransform);

    return normalize(tangent);
}
#endif

void main() {
    mat4 boneTransform = getBoneTransform();

    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = mat4(i_matrix_col1, i_matrix_col2, i_matrix_col3, i_matrix_col4);
        vec4 pos = instanceMatrix * getPosition(boneTransform);
    #else
        vec4 pos = getPosition(boneTransform);
    #endif

    vec4 worldPos = pbrParams.modelMatrix * pos;
    
    #ifndef MATERIAL_UNLIT
        v_position = vec3(worldPos.xyz) / worldPos.w;
    #endif

    #ifdef HAS_NORMALS
        vec3 worldNormal = normalize(vec3(pbrParams.normalMatrix * vec4(getNormal(boneTransform, pos), 0.0)));
    #ifdef HAS_TANGENTS
        vec3 tangent = getTangent(boneTransform);
        vec3 tangentW = normalize(vec3(pbrParams.modelMatrix * vec4(tangent, 0.0)));
        vec3 bitangentW = cross(worldNormal, tangentW) * a_tangent.w;
        v_tbn = mat3(tangentW, bitangentW, worldNormal);
    #else // !HAS_TANGENTS
        v_normal = worldNormal;
    #endif
    #endif

    #ifdef HAS_UV_SET1
        v_uv1 = vec2(0.0, 0.0);
    #endif
    #ifdef HAS_UV_SET2
        v_uv2 = vec2(0.0, 0.0);
    #endif

    #ifdef HAS_UV_SET1
        #ifndef HAS_TERRAIN
            v_uv1 = a_texcoord1;
            #ifdef HAS_TEXTURERECT
                v_uv1 = v_uv1 * spriteParams.textureRect.zw + spriteParams.textureRect.xy;
            #endif
            #ifdef HAS_INSTANCING
                v_uv1 = v_uv1 * i_textureRect.zw + i_textureRect.xy;
            #endif
        #else
            v_uv1 = getTerrainTiledTexture(pos.xyz);
        #endif
    #endif

    #ifdef HAS_UV_SET2
        v_uv2 = a_texcoord2;
        #ifdef HAS_TEXTURERECT
            v_uv2 = v_uv2 * spriteParams.textureRect.zw + spriteParams.textureRect.xy;
        #endif
        #ifdef HAS_INSTANCING
            v_uv2 = v_uv2 * i_textureRect.zw + i_textureRect.xy;
        #endif
    #endif

    #ifdef HAS_INSTANCING
        #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
            v_color = a_color * i_color;
        #else
            v_color = i_color;
        #endif
    #else
        #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
            v_color = a_color;
        #endif
    #endif

    #ifdef USE_SHADOWS
    for (int i = 0; i < MAX_SHADOWSMAP; ++i){
        #ifdef HAS_NORMALS
            v_lightProjPos[i] = lightVPMatrix[i] * (worldPos + vec4(worldNormal * shadowParams[i].x, 0.0));
        #else
            v_lightProjPos[i] = lightVPMatrix[i] * worldPos;
        #endif
    }
    #endif

    gl_Position = pbrParams.mvpMatrix * pos;
}