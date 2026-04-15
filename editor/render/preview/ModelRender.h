#pragma once

#include "Scene.h"
#include "object/Camera.h"
#include "object/Model.h"
#include "object/Light.h"

namespace doriax::editor{

    class ModelRender{
    private:
        Scene* scene;
        Camera* camera;
        Light* light;
        Model* model;

    public:
        ModelRender();
        virtual ~ModelRender();

        ModelRender(const ModelRender&) = delete;
        ModelRender& operator=(const ModelRender&) = delete;
        ModelRender(ModelRender&&) noexcept = default;
        ModelRender& operator=(ModelRender&&) noexcept = default;

        void clearScene();
        void clearAndInitScene();
        bool loadModel(const std::string& filename);
        void positionCameraForModel();
        void fixDarkMaterials();

        Framebuffer* getFramebuffer();
        Texture getTexture();
        Scene* getScene();
        Object* getObject();
    };

}
