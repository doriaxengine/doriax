// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef MESHSYSTEM_H
#define MESHSYSTEM_H

#include "SubSystem.h"


#include "component/MeshComponent.h"
#include "component/ModelComponent.h"
#include "component/SkinComponent.h"
#include "component/SpriteComponent.h"
#include "component/MeshPolygonComponent.h"
#include "component/CameraComponent.h"
#include "component/InstancedMeshComponent.h"
#include "component/TerrainComponent.h"
#include "component/TilemapComponent.h"
#include "component/Transform.h"
#include "math/Ray.h"

#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace tinygltf {
    struct Accessor;
    struct Image;
    class Model;
    class TinyGLTF;
}

namespace doriax{

    class DORIAX_API MeshSystem : public SubSystem {

    private:
        struct AsyncModelLoadResult;
        typedef std::unordered_map<std::string, std::shared_future<std::shared_ptr<AsyncModelLoadResult>>> async_model_loads_t;

        static std::mutex& getAsyncModelMutex();
        static async_model_loads_t& getPendingModelLoads();

        // Files that already failed to load, so createOrUpdateModel stops retrying them.
        std::set<std::string> failedModelLoads;

        // One instanced entity per chunk, so each is culled on its own AABB. Slots are recycled as
        // the ring follows the camera: a chunk that stays in it keeps its mesh and its instances.
        struct TerrainFoliageChunk{
            Entity entity = NULL_ENTITY;
            int chunkX = 0;
            int chunkZ = 0;
            bool assigned = false; //false until the slot holds the resolve of the coordinate above
            bool meshLoaded = false;
        };

        struct TerrainFoliageInstances{
            std::vector<TerrainFoliageChunk> chunks; //grid indexed by wrapped coordinates
            std::string loadedMeshPath;
            bool loadFailed = false;
            float chunkSize = 0; //the size the slot coordinates are in
            bool needUpdate = true;
            bool pending = false;
        };

        Entity foliagePreviewEntity = NULL_ENTITY;

        // Resolve of each terrain's foliageLayers. Kept here so the authored component stays copiable.
        std::unordered_map<Entity, std::vector<TerrainFoliageInstances>> terrainFoliage;

        static void decodeGLTFImage(tinygltf::Image& image, size_t index, int maxDimension);
        template<typename Fn>
        static void parallelForIndexed(size_t count, Fn&& fn);
        static void decodeGLTFImagesParallel(tinygltf::Model& model, int maxDimension);
        static std::shared_ptr<std::array<TextureData,6>> buildGLTFTextureFaces(tinygltf::Model& model, int textureIndex);

        bool createSprite(SpriteComponent& sprite, MeshComponent& mesh, CameraComponent& camera);
        bool createMeshPolygon(MeshPolygonComponent& polygon, MeshComponent& mesh);
        bool createTilemap(TilemapComponent& tilemap, MeshComponent& mesh);

        void changeFlipY(bool& flipY, CameraComponent& camera, MeshComponent& mesh);
        static Rect normalizeTileRect(Rect tileRect, unsigned int texWidth, unsigned int texHeight);

        // Mesh aux
        std::vector<float> getCylinderSideNormals(float baseRadius, float topRadius, float height, float slices);
        std::vector<float> buildUnitCircleVertices(float slices);

        // Model
        static std::string readFileToString(const char* filename);
        static bool fileExists(const std::string &abs_filename, void *);
        static bool readWholeFile(std::vector<unsigned char> *out, std::string *err, const std::string &filepath, void *);
        static bool getFileSizeInBytes(size_t *filesize_out, std::string *err, const std::string &filepath, void *userdata);
        // An FBX is read from its imported .glb, with texture URIs still relative to the FBX
        static bool loadGLTFFile(tinygltf::TinyGLTF& loader, tinygltf::Model* model, std::string* err, std::string* warn, const std::string& filename);
        static std::string getAsyncModelLoadScenePrefix(const Scene* scene);
        static std::string getAsyncModelLoadKey(const Scene* scene, const std::string& filename);
        std::string getAsyncModelLoadKey(const std::string& filename) const;
        static std::shared_ptr<AsyncModelLoadResult> pollOrStartAsyncModelLoad(const Scene* scene, const std::string& filename, bool obj);
        static void cancelAsyncModelLoadByKey(const std::string& key);
        bool isAsyncModelLoadPending(const std::string& filename) const;
        static bool isAsyncModelLoadPending(const Scene* scene, const std::string& filename);
        static bool isModelPreloading(const std::string& filename);
        static std::shared_ptr<AsyncModelLoadResult> loadModelFileOnWorker(const std::string& filename, bool obj, uint64_t buildId);
        static std::shared_ptr<void> poolModelResult(const std::string& poolKey, const AsyncModelLoadResult& result);
        template<typename T>
        static bool isValidGLTFIndex(int index, const std::vector<T>& values) {
            return index >= 0 && static_cast<size_t>(index) < values.size();
        }
        static void applyDefaultGLTFMaterial(Material& material);
        static void applyDefaultObjMaterial(Submesh& submesh);
        static void applySubmeshOverride(const Submesh& saved, Submesh& submesh);
        void addSubmeshAttribute(Submesh& submesh, const std::string& bufferName, AttributeType attribute, unsigned int elements, AttributeDataType dataType, size_t size, size_t offset, bool normalized);
        bool loadGLTFBuffer(int bufferViewIndex, MeshComponent& mesh, ModelComponent& model, const int stride, std::vector<std::string>& loadedBuffers);
        int convertGLTFByteIndicesToShort(const tinygltf::Accessor& indexAccessor, ModelComponent& model);
        int materializeGLTFSparseAccessor(tinygltf::Accessor& accessor, ModelComponent& model);
        int bakeGLTFTransformedAttribute(const tinygltf::Accessor& accessor, const Matrix4& matrix, const Matrix3& normalMatrix, bool isNormal, ModelComponent& model);
        int bakeGLTFTransformedTangent(const tinygltf::Accessor& accessor, const Matrix3& tangentMatrix, ModelComponent& model);
        // The bake helpers need float positions, normals and tangents, and no morph targets
        bool canBakeGLTFMesh(const ModelComponent& model, int meshIndex, std::string* reason = nullptr) const;
        int convertGLTFColorToVec4(const tinygltf::Accessor& accessor, ModelComponent& model);
        bool loadGLTFTexture(int textureIndex, ModelComponent& model, Texture& texture, const std::string& textureName);
        std::string getBufferName(int bufferViewIndex, ModelComponent& model);
        Matrix4 getGLTFNodeMatrix(int nodeIndex, ModelComponent& model);
        Matrix4 getGLTFMeshGlobalMatrix(int nodeIndex, ModelComponent& model, std::map<int, int>& nodesParent);
        Matrix4 getGLTFInverseBindMatrix(const ModelComponent& model, int skinIndex, size_t jointIndex);
        bool buildGLTFNodeHierarchy(Entity entity, ModelComponent& model, const std::map<int, int>& nodesParent);
        // Children left without a node stay in the scene, dropped from the mapping
        void remapMeshNodesByName(ModelComponent& model, const std::vector<int>& meshNodes);
        void buildGLTFSkinBindings(Entity entity, ModelComponent& model);
        Entity generateSketetalStructure(Entity entity, ModelComponent& model, int nodeIndex, int skinIndex);
        TextureFilter convertFilter(int filter);
        TextureWrap convertWrap(int wrap);

        // Terrain
        size_t getTerrainGridArraySize(int rootGridSize, int levels);
        void getTerrainHeightRangeArea(TerrainComponent& terrain, float x, float z, float w, float h, float& minHeight, float& maxHeight);
        void createPlaneNodeSubmesh(unsigned int submeshIndex, TerrainComponent& terrain, MeshComponent& mesh, int width, int height, int widthSegments, int heightSegments);
        void resetTerrainToUnbuilt(TerrainComponent& terrain, MeshComponent& mesh);
        bool createTerrain(TerrainComponent& terrain, MeshComponent& mesh);
        void createTerrainNode(TerrainComponent& terrain, float x, float y, float size, int lodDepth);
        void updateTerrainAutoRanges(TerrainComponent& terrain);

        Entity createFoliageEntity(unsigned int capacity);
        void destroyFoliageEntity(TerrainFoliageChunk& chunk);
        void destroyFoliageInstances(TerrainFoliageInstances& instances);
        void destroyTerrainFoliage(Entity entity);
        bool loadFoliageMesh(Entity entity, const std::string& path);
        float sampleFoliageDensity(TerrainComponent& terrain, TerrainFoliageLayer& layer, float localX, float localZ);
        void appendFoliageCell(TerrainComponent& terrain, TerrainFoliageLayer& layer, int cellX, int cellZ, std::vector<InstanceData>& instances);
        void updateFoliageLayer(TerrainComponent& terrain, TerrainFoliageLayer& layer, TerrainFoliageInstances& instances, const Vector3& viewLocal, bool preview);
        void updateTerrainFoliage(Entity entity, TerrainComponent& terrain, Transform& transform);

    public:
        MeshSystem(Scene* scene);
        virtual ~MeshSystem();

        // Frames are authored in pixels, textureRect is in UV space
        static void setSpriteFrameRect(MeshComponent& mesh, SpriteComponent& sprite, Rect frameRect);
        // true when a frame of another size needs the sprite rebuilt for its texture scale factor
        static bool changesSpriteInset(const SpriteComponent& sprite, const Rect& oldRect, const Rect& newRect);

        bool setFoliagePreviewEntity(Entity entity);
        bool hasPendingFoliageUpdates() const;

        // Foliage ownership, for editor shader collection and selection.
        std::vector<Entity> getFoliageEntities(Entity terrainEntity) const;
        Entity getFoliageOwner(Entity foliageEntity) const;
        // Reloads the foliage chunks drawn from a model file that changed on disk
        void reloadFoliageMesh(const std::string& filename);
        // Retries the failed loads of a file that appeared or changed on disk
        void retryFailedModelLoads(const std::string& filename);

        void createPlane(MeshComponent& mesh, float width=1, float depth=1, unsigned int tiles=1);
        void createWall(MeshComponent& mesh, float width=1, float height=1, unsigned int tiles=1);
        void createBox(MeshComponent& mesh, float width=1, float height=1, float depth=1, unsigned int tiles=1);
        void createSphere(MeshComponent& mesh, float radius=1, unsigned int slices=36, unsigned int stacks=18);
        void createCylinder(MeshComponent& mesh, float baseRadius=1, float topRadius=1, float height=2, unsigned int slices=36, unsigned int stacks=18);
        void createCapsule(MeshComponent& mesh, float baseRadius=1, float topRadius=1, float height=2, unsigned int slices=36, unsigned int stacks=18);
        void createTorus(MeshComponent& mesh, float radius=1, float ringRadius=0.5, unsigned int sides=36, unsigned int rings=16);
        bool canMergeStaticModel(const ModelComponent& model, std::string* reason = nullptr) const;
        bool canEditModelHierarchy(const ModelComponent& model, std::string* reason = nullptr) const;
        // Whether a part or local group can move without breaking the model's animation.
        bool canEditModelPart(const ModelComponent& model, Entity part, std::string* reason = nullptr) const;
        // Where the file puts each node (by glTF index): its parent's entity, or the model on the flat path
        std::map<int, Entity> getModelNodeDefaultParents(Entity modelEntity, const ModelComponent& model) const;
        // The glTF node name, empty when the file has none or the model is not loaded
        static std::string getModelNodeName(const ModelComponent& model, int nodeIdx);
        bool hasCustomMeshParenting(Entity modelEntity, const ModelComponent& model) const;

        // Canonical form of a model path: the same file spelled in different ways maps to one key.
        static std::string getModelFilenameKey(const std::string& filename);
        // The .glb the editor imports an FBX into
        static std::string getImportedModelPath(const std::string& filename);

        // Submesh edits (Submesh::overrideFields) carried across a load, keyed by the ordinal of
        // the primitive that built each one. The loaders do this themselves; it is public for the
        // editor, which destroys the generated mesh entities before reloading.
        typedef std::map<unsigned int, Submesh> SubmeshOverrides;
        SubmeshOverrides collectSubmeshOverrides(Entity entity, const ModelComponent& model) const;
        void applySubmeshOverrides(const SubmeshOverrides& overrides, Entity entity, const ModelComponent& model) const;
        std::vector<std::pair<MeshComponent*, unsigned int>> getSubmeshRuns(Entity entity, const ModelComponent& model) const;

        // The settings of a model's meshes that its file does not give, carried across a reload
        // like the submesh edits. Keyed by each part's node index and the file's name for it, and
        // by -1 for the model's, as its getters read them. Another file is a fresh import.
        struct MeshSettings{
            bool receiveLights;
            bool receiveIBL;
            bool castShadows;
            bool receiveShadows;
            bool shadowsBillboard;
            bool renderInReflectionProbes;
            bool transparent;
            bool autoTransparency;
            bool lodEnabled;
            float lodBias;
            std::string customShader;
            std::string customDepthShader;
            ShaderUniformValues shaderUniforms;
        };
        typedef std::map<std::pair<int, std::string>, MeshSettings> MeshSettingsByNode;
        MeshSettingsByNode collectMeshSettings(Entity entity, const ModelComponent& model, const std::string& filename) const;
        void applyMeshSettings(const MeshSettingsByNode& settings, Entity entity, const ModelComponent& model) const;

        // The meshes of a model with the named morph target, and its index in each
        std::vector<std::pair<MeshComponent*, int>> findMorphTargets(Entity entity, const ModelComponent& model, const std::string& name) const;

        bool loadGLTF(Entity entity, const std::string filename, bool asyncLoad=false, bool skipEntities=false, bool changeRootTransform=true);
        bool loadOBJ(Entity entity, const std::string filename, bool asyncLoad=false);

        // Parses a model file into ModelPool without a scene, for SceneManager::preloadScene().
        // False while it loads; data then holds the model, or stays empty when it failed.
        static bool preloadModel(const std::string& filename, std::shared_ptr<void>& data);
        static void cancelPreloadModel(const std::string& filename);
        // Drops a preloaded model and its images from the pools, unless a scene uses them
        static void releasePreloadedModel(const std::string& filename);
        // Drops the cached parse and images of a file that changed on disk, even while in use
        static void invalidateModelFile(const std::string& filename);
        // Parses a changed file in place of its cached model, which stays when the parse fails
        static bool reparseModelFile(const std::string& filename);

        // Caps the resolution glTF images are decoded to on the CALLING thread (0 = full resolution).
        // Used by the thumbnail worker so previews don't decode/upload full-size 4K maps.
        static void setImageDecodeMaxDimension(int dimension);

        void createInstancedMesh(Entity entity);
        void removeInstancedMesh(Entity entity);
        bool hasInstancedMesh(Entity entity) const;

        void clearBoneMapping(ModelComponent& model);
        void clearAnimationMapping(ModelComponent& model);
        void clearMeshNodeMapping(ModelComponent& model);
        void destroyModel(ModelComponent& model);

        void resetModelToBindPose(Entity entity, ModelComponent& model);

        bool raycastTerrainSurface(const Ray& ray, TerrainComponent& terrain, Transform& transform, Vector3& worldPoint);
        // Height and normal in terrain-local space, the surface the foliage scatter uses
        void sampleTerrainSurface(TerrainComponent& terrain, float localX, float localZ, float& height, Vector3& normal);

        bool hasPendingAsyncModelLoads() const;
        void cancelAsyncModelLoads();
        static void cancelAllAsyncModelLoads();
        bool isAsyncModelLoadPending(Entity entity, const std::string& filename) const;
        void cancelAsyncModelLoad(Entity entity, const std::string& filename);

        bool createOrUpdateSprite(SpriteComponent& sprite, MeshComponent& mesh);
        bool createOrUpdateTerrain(TerrainComponent& terrain, MeshComponent& mesh, Transform& transform);
        bool createOrUpdateMeshPolygon(MeshPolygonComponent& polygon, MeshComponent& mesh);
        bool createOrUpdateTilemap(TilemapComponent& tilemap, MeshComponent& mesh);
        bool createOrUpdateModel(Entity entity, ModelComponent& model);

        void calculateMeshAABB(MeshComponent& mesh, SkinComponent* skin = nullptr);

        void load() override;
        void draw() override;
        void destroy() override;
        void update(double dt) override;

        void onComponentAdded(Entity entity, ComponentId componentId) override;
        void onComponentRemoved(Entity entity, ComponentId componentId) override;
    };

}

#endif //MESHSYSTEM_H
