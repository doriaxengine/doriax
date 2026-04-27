#pragma once

#include "SceneRender.h"

#include "Doriax.h"
#include "gizmo/ViewportGizmo.h"

namespace doriax::editor{

    struct LightObjects{
        Sprite* icon = nullptr;
        Lines* lines = nullptr;

        LightType type;
        float innerConeCos = 0.0f;
        float outerConeCos = 0.0f;
        Vector3 direction = Vector3::ZERO;
        float range = 0.0f;
    };

    struct BodyObjects{
        Lines* lines = nullptr;
    };

    class SceneRender3D: public SceneRender{
    private:

        Lines* lines;

        std::map<Entity, LightObjects> lightObjects;
        std::map<Entity, CameraObjects> cameraObjects;
        std::map<Entity, SoundObjects> soundObjects;
        std::map<Entity, BodyObjects> bodyObjects;
        std::map<Entity, Lines*> jointLines;
        std::map<Entity, Lines*> boneLines;

        ViewportGizmo viewgizmo;

        Vector2 linesOffset;
        float lastGridSpacing;

        void createLines();
        bool instanciateLightObject(Entity entity);
        bool instanciateCameraObject(Entity entity);
        bool instanciateSoundObject(Entity entity);
        bool instanciateBodyObject(Entity entity);
        bool instanciateJointObject(Entity entity);
        bool instanciateBoneLines(Entity entity);
        void createOrUpdateBoneLines(Entity entity, const ModelComponent& model, bool visible, bool highlighted);
        void createOrUpdateLightIcon(Entity entity, const Transform& transform, LightType lightType, bool newLight);
        void createOrUpdateCameraIcon(Entity entity, const Transform& transform, bool newCamera);
        void createOrUpdateSoundIcon(Entity entity, const Transform& transform, bool newSound);
        void createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body3DComponent& body);
        void createOrUpdateJointLines(Entity entity, const Joint3DComponent& joint, bool visible, bool highlighted);
        void createCameraFrustum(Entity entity, const Transform& transform, const CameraComponent& cameraComponent, bool fixedSizeFrustum, bool isMainCamera);
        void createDirectionalLightArrow(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);
        void createPointLightSphere(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);
        void createSpotLightCones(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);

    protected:
        void hideAllGizmos() override;

    public:
        SceneRender3D(Scene* scene);
        virtual ~SceneRender3D();

        void activate() override;
        void updateSelLines(std::vector<OBB> obbs) override;

        void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{}) override;
        void mouseHoverEvent(float x, float y) override;
        void mouseClickEvent(float x, float y, std::vector<Entity> selEntities) override;
        void mouseReleaseEvent(float x, float y) override;
        void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection) override;

        ViewportGizmo* getViewportGizmo();
    };

}