// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#ifndef MESHSYSTEM_H
#define MESHSYSTEM_H

#include "SubSystem.h"


#include "component/MeshComponent.h"
#include "component/ModelComponent.h"
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

        static void decodeGLTFImage(tinygltf::Image& image, size_t index, int maxDimension);
        template<typename Fn>
        static void parallelForIndexed(size_t count, Fn&& fn);
        static void decodeGLTFImagesParallel(tinygltf::Model& model, int maxDimension);
        static std::shared_ptr<std::array<TextureData,6>> buildGLTFTextureFaces(tinygltf::Model& model, int textureIndex);

        bool createSprite(SpriteComponent& sprite, MeshComponent& mesh, CameraComponent& camera);
        bool createMeshPolygon(MeshPolygonComponent& polygon, MeshComponent& mesh);
        bool createTilemap(TilemapComponent& tilemap, MeshComponent& mesh);

        void changeFlipY(bool& flipY, CameraComponent& camera, MeshComponent& mesh);
        Rect normalizeTileRect(Rect tileRect, unsigned int texWidth, unsigned int texHeight);

        // Mesh aux
        std::vector<float> getCylinderSideNormals(float baseRadius, float topRadius, float height, float slices);
        std::vector<float> buildUnitCircleVertices(float slices);

        // Model
        static std::string readFileToString(const char* filename);
        static bool fileExists(const std::string &abs_filename, void *);
        static bool readWholeFile(std::vector<unsigned char> *out, std::string *err, const std::string &filepath, void *);
        static bool getFileSizeInBytes(size_t *filesize_out, std::string *err, const std::string &filepath, void *userdata);
        static std::string getAsyncModelLoadScenePrefix(const Scene* scene);
        static std::string getAsyncModelLoadKey(const Scene* scene, const std::string& filename);
        std::string getAsyncModelLoadKey(const std::string& filename) const;
        std::shared_ptr<AsyncModelLoadResult> pollOrStartAsyncModelLoad(const std::string& filename, bool obj);
        bool isAsyncModelLoadPending(const std::string& filename) const;
        static std::shared_ptr<AsyncModelLoadResult> loadModelFileOnWorker(const std::string& filename, bool obj, uint64_t buildId);
        template<typename T>
        static bool isValidGLTFIndex(int index, const std::vector<T>& values) {
            return index >= 0 && static_cast<size_t>(index) < values.size();
        }
        static void applyDefaultGLTFMaterial(Material& material);
        static void applyDefaultObjMaterial(Submesh& submesh);
        std::vector<std::pair<MeshComponent*, unsigned int>> getSubmeshRuns(Entity entity, const ModelComponent& model) const;
        static void applySubmeshOverride(const Submesh& saved, Submesh& submesh);
        void addSubmeshAttribute(Submesh& submesh, const std::string& bufferName, AttributeType attribute, unsigned int elements, AttributeDataType dataType, size_t size, size_t offset, bool normalized);
        bool loadGLTFBuffer(int bufferViewIndex, MeshComponent& mesh, ModelComponent& model, const int stride, std::vector<std::string>& loadedBuffers);
        int convertGLTFByteIndicesToShort(const tinygltf::Accessor& indexAccessor, ModelComponent& model);
        int materializeGLTFSparseAccessor(tinygltf::Accessor& accessor, ModelComponent& model);
        int bakeGLTFTransformedAttribute(const tinygltf::Accessor& accessor, const Matrix4& matrix, const Matrix3& normalMatrix, bool isNormal, ModelComponent& model);
        int bakeGLTFTransformedTangent(const tinygltf::Accessor& accessor, const Matrix3& tangentMatrix, ModelComponent& model);
        int convertGLTFColorToVec4(const tinygltf::Accessor& accessor, ModelComponent& model);
        bool loadGLTFTexture(int textureIndex, ModelComponent& model, Texture& texture, const std::string& textureName);
        std::string getBufferName(int bufferViewIndex, ModelComponent& model);
        Matrix4 getGLTFNodeMatrix(int nodeIndex, ModelComponent& model);
        Matrix4 getGLTFMeshGlobalMatrix(int nodeIndex, ModelComponent& model, std::map<int, int>& nodesParent);
        Matrix4 getGLTFInverseBindMatrix(const ModelComponent& model, int skinIndex, size_t jointIndex);
        bool buildGLTFNodeHierarchy(Entity entity, ModelComponent& model, const std::map<int, int>& nodesParent);
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
        bool loadFoliageMesh(Entity entity, const std::string& path);
        void sampleTerrainSurface(TerrainComponent& terrain, float localX, float localZ, float& height, Vector3& normal);
        float sampleFoliageDensity(TerrainComponent& terrain, TerrainFoliageLayer& layer, float localX, float localZ);
        void fillFoliageChunk(TerrainComponent& terrain, TerrainFoliageLayer& layer, float chunkSize, int chunkX, int chunkZ, std::vector<InstanceData>& instances);
        void updateFoliageLayer(TerrainComponent& terrain, TerrainFoliageLayer& layer, TerrainFoliageInstances& instances, const Vector3& terrainPosition, const Quaternion& terrainRotation, const Vector3& terrainScale, const Vector3& viewLocal);
        void updateTerrainFoliage(TerrainComponent& terrain, Transform& transform);

    public:
        MeshSystem(Scene* scene);
        virtual ~MeshSystem();

        void createPlane(MeshComponent& mesh, float width=1, float depth=1, unsigned int tiles=1);
        void createWall(MeshComponent& mesh, float width=1, float height=1, unsigned int tiles=1);
        void createBox(MeshComponent& mesh, float width=1, float height=1, float depth=1, unsigned int tiles=1);
        void createSphere(MeshComponent& mesh, float radius=1, unsigned int slices=36, unsigned int stacks=18);
        void createCylinder(MeshComponent& mesh, float baseRadius=1, float topRadius=1, float height=2, unsigned int slices=36, unsigned int stacks=18);
        void createCapsule(MeshComponent& mesh, float baseRadius=1, float topRadius=1, float height=2, unsigned int slices=36, unsigned int stacks=18);
        void createTorus(MeshComponent& mesh, float radius=1, float ringRadius=0.5, unsigned int sides=36, unsigned int rings=16);
        bool canMergeStaticModel(const ModelComponent& model, const MeshComponent& mesh, std::string* reason = nullptr) const;

        // Canonical form of a model path: the same file spelled in different ways maps to one key.
        static std::string getModelFilenameKey(const std::string& filename);

        // Submesh edits (Submesh::overrideFields) carried across a load, keyed by the ordinal of
        // the primitive that built each one. The loaders do this themselves; it is public for the
        // editor, which destroys the generated mesh entities before reloading.
        typedef std::map<unsigned int, Submesh> SubmeshOverrides;
        SubmeshOverrides collectSubmeshOverrides(Entity entity, const ModelComponent& model) const;
        void applySubmeshOverrides(const SubmeshOverrides& overrides, Entity entity, const ModelComponent& model) const;

        bool loadGLTF(Entity entity, const std::string filename, bool asyncLoad=false, bool skipEntities=false, bool changeRootTransform=true);
        bool loadOBJ(Entity entity, const std::string filename, bool asyncLoad=false);

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

        void resetModelToBindPose(ModelComponent& model);

        bool raycastTerrainSurface(const Ray& ray, TerrainComponent& terrain, Transform& transform, Vector3& worldPoint);

        bool hasPendingAsyncModelLoads() const;
        void cancelAsyncModelLoads();
        static void cancelAllAsyncModelLoads();
        bool isAsyncModelLoadPending(Entity entity, const std::string& filename) const;
        void cancelAsyncModelLoad(Entity entity, const std::string& filename);

        bool createOrUpdateSprite(SpriteComponent& sprite, MeshComponent& mesh);
        bool createOrUpdateTerrain(TerrainComponent& terrain, MeshComponent& mesh, Transform& transform);
        bool createOrUpdateMeshPolygon(MeshPolygonComponent& polygon, MeshComponent& mesh);
        bool createOrUpdateTilemap(TilemapComponent& tilemap, MeshComponent& mesh);
        bool createOrUpdateModel(Entity entity, ModelComponent& model, MeshComponent& mesh);

        void calculateMeshAABB(MeshComponent& mesh);

        void load() override;
        void draw() override;
        void destroy() override;
        void update(double dt) override;

        void onComponentAdded(Entity entity, ComponentId componentId) override;
        void onComponentRemoved(Entity entity, ComponentId componentId) override;
    };

}

#endif //MESHSYSTEM_H
