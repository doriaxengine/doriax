// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef POINTS_H
#define POINTS_H

#include "Object.h"
#include "buffer/ExternalBuffer.h"

namespace doriax{

    class DORIAX_API Points: public Object{
    public:
        Points(Scene* scene);
        Points(Scene* scene, Entity entity);
        virtual ~Points();

        bool load();

        void setMaxPoints(unsigned int maxPoints);
        unsigned int getMaxPoints() const;

        void addPoint(PointData point);
        void addPoint(Vector3 position);
        void addPoint(float x, float y, float z);
        void addPoint(Vector3 position, Vector4 color);
        void addPoint(Vector3 position, Vector4 color, float size);
        void addPoint(Vector3 position, Vector4 color, float size, float rotation);
        void addPoint(Vector3 position, Vector4 color, float size, float rotation, Rect textureRect);

        PointData& getPoint(size_t index);

        void updatePoint(size_t index, PointData point);
        void updatePoint(size_t index, Vector3 position);
        void updatePoint(size_t index, float x, float y, float z);
        void updatePoint(size_t index, Vector3 position, Vector4 color);
        void updatePoint(size_t index, Vector3 position, Vector4 color, float size);
        void updatePoint(size_t index, Vector3 position, Vector4 color, float size, float rotation);
        void updatePoint(size_t index, Vector3 position, Vector4 color, float size, float rotation, Rect textureRect);

        void removePoint(size_t index);

        bool isPointVisible(size_t index);
        void setPointVisible(size_t index, bool visible) const;

        void updatePoints();
        size_t getNumPoints();

        void clearPoints();

        void addSpriteFrame(int id, const std::string& name, Rect rect);
        void addSpriteFrame(const std::string& name, float x, float y, float width, float height);
        void addSpriteFrame(float x, float y, float width, float height);
        void addSpriteFrame(Rect rect);
        void removeSpriteFrame(int id);
        void removeSpriteFrame(const std::string& name);

        void setTexture(const std::string& path);
        void setTexture(const std::string& id, TextureData data);
        void setTexture(Framebuffer* framebuffer);

        void setTransparent(bool transparent);
        bool isTransparent() const;

        void setAutoTransparency(bool autoTransparency);
        bool isAutoTransparency() const;

        // .glsl file with the shader hooks
        void setShader(const std::string& path);
        std::string getShader() const;

        void setShaderUniform(const std::string& name, const Vector4& value);
        void setShaderUniform(const std::string& name, const Vector3& value);
        void setShaderUniform(const std::string& name, const Vector2& value);
        void setShaderUniform(const std::string& name, float value);
        Vector4 getShaderUniform(const std::string& name) const;
        bool removeShaderUniform(const std::string& name);

        void setShaderTexture(const std::string& name, const Texture& texture);
        Texture getShaderTexture(const std::string& name) const;
    };
}

#endif //POINTS_H
