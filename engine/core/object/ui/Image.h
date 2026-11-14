// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef IMAGE_H
#define IMAGE_H

#include "UILayout.h"

namespace doriax{
    class DORIAX_API Image: public UILayout{
    public:
        Image(Scene* scene);
        Image(Scene* scene, Entity entity);
        virtual ~Image();

        bool createImage();
        bool load();

        void setPatchMargin(unsigned int margin);
        void setPatchMargin(unsigned int marginLeft, unsigned int marginRight, unsigned int marginTop, unsigned int marginBottom);

        void setPatchMarginBottom(unsigned int marginBottom);
        void setPatchMarginLeft(unsigned int marginLeft);
        void setPatchMarginRight(unsigned int marginRight);
        void setPatchMarginTop(unsigned int marginTop);

        unsigned int getPatchMarginBottom() const;
        unsigned int getPatchMarginLeft() const;
        unsigned int getPatchMarginRight() const;
        unsigned int getPatchMarginTop() const;

        void setTexture(const std::string& path);
        void setTexture(const std::string& id, TextureData data);
        void setTexture(Framebuffer* framebuffer);

        void setTextureScaleFactor(float textureScaleFactor);
        float getTextureScaleFactor() const;

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

#endif //IMAGE_H