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
    #ifdef HOOK_NORMAL
        mat4 worldNormalMatrix;
    #endif
} gbufferParams;

in vec3 a_position;
out vec2 v_projZW;
out vec3 v_normal;          // view space
#ifdef USE_FRAGMENT_HOOKS
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
    #include "includes/instance_matrix.glsl"
#endif

#if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
    out vec2 v_uv1;
#endif
// Terrain carries no texcoord vertex attribute (its UVs are generated from the
// world position in-shader, like mesh.vert). Declaring a_texcoord1 for terrain
// would leave that attribute slot unbound, making the pipeline's vertex layout
// non-continuous and failing sokol validation, so only non-terrain declares it.
#if !defined(HAS_TERRAIN) && (defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE) || (defined(HAS_OCCLUSION_TEXTURE) && !defined(OCCLUSION_UV2)) || defined(HOOK_UV1))
    in vec2 a_texcoord1;
#endif
// baked occlusion (material occlusionTexture), on the UV set the material names
#ifdef HAS_OCCLUSION_TEXTURE
    #ifdef OCCLUSION_UV2
        in vec2 a_texcoord2;
    #endif
    out vec2 v_uvOcclusion;
#endif

#ifdef HOOK_COLOR
    #if defined(HAS_VERTEX_COLOR_VEC3)
        in vec3 a_color;
    #elif defined(HAS_VERTEX_COLOR_VEC4)
        in vec4 a_color;
    #endif
    #ifdef HAS_INSTANCING
        in vec4 i_color;
    #endif
#endif
#ifdef HOOK_FRAGMENT_NORMAL
    out vec3 v_hookNormal;  // world space
#endif
#ifdef HOOK_FRAGMENT_COLOR
    out vec4 v_hookColor;
#endif
#ifdef HOOK_FRAGMENT_INSTANCE
    flat out vec4 v_hookInstance;
#endif
#ifdef USE_VARYINGS
    #include "shader_varyings_vs.glsl"
#endif

#include "includes/parameters_vs.glsl"
#include "includes/skinning.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif
#if defined(USE_VERTEX_HOOKS) || defined(USE_FRAGMENT_HOOKS)
    #define HOOK_NORMAL_MATRIX gbufferParams.worldNormalMatrix
    #include "includes/hooks_vs.glsl"
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
    loadShaderParameters();

    mat4 mvpMatrix = gbufferParams.viewProjectionMatrix * gbufferParams.modelMatrix;

    mat4 boneTransform = getBoneTransform();

    vec4 objPos = getPosition(boneTransform);
    vec3 objNormal = getNormalObj(boneTransform, objPos);

    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = getInstanceMatrix();
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

    #if defined(HAS_BASECOLOR_TEXTURE) || defined(HAS_METALLICROUGHNESS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
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

    #ifdef USE_VERTEX_HOOKS
        VertexContext context = getVertexContext(objPos.xyz, (gbufferParams.modelMatrix * pos).xyz, boneTransform);
    #endif
    #ifdef USE_VERTEX_OFFSET
        pos.xyz += getModelOffset(context);
    #endif
    #ifdef USE_VARYINGS
        Varyings varyings;
        getVaryings(context, varyings);
        writeVaryings(varyings);
    #endif

    gl_Position = mvpMatrix * pos;
    #ifdef USE_CLIP_POSITION
        gl_Position = getHookClipPosition(context, gl_Position);
    #endif

    v_projZW = gl_Position.zw;
    #ifdef USE_FRAGMENT_HOOKS
        vec4 worldPosition = gbufferParams.modelMatrix * pos;
        v_worldPosition = worldPosition.xyz / worldPosition.w;
    #endif
    #ifdef HOOK_FRAGMENT_NORMAL
        v_hookNormal = getHookNormal(boneTransform);
    #endif
    #ifdef HOOK_FRAGMENT_COLOR
        v_hookColor = getHookColor();
    #endif
    #ifdef HOOK_FRAGMENT_INSTANCE
        v_hookInstance = getInstanceCustom();
    #endif
    // No Y flip here: the G-buffer keeps the target's native orientation, and SSR
    // pairs it with the scene color through the flip flag in misc.z.
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}
