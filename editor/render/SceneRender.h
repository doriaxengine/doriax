#pragma once

#include "Doriax.h"

#include "ToolsLayer.h"
#include "UILayer.h"
#include "RenderUtil.h"
#include "command/Command.h"

namespace doriax::editor{

    struct SceneDisplaySettings {
        bool showAllJoints       = false;
        bool showAllBones        = false;
        bool hideAllBodies       = false;
        bool hideCameraView      = false;
        bool hideLightIcons      = false;
        bool hideContainerGuides = false;
        bool showOrigin          = true;
        bool showGrid3D          = true;
        bool hideSelectionOutline = false;
        bool showGrid2D          = false;
        float gridSpacing2D      = 50.0f;
        float gridSpacing3D      = 1.0f;
        bool snapToGrid          = false;
    };

    struct CameraObjects{
        Sprite* icon = nullptr;
        Lines* lines = nullptr;

        CameraType type = CameraType::CAMERA_UI;
        bool isMainCamera = false;
        float yfov = 0;
        float aspect = 0;
        float nearClip = 0;
        float farClip = 0;
        float leftClip = 0;
        float rightClip = 0;
        float bottomClip = 0;
        float topClip = 0;
    };

    class Project;

    class SceneRender{
    private:
        Plane cursorPlane;
        Vector3 rotationAxis;
        Vector3 gizmoStartPosition;
        Vector3 cursorStartOffset;
        Quaternion rotationStartOffset;
        Vector3 scaleStartOffset;
        std::map<Entity, Matrix4> objectMatrixOffset;
        std::map<Entity, Vector2> objectSizeOffset;

        Ray mouseRay;
        bool mouseClicked;
        bool useGlobalTransform;

        float gizmoScale;
        float selectionOffset;

        Command* lastCommand;

        CursorSelected cursorSelected;

        // Tile sub-selection within a tilemap entity
        Entity selectedTileEntity = 0;  // NULL_ENTITY
        int selectedTileIndex = -1;
        Vector2 tileStartPosition;
        float tileStartWidth = 0;
        float tileStartHeight = 0;

        AABB getAABB(Entity entity, bool local);
        AABB getFamilyAABB(Entity entity, float offset);

        OBB getOBB(Entity entity, bool local);
        OBB getFamilyOBB(Entity entity, float offset);

    protected:
        void updateCameraFrustum(CameraObjects& co, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum = true);
        void drawCameraFrustumLines(Lines* lines, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum = true);
        void setupCameraIcon(CameraObjects& co);

        Scene* scene;
        Camera* camera;
        Framebuffer framebuffer;

        Lines* selLines;

        ToolsLayer toolslayer;
        UILayer uilayer;

        std::vector<Scene*> childSceneLayers;

        bool multipleEntitiesSelected;
        bool isPlaying;

        SceneDisplaySettings displaySettings;

        float zoom;       // current zoom level (units per pixel) for 2D

    public:

        SceneRender(Scene* scene, bool use2DGizmos, bool enableViewGizmo, float gizmoScale, float selectionOffset);
        virtual ~SceneRender();

        virtual void hideAllGizmos();

        void setPlayMode(bool isPlaying);

        virtual void activate();
        virtual void updateSize(int width, int height);
        virtual void updateSelLines(std::vector<OBB> obbs) = 0;

        void updateRenderSystem();

        virtual void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{});
        virtual void mouseHoverEvent(float x, float y);
        virtual void mouseClickEvent(float x, float y, std::vector<Entity> selEntities);
        virtual void mouseReleaseEvent(float x, float y);
        virtual void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection);

        virtual bool isAnyGizmoSideSelected() const;

        void setChildSceneLayers(const std::vector<Scene*>& layers);

        TextureRender& getTexture();
        Camera* getCamera();
        ToolsLayer* getToolsLayer();
        UILayer* getUILayer();

        bool isUseGlobalTransform() const;
        void setUseGlobalTransform(bool useGlobalTransform);
        void changeUseGlobalTransform();

        void enableCursorPointer();
        void enableCursorHand();
        CursorSelected getCursorSelected() const;

        bool isMultipleEntitesSelected() const;

        AABB getEntitiesAABB(const std::vector<Entity>& entities);

        // Tile sub-selection
        int getSelectedTileIndex() const { return selectedTileIndex; }
        Entity getSelectedTileEntity() const { return selectedTileEntity; }
        void selectTile(Entity entity, int tileIndex);
        void clearTileSelection();
        int hitTestTile(Entity entity, float x, float y);
        OBB getTileOBB(Entity entity, int tileIndex);
    };

}