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
        // frames left to draw: a change asks for some, so builds and uploads that end late show
        int frames;

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
        // what it draws changed with nothing here set, as a value its shader reads
        void requestRender();
        // draws while something changed or still loads, and leaves the image as it is otherwise
        void render();

        Texture getTexture();
        Scene* getScene();
    };

}
