//
// (c) 2026 Eduardo Doria.
//

#ifndef MESH_H
#define MESH_H

#include "Object.h"
#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"

namespace doriax{

    class DORIAX_API Mesh: public Object{
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

        void setCastShadows(bool castShadows);
        bool isCastShadows() const;

        void setReceiveShadows(bool receiveShadows);
        bool isReceiveShadows() const;

        void setShadowsBillboard(bool shadowsBillboard);
        bool isShadowsBillboard() const;

        void setTransparent(bool transparent);
        bool isTransparent() const;

        void setAutoTransparency(bool autoTransparency);
        bool isAutoTransparency() const;

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