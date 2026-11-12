// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef MESH_COMPONENT_H
#define MESH_COMPONENT_H

#include "Engine.h"
#include "util/HybridArray.h"
#include "math/Vector3.h"
#include "math/Quaternion.h"
#include "math/AABB.h"
#include "buffer/Buffer.h"
#include "render/ObjectRender.h"
#include "render/TextureRender.h"
#include "texture/TextureData.h"
#include "texture/Material.h"
#include "shader/ShaderParameters.h"
#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "buffer/ExternalBuffer.h"
#include "pool/MeshLodPool.h"
#include "math/Rect.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doriax{

    // Submesh fields a model load rewrites from its source file, so an edit to one only survives
    // the load while it is flagged in Submesh::overrideFields.
    enum SubmeshOverrideFlags : uint32_t {
        SubmeshOverride_BaseColorFactor          = 1 << 0,
        SubmeshOverride_MetallicFactor           = 1 << 1,
        SubmeshOverride_RoughnessFactor          = 1 << 2,
        SubmeshOverride_AlphaCutoff              = 1 << 3,
        SubmeshOverride_EmissiveFactor           = 1 << 4,
        SubmeshOverride_AlphaMode                = 1 << 5,
        SubmeshOverride_MaterialName             = 1 << 6,
        SubmeshOverride_BaseColorTexture         = 1 << 7,
        SubmeshOverride_EmissiveTexture          = 1 << 8,
        SubmeshOverride_MetallicRoughnessTexture = 1 << 9,
        SubmeshOverride_OcclusionTexture         = 1 << 10,
        SubmeshOverride_NormalTexture            = 1 << 11,
        SubmeshOverride_FaceCulling              = 1 << 12,
        SubmeshOverride_TextureShadow            = 1 << 13,
        SubmeshOverride_PrimitiveType            = 1 << 14,
        SubmeshOverride_FoliageTransmission      = 1 << 15,
        SubmeshOverride_Shader                   = 1 << 16,
        SubmeshOverride_ShaderUniforms           = 1 << 17,
        SubmeshOverride_ShaderTextures           = 1 << 18,

        SubmeshOverride_Material = SubmeshOverride_BaseColorFactor | SubmeshOverride_MetallicFactor |
                                   SubmeshOverride_RoughnessFactor | SubmeshOverride_AlphaCutoff |
                                   SubmeshOverride_EmissiveFactor | SubmeshOverride_AlphaMode |
                                   SubmeshOverride_MaterialName | SubmeshOverride_BaseColorTexture |
                                   SubmeshOverride_EmissiveTexture | SubmeshOverride_MetallicRoughnessTexture |
                                   SubmeshOverride_OcclusionTexture | SubmeshOverride_NormalTexture |
                                   SubmeshOverride_FoliageTransmission | SubmeshOverride_Shader |
                                   SubmeshOverride_ShaderUniforms | SubmeshOverride_ShaderTextures
    };

    struct DORIAX_API Submesh{
        Material material;
        std::map<AttributeType, Attribute> attributes;

        ObjectRender render;
        ObjectRender depthRender;
        ObjectRender gbufferRender;

        std::shared_ptr<ShaderRender> shader;
        std::shared_ptr<ShaderRender> depthShader;
        std::shared_ptr<ShaderRender> gbufferShader;

        uint32_t shaderProperties = 0;
        uint32_t depthShaderProperties = 0;
        uint32_t gbufferShaderProperties = 0;

        // the shader file's id each pass was taken with (0 = built-in), so the
        // matching ShaderPool::remove uses the same key
        uint16_t shaderFileId = 0;   // the file asked for; customShaderId is 0 once it failed to build
        uint16_t customShaderId = 0;
        uint16_t customDepthShaderId = 0;
        uint16_t customGBufferShaderId = 0;

        int slotVSParams = -1;
        int slotFSParams = -1;
        int slotFSTexCoordSets = -1;
        int slotVSFade = -1;
        int slotFSLighting = -1;
        int slotFSReflectionProbe = -1;
        int slotFSLighting2D = -1;
        int slotFSFog = -1;
        int slotFSMirror = -1;
        int slotVSSprite = -1;
        int slotFSShadows = -1;
        unsigned int sizeFSShadows = 0; // without the point lights in a directional-only variant
        int slotVSSkinning = -1;
        int slotVSMorphTarget = -1;
        int slotVSTerrain = -1;
        int slotFSTerrainLayers = -1;
        // the scene its shader file samples
        std::pair<int, int> slotSceneColor = {-1, -1};
        std::pair<int, int> slotSceneDepth = {-1, -1};

        int slotVSDepthParams = -1;
        unsigned int sizeVSDepthParams = 0; // reflected: the normal matrix is there only when a hook reads normals
        int slotFSDepthMaterial = -1;
        unsigned int sizeFSDepthMaterial = 0;
        int slotVSDepthFade = -1;
        int slotVSDepthSkinning = -1;
        int slotVSDepthMorphTarget = -1;
        int slotVSDepthTerrain = -1;

        int slotVSGBufferParams = -1;
        unsigned int sizeVSGBufferParams = 0;
        int slotFSGBufferMaterial = -1;
        int slotVSGBufferSkinning = -1;
        int slotVSGBufferMorphTarget = -1;
        int slotVSGBufferTerrain = -1;
        int slotFSGBufferTerrainLayers = -1;

        // declared parameters of each pass and stage, filled from the material's shaderUniforms
        ShaderParameterBlock paramsVS;
        ShaderParameterBlock paramsFS;
        ShaderParameterBlock depthParamsVS;
        ShaderParameterBlock depthParamsFS;
        ShaderParameterBlock gbufferParamsVS;
        ShaderParameterBlock gbufferParamsFS;

        Rect textureRect = Rect(0.0, 0.0, 1.0, 1.0);

        PrimitiveType primitiveType = PrimitiveType::TRIANGLES;
        unsigned int vertexCount = 0;

        uint32_t overrideFields = 0; // SubmeshOverrideFlags edited after import, kept across loads

        bool faceCulling = true;
        bool textureShadow = false;

        bool hasTexCoord1 = false;
        bool hasTexCoord2 = false;
        bool hasNormal = false;
        bool hasIBL = false;
        bool hasNormalMap = false;
        bool hasTangent = false;
        bool hasVertexColor3 = false;
        bool hasVertexColor4 = false;
        bool hasTextureRect = false;
        bool hasSkinning = false;
        bool hasMorphTarget = false;
        bool hasMorphNormal = false;
        bool hasMorphTangent = false;

        bool alphaBlend = false;
        bool needUpdateTexture = false;
        bool needUpdateDepthTexture = false;
        bool needUpdateGBufferTexture = false;

        bool generated = false;

        float normAdjustJoint = 1.0f;
        float normAdjustWeight = 1.0f;
        bool hasSkinningNormalization = false;

        // detail levels over the same vertices, keyed by the geometry they were built from
        std::shared_ptr<MeshLodData> lodData;
        ExternalBuffer lodIndices;
        uint64_t lodKey = 0;
    };

    struct MeshComponent{
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;
        IndexBuffer indices;
        HybridArray<ExternalBuffer, MAX_EXTERNAL_BUFFERS> eBuffers;
        unsigned int numExternalBuffers = 0;

        unsigned int vertexCount = 0;

        HybridArray<Submesh, MAX_SUBMESHES> submeshes;
        unsigned int numSubmeshes = 0;

        float vertexOffsetMax = 0.0f; // longest vertex offset, widens the bounds; 0 = no limit
        // the materials' shader values changed
        bool needUpdateShaderUniforms = false;

        float normAdjustJoint = 1;
        float normAdjustWeight = 1;

        // planar reflection: logical view-projection of the mirror's reflection
        // camera, set each frame by RenderSystem::updateMirrors, uploaded to the
        // USE_MIRROR shader for projective sampling of the reflection texture
        Matrix4 mirrorViewProjection;

        float morphWeights[MAX_MORPHTARGETS] = {};

        AABB aabb = AABB::ZERO;
        AABB verticesAABB = AABB::ZERO; // is not influenced by instances
        AABB worldAABB; // initially NULL

        AABB skinnedAABB; // local bounds of the posed mesh, null when not skinned

        bool receiveLights = true;
        bool receiveIBL = false; // image-based lighting from a sky environment or a reflection probe
        bool castShadows = true;
        bool receiveShadows = true;
        bool shadowsBillboard = true;
        bool renderInReflectionProbes = true; // when false the mesh is skipped by probe captures

        bool transparent = false;
        bool autoTransparency = true;
        // a material's shader file samples the scene, so the mesh draws after what it samples
        bool readsSceneColor = false;
        bool readsSceneDepth = false;

        // opt-out of the scene's detail levels (Scene::setMeshLodEnabled); bias above 1 keeps detail longer
        bool lodEnabled = true;
        float lodBias = 1.0f;
        float lodError[MAX_MESH_LODS] = {}; // largest error of any submesh at each level
        unsigned int numLods = 1;

        CullingMode cullingMode = CullingMode::BACK;
        WindingOrder windingOrder = WindingOrder::CCW;

        bool needUpdateAABB = false;
        bool needUpdateBuffer = false;
        bool needReload = false;
    };
    
}

#endif //MESH_COMPONENT_H
