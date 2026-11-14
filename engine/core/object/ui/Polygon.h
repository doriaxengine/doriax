// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef POLYGON_H
#define POLYGON_H

#include "UILayout.h"

namespace doriax{
    class DORIAX_API Polygon: public UILayout{

    public:
        Polygon(Scene* scene);
        Polygon(Scene* scene, Entity entity);
        virtual ~Polygon();

        bool createPolygon();
        bool load();

        void addVertex(Vector3 vertex);
        void addVertex(float x, float y);

        void clearVertices();

        void setTexture(const std::string& path);
        void setTexture(const std::string& id, TextureData data);
        void setTexture(Framebuffer* framebuffer);

        void setColor(Vector4 color);
        void setColor(const float red, const float green, const float blue, const float alpha);
        void setColor(const float red, const float green, const float blue);
        void setAlpha(const float alpha);
        Vector4 getColor() const;
        float getAlpha() const;

        void setFlipY(bool flipY);
        bool isFlipY() const;

        AABB getAABB() const;
        AABB getWorldAABB() const;

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

#endif //POLYGON_H