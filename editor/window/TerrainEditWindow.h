// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "math/Ray.h"
#include "util/TerrainMapUtils.h"

#include "imgui.h"

#include <chrono>
#include <string>
#include <vector>

namespace doriax::editor{

    enum class TerrainBrushMode{
        Raise,
        Lower,
        Smooth,
        Flatten,
        PaintRed,
        PaintGreen,
        PaintBlue,
        PaintDensity,
        EraseDensity
    };

    enum class TerrainBrushShape{
        Circle,
        Square
    };

    enum class TerrainBrushFalloff{
        Smooth,
        Linear,
        Constant
    };

    struct TerrainBrushCursor{
        bool visible = false;
        // World-space closed loops draped over the terrain surface: the brush
        // boundary and (when the falloff has a gradient) the half-strength contour.
        std::vector<Vector3> outerPoints;
        std::vector<Vector3> innerPoints;
    };

    struct TerrainMapSnapshot{
        bool empty = true;
        std::string path;
        std::string id;
        TextureFilter minFilter = TextureFilter::LINEAR;
        TextureFilter magFilter = TextureFilter::LINEAR;
        TextureWrap wrapU = TextureWrap::REPEAT;
        TextureWrap wrapV = TextureWrap::REPEAT;
        ColorFormat colorFormat = ColorFormat::RGBA;
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> pixels;
    };

    struct TerrainMapInfo{
        bool present = false;
        bool sizeKnown = false;
        bool framebuffer = false;
        int width = 0;
        int height = 0;
        int channels = 0;
    };

    struct ActiveStroke{
        bool active = false;
        uint32_t sceneId = NULL_PROJECT_SCENE;
        Entity entity = NULL_ENTITY;
        TerrainMapRef ref;
        TerrainMapSnapshot beforeSnapshot;
        // Brush mode for this stroke (modifiers can override the selected mode)
        TerrainBrushMode effectiveMode = TerrainBrushMode::Raise;
        // Normalized flatten target, sampled from the terrain at stroke start
        float flattenTarget = 0.5f;
        // Stamp pacing: previous stamp position/time for path interpolation and
        // time-based flow
        bool hasLastPoint = false;
        Vector3 lastPoint = Vector3::ZERO;
        std::chrono::steady_clock::time_point lastStampTime;
        // Float-space copy of the edited map (1 channel for heights, 4 for blend):
        // stamps accumulate here and are quantized to the stored bit depth only on
        // write-back, so gentle repeated stamps aren't lost to rounding.
        std::vector<float> workingPixels;
        int workingWidth = 0;
        int workingHeight = 0;
        // Texels written so far, cut into the undo patch when the stroke ends
        TerrainMapRegion dirtyRegion;
        bool heightReferenceValid = false;
        float heightReferenceTerrainSize = 0.0f;
        float heightReferenceMaxHeight = 0.0f;
        int heightReferenceWidth = 0;
        int heightReferenceHeight = 0;
        int heightReferenceChannels = 0;
        int heightReferenceBytesPerChannel = 1;
        std::vector<unsigned char> heightReferencePixels;
    };

    class TerrainEditWindow{
    private:
        class TerrainTextureEditCmd;

        Project* project;

        bool windowOpen;
        bool focusRequested;
        bool brushActive;
        bool normalizeBlendPaint;
        bool heightMapStartAtMiddle;
        bool flattenPickOnStroke;

        uint32_t selectedSceneId;
        Entity selectedEntity;

        TerrainBrushMode brushMode;
        TerrainBrushShape brushShape;
        TerrainBrushFalloff brushFalloff;

        float brushSize;
        float brushStrength;
        float flattenHeight;

        int heightMapResolution;
        int blendMapResolution;
        int densityMapResolution;
        int selectedFoliageLayer;

        uint64_t editTextureCounter = 1;

        ActiveStroke stroke;

        void showTooltip(const char* text, ImGuiHoveredFlags flags = 0);
        bool iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size);
        bool colorIconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec4& color, const ImVec2& size);
        std::string makeEditableTextureId(uint32_t sceneId, Entity entity, const TerrainMapRef& ref);
        std::string makeEditableTexturePath(Project* project, uint32_t sceneId, Entity entity, const TerrainMapRef& ref);
        int expectedChannels(TerrainMapTarget target);
        ColorFormat expectedFormat(TerrainMapTarget target);
        int expectedBytesPerTexel(TerrainMapTarget target);
        static float decodeHeightTexel(const unsigned char* pixels, size_t texelIndex, int channels, int bytesPerChannel);
        static void encodeHeightTexel(unsigned char* pixels, size_t texelIndex, int bytesPerChannel, float value);
        unsigned char clampByte(float value);
        bool setFileBackedTextureData(Project* project, Texture& texture, const std::string& relativePath, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels);
        bool isOwnedEditableTexturePath(const std::string& path, uint32_t sceneId, Entity entity, const TerrainMapRef& ref);
        bool loadTerrainTextureDataFromPath(Project* project, const std::string& path, TextureData& data);
        TerrainMapInfo getTerrainMapInfo(Texture& texture);
        std::string getTerrainMapStatusText(const TerrainMapInfo& info);
        void showTerrainMapStatus(const TerrainMapInfo& info);
        std::vector<unsigned char> copyTexturePixels(TextureData& data);
        std::vector<unsigned char> convertTexturePixels(TextureData& data, TerrainMapTarget target);
        std::vector<unsigned char> makeInitialMapPixels(TerrainMapTarget target, int width, int height);
        void setOwnedTextureData(Texture& texture, const std::string& id, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels);
        TerrainMapSnapshot captureSnapshot(Project* project, Texture& texture, bool forcePixels);
        bool snapshotsEqual(const TerrainMapSnapshot& a, const TerrainMapSnapshot& b);
        void applySnapshotToTexture(Project* project, Texture& texture, const TerrainMapSnapshot& snapshot);
        bool ensureEditableMap(Project* project, SceneProject* sceneProject, Entity entity, const TerrainMapRef& ref, int resolution);
        void writeHeight(TextureData& data, int x, int y, float value);
        static float bilinearHeightSample(const unsigned char* pixels, int width, int height, int channels, int bytesPerChannel, float texelX, float texelY);
        bool raycastTerrainStrokeSurface(const Ray& localRay, TerrainComponent& terrain, const ActiveStroke* activeStroke, Vector3& localPoint, float& localHeight) const;

        SceneProject* findSceneProject(Scene* scene) const;
        SceneProject* getTargetSceneProject() const;
        bool updateTargetFromSelection();
        bool hasValidTarget(SceneProject* sceneProject = nullptr) const;
        int mapResolutionFor(TerrainMapTarget target) const;
        TerrainMapRef getBrushMapRef() const;
        bool isHeightBrush() const;
        bool isDensityBrush() const;
        static bool isScalarTarget(TerrainMapTarget target);

        void captureStrokeHeightReference(TerrainComponent& terrain);
        bool findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight, const ActiveStroke* activeStroke = nullptr) const;
        bool applyBrush(SceneProject* sceneProject, Entity entity, const Vector3& localPoint);
        bool stampBrush(TerrainComponent& terrain, TextureData& data, TerrainMapTarget target, const Vector3& localPoint, float deltaTime);
        bool addStrokePatchCommand(SceneProject* sceneProject, Texture& texture);
        void clearStroke();

        bool createMapForTarget(const TerrainMapRef& ref, int width, int height);
        bool deleteMapForTarget(const TerrainMapRef& ref);
        bool setFoliageLayers(const std::vector<TerrainFoliageLayer>& layers);
        template<typename T> bool setFoliageLayerProperty(const char* field, const T& value);

    public:
        static constexpr const char* WINDOW_NAME = "Terrain Editor";

        static constexpr float MIN_BRUSH_SIZE = 0.1f;
        static constexpr float MAX_BRUSH_SIZE = 50.0f;
        static constexpr float MIN_BRUSH_STRENGTH = 0.01f;
        static constexpr float MAX_BRUSH_STRENGTH = 1.0f;

        // Returns false when terrain map edits could not be persisted to disk —
        // the caller should keep the scene marked unsaved so another save (which
        // retries the writes) is prompted for.
        static bool cleanUnusedTerrainMaps(Project* project);

        TerrainEditWindow(Project* project);
        ~TerrainEditWindow();

        void show();
        void open();
        void openForEntity(Entity entity, uint32_t sceneId);
        void setOpen(bool open);
        bool isOpen() const;

        bool isEditingScene(Scene* scene) const;
        bool beginStroke(Scene* scene, const Ray& ray);
        bool paintStroke(Scene* scene, const Ray& ray);
        void endStroke();
        bool updateCursor(Scene* scene, const Ray& ray, TerrainBrushCursor& cursor) const;

        void adjustBrushSize(float factor);
        void adjustBrushStrength(float factor);
    };

}

