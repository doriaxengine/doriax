// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "render/Render.h"
#include "render/preview/MeshPreviewRender.h"
#include "render/preview/DirectionRender.h"
#include "util/ShapeParameters.h"
#include "window/dialog/ScriptCreateDialog.h"
#include "window/dialog/ShaderForkDialog.h"
#include "window/dialog/ComponentAddDialog.h"
#include "window/dialog/TextureSlicerToolDialog.h"

#include "imgui.h"
#include "yaml-cpp/yaml.h"
#include "ecs/Signature.h"
#include "soloud.h"
#include "soloud_wav.h"

#include <functional>

namespace doriax::editor{

    class SceneWindow;
    class ForkShaderCmd;
    class MultiPropertyCmd;

    enum class RowPropertyType{
        Label,
        Bool,
        String,
        MultilineString,
        Float,
        Double,
        FloatPositive,
        DoublePositive,
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
        Ease,
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
        // Inclusive clamp for RowPropertyType::Int (drag and typed entry). Left at
        // 0/0 (min >= max) the field stays unbounded, preserving existing rows.
        int intMin = 0;
        int intMax = 0;
        bool child = false;
        std::string help = "";
        const char *format = "%.2f";
        bool showColors = true;
        std::vector<EnumEntry>* enumEntries = nullptr;
        std::vector<int>* sliderValues = nullptr;
        std::function<void()> onValueChanged = nullptr;
        // Some texture properties only accept file/data-backed images.
        bool allowCameraTexture = true;
        // Hide sampler controls when the consumer owns its filtering and wrapping.
        bool allowTextureSettings = true;
        // Optional filter for LocalEntity picker: only show entities matching all bits
        Signature entityFilter;
        // Entities hidden from the LocalEntity picker and rejected on drag-drop
        // (e.g. to prevent self-references)
        std::vector<Entity> excludeEntities;
    };

    class Properties{
    private:
        Project* project;
        SceneWindow* sceneWindow;
        Command* cmd;

        bool windowOpen;
        bool focusRequested;
        int nameFocusFrames = 0;   // visible frames left for focusNameInput() to take
        bool finishProperty;

        std::set<std::string> usedPreviewIds;

        std::map<std::string, MaterialRender> materialRenders;
        std::map<std::string, DirectionRender> directionRenders;

        MeshPreviewRender shapePreviewRender;

        // for drag and drop textures
        std::map<std::string, bool> hasTextureDrag;
        std::map<std::string, std::map<Entity, Texture>> originalTex;

        // for drag and drop fonts, keyed by property id and slot
        std::map<std::string, bool> hasFontDrag;
        std::map<std::string, std::map<Entity, FontArray>> originalFont;

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
        bool keyframeEasingsExpanded = false;
        bool instancesExpanded = false;
        std::map<std::string, bool> instancesButtonGroups;
        bool pointsExpanded = false;
        std::map<std::string, bool> pointsButtonGroups;
        bool linesExpanded = false;
        std::map<std::string, bool> linesButtonGroups;
        bool polygonExpanded = false;
        std::map<std::string, bool> polygonButtonGroups;
        bool meshPolygonExpanded = false;
        std::map<std::string, bool> meshPolygonButtonGroups;

        std::map<std::string, bool> textureCubeSingleMode;

        std::unordered_map<std::string, Texture> thumbnailTextures;

        // Dirty material deferred-write state
        enum class ScenePropertyInputType {
            Checkbox,
            DragFloat,
            DragInt,
            SliderFloat,
            ColorRGB,
            ColorRGBA,
            Combo
        };

        struct DirtyMaterialEntry {
            unsigned int sceneId;
            Entity entity;
            int submeshIndex;
            std::string relativePath;
            float timer;
        };

        static std::vector<DirtyMaterialEntry> dirtyMaterials;
        static constexpr float materialWriteDelaySec = 0.3f;

        // Sound preview state
        struct SoundPreviewRuntime {
            SoLoud::Wav sample;
            bool loaded = false;
            bool active = false;
            bool playing = false;
            Entity entity = NULL_ENTITY;
            uint32_t sceneId = 0;
            unsigned int handle = 0;
            std::string filename;
            std::string error;
            double length = 0.0;
            double time = 0.0;
        };

        SoundPreviewRuntime soundPreviewRuntime;

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
        void stopActionPreviewIfActive();

        // Dirty material helpers
        void markMaterialDirty(unsigned int sceneId, Entity entity, int submeshIndex, const std::string& relativePath);
        void flushDirtyMaterials(float deltaTime);
        void writeDirtyMaterial(const DirtyMaterialEntry& entry);
        template<typename T>
        void drawScenePropertyRow(SceneProject* sceneProject, const std::string& propertyName, const char* label, ScenePropertyInputType inputType, float inputWidth = -1.0f, float minValue = 0.0f, float maxValue = 1.0f, const std::string& help = "");

        // Sound preview helpers
        SoundPreviewRuntime& soundPreview();
        bool ensureSoundPreviewInitialized();
        void stopSoundPreview(bool unload = false);
        std::filesystem::path resolveSoundPreviewPath(const std::string& filename);
        void applySoundPreviewSettings(const SoundComponent& audio);
        bool loadSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio);
        bool startSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio);
        void pauseSoundPreview();
        bool seekSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio, double time);
        void updateSoundPreview(const SoundComponent& audio);

        // Property label formatting
        std::string formatPropertyLabelValue(const PropertyData& prop);

        // For entity picker popup
        char entityPickerSearchBuffer[128] = {};

        // For component menu
        char componentSearchBuffer[128] = "";
        int hoveredComponentIndex = -1;
        bool addComponentModalOpen = false;
        bool componentMenuJustOpened = false;

        // Dialogs
        ScriptCreateDialog scriptCreateDialog;
        ShaderForkDialog shaderForkDialog;
        ComponentAddDialog componentAddDialog;
        TextureSlicerToolDialog textureSlicerToolDialog;

        static RowPropertyType scriptPropertyTypeToRowPropertyType(ScriptPropertyType scriptType);

        // replace [number] with []
        std::string replaceNumberedBrackets(const std::string& input);
        Vector3 roundZero(const Vector3& val, const float threshold) const;
        float snapDisplayedAngle(float angle, float threshold) const;
        Vector3 getDisplayedEulerAngles(const Quaternion& value, const RotationOrder& order, float zeroThreshold) const;

        bool compareVectorFloat(const float* a, const float* b, size_t elements, const float threshold);

        float getLabelSize(std::string label, bool addRotateIconSpace = true);
        static bool drawSummaryAddButton(const std::string& label, float trailingReservedWidth = 0.0f, bool enabled = true, const char* disabledTooltip = nullptr);

        void helpMarker(std::string desc);

        Texture* findThumbnail(const std::string& path);
        void drawImageWithBorderAndRounding(Texture* texture, const ImVec2& size, float rounding = 4.0f, ImU32 border_col = IM_COL32(0, 0, 0, 255), float border_thickness = 1.0f, bool flipY = false);
        void dragDropResourcesFont(ComponentType cpType, std::string id, size_t slot, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType);
        void setFontSlot(ComponentType cpType, const std::string& id, size_t slot, const std::string& path, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged);
        // one slot of a font property: file name frame, file picker, clear button and drag-drop.
        // width covers the buttons too, -1 fills the table cell; extraButton adds one more
        void drawFontSlot(ComponentType cpType, const std::string& id, size_t slot, bool dif, const FontArray& value, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged, float width = -1.0f, const std::function<void(const ImVec2&)>& extraButton = nullptr);
        // fallback slots of a font property, opened by the Font row
        void drawFontFallbacksPopup(const char* popupId, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged);
        void dragDropResourcesTexture(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities);
        void applyCameraTexture(Entity cameraEntity, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged);
        void drawTextureSettingsPopup(const char* popupId, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged);
        void dragDropResourcesTextureCubeSingleFile(ComponentType cpType, const std::string& id, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType);
        void dragDropResourcesTextureCubeFace(ComponentType cpType, const std::string& id, size_t faceIndex, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType);

        void handleComponentMenu(SceneProject* sceneProject, std::vector<Entity> entities, ComponentType cpType, bool isBundle, bool isBundleOverridden, bool& headerOpen, bool readOnly);

        bool canAddComponent(SceneProject* sceneProject, Entity entity, ComponentType cpType);

        Texture getMaterialPreview(const Material& material, const std::string id, bool receiveIBL = true);
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

        EntityPickerResult drawEntityPickerPopup(const std::string& popupId, const Signature& filter, SceneProject* owningScene, bool includeRelatedScenes, Entity currentValue, uint32_t currentValueSceneId = 0, const std::vector<Entity>& excludeEntities = {});
        void syncSubSelection(SceneProject* sceneProject, Entity entity, int tileIndex, int instanceIndex);

        void beginTable(ComponentType cpType, float firstColSize, std::string nameAddon = "");
        void endTable();
        bool propertyHeader(std::string label, float secondColSize = -1, bool defChanged = false, bool child = false);
        bool propertyRow(RowPropertyType type, ComponentType cpType, std::string id, std::string label, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings = RowSettings());
        bool propertyRowWithAutoButton(RowPropertyType propType, ComponentType cpType, std::string id, std::string label, std::string autoId, std::string autoLabel, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings = RowSettings());
        void setParticleFrames(ComponentType cpType, const std::string& propertyId, SceneProject* sceneProject, Entity entity, const std::vector<int>& frames);
        void setParticleBursts(ComponentType cpType, SceneProject* sceneProject, Entity entity, const std::vector<ParticleBurst>& bursts);
        void setParticleColorGradient(ComponentType cpType, SceneProject* sceneProject, Entity entity, const ParticleColorGradient& gradient);
        void drawParticleFrameList(ComponentType cpType, const std::string& propertyId, const std::string& tableId, SceneProject* sceneProject, const std::vector<Entity>& entities);
        // Shared "Shader" row for components that support forking a built-in shader
        // (Mesh/UI/Points/Lines/Sky): fork, edit files, open .vert/.frag, reset to built-in,
        // plus .vert/.frag drag-drop. The .vert/.frag entry points may share a base name or
        // live in separate files (see Util::resolveCustomShaderPaths).
        void drawCustomShaderRow(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, std::vector<Entity> entities);
        // one shader property of that row (customShader, a mesh's customDepthShader or a
        // water's customUnderwaterShader)
        void drawComponentShaderRow(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, Entity entity, const char* propertyName, const std::string& idSuffix);
        // Custom block members of the component's loaded shader (own or scene default
        // fork), one per name with the vertex declaration first.
        struct ShaderUniformRows {
            std::vector<ShaderUniform> members;
            std::vector<std::string> warnings;
            bool buildFailed = false; // the fork failed and the built-in is rendering
            // scene values of the default shader the component inherits, null with its own fork
            const ShaderUniformValues* inherited = nullptr;
        };
        ShaderUniformRows resolveShaderUniformRows(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, Entity entity);
        // the build state and warnings (status), or the member rows, stored by name in shaderUniforms
        void drawShaderUniformRows(ComponentType cpType, SceneProject* sceneProject, Entity entity, const ShaderUniformRows& rows, bool status);
        enum class ShaderUniformEdit { NONE, CHANGED, RESET };
        // one member row; an unset value shows the inherited one, and RESET drops an override
        ShaderUniformEdit drawShaderUniformRow(const ShaderUniform& uniform, const ShaderUniformValues& values,
                                               const ShaderUniformValues* inherited, const std::string& idPrefix, Vector4& newValue);
        void drawShaderUniformWarnings(const std::vector<std::string>& warnings);
        // label column of a uniforms table, with room for the reset arrow when it can show
        float getShaderUniformsLabelSize(const std::vector<ShaderUniform>& members, bool resetArrow);
        // Scene-level variant: edits a scene default shader property (e.g. "default_mesh_shader"),
        // used by every component of that type whose customShader is empty.
        void drawSceneShaderRow(SceneProject* sceneProject, ShaderType shaderType, const char* scenePropertyName, const char* label);
        // members of a scene default fork, from the components rendering with it (used)
        ShaderUniformRows resolveSceneShaderUniformRows(SceneProject* sceneProject, ShaderType shaderType, bool& used);
        // same split for a default shader's rows, whose values are stored on the scene
        void drawSceneShaderUniforms(SceneProject* sceneProject, ShaderType shaderType, const ShaderUniformRows& rows, bool used, bool status);

        // Ordered user post-process chain: per-pass enable, shader fork/pick, reorder
        // and remove. Uniform rows come from the fork's own u_fs_postParams block.
        void drawScenePostProcess(SceneProject* sceneProject);
        // A pass's resolved shader. The pool lookup takes a lock, so it is done once per
        // pass per frame and shared by the label column and the uniform rows.
        struct PostProcessShader {
            std::shared_ptr<ShaderRender> shader;
            CustomUniformBlock block; // u_fs_postParams, slot -1 when not declared
            bool buildFailed = false;
        };
        PostProcessShader resolvePostProcessShader(const PostProcessPass& pass);
        // same split as drawShaderUniformRows, for a pass's u_fs_postParams members
        void drawPostProcessUniforms(uint32_t sceneId, const std::vector<PostProcessPass>& passes, size_t index,
                                     const PostProcessShader& resolved, bool status);

        // Commits a planned fork as one undoable step and opens the new .vert/.frag in the
        // code editor. Alerts instead when the plan is invalid or the files cannot be written.
        void commitShaderFork(uint32_t sceneId, std::unique_ptr<ForkShaderCmd> forkCmd);

        // Interaction core shared by the entity and scene shader rows; draws into the current
        // table value cell. setShader issues the undoable command; onFork opens the modal
        // that selects the destination and exact shader name before files are written.
        // idSuffix uniquifies ImGui IDs and the edit-files popup within the window.
        // inheritedShader names the scene default an empty component shader falls back to.
        void drawShaderRowContents(ShaderType shaderType, const std::string& currentShader,
                                   const std::function<void(const std::string&)>& setShader,
                                   const std::function<void()>& onFork,
                                   const std::string& idSuffix,
                                   const std::string& inheritedShader = std::string());
        void drawShaderFilesPopup(const std::string& popupName, const std::function<void(const std::string&)>& setShader, const std::string& currentVert, const std::string& currentFrag);
        void drawTransform(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawModelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUIComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawButtonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawScrollbarComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawProgressbarComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTextEditComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawPanelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTextComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUILayoutComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawUIContainerComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawImageComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawSpriteComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTilemapComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawTerrainComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawLightComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawLight2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawOccluder2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawFogComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMirrorComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawReflectionProbeComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawCameraComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawAudioComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawSkyComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawWaterComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawWaterExclusionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawParticlesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawLinesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawPolygonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMeshPolygonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
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
        void drawSkinComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawKeyframeTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        // onRemoveValue/onAddValue let the caller add companion property commands
        // (same undo step) when an entry is deleted or appended, e.g. keyframe
        // easings or cubic-spline tangents
        template<typename Component, typename ValueType>
        void drawTrackValues(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities, RowPropertyType rowType, const ValueType& defaultNewValue, const char* idPrefix, std::vector<ValueType> Component::*memberPtr, const char* propertyName,
                             const std::function<void(MultiPropertyCmd*, Entity, Component*, size_t)>& onRemoveValue = nullptr,
                             const std::function<void(MultiPropertyCmd*, Entity, Component*)>& onAddValue = nullptr);
        void drawTranslateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawRotateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawScaleTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);
        void drawMorphTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities);

    public:
        static constexpr const char* WINDOW_NAME = "Properties";

        Properties(Project* project);

        void setSceneWindow(SceneWindow* sceneWindow);

        void show();
        void stopTransientPreviews();
        void clearSceneState(uint32_t sceneId);
        void flushDirtyMaterialsForScene(uint32_t sceneId);
        void setOpen(bool open);
        bool isOpen() const;
        void focusNameInput();
    };

}
