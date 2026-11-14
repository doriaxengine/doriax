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

    // a lit shape with a mesh shader, for the graph window
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
        // frames left to draw, so late loads still show
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
        // in pixels; the new target is empty until drawn
        void setSize(unsigned int size);
        unsigned int getSize() const;

        // takes the shader again, once it was built anew
        void reload();
        // for changes it cannot see, as a uniform
        void requestRender();
        // draws only while something changed or loads
        void render();

        Texture getTexture();
        Scene* getScene();
    };

}
