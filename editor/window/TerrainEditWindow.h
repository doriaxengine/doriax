#ifndef TERRAINEDITWINDOW_H
#define TERRAINEDITWINDOW_H

#include "Project.h"
#include "math/Ray.h"

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
        struct ActiveStroke{
            bool active = false;
            uint32_t sceneId = NULL_PROJECT_SCENE;
            Entity entity = NULL_ENTITY;
            TerrainMapTarget target = TerrainMapTarget::HeightMap;
            TerrainMapSnapshot beforeSnapshot;
        };

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

        bool findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight) const;
        bool applyBrush(SceneProject* sceneProject, Entity entity, const Vector3& localPoint);
        void refreshTerrain(SceneProject* sceneProject, Entity entity, TerrainMapTarget target);
        void clearStroke();

        bool createMapForTarget(TerrainMapTarget target, int resolution);

    public:
        static constexpr const char* WINDOW_NAME = "Terrain Editor";

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
