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
#include "shader/ShaderUniforms.h"
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

        SubmeshOverride_Material = SubmeshOverride_BaseColorFactor | SubmeshOverride_MetallicFactor |
                                   SubmeshOverride_RoughnessFactor | SubmeshOverride_AlphaCutoff |
                                   SubmeshOverride_EmissiveFactor | SubmeshOverride_AlphaMode |
                                   SubmeshOverride_MaterialName | SubmeshOverride_BaseColorTexture |
                                   SubmeshOverride_EmissiveTexture | SubmeshOverride_MetallicRoughnessTexture |
                                   SubmeshOverride_OcclusionTexture | SubmeshOverride_NormalTexture |
                                   SubmeshOverride_FoliageTransmission
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

        // resolved ids of MeshComponent::customShader/customDepthShader (0 = built-in);
        // cached here so the matching ShaderPool::remove uses the same key
        uint16_t customShaderId = 0;
        uint16_t customDepthShaderId = 0;

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
        int slotVSShadows = -1;
        int slotFSShadows = -1;
        int slotFSPointShadows = -1;
        int slotVSSkinning = -1;
        int slotVSMorphTarget = -1;
        int slotVSTerrain = -1;
        int slotFSTerrainLayers = -1;

        int slotVSDepthParams = -1;
        unsigned int sizeVSDepthParams = 0; // reflected: an older fork declares the block without mvpMatrix
        int slotFSDepthMaterial = -1;
        int slotVSDepthFade = -1;
        int slotVSDepthSkinning = -1;
        int slotVSDepthMorphTarget = -1;
        int slotVSDepthTerrain = -1;

        int slotVSGBufferParams = -1;
        int slotFSGBufferMaterial = -1;
        int slotVSGBufferSkinning = -1;
        int slotVSGBufferMorphTarget = -1;
        int slotVSGBufferTerrain = -1;
        int slotFSGBufferTerrainLayers = -1;

        // custom uniform blocks of the forked shaders, filled from MeshComponent::shaderUniforms
        CustomUniformBlock customVSParams;
        CustomUniformBlock customFSParams;
        CustomUniformBlock customVSDepthParams;
        CustomUniformBlock customFSDepthParams;

        struct CustomTextureBinding
        {
            std::string name;
            std::pair<int, int> slot = {-1, -1};
            TextureRender* render = nullptr;
        };
        std::vector<CustomTextureBinding> customTextureBindings;

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

        // Optional user-forked shader. Project-relative base path (no extension),
        // e.g. "shaders/myMesh" -> shaders/myMesh.vert + shaders/myMesh.frag.
        // Empty = use the built-in Mesh shader. Drives the main render pass only;
        // the gbuffer pass keeps the built-in shader.
        std::string customShader;
        // Same for the depth shader (shadow maps, and the SSAO/post-process depth when SSR
        // is off; with SSR on those read the built-in gbuffer), so a fork that displaces
        // vertices or discards keeps its shadows in step.
        std::string customDepthShader;
        // values of the fork's custom uniform blocks, by member name, shared by all submeshes
        ShaderUniformValues shaderUniforms;
        bool needUpdateShaderUniforms = false;

        HybridArray<Matrix4, MAX_BONES> bonesMatrix;
        BufferRender bonesBuffer;
        TextureRender bonesTexture;
        bool needUpdateBones = false;
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

        std::vector<AABB> bonesAABB; // bind-pose bounds per bone index, empty when not skinned
        AABB skinnedAABB; // local bounds of the posed mesh, null when not skinned

        bool receiveLights = true;
        bool receiveIBL = false; // image-based lighting from a sky environment or a reflection probe
        bool castShadows = true;
        bool receiveShadows = true;
        bool shadowsBillboard = true;
        bool renderInReflectionProbes = true; // when false the mesh is skipped by probe captures

        bool transparent = false;
        bool autoTransparency = true;

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
