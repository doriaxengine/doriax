#pragma once

#include "Scene.h"
#include "object/Camera.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "object/Light.h"

namespace doriax::editor{

    class MaterialRender{
    private:
        Scene* scene;
        Camera* camera;

        Light* light;

        Shape* sphere;
    public:
        MaterialRender();
        virtual ~MaterialRender();

        MaterialRender(const MaterialRender&) = delete;
        MaterialRender& operator=(const MaterialRender&) = delete;
        MaterialRender(MaterialRender&&) noexcept = default;
        MaterialRender& operator=(MaterialRender&&) noexcept = default;

        void applyMaterial(const Material& material);
        const Material getMaterial();

        Framebuffer* getFramebuffer();
        Texture getTexture();
        Scene* getScene();
        Object* getObject();
    };

}