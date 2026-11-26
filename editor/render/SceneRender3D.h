// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "SceneRender.h"

#include "Doriax.h"
#include "gizmo/ViewportGizmo.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

namespace doriax::editor{

    struct LightObjects{
        Sprite* icon = nullptr;
        Lines* lines = nullptr;

        LightType type;
        float innerConeCos = 0.0f;
        float outerConeCos = 0.0f;
        Vector3 direction = Vector3::ZERO;
        float range = 0.0f;
        bool spotMaskReady = false;
        float spotMaskAspect = 1.0f;
    };

    struct BodyObjects{
        Lines* lines = nullptr;

        // Mesh and hull shapes walk their whole source buffer to build these, so they
        // are kept until the signature of what feeds them changes.
        struct ShapeEdgeCache {
            bool valid = false;
            uint64_t signature = 0;
            std::vector<std::pair<Vector3, Vector3>> edges; // body-local space
        };
        std::vector<ShapeEdgeCache> shapeEdgeCaches;
    };

    class SceneRender3D: public SceneRender{
    private:

        static constexpr float kHullQuantizeScale = 4096.0f;
        // Above this the cloud is reduced to its extremes first. Matches
        // ConvexHullShape::cMaxPointsInHull, the cap Jolt applies to the same shape.
        static constexpr size_t kMaxHullInputPoints = 256;
        static constexpr size_t kMaxSourceParentHops = 64;

        Lines* lines;

        std::map<Entity, LightObjects> lightObjects;
        std::map<Entity, CameraObjects> cameraObjects;
        std::map<Entity, SoundObjects> soundObjects;
        std::map<Entity, BodyObjects> bodyObjects;
        std::map<Entity, Lines*> jointLines;
        std::map<Entity, Lines*> boneLines;
        std::map<Entity, Lines*> linePointLines;
        std::map<Entity, Lines*> polygonPointLines;
        std::map<Entity, Lines*> trackLines;
        std::map<Entity, Lines*> reflectionProbeLines;
        std::map<Entity, Lines*> waterExclusionLines;
        std::map<Entity, Lines*> characterLines;

        ViewportGizmo viewgizmo;

        float walkSpeedOffset = 0.0f;

        Vector2 linesOffset;
        float lastGridSpacing;
        float lastGridFarClip = 0.0f;

        void createLines();
        bool instanciateLightObject(Entity entity);
        bool instanciateCameraObject(Entity entity);
        bool instanciateSoundObject(Entity entity);
        bool instanciateBodyObject(Entity entity);
        bool instanciateJointObject(Entity entity);
        bool instanciateBoneLines(Entity entity);
        bool instanciateLinePointLines(Entity entity);
        static uint64_t quantizeHullKey(const Vector3& p);
        static void pushUniqueHullPoint(std::vector<Vector3>& points, std::unordered_set<uint64_t>& seen, const Vector3& p);
        static void reduceToExtremes(std::vector<Vector3>& points);
        static void buildConvexHullEdges(std::vector<Vector3> points, const std::function<void(const Vector3&, const Vector3&)>& emit);
        void createOrUpdateBoneLines(Entity entity, const ModelComponent& model, bool visible, bool highlighted);
        void createOrUpdateLightIcon(Entity entity, const Transform& transform, LightType lightType, bool newLight);
        void createOrUpdateCameraIcon(Entity entity, const Transform& transform, bool newCamera);
        void createOrUpdateSoundIcon(Entity entity, const Transform& transform, bool newSound);
        void createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body3DComponent& body, bool visible, bool highlighted);
        void createOrUpdateJointLines(Entity entity, const Joint3DComponent& joint, bool visible, bool highlighted);
        void createOrUpdateLinePointLines(Entity entity, const Transform& transform, const LinesComponent& lines, bool visible);
        bool instanciatePolygonPointLines(Entity entity);
        void createOrUpdatePolygonPointLines(Entity entity, const Transform& transform, const std::vector<PolygonPoint>& points, bool isMesh, bool visible);
        bool instanciateTrackLines(Entity entity);
        void createOrUpdateTrackLines(Entity entity, const TranslateTracksComponent& tracks, bool visible);
        bool instanciateReflectionProbeLines(Entity entity);
        void createOrUpdateReflectionProbeLines(Entity entity, const Transform& transform, const ReflectionProbeComponent& probe, bool visible);
        bool instanciateWaterExclusionLines(Entity entity);
        void createOrUpdateWaterExclusionLines(Entity entity, const Transform& transform, const WaterExclusionComponent& exclusion, bool visible);
        bool instanciateCharacterLines(Entity entity);
        void createOrUpdateCharacterLines(Entity entity, const Transform& transform, const Character3DComponent& character, bool visible, bool highlighted);
        void createCameraFrustum(Entity entity, const Transform& transform, const CameraComponent& cameraComponent, bool fixedSizeFrustum, bool isMainCamera);
        void createDirectionalLightArrow(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);
        void createPointLightSphere(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);
        void createSpotLightCones(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected);

    protected:
        void hideAllGizmos() override;
        void clearEntityOverlays() override;

    public:
        SceneRender3D(Scene* scene);
        virtual ~SceneRender3D();

        void activate() override;
        void setOverlayScale(float scale) override;
        void updateSelLines(Lines* lines, const std::vector<OBB>& obbs) override;

        void zoomCamera(float amount);

        float getWalkSpeedOffset() const;
        void setWalkSpeedOffset(float offset);

        void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{}) override;
        void mouseHoverEvent(float x, float y) override;
        void mouseClickEvent(float x, float y, std::vector<Entity> selEntities) override;
        void mouseReleaseEvent(float x, float y) override;
        void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio) override;

        ViewportGizmo* getViewportGizmo();
    };

}
