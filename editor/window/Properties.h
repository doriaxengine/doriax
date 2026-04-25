#pragma once

#include "Project.h"
#include "render/preview/MeshPreviewRender.h"
#include "render/preview/DirectionRender.h"
#include "util/ShapeParameters.h"
#include "window/dialog/ScriptCreateDialog.h"
#include "window/dialog/ComponentAddDialog.h"
#include "window/dialog/TextureSlicerToolDialog.h"

#include "imgui.h"
#include "yaml-cpp/yaml.h"
#include "ecs/Signature.h"

namespace doriax::editor{

    enum class RowPropertyType{
        Label,
        Bool,
        String,
        MultilineString,
        Float,
        FloatPositive,
        Float_0_1,
        Vector2,
        Vector3,
        Vector4,
        Quat,
        Color3L,
        Color4L,
        Int,
        UInt,
        Material,
        Texture,
        TextureCube,
        HalfCone,
        UIntSlider,
        IntSlider,
        Direction,
        Enum,
        Custom,
        LocalEntity,
        ExternalEntity,
        Font
    };

    struct EnumEntry {
        int value;
        const char* name;
    };

    struct RowSettings{
        float stepSize = 0.1f;
        float secondColSize = -1;
        bool child = false;
        std::string help = "";
        const char *format = "%.2f";
        bool showColors = true;
        std::vector<EnumEntry>* enumEntries = nullptr;
        std::vector<int>* sliderValues = nullptr;
        std::function<void()> onValueChanged = nullptr;
        // Optional filter for LocalEntity picker: only show entities matching all bits
        Signature entityFilter;
    };

    class Properties{
    private:
        Project* project;
        Command* cmd;

        bool finishProperty;

        std::set<std::string> usedPreviewIds;

        std::map<std::string, MaterialRender> materialRenders;
        std::map<std::string, DirectionRender> directionRenders;

        MeshPreviewRender shapePreviewRender;

        // for drag and drop textures
        std::map<std::string, bool> hasTextureDrag;
        std::map<std::string, std::map<Entity, Texture>> originalTex;

        // for drag and drop fonts
        std::map<std::string, bool> hasFontDrag;
        std::map<std::string, std::map<Entity, std::string>> originalFont;

        std::map<std::string, bool> materialButtonGroups;
        std::map<std::string, bool> spriteFramesButtonGroups;
        bool spriteFramesExpanded = false;

        std::map<std::string, bool> tilemapRectsButtonGroups;
        bool tilemapRectsExpanded = true;

        std::map<std::string, bool> tilemapTilesButtonGroups;
        bool tilemapTilesExpanded = false;
        std::map<std::string, bool> spriteAnimationFramesButtonGroups;
        bool spriteAnimationFramesExpanded = false;
        std::map<std::string, bool> trackValuesExpanded;
        bool instancesExpanded = false;
        std::map<std::string, bool> instancesButtonGroups;
        bool pointsExpanded = false;
        std::map<std::string, bool> pointsButtonGroups;

        std::map<std::string, bool> textureCubeSingleMode;

        std::unordered_map<std::string, Texture> thumbnailTextures;

        // Action preview state
        bool actionPreviewPlaying = false;
        bool actionPreviewing = false;
        Entity actionPreviewEntity = 0;
        uint32_t actionPreviewSceneId = 0;
        struct ActionPreviewState {
            Entity entity = 0;
            Entity parent = 0;
            YAML::Node components;
        };
        std::vector<ActionPreviewState> actionPreviewStates;

        void startActionPreview(Entity entity, Scene* scene, SceneProject* sceneProject);
        void stopActionPreview(Scene* scene, SceneProject* sceneProject);

        // For entity picker popup
        char entityPickerSearchBuffer[128] = {};

        // For component menu
        char componentSearchBuffer[128] = "";
        int hoveredComponentIndex = -1;
        bool addComponentModalOpen = false;
        bool componentMenuJustOpened = false;

        // Dialogs
        ScriptCreateDialog scriptCreateDialog;
        ComponentAddDialog componentAddDialog;
        TextureSlicerToolDialog textureSlicerToolDialog;

        static RowPropertyType scriptPropertyTypeToRowPropertyType(ScriptPropertyType scriptType);

        // replace [number] with []
        std::string replaceNumberedBrackets(const std::string& input);
        Vector3 roundZero(const Vector3& val, const float threshold);

        bool compareVectorFloat(const float* a, const float* b, size_t elements, const float threshold);

        float getLabelSize(std::string label, bool addRotateIconSpace = true);

        void helpMarker(std::string desc);

        Texture* findThumbnail(const std::string& path);
        void drawImageWithBorderAndRounding(Texture* texture, const ImVec2& size, float rounding = 4.0f, ImU32 border_col = IM_COL32(0, 0, 0, 255), float border_thickness = 1.0f, bool flipY = false);
        void dragDropResourcesFont(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType);
        void dragDropResourcesTexture(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType);
        void dragDropResourcesTextureCubeSingleFile(ComponentType cpType, const std::string& id, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType);
        void dragDropResourcesTextureCubeFace(ComponentType cpType, const std::string& id, size_t faceIndex, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType);

        void handleComponentMenu(SceneProject* sceneProject, std::vector<Entity> entities, ComponentType cpType, bool isBundle, bool isBundleOverridden, bool& headerOpen, bool readOnly);

        bool canAddComponent(SceneProject* sceneProject, Entity entity, ComponentType cpType);

        Texture getMaterialPreview(const Material& material, const std::string id);
        Texture getDirectionPreview(const Vector3& direction, const std::string id);
        bool drawSpriteFramePreview(Texture* texture, const Rect& rect, const ImVec2& size, const char* itemId);

        void updateShapePreview(const ShapeParameters& shapeParams);
        void updateMeshShape(MeshComponent& meshComp, MeshSystem* meshSys, const ShapeParameters& shapeParams);

        void drawNinePatchesPreview(const ImageComponent& img, Texture* texture, Texture* thumbTexture, const ImVec2& size = ImVec2(0, 0));

        struct EntityPickerResult {
            bool chosen = false;
            Entity entity = NULL_ENTITY;
            uint32_t sceneId = 0;
        };

        EntityPickerResult drawEntityPickerPopup(const std::string& popupId, const Signature& filter, SceneProject* owningScene, bool includeChildScenes, Entity currentValue, uint32_t currentValueSceneId = 0);

        void beginTable(ComponentType cpType, float firstColSize, std::string nameAddon = "");
        void endTable();
        bool propertyHeader(std::string label, float secondColSize = -1, bool defChanged = false, bool child = false);
        bool propertyRow(RowPropertyType type, ComponentType cpType, std::string id, std::string label, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings = RowSettings());
        bool propertyRowWithAutoButton(RowPropertyType propType, ComponentType cpType, std::string id, std::string label, std::string autoId, std::string autoLabel, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings = RowSettings());
        void drawTransform(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawModelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUIComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawButtonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTextComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUILayoutComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUIContainerComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawImageComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawSpriteComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTilemapComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTerrainComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawLightComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawCameraComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawSkyComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawParticlesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawPointsComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawInstancedMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawScriptComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawBody2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawBody3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawJoint2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawJoint3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTimedActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawPositionActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawRotationActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawScaleActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawColorActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawAlphaActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawSpriteAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawBundleComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawBoneComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawKeyframeTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        template<typename Component, typename ValueType>
        void drawTrackValues(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities, RowPropertyType rowType, const ValueType& defaultNewValue, const char* idPrefix, std::vector<ValueType> Component::*memberPtr, const char* propertyName);
        void drawTranslateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawRotateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawScaleTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMorphTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);

    public:
        static constexpr const char* WINDOW_NAME = "Properties";

        Properties(Project* project);

        void show();
    };

}