// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SKYBOX_H
#define SKYBOX_H

#include "object/EntityHandle.h"

namespace doriax{
    class DORIAX_API SkyBox: public EntityHandle{

    public:
        SkyBox(Scene* scene);
        SkyBox(Scene* scene, Entity entity);
        virtual ~SkyBox();

        bool load();

        void setTextures(const std::string& id,
                        TextureData texturePositiveX, TextureData textureNegativeX,
                        TextureData texturePositiveY, TextureData textureNegativeY,
                        TextureData texturePositiveZ, TextureData textureNegativeZ);

        void setTextures(const std::string& texturePositiveX, const std::string& textureNegativeX,
                        const std::string& texturePositiveY, const std::string& textureNegativeY,
                        const std::string& texturePositiveZ, const std::string& textureNegativeZ);

        void setTexture(const std::string& texture);

        void setTexturePositiveX(const std::string& texture);
        void setTextureNegativeX(const std::string& texture);
        void setTexturePositiveY(const std::string& texture);
        void setTextureNegativeY(const std::string& texture);
        void setTexturePositiveZ(const std::string& texture);
        void setTextureNegativeZ(const std::string& texture);

        void setColor(Vector4 color);
        void setColor(const float r, const float g, const float b);
        void setColor(const float r, const float g, const float b, const float a);
        void setAlpha(const float alpha);
        Vector4 getColor() const;
        float getAlpha() const;

        void setRotation(float angle);
        float getRotation() const;

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

#endif //SKYBOX_H