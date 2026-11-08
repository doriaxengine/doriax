#version 450
#include "includes/instance_normal.glsl"

// G-buffer geometry pass (vertex). Mirrors depth.vert's position pipeline
// (skinning / morph / terrain / instancing) and additionally carries the surface
// normal into view space for the fragment stage.
//
// The build defines DEPTH_SHADER (see ShaderBuilder) for the same reason depth.vert does:
// it suppresses the morph-normal machinery and, unless HAS_TERRAIN_PBR samples them, the
// terrain texture-coordinate varyings. Skinning and terrain normals are still applied;
// per-target morph-normal blending is not (fine for SSR).

uniform u_vs_gbufferParams {
    mat4 modelMatrix;
    mat4 viewProjectionMatrix;
    mat4 normalMatrix;       // view-space normal matrix: transpose(inverse(view*model))
} gbufferParams;

in vec3 a_position;
out vec2 v_projZW;
out vec3 v_normal;          // view space
#ifdef USE_SEE_THROUGH
    out vec3 v_worldPosition;
#endif

#ifdef HAS_NORMALS
    in vec3 a_normal;
#endif

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
#endif

#if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE)
    out vec2 v_uv1;
#endif
// Terrain carries no texcoord vertex attribute (its UVs are generated from the
// world position in-shader, like mesh.vert). Declaring a_texcoord1 for terrain
// would leave that attribute slot unbound, making the pipeline's vertex layout
// non-continuous and failing sokol validation, so only non-terrain declares it.
#if !defined(HAS_TERRAIN) && (defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE) || (defined(HAS_OCCLUSION_TEXTURE) && !defined(OCCLUSION_UV2)))
    in vec2 a_texcoord1;
#endif
// baked occlusion (material occlusionTexture), on the UV set the material names
#ifdef HAS_OCCLUSION_TEXTURE
    #ifdef OCCLUSION_UV2
        in vec2 a_texcoord2;
    #endif
    out vec2 v_uvOcclusion;
#endif

#include "includes/skinning.glsl"
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
        pos = getTerrainPosition(pos, gbufferParams.modelMatrix);
    #endif

    return vec4(pos, 1.0);
}

vec3 getNormalObj(mat4 boneTransform, vec4 position){
    vec3 normal = vec3(0.0, 0.0, 1.0);
    #ifdef HAS_NORMALS
        normal = a_normal;
        normal = getSkinNormal(normal, boneTransform);
    #endif
    #ifdef HAS_TERRAIN
        // terrain generates its normal from the heightmap (needs HAS_NORMALS)
        normal = getTerrainNormal(normal, position.xyz);
    #endif
    return normalize(normal);
}

void main() {
    mat4 mvpMatrix = gbufferParams.viewProjectionMatrix * gbufferParams.modelMatrix;

    mat4 boneTransform = getBoneTransform();

    vec4 objPos = getPosition(boneTransform);
    vec3 objNormal = getNormalObj(boneTransform, objPos);

    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = mat4(i_matrix_col1, i_matrix_col2, i_matrix_col3, i_matrix_col4);
        vec4 pos = instanceMatrix * objPos;
        // instance matrix sits between model and object; fold it in before the
        // (model+view) normal matrix
        v_normal = normalize(mat3(gbufferParams.normalMatrix) * instanceNormal(mat3(instanceMatrix), objNormal));
    #else
        vec4 pos = objPos;
        v_normal = normalize(mat3(gbufferParams.normalMatrix) * objNormal);
    #endif

    #if defined(HAS_TERRAIN_PBR) && defined(HAS_NORMALS)
        setTerrainShadingAxes(gbufferParams.normalMatrix);
    #endif

    #ifdef HAS_TERRAIN_PBR
        // fills the surface varyings the painted layers are sampled with, and returns
        // the same base-tile UV the branch below computes by hand
        vec2 terrainBaseUV = getTerrainTiledTexture(objPos.xyz);
    #endif

    #if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE)
        #ifdef HAS_TERRAIN_PBR
            v_uv1 = terrainBaseUV;
        #elif defined(HAS_TERRAIN)
            // base-tile terrain UV, matching mesh.vert's getTerrainTiledTexture()
            v_uv1 = (objPos.xz + (terrain.size / 2.0)) / terrain.size * float(terrain.textureBaseTiles);
        #else
            v_uv1 = a_texcoord1;
        #endif
    #endif

    #ifdef HAS_OCCLUSION_TEXTURE
        #ifdef OCCLUSION_UV2
            v_uvOcclusion = a_texcoord2;
        #else
            v_uvOcclusion = a_texcoord1;
        #endif
    #endif

    #ifdef USE_VERTEX_OFFSET
        pos.xyz += getModelOffset(objPos.xyz, (gbufferParams.modelMatrix * pos).xyz);
    #endif

    gl_Position = mvpMatrix * pos;

    v_projZW = gl_Position.zw;
    #ifdef USE_SEE_THROUGH
        vec4 worldPosition = gbufferParams.modelMatrix * pos;
        v_worldPosition = worldPosition.xyz / worldPosition.w;
    #endif
    // No Y flip here: the G-buffer keeps the target's native orientation, and SSR
    // pairs it with the scene color through the flip flag in misc.z.
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}
