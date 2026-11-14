// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LINES_H
#define LINES_H

#include "Object.h"

namespace doriax{

    class DORIAX_API Lines: public Object{
    public:
        Lines(Scene* scene);
        Lines(Scene* scene, Entity entity);
        virtual ~Lines();

        bool load();

        void setMaxLines(unsigned int maxLines);
        unsigned int getMaxLines() const;

        void addLine(LineData line);
        void addLine(Vector3 pointA, Vector3 pointB);
        void addLine(Vector3 pointA, Vector3 pointB, Vector3 color);
        void addLine(Vector3 pointA, Vector3 pointB, Vector4 color);
        void addLine(Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB);

        LineData& getLine(size_t index);

        void updateLine(size_t index, LineData line);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector3 color);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 color);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB);
        void updateLine(size_t index, Vector3 color);
        void updateLine(size_t index, Vector4 color);
        void updateLine(size_t index, Vector4 colorA, Vector4 colorB);

        void removeLine(size_t index);

        void updateLines();
        size_t getNumLines();

        void clearLines();

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

#endif //LINES_H