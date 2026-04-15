#pragma once

#include "Scene.h"
#include "object/Camera.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "object/Light.h"

#include "yaml-cpp/yaml.h"

namespace doriax::editor{

    class MeshPreviewRender{
    private:
        Scene* scene;
        Camera* camera;

        Light* light;

        Mesh* mesh;

    public:
        MeshPreviewRender();
        virtual ~MeshPreviewRender();

        MeshPreviewRender(const MeshPreviewRender&) = delete;
        MeshPreviewRender& operator=(const MeshPreviewRender&) = delete;
        MeshPreviewRender(MeshPreviewRender&&) noexcept = default;
        MeshPreviewRender& operator=(MeshPreviewRender&&) noexcept = default;

        void applyMesh(YAML::Node meshData, bool updateCamera = true, bool removeMaterial = false);
        void setBackground(Vector4 color);

        void positionCameraForMesh();

        Framebuffer* getFramebuffer();
        Texture getTexture();
        Entity getMeshEntity();
        MeshComponent& getMeshComponent();
        Scene* getScene();
        Object* getObject();
    };

}