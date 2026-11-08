#version 450

// A fork may declare a u_vs_customParams block: its members are filled by name from the
// component's shader uniforms (Properties window, setShaderUniform). Two names are
// written by the engine instead: "time" (seconds since startup) and "resolution"
// (xy = render target size, zw = 1 / size). Do not mix int and float members: GL
// uploads the block typed after its first member.
//
// The fork drives the color pass only: to keep shadows (and SSAO, when SSR is off) in
// step with positions displaced here, fork the depth shader too (Depth Shader in Properties).
//
//   uniform u_vs_customParams {
//       float time;
//       vec4 tint;
//   } customParams;

uniform u_vs_pbrParams {
    mat4 modelMatrix;
    mat4 normalMatrix;
    mat4 mvpMatrix;
} pbrParams;

// same expression as depth.vert, so a depth prepass and this pass agree bit for bit
invariant gl_Position;

#ifdef USE_INSTANCE_FADE
    uniform u_vs_fade {
        vec4 fadeRange; //start.x, end.y
        vec4 fadeEye;   //camera in model space.xyz
    } fadeParams;
#endif

#if defined(HAS_TEXTURERECT) && (defined(HAS_UV_SET1) || defined(HAS_UV_SET2))
    uniform u_vs_spriteParams {
        vec4 textureRect;
    } spriteParams;
#endif

in vec3 a_position;

#if !defined(MATERIAL_UNLIT) || defined(USE_MIRROR) || defined(USE_LIGHT2D) || defined(USE_SEE_THROUGH)
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

// with MSAA an edge pixel is shaded at its center, which can lie outside a sprite
// or tile quad; centroid keeps the atlas UV inside the rect
#ifdef HAS_UV_SET1
    #ifndef HAS_TERRAIN
        in vec2 a_texcoord1;
    #endif
    #ifdef HAS_TEXTURERECT
        centroid out vec2 v_uv1;
    #else
        out vec2 v_uv1;
    #endif
#endif

#ifdef HAS_UV_SET2
    in vec2 a_texcoord2;
    #ifdef HAS_TEXTURERECT
        centroid out vec2 v_uv2;
    #else
        out vec2 v_uv2;
    #endif
#endif

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 a_color;
#endif

#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 a_color;
#endif
// instancing multiplies by the vec4 instance color, so a vec3 attribute is promoted
#if defined(HAS_VERTEX_COLOR_VEC3) && !defined(HAS_INSTANCING)
    out vec3 v_color;
#endif
#if defined(HAS_VERTEX_COLOR_VEC4) || defined(HAS_INSTANCING)
    out vec4 v_color;
#endif

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
    in vec4 i_color;
    #if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
        in vec4 i_textureRect;
    #endif
#endif

#include "includes/skinning.glsl"
#include "includes/instance_normal.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif
#ifdef USE_VERTEX_OFFSET
    #include "includes/vertex_offset.glsl"
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

    vec4 localPos = getPosition(boneTransform);
    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = mat4(i_matrix_col1, i_matrix_col2, i_matrix_col3, i_matrix_col4);
        vec4 pos = instanceMatrix * localPos;
    #else
        vec4 pos = localPos;
    #endif

    // before the fade, so a faded instance still shrinks to nothing
    #ifdef USE_VERTEX_OFFSET
        pos.xyz += getModelOffset(localPos.xyz, (pbrParams.modelMatrix * pos).xyz);
    #endif

    #ifdef USE_INSTANCE_FADE
        // Shrink toward the instance base, on XZ like the chunk selection so looking down does
        // not eat grass still inside the spawn square. An empty range keeps everything solid.
        vec3 fadeOrigin = i_matrix_col4.xyz;
        float fadeVisible = 1.0;
        if (fadeParams.fadeRange.y > fadeParams.fadeRange.x){
            float fadeDistance = length(fadeParams.fadeEye.xz - fadeOrigin.xz);
            fadeVisible = clamp((fadeParams.fadeRange.y - fadeDistance) /
                (fadeParams.fadeRange.y - fadeParams.fadeRange.x), 0.0, 1.0);
        }
        pos.xyz = mix(fadeOrigin, pos.xyz, fadeVisible);
    #endif

    vec4 worldPos = pbrParams.modelMatrix * pos;

    #if !defined(MATERIAL_UNLIT) || defined(USE_MIRROR) || defined(USE_LIGHT2D) || defined(USE_SEE_THROUGH)
        v_position = vec3(worldPos.xyz) / worldPos.w;
    #endif

    #ifdef HAS_NORMALS
        vec3 objectNormal = getNormal(boneTransform, pos);
        #ifdef HAS_INSTANCING
            objectNormal = instanceNormal(mat3(instanceMatrix), objectNormal);
        #endif
        vec3 worldNormal = normalize(vec3(pbrParams.normalMatrix * vec4(objectNormal, 0.0)));
    #ifdef HAS_TANGENTS
        vec3 tangent = getTangent(boneTransform);
        #ifdef HAS_INSTANCING
            tangent = mat3(instanceMatrix) * tangent;
        #endif
        vec3 tangentW = normalize(vec3(pbrParams.modelMatrix * vec4(tangent, 0.0)));
        vec3 bitangentW = cross(worldNormal, tangentW) * a_tangent.w;
        v_tbn = mat3(tangentW, bitangentW, worldNormal);
    #else // !HAS_TANGENTS
        v_normal = worldNormal;
    #endif
    #endif

    #if defined(HAS_TERRAIN_PBR) && defined(HAS_NORMALS)
        setTerrainShadingAxes(pbrParams.normalMatrix);
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
        #ifdef HAS_VERTEX_COLOR_VEC3
            v_color = vec4(a_color, 1.0) * i_color;
        #elif defined(HAS_VERTEX_COLOR_VEC4)
            v_color = a_color * i_color;
        #else
            v_color = i_color;
        #endif
    #else
        #if defined(HAS_VERTEX_COLOR_VEC3) || defined(HAS_VERTEX_COLOR_VEC4)
            v_color = a_color;
        #endif
    #endif

    gl_Position = pbrParams.mvpMatrix * pos;
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}
