// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Scene.h"
#include "object/Camera.h"
#include "object/Light.h"
#include "object/Shape.h"
#include "object/environment/SkyBox.h"

#include <string>

namespace doriax::editor{

    // a lit shape drawn with a mesh shader file, as the graph window shows it
    class ShaderPreviewRender{
    public:
        enum class ShapeType{
            SPHERE,
            CUBE,
            PLANE
        };

    private:
        Scene* scene;
        Camera* camera;
        Light* light;
        SkyBox* sky;
        Shape* shape;

        ShapeType shapeType;
        std::string shader;

        void createShape();

    public:
        explicit ShaderPreviewRender(unsigned int size);
        virtual ~ShaderPreviewRender();

        ShaderPreviewRender(const ShaderPreviewRender&) = delete;
        ShaderPreviewRender& operator=(const ShaderPreviewRender&) = delete;

        void setShader(const std::string& shader);
        const std::string& getShader() const;
        void setShape(ShapeType type);
        ShapeType getShape() const;

        // takes the shader again, once it was built anew
        void reload();
        void render();

        Texture getTexture();
        Scene* getScene();
    };

}
