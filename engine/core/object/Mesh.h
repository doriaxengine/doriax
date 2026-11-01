// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef MESH_H
#define MESH_H

#include "Object.h"
#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"

namespace doriax{

    struct ModelComponent;

    class DORIAX_API Mesh: public Object{
    private:
        ModelComponent* findPartsModel() const;
        AABB getPartsAABB(const ModelComponent& model, AABB MeshComponent::*bounds) const;
        void updateSpriteBatch();

    protected:
        // A model with its geometry on node children uses their meshes: the setters change all of
        // them, the getters read the first, and a submesh index counts across them as when merged
        std::vector<MeshComponent*> findMeshes() const;
        std::vector<MeshComponent*> getMeshes();
        const MeshComponent& getMesh() const;
        MeshComponent* findSubmesh(unsigned int& submesh) const;
        const Submesh& getSubmesh(unsigned int submesh) const;

    public:
        Mesh(Scene* scene);
        Mesh(Scene* scene, Entity entity);
        virtual ~Mesh();

        bool load();

        void setTexture(const std::string& path);
        void setTexture(const std::string& id, TextureData data);
        void setTexture(Framebuffer* framebuffer);
        
        void setColor(Vector4 color);
        void setColor(const float red, const float green, const float blue, const float alpha);
        void setColor(const float red, const float green, const float blue);
        void setAlpha(const float alpha);
        Vector4 getColor() const;
        float getAlpha() const;

        void setMaterial(const Material& material);
        Material getMaterial() const;

        void setMaterial(unsigned int submesh, const Material& material);
        Material getMaterial(unsigned int submesh) const;

        void setPrimitiveType(PrimitiveType primitiveType);
        PrimitiveType getPrimitiveType() const;

        void setPrimitiveType(unsigned int submesh, PrimitiveType primitiveType);
        PrimitiveType getPrimitiveType(unsigned int submesh) const;

        void setFaceCulling(bool faceCulling);
        bool isFaceCulling() const;

        void setFaceCulling(unsigned int submesh, bool faceCulling);
        bool isFaceCulling(unsigned int submesh) const;

        void setCastShadowsWithTexture(bool castShadowsWithTexture);
        bool isCastShadowsWithTexture() const;

        void setCastShadowsWithTexture(unsigned int submesh, bool castShadowsWithTexture);
        bool isCastShadowsWithTexture(unsigned int submesh) const;

        void setCullingMode(CullingMode cullingMode);
        CullingMode getCullingMode() const;

        void setWindingOrder(WindingOrder windingOrder);
        WindingOrder getWindingOrder() const;

        AABB getAABB() const;
        AABB getVerticesAABB() const;
        AABB getWorldAABB() const;

        unsigned int getNumSubmeshes() const;

        void setReceiveLights(bool receiveLights);
        bool isReceiveLights() const;

        void setReceiveIBL(bool receiveIBL);
        bool isReceiveIBL() const;

        void setCastShadows(bool castShadows);
        bool isCastShadows() const;

        void setReceiveShadows(bool receiveShadows);
        bool isReceiveShadows() const;

        void setLodEnabled(bool lodEnabled);
        bool isLodEnabled() const;

        void setLodBias(float lodBias);
        float getLodBias() const;

        void setRenderInReflectionProbes(bool renderInReflectionProbes);
        bool isRenderInReflectionProbes() const;

        void setShadowsBillboard(bool shadowsBillboard);
        bool isShadowsBillboard() const;

        void setTransparent(bool transparent);
        bool isTransparent() const;

        void setAutoTransparency(bool autoTransparency);
        bool isAutoTransparency() const;

        void setCustomShader(const std::string& path);
        std::string getCustomShader() const;

        void setCustomDepthShader(const std::string& path);
        std::string getCustomDepthShader() const;

        void setShaderUniform(const std::string& name, const Vector4& value);
        void setShaderUniform(const std::string& name, const Vector3& value);
        void setShaderUniform(const std::string& name, const Vector2& value);
        void setShaderUniform(const std::string& name, float value);
        Vector4 getShaderUniform(const std::string& name) const;
        bool removeShaderUniform(const std::string& name);

        void setAsMirror();
        void setAsMirror(Vector3 normal);
        void removeMirror();
        bool isMirror() const;

        void createInstancedMesh();
        void removeInstancedMesh();
        bool hasInstancedMesh() const;

        void setInstancedBillboard(bool billboard, bool cylindrical);

        void setInstancedBillboard(bool billboard);
        bool isInstancedBillboard() const;

        void setInstancedCylindricalBillboard(bool cylindricalBillboard);
        bool isInstancedCylindricalBillboard() const;

        void setMaxInstances(unsigned int maxInstances);
        unsigned int getMaxInstances() const;

        // instances shrink to nothing between fadeStart and fadeEnd (model-space distances)
        void setDistanceFade(bool distanceFade);
        bool isDistanceFade() const;

        void setFadeRange(float fadeStart, float fadeEnd);
        void setFadeStart(float fadeStart);
        float getFadeStart() const;
        void setFadeEnd(float fadeEnd);
        float getFadeEnd() const;

        void setCullInstances(bool cullInstances);
        bool isCullInstances() const;
        void setCullDistance(float distance);
        float getCullDistance() const;

        void addInstance(InstanceData instance);
        void addInstance(Vector3 position);
        void addInstance(float x, float y, float z);
        void addInstance(Vector3 position, Quaternion rotation, Vector3 scale);
        void addInstance(Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color);
        void addInstance(Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color, Rect textureRect);

        InstanceData& getInstance(size_t index);

        void updateInstance(size_t index, InstanceData instance);
        void updateInstance(size_t index, Vector3 position);
        void updateInstance(size_t index, float x, float y, float z);
        void updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale);
        void updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color);
        void updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color, Rect textureRect);

        void removeInstance(size_t index);

        bool isInstanceVisible(size_t index);
        void setInstanceVisible(size_t index, bool visible) const;

        void updateInstances();
        size_t getNumInstances();

        void clearInstances();
    };
}

#endif //MESH_H
