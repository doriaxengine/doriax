// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Doriax.h"

#include "ToolsLayer.h"
#include "UILayer.h"
#include "RenderUtil.h"
#include "command/Command.h"

namespace doriax::editor{

    constexpr float DEFAULT_EDITOR_CAMERA_NEAR = 0.3f;
    constexpr float DEFAULT_EDITOR_CAMERA_FAR = 4000.0f;
    constexpr float MIN_EDITOR_CAMERA_DISTANCE = 0.2f;
    constexpr float MAX_EDITOR_CAMERA_DISTANCE = 2000.0f;

    constexpr float DEFAULT_EDITOR_WALK_SPEED = 10.0f;
    constexpr float MIN_EDITOR_WALK_SPEED = 0.5f;
    constexpr float MAX_EDITOR_WALK_SPEED = 1010.0f;

    struct SceneDisplaySettings {
        bool showAllJoints       = false;
        bool showAllBones        = false;
        bool showAllBodies       = false;
        bool hideCameraView      = false;
        bool showFullFrustum     = false;
        bool hideLightIcons      = false;
        bool hideSoundIcons      = false;
        bool hideContainerGuides = false;
        bool showOrigin          = true;
        bool showGrid3D          = true;
        bool hideSelectionOutline = false;
        bool disableFaceCulling  = false;
        bool disableFog          = false;
        bool disableUnderwater   = false;
        bool showGrid2D          = false;
        float gridSpacing2D      = 50.0f;
        float gridSpacing3D      = 1.0f;
        bool snapToGrid          = false;
        bool snapTile            = false;
        bool snapRotation        = false;
        float rotationSnapDegrees = 15.0f;
    };

    struct CameraObjects{
        Sprite* icon = nullptr;
        Lines* lines = nullptr;

        CameraType type = CameraType::CAMERA_UI;
        bool isMainCamera = false;
        bool fixedSizeFrustum = true;
        float yfov = 0;
        float aspect = 0;
        float nearClip = 0;
        float farClip = 0;
        float leftClip = 0;
        float rightClip = 0;
        float bottomClip = 0;
        float topClip = 0;
    };

    struct SoundObjects{
        Sprite* icon = nullptr;
    };

    struct Light2DObjects{
        Sprite* icon = nullptr;
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

        // Instance sub-selection within an instanced mesh entity
        Entity selectedInstanceEntity = 0;  // NULL_ENTITY
        int selectedInstanceIndex = -1;
        Vector3 instanceStartPosition;
        Quaternion instanceStartRotation;
        Vector3 instanceStartScale;

        // Occluder2D polygon point sub-selection
        Entity selectedOccluderPointEntity = 0;  // NULL_ENTITY
        int selectedOccluderPointIndex = -1;

        // Lines endpoint sub-selection (index = lineIndex * 2 + endpoint, 0 = A, 1 = B)
        Entity selectedLinePointEntity = 0;  // NULL_ENTITY
        int selectedLinePointIndex = -1;

        // Polygon / MeshPolygon vertex sub-selection (both store std::vector<PolygonPoint>;
        // isMesh picks which component, avoiding a ComponentType dependency in this header)
        Entity selectedPolygonPointEntity = 0;  // NULL_ENTITY
        int selectedPolygonPointIndex = -1;
        bool selectedPolygonPointIsMesh = false;

        // TranslateTracks keyframe point sub-selection (the tracks entity has no
        // Transform; its values[] live in the action target's parent space)
        Entity selectedTrackPointEntity = 0;  // NULL_ENTITY
        int selectedTrackPointIndex = -1;

        // Body2D polygon and chain vertex sub-selection
        Entity selectedBody2DPointEntity = 0;  // NULL_ENTITY
        int selectedBody2DPointShape = -1;
        int selectedBody2DPointIndex = -1;

        // editor camera copy in a scene SceneManager loaded during Play
        Scene* runtimeViewScene = nullptr;
        Entity runtimeViewCamera = NULL_ENTITY;
        float runtimeViewMirrorY = 0.0f;

        // world position of vertex pointIndex of the selected entity's Polygon/MeshPolygon
        // points (nullptr list / bad index -> false)
        bool getPolygonPointWorld(Entity entity, bool isMesh, int pointIndex, Vector3& worldPoint);

        bool getTrackPointWorld(Entity entity, int pointIndex, Vector3& worldPoint);

        AABB getAABB(Entity entity, bool local);
        AABB getFamilyAABB(Entity entity, float offset);

        // visual=true returns a shear-preserving box (parallelepiped) that hugs the
        // rendered geometry, for the selection outline / gizmo handles only.
        // visual=false (default) returns an orthonormal OBB, required by ray picking
        // (Ray::intersects) and OBB::enclose, which assume orthonormal axes.
        OBB getOBB(Entity entity, bool local, bool visual = false);
        OBB getFamilyVisualOBB(Entity entity, float offset);
        static OBB transformAABBPreservingShear(const Matrix4& modelMatrix, const AABB& localAABB);
        void updateTerrainBrushCursor();
        float snapRotationAngle(float angle, bool invertRotationSnap) const;
        static Vector3 getMatrixScale(const Matrix4& matrix);
        static bool isCorner2DGizmoSide(Gizmo2DSideSelected side);
        static bool gizmo2DSideUsesNegativeX(Gizmo2DSideSelected side);
        static bool gizmo2DSideUsesNegativeY(Gizmo2DSideSelected side);
        static Vector2 lockObject2DAspectRatio(const Vector2& startSize, const Vector2& candidateSize);
        bool isPreviewCameraUsable(Entity entity);
        Entity getActiveCameraEntity();
        void syncSceneCamera();
        void syncRuntimeViewCamera();
        void releaseRuntimeViewCamera();

    protected:
        void updateCameraFrustum(CameraObjects& co, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum = true);
        void drawCameraFrustumLines(Lines* lines, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum = true);
        void setupCameraIcon(CameraObjects& co);
        void setupSoundIcon(SoundObjects& so);
        void setupLight2DIcon(Light2DObjects& lo);

        float overlayPx(float logicalPixels) const { return logicalPixels * overlayScale; }

        Scene* scene;
        Camera* camera;
        Framebuffer framebuffer;
        Entity previewCameraEntity;

        Lines* selLines;
        Lines* terrainBrushLines;

        ToolsLayer toolslayer;
        UILayer uilayer;

        std::vector<Scene*> childSceneLayers;

        bool multipleEntitiesSelected;
        bool isPlaying;
        bool cameraDetached;

        bool isGameView() const { return isPlaying && !cameraDetached; }

        SceneDisplaySettings displaySettings;

        float zoom;       // current zoom level (units per pixel) for 2D
        float overlayScale = 1.0f;

    public:

        SceneRender(Scene* scene, bool use2DGizmos, bool enable3DOverlays, float gizmoScale, float selectionOffset);
        virtual ~SceneRender();

        // bounds as drawn: a skinned mesh is posed by its bones, so skinnedAABB and not aabb
        static AABB getMeshLocalAABB(const MeshComponent& mesh);

        virtual void hideAllGizmos();
        // per-entity icons and lines, rebuilt by the next update()
        virtual void clearEntityOverlays() {}

        void setPlayMode(bool isPlaying);

        void setCameraDetached(bool detached);
        bool isCameraDetached() const;

        virtual void activate();
        virtual void updateSize(int width, int height);
        virtual void setOverlayScale(float scale);
        float billboardScreenScale(const Vector3& worldPos, float logicalScale) const;
        virtual void updateSelLines(std::vector<OBB> obbs) = 0;

        void updateRenderSystem();

        virtual void update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings = SceneDisplaySettings{});
        virtual void mouseHoverEvent(float x, float y);
        virtual void mouseClickEvent(float x, float y, std::vector<Entity> selEntities);
        virtual void mouseReleaseEvent(float x, float y);
        virtual void mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio);

        virtual bool isAnyGizmoSideSelected() const;
        bool isTerrainEditing() const;
        // Gizmo hover highlight or terrain brush, which follow the mouse
        bool hasHoverFeedback() const;

        void setChildSceneLayers(const std::vector<Scene*>& layers);

        TextureRender& getTexture();
        Camera* getCamera();
        bool setPreviewCamera(Entity entity);
        void clearPreviewCamera();
        Entity getPreviewCameraEntity() const;
        bool isPreviewCameraActive();
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
        OBB getTileOBB(Entity entity, int tileIndex, bool visual = false);

        // Instance sub-selection
        int getSelectedInstanceIndex() const { return selectedInstanceIndex; }
        Entity getSelectedInstanceEntity() const { return selectedInstanceEntity; }
        void selectInstance(Entity entity, int instanceIndex);
        void clearInstanceSelection();
        int hitTestInstance(Entity entity, float x, float y);
        OBB getInstanceOBB(Entity entity, int instanceIndex, bool visual = false);
        Quaternion getInstanceWorldRotation(const Transform& transform, const InstancedMeshComponent& instmesh, const InstanceData& inst) const;

        // Occluder2D polygon point sub-selection
        int getSelectedOccluderPointIndex() const { return selectedOccluderPointIndex; }
        Entity getSelectedOccluderPointEntity() const { return selectedOccluderPointEntity; }
        void selectOccluderPoint(Entity entity, int pointIndex);
        void clearOccluderPointSelection();
        int hitTestOccluderPoint(Entity entity, float x, float y);
        OBB getOccluderPointOBB(Entity entity, int pointIndex);

        // Lines endpoint sub-selection (index = lineIndex * 2 + endpoint, 0 = A, 1 = B)
        int getSelectedLinePointIndex() const { return selectedLinePointIndex; }
        Entity getSelectedLinePointEntity() const { return selectedLinePointEntity; }
        void selectLinePoint(Entity entity, int pointIndex);
        void clearLinePointSelection();
        int hitTestLinePoint(Entity entity, float x, float y);
        OBB getLinePointOBB(Entity entity, int pointIndex);
        // world half-extent of a point handle so it stays a constant size on screen
        // (~8px): zoom-scaled in ortho views, distance-scaled in perspective views
        float getPointHandleHalfSize(const Vector3& worldPoint);

        // Polygon / MeshPolygon vertex sub-selection
        int getSelectedPolygonPointIndex() const { return selectedPolygonPointIndex; }
        Entity getSelectedPolygonPointEntity() const { return selectedPolygonPointEntity; }
        bool isSelectedPolygonPointMesh() const { return selectedPolygonPointIsMesh; }
        void selectPolygonPoint(Entity entity, bool isMesh, int pointIndex);
        void clearPolygonPointSelection();
        int hitTestPolygonPoint(Entity entity, bool isMesh, float x, float y);
        OBB getPolygonPointOBB(Entity entity, bool isMesh, int pointIndex);

        // TranslateTracks keyframe point sub-selection
        int getSelectedTrackPointIndex() const { return selectedTrackPointIndex; }
        Entity getSelectedTrackPointEntity() const { return selectedTrackPointEntity; }
        void selectTrackPoint(Entity entity, int pointIndex);
        void clearTrackPointSelection();
        int hitTestTrackPoint(Entity entity, float x, float y);
        OBB getTrackPointOBB(Entity entity, int pointIndex);
        // matrix mapping TranslateTracks values[] to world space: the action
        // target's parent modelMatrix (identity when there is no target/parent)
        Matrix4 getTrackPointsWorldMatrix(Entity entity);

        // Body2D polygon and chain vertex sub-selection
        int getSelectedBody2DPointIndex() const { return selectedBody2DPointIndex; }
        int getSelectedBody2DPointShape() const { return selectedBody2DPointShape; }
        Entity getSelectedBody2DPointEntity() const { return selectedBody2DPointEntity; }
        void selectBody2DPoint(Entity entity, int shapeIndex, int pointIndex);
        void clearBody2DPointSelection();
        int hitTestBody2DPoint(Entity entity, float x, float y, int& shapeIndex);
        OBB getBody2DPointOBB(Entity entity, int shapeIndex, int pointIndex);
        // false for shapes without editable vertices (not polygon or chain, or a tilemap source)
        bool getBody2DPointWorld(Entity entity, int shapeIndex, int pointIndex, Vector3& worldPoint);
    };

}
