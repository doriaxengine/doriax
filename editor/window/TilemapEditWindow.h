// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "command/Command.h"
#include "component/TilemapComponent.h"
#include "math/Ray.h"
#include "window/dialog/TextureSlicerToolDialog.h"

#include "imgui.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace doriax::editor{

    enum class TilemapTool{
        Brush,
        Eraser,
        Line,
        RectFill,
        FloodFill,
        Eyedropper
    };

    enum class TilemapPaletteView{
        Sheet,
        Grid
    };

    struct TileStampCell{
        int colOffset = 0;
        int rowOffset = 0;
        int rectId = -1;
    };

    class TilemapEditWindow{
    private:
        struct ActiveStroke{
            bool active = false;
            bool rightButtonErase = false;
            uint32_t sceneId = NULL_PROJECT_SCENE;
            Entity entity = NULL_ENTITY;
            TilemapTool effectiveTool = TilemapTool::Brush;
            std::vector<TileData> beforeTiles;
            bool wasModifiedBefore = false;
            int startCellX = 0;
            int startCellY = 0;
            int currentCellX = 0;
            int currentCellY = 0;
            int lastCellX = 0;
            int lastCellY = 0;
            bool hasLastCell = false;
            uint32_t randomSeed = 1;
        };

        Project* project;

        bool windowOpen = false;
        bool focusRequested = false;
        bool paintActive = false;

        uint32_t selectedSceneId = NULL_PROJECT_SCENE;
        Entity selectedEntity = NULL_ENTITY;

        TilemapTool activeTool = TilemapTool::Brush;
        TilemapPaletteView paletteView = TilemapPaletteView::Sheet;

        int selectedSubmesh = 0;
        int selectedRectId = 0;
        std::vector<TileStampCell> stampCells;
        int stampCols = 1;
        int stampRows = 1;
        std::vector<int> variantRectIds;
        uint32_t brushSceneId = NULL_PROJECT_SCENE;
        Entity brushEntity = NULL_ENTITY;

        bool autoCellSize = true;
        float customCellWidth = 32.0f;
        float customCellHeight = 32.0f;
        bool replaceExisting = true;
        bool randomizeBrush = false;
        bool showGrid = true;
        bool showCollisionOverlay = false;
        bool collisionPaintMode = false;

        float sheetZoom = 2.0f;
        bool sheetFitPending = false;
        float gridThumbSize = 48.0f;
        char searchBuffer[128] = "";
        int collisionFilter = 0; // 0 = All, 1 = Solid, 2 = Passable

        bool sheetDragging = false;
        ImVec2 sheetDragStartTex = ImVec2(0.0f, 0.0f);
        ImVec2 sheetDragEndTex = ImVec2(0.0f, 0.0f);

        bool collisionDragging = false;
        bool collisionDragValue = true;
        bool collisionDragWasModified = false;
        uint32_t collisionDragSceneId = NULL_PROJECT_SCENE;
        Entity collisionDragEntity = NULL_ENTITY;
        std::map<int, bool> collisionDragBefore;

        bool hoverValid = false;
        uint32_t hoverSceneId = NULL_PROJECT_SCENE;
        Entity hoverEntity = NULL_ENTITY;
        int hoverCellX = 0;
        int hoverCellY = 0;
        Vector2 hoverLocalPos = Vector2::ZERO;

        bool hasLastPaintedCell = false;
        int lastPaintedCellX = 0;
        int lastPaintedCellY = 0;

        uint32_t strokeSeedCounter = 1;
        ActiveStroke stroke;

        TextureSlicerToolDialog slicerDialog;

        static void showTooltip(const char* text, ImGuiHoveredFlags flags = 0);
        static bool iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size);
        static void drawRectTooltip(int rectId, const TileRectData& rectData);
        static void removeTileAt(TilemapComponent& tilemap, unsigned int index);
        static std::vector<TileData> snapshotTiles(const TilemapComponent& tilemap);
        static bool tilesEqual(const std::vector<TileData>& a, const std::vector<TileData>& b);
        static void notifyTilemapChanged(SceneProject* sceneProject, Entity entity);

        SceneProject* findSceneProject(Scene* scene) const;
        SceneProject* getTargetSceneProject() const;
        bool updateTargetFromSelection();
        bool refreshTarget();
        void selectTilemapEntity(SceneProject* sceneProject, Entity entity);
        bool hasValidTarget(SceneProject* sceneProject = nullptr) const;
        void ensureValidSelection(const TilemapComponent& tilemap, const MeshComponent* meshComp);

        Vector2 getEffectiveCellSize(const TilemapComponent& tilemap) const;
        bool raycastTilemapPlane(Scene* scene, Entity entity, const Ray& ray, Vector2& outLocalPos) const;
        bool worldToScreen(Camera* camera, const Vector3& worldPos, const ImVec2& imageMin, const ImVec2& imageSize, ImVec2& outScreen) const;

        void resetBrush();
        void selectSingleRect(int rectId, const TilemapComponent& tilemap);
        void toggleVariantRect(int rectId, const TilemapComponent& tilemap);
        void selectPaletteTile(SceneProject* sceneProject, const TilemapComponent& tilemap, int rectId, bool ctrlHeld);
        void enablePaintMode(SceneProject* sceneProject);
        void paintWithPalette(SceneProject* sceneProject);
        bool buildStampFromSheetRect(const TilemapComponent& tilemap, int submeshId, float x0, float y0, float x1, float y1);
        std::unordered_set<int> buildSelectedRectSet() const;
        int pickBrushRectForCell(int cellX, int cellY, uint32_t seed) const;
        int pickStampRectForOffset(int offsetX, int offsetY, bool flipY) const;
        int resolveCellRect(int cellX, int cellY, int originX, int originY, bool flipY, uint32_t seed) const;
        bool hasValidBrush(const TilemapComponent& tilemap) const;

        bool paintCellSingle(TilemapComponent& tilemap, int cellX, int cellY, int rectId, const Vector2& cellSize);
        bool paintStampAt(TilemapComponent& tilemap, int anchorCellX, int anchorCellY, const Vector2& cellSize, uint32_t seed);
        bool eraseCell(TilemapComponent& tilemap, int cellX, int cellY, const Vector2& cellSize);
        void paintLineCells(TilemapComponent& tilemap, int x0, int y0, int x1, int y1, const Vector2& cellSize, uint32_t seed);
        void paintRectCells(TilemapComponent& tilemap, int x0, int y0, int x1, int y1, const Vector2& cellSize, uint32_t seed);
        bool applyFloodFill(TilemapComponent& tilemap, int startCellX, int startCellY, const Vector2& cellSize, uint32_t seed);
        int findTileAtLocalPos(const TilemapComponent& tilemap, const Vector2& localPos) const;

        void handleCollisionPaint(SceneProject* sceneProject, TilemapComponent& tilemap, int rectId, bool clicked, bool hovered);
        void finishCollisionDrag();
        void clearStroke();

        void openSlicerForTarget(SceneProject* sceneProject, Entity entity);
        void drawLayerBar(SceneProject* sceneProject);
        void drawToolbar(SceneProject* sceneProject, TilemapComponent& tilemap);
        void drawPlacementOptions(TilemapComponent& tilemap);
        void drawPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp);
        void drawSheetPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp);
        void drawGridPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp);
        void drawFooter(SceneProject* sceneProject, TilemapComponent& tilemap);

    public:
        static constexpr const char* WINDOW_NAME = "Tilemap Editor";
        static constexpr int MAX_FLOOD_FILL_CELLS = 4096;

        // Builds an undoable command replacing one submesh's tile rects while keeping other
        // submeshes intact and preserving collision flags on matching rect bounds.
        static Command* buildApplySliceCmd(Project* project, uint32_t sceneId, Entity entity,
                                           const TextureSlicerToolDialog::SliceResult& result,
                                           int* outFirstNewRectIndex = nullptr);

        TilemapEditWindow(Project* project);
        ~TilemapEditWindow();

        void show();
        void open();
        void openForEntity(Entity entity, uint32_t sceneId);
        void setOpen(bool open);
        bool isOpen() const;
        void resetProjectState();

        bool isEditingScene(Scene* scene) const;
        bool isRightStrokeActive() const;
        bool handleShortcutKeys(Scene* scene);

        void updateHover(Scene* scene, const Ray& ray);
        void clearHover();

        bool beginStroke(Scene* scene, const Ray& ray, bool rightButton = false);
        bool paintStroke(Scene* scene, const Ray& ray);
        void endStroke();
        void cancelStroke();

        void drawViewportOverlay(SceneProject* sceneProject, const ImVec2& imageMin, const ImVec2& imageSize, bool isMouseInCanvas);
    };

}
