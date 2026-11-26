// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "SceneRender.h"

#include "Doriax.h"
#include "UILayer.h"
#include "command/Command.h"

namespace doriax::editor{

    class SceneRender2D: public SceneRender{
    private:
        Lines* lines;
        Lines* gridLines;
        std::map<Entity, Lines*> containerLines;
        std::map<Entity, Lines*> bodyLines;
        std::map<Entity, Lines*> jointLines;
        std::map<Entity, Lines*> light2DLines;
        std::map<Entity, Lines*> occluder2DLines;
        std::map<Entity, Lines*> linePointLines;
        std::map<Entity, Lines*> polygonPointLines;
        std::map<Entity, Lines*> trackLines;
        std::map<Entity, CameraObjects> cameraObjects;
        std::map<Entity, SoundObjects> soundObjects;
        std::map<Entity, Light2DObjects> light2DObjects;
        Lines* tileLines = nullptr;
        bool isUI;
        int viewportWidth;
        int viewportHeight;

        void createLines(unsigned int width, unsigned int height);
        void applyZoomProjection();
        void updateGridLines();
        bool instanciateBodyLines(Entity entity);
        bool instanciateJointLines(Entity entity);
        bool instanciateLight2DLines(Entity entity);
        bool instanciateOccluder2DLines(Entity entity);
        bool instanciateLinePointLines(Entity entity);
        bool instanciatePolygonPointLines(Entity entity);
        void addPointHandle(Lines* linesObj, const Vector3& worldPoint, float halfSize, const Vector4& color);
        void createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body2DComponent& body, bool visible, bool highlighted);
        void createOrUpdateJointLines(Entity entity, const Joint2DComponent& joint, bool visible, bool highlighted);
        void createOrUpdateLight2DLines(Entity entity, const Transform& transform, const Light2DComponent& light, bool visible, bool highlighted);
        void createOrUpdateOccluder2DLines(Entity entity, const Transform& transform, const Occluder2DComponent& occluder, bool visible, bool highlighted);
        void createOrUpdateLinePointLines(Entity entity, const Transform& transform, const LinesComponent& lines, bool visible);
        void createOrUpdatePolygonPointLines(Entity entity, const Transform& transform, const std::vector<PolygonPoint>& points, bool isMesh, bool visible);
        bool instanciateTrackLines(Entity entity);
        void createOrUpdateTrackLines(Entity entity, const TranslateTracksComponent& tracks, bool visible);

    protected:
        void hideAllGizmos() override;
        void clearEntityOverlays() override;

    public:
        SceneRender2D(Scene* scene, unsigned int width, unsigned int height, bool isUI);
        virtual ~SceneRender2D();

        void activate() override;
        void setCanvasFrameSize(unsigned int width, unsigned int height);
        void updateSize(int width, int height) override;
        void updateSelLines(Lines* lines, const std::vector<OBB>& obbs) override;

        void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{}) override;
        void mouseHoverEvent(float x, float y) override;
        void mouseClickEvent(float x, float y, std::vector<Entity> selEntities) override;
        void mouseReleaseEvent(float x, float y) override;
        void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio) override;

        void zoomAtPosition(float width, float height, Vector2 pos, float zoomFactor);

        float getZoom() const;
        void setZoom(float zoom);
    };

}