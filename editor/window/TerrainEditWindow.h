#ifndef TERRAINEDITWINDOW_H
#define TERRAINEDITWINDOW_H

#include "Project.h"
#include "math/Ray.h"

#include "imgui.h"

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
        PaintBlue
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
        Vector3 center = Vector3::ZERO;
        Vector3 axisX = Vector3::ZERO;
        Vector3 axisZ = Vector3::ZERO;
        TerrainBrushShape shape = TerrainBrushShape::Circle;
    };

    enum class TerrainMapTarget{
        HeightMap,
        BlendMap
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

    class TerrainEditWindow{
    private:
        struct TerrainMapInfo{
            bool present = false;
            bool sizeKnown = false;
            bool framebuffer = false;
            int width = 0;
            int height = 0;
            int channels = 0;
        };

        class TerrainTextureEditCmd;

        static uint64_t s_editTextureCounter;

        static void showTooltip(const char* text, ImGuiHoveredFlags flags = 0);
        static bool iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size);
        static bool colorIconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec4& color, const ImVec2& size);
        static std::string makeEditableTextureId(uint32_t sceneId, Entity entity, TerrainMapTarget target);
        static std::string makeEditableTexturePath(Project* project, uint32_t sceneId, Entity entity, TerrainMapTarget target);
        static bool isEditableTexturePath(const std::string& path);
        static Texture& getTerrainTexture(TerrainComponent& terrain, TerrainMapTarget target);
        static const char* getTerrainPropertyName(TerrainMapTarget target);
        static int expectedChannels(TerrainMapTarget target);
        static ColorFormat expectedFormat(TerrainMapTarget target);
        static unsigned char clampByte(float value);
        static bool writeTextureFile(Project* project, const std::string& relativePath, int width, int height, int channels, const std::vector<unsigned char>& pixels);
        static bool setFileBackedTextureData(Project* project, Texture& texture, const std::string& relativePath, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels);
        static bool hasLoadedData(Texture& texture);
        static bool isOwnedEditableTexturePath(const std::string& path, uint32_t sceneId, Entity entity, TerrainMapTarget target);
        static bool loadTerrainTextureDataFromPath(Project* project, const std::string& path, TextureData& data);
        static TerrainMapInfo getTerrainMapInfo(Texture& texture);
        static std::string getTerrainMapStatusText(const TerrainMapInfo& info);
        static void showTerrainMapStatus(const TerrainMapInfo& info);
        static std::vector<unsigned char> copyTexturePixels(TextureData& data);
        static std::vector<unsigned char> convertTexturePixels(TextureData& data, TerrainMapTarget target);
        static void setOwnedTextureData(Texture& texture, const std::string& id, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels);
        static TerrainMapSnapshot captureSnapshot(Project* project, Texture& texture, bool forcePixels);
        static bool snapshotsEqual(const TerrainMapSnapshot& a, const TerrainMapSnapshot& b);
        static void applySnapshotToTexture(Project* project, Texture& texture, const TerrainMapSnapshot& snapshot);
        static bool ensureEditableMap(Project* project, SceneProject* sceneProject, Entity entity, TerrainMapTarget target, int resolution);
        static float readHeight(TextureData& data, int x, int y);
        static void writeHeight(TextureData& data, int x, int y, float value);

        struct ActiveStroke{
            bool active = false;
            uint32_t sceneId = NULL_PROJECT_SCENE;
            Entity entity = NULL_ENTITY;
            TerrainMapTarget target = TerrainMapTarget::HeightMap;
            TerrainMapSnapshot beforeSnapshot;
            bool heightReferenceValid = false;
            float heightReferenceTerrainSize = 0.0f;
            float heightReferenceMaxHeight = 0.0f;
            int heightReferenceWidth = 0;
            int heightReferenceHeight = 0;
            int heightReferenceChannels = 0;
            std::vector<unsigned char> heightReferencePixels;
        };

        static bool sampleTerrainHeightPixels(const unsigned char* pixels, int width, int height, int channels, float terrainSize, float maxHeight, float localX, float localZ, float& localHeight);
        static bool raycastTerrainSurface(const Ray& localRay, TerrainComponent& terrain, const ActiveStroke* activeStroke, Vector3& localPoint, float& localHeight);

        Project* project;

        bool windowOpen;
        bool brushActive;
        bool normalizeBlendPaint;

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

        ActiveStroke stroke;

        SceneProject* findSceneProject(Scene* scene) const;
        SceneProject* getTargetSceneProject() const;
        bool updateTargetFromSelection();
        bool hasValidTarget(SceneProject* sceneProject = nullptr) const;
        TerrainMapTarget getBrushTarget() const;
        bool isHeightBrush() const;

        void captureStrokeHeightReference(TerrainComponent& terrain);
        bool findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight, const ActiveStroke* activeStroke = nullptr) const;
        bool applyBrush(SceneProject* sceneProject, Entity entity, const Vector3& localPoint);
        void refreshTerrain(SceneProject* sceneProject, Entity entity, TerrainMapTarget target);
        void clearStroke();

        bool createMapForTarget(TerrainMapTarget target, int width, int height);
        bool deleteMapForTarget(TerrainMapTarget target);

    public:
        static constexpr const char* WINDOW_NAME = "Terrain Editor";

        static void cleanUnusedTerrainMaps(Project* project);

        TerrainEditWindow(Project* project);
        ~TerrainEditWindow();

        void show();
        void openForEntity(Entity entity, uint32_t sceneId);

        bool isEditingScene(Scene* scene) const;
        bool beginStroke(Scene* scene, const Ray& ray);
        bool paintStroke(Scene* scene, const Ray& ray);
        void endStroke();
        bool updateCursor(Scene* scene, const Ray& ray, TerrainBrushCursor& cursor) const;
    };

}

#endif /* TERRAINEDITWINDOW_H */
