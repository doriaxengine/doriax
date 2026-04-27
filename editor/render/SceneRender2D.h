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
        std::map<Entity, CameraObjects> cameraObjects;
        std::map<Entity, SoundObjects> soundObjects;
        Lines* tileLines = nullptr;
        bool isUI;
        int viewportWidth;
        int viewportHeight;

        void createLines(unsigned int width, unsigned int height);
        void applyZoomProjection();
        void updateGridLines();
        bool instanciateBodyLines(Entity entity);
        bool instanciateJointLines(Entity entity);
        void createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body2DComponent& body);
        void createOrUpdateJointLines(Entity entity, const Joint2DComponent& joint, bool visible, bool highlighted);

    protected:
        void hideAllGizmos() override;

    public:
        SceneRender2D(Scene* scene, unsigned int width, unsigned int height, bool isUI);
        virtual ~SceneRender2D();

        void activate() override;
        void setCanvasFrameSize(unsigned int width, unsigned int height);
        void updateSize(int width, int height) override;
        void updateSelLines(std::vector<OBB> obbs) override;

        void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{}) override;
        void mouseHoverEvent(float x, float y) override;
        void mouseClickEvent(float x, float y, std::vector<Entity> selEntities) override;
        void mouseReleaseEvent(float x, float y) override;
        void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection) override;

        void zoomAtPosition(float width, float height, Vector2 pos, float zoomFactor);

        float getZoom() const;
        void setZoom(float zoom);
    };

}