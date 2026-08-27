// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "Scene.h"
#include "Catalog.h"
#include "render/SceneRender.h"
#include "command/CommandHistory.h"
#include "render/preview/MaterialRender.h"
#include "Conector.h"
#include "Generator.h"
#include "Configs.h"
#include "util/EntityBundle.h"
#include "util/ScriptParser.h"
#include "util/ScopedDefaultEntityPool.h"

#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <chrono>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <atomic>
#include <mutex>

namespace doriax::editor{

    enum class SceneType{
        SCENE_3D,
        SCENE_2D,
        SCENE_UI
    };

    enum class ScenePlayState {
        STOPPED,
        SAVING,
        LOADING,
        PLAYING,
        PAUSED,
        CANCELLING
    };

    enum class InsertionType{
        BEFORE,
        AFTER,
        INTO
    };

    enum class TabType{
        SCENE,
        CODE_EDITOR,
        IMAGE_VIEWER
    };

    struct TabEntry{
        TabType type;
        std::string filepath;
    };

    struct SceneMaxValues {
        unsigned int maxSubmeshes = 0;
        unsigned int maxTilemapTilesRect = 0;
        unsigned int maxTilemapTiles = 0;
        unsigned int maxExternalBuffers = 0;
        unsigned int maxSpriteFrames = 0;
        unsigned int maxBones = 0;
    };

    struct ChildSceneRef {
        uint32_t id = NULL_PROJECT_SCENE;
        bool startActive = true;
    };

    struct SceneProject{
        uint32_t id = NULL_PROJECT_SCENE;
        std::string name = "Unknown";
        Scene* scene = nullptr;
        SceneType sceneType;
        std::vector<Entity> entities;
        Entity mainCamera = NULL_ENTITY;
        Entity defaultCamera = NULL_ENTITY;
        SceneRender* sceneRender = nullptr;
        std::vector<Entity> selectedEntities;
        fs::path filepath;
        bool needUpdateRender = true;
        // Extra frames to keep drawing after a change so late async uploads still show.
        int renderSettleFrames = 0;
        // Bumped on structural changes so the Structure window can cache its tree.
        uint64_t structureVersion = 0;
        bool isModified = false;
        bool isVisible = false;
        bool opened = true;
        bool expandedInline = false;
        SceneDisplaySettings displaySettings;
        ScenePlayState playState = ScenePlayState::STOPPED;
        YAML::Node playStateSnapshot;
        SceneMaxValues maxValues;
        std::set<ShaderKey> shaderKeys;
        std::vector<ChildSceneRef> childScenes;
        std::vector<SceneScriptSource> cppScripts;
        std::vector<BundleSceneInfo> bundles;
        YAML::Node editorCameraState;
    };

    struct NestedBundleRecovery {
        std::filesystem::path path;
        Entity rootEntity = NULL_ENTITY;
        std::vector<EntityBundle::EntityMember> members;
    };

    struct NodeRecoveryEntry {
        YAML::Node node;
        size_t transformIndex = 0;
        std::vector<EntityBundle::EntityMember> members;
        std::map<Entity, uint64_t> overrides;
        std::vector<NestedBundleRecovery> nestedBundles;
    };

    using NodeRecovery = std::map<std::string, NodeRecoveryEntry>;

    struct SharedMoveRecoveryEntry {
        Entity oldParent;
        size_t oldIndex;
        size_t hasTransform;
    };

    struct TerrainEditorSettings {
        int brushMode = 0;           // TerrainBrushMode::Raise
        int brushShape = 0;          // TerrainBrushShape::Circle
        int brushFalloff = 0;        // TerrainBrushFalloff::Smooth
        float brushSize = 4.0f;
        float brushStrength = 0.3f;  // normalized flow per second at full falloff
        float flattenHeight = 0.5f;
        int heightMapResolution = 512;
        int blendMapResolution = 512;
        bool normalizeBlendPaint = true;
        bool heightMapStartAtMiddle = true;
        bool flattenPickOnStroke = true;
    };

    using SharedMoveRecovery = std::map<std::string, SharedMoveRecoveryEntry>;

    struct ComponentRecoveryEntry {
        Entity entity;
        YAML::Node node;
    };

    using ComponentRecovery = std::map<std::string, ComponentRecoveryEntry>;

    class Project{
    private:

        Conector conector;
        Generator generator;

        std::string name;

        unsigned int canvasWidth;
        unsigned int canvasHeight;
        Scaling scalingMode;
        TextureStrategy textureStrategy;
        bool vsyncEnabled;
        WindowMode windowMode;
        unsigned int windowWidth;
        unsigned int windowHeight;
        bool windowResizable;
        std::string windowTitle;  // empty = use the project name
        std::filesystem::path windowIcon;  // project-relative image path; empty = no icon
        std::filesystem::path assetsDir;
        std::filesystem::path luaDir;
        std::vector<std::filesystem::path> scriptDirs;  // extra C++ include and source roots
        std::string cmakeCCompiler;
        std::string cmakeCxxCompiler;
        std::string cmakeGenerator;
        // Atomic: read by the play-startup thread while the settings dialog can
        // write it from the UI thread.
        std::atomic<unsigned int> cmakeBuildJobs{0};
        bool packNativeResources;
        CommandHistory projectHistory;

        uint32_t startSceneId;
        TerrainEditorSettings terrainEditorSettings;

        uint32_t nextSceneId;

        std::vector<SceneProject> scenes;
        std::vector<TabEntry> tabs;

        struct PlayRuntimeScene {
            uint32_t sourceSceneId = NULL_PROJECT_SCENE;
            SceneProject* runtime = nullptr; // Scene used by the Engine while playing
            bool ownedRuntime = false;       // true when runtime was cloned and must be deleted
            bool initialized = false;        // true when scripts and library are initialized
        };

        struct PlaySession {
            uint32_t mainSceneId = NULL_PROJECT_SCENE;
            std::vector<PlayRuntimeScene> runtimeScenes;
            std::atomic<bool> cancelled{false};
            std::atomic<bool> startupThreadDone{false};  // Set when connect thread exits
            std::atomic<bool> startupSucceeded{false};   // True only if finalizeStart was called
        };

        mutable std::mutex playSessionMutex;
        std::shared_ptr<PlaySession> activePlaySession;

        SceneProject* createRuntimeCloneFromSource(const SceneProject* source);
        void prepareRuntimeScene(PlayRuntimeScene& entry);
        Entity getSceneCamera(const SceneProject* sceneProject) const;
        void cleanupPlaySession(const std::shared_ptr<PlaySession>& session);

        SceneRender* createSceneRender(SceneType type, Scene* scene) const;
        Entity createDefaultCamera(SceneType type, Scene* scene) const;
        Ray screenToRayFromCamera(const CameraComponent& camera, float x, float y) const;
        AABB getEntityWorldAABB(Scene* scene, Entity entity, Scene* mainScene) const;
        AABB getEntityLocalAABB(Scene* scene, Entity entity) const;
        Entity findBestEntityByRay(const std::vector<Entity>& entities, Scene* scene, const Ray& ray, Scene* mainScene, SceneType sceneType, float& distance, size_t& index) const;
        bool selectEntitiesInRect(uint32_t sceneId, const std::vector<Entity>& entities, Scene* scene, const Matrix4& vpMatrix, Vector2 start, Vector2 end);
        uint32_t selectedScene;
        uint32_t selectedSceneForProperties;

        using MaterialLinkKey = std::tuple<uint32_t, Entity, unsigned int>;
        struct MaterialLinkEntry {
            std::string filePath;
            std::filesystem::file_time_type lastWriteTime;
        };

        std::filesystem::path projectPath;
        bool resourcesFocused;
        std::map<MaterialLinkKey, MaterialLinkEntry> materialFileLinks;
        std::chrono::steady_clock::time_point lastMaterialRefreshTime;
        static constexpr double materialRefreshIntervalSec = 0.2;

        std::map<std::filesystem::path, EntityBundle> entityBundles;
        std::vector<std::filesystem::path> standaloneBundles;  // built without a scene instance

        std::string libName;

        template<typename T>
        T* findScene(uint32_t sceneId) const;

        Entity createNewEntity(uint32_t sceneId, std::string entityName);
        bool createNewComponent(uint32_t sceneId, Entity entity, ComponentType component);
        void calculateSceneMaxValues(const SceneProject* sceneProject, SceneMaxValues& maxValues) const;
        void resetEngineConfigs(bool executeViewChanged);
        void resetConfigs();

        void updateSceneCppScripts(SceneProject* sceneProject);
        void updateSceneBundles(SceneProject* sceneProject);
        void removeBundleInstanceTracking(uint32_t sceneId, Entity rootEntity);

        std::vector<SceneScriptSource> collectAllSceneCppScripts() const;
        std::vector<BundleSceneInfo> collectAllBundles() const;

        void pauseEngineScene(Scene* scene, bool pause) const;

        void copyEngineApiToProject();

        void registerSceneManager();
        void registerBundleManager();

        SceneProject* findSceneProjectByScene(Scene* scene);

        static bool matchesRelativePath(const fs::path& relativeBase, const fs::path& currentPath);
        static bool matchesRelativeString(const fs::path& relativeBase, const std::string& currentPath);
        static std::vector<ChildSceneRef>::iterator findChildScene(std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId);
        static std::vector<ChildSceneRef>::const_iterator findChildScene(const std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId);
        static bool eraseChildSceneReference(std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId);
        static bool remapRelativePath(const fs::path& oldRelative, const fs::path& newRelative,
                          const fs::path& currentPath, fs::path& updatedPath);
        static bool remapRelativeString(const fs::path& oldRelative, const fs::path& newRelative,
                        const std::string& currentPath, std::string& updatedPath);
        static bool remapScriptEntryPaths(ScriptEntry& scriptEntry, const fs::path& oldRelative,
                          const fs::path& newRelative);
        // Absolute paths: Lua entries match against the Lua root, C++ entries against the project.
        bool remapScriptPathsInRegistry(EntityRegistry* registry, const fs::path& oldPath,
                        const fs::path& newPath);
        bool cleanupScriptPathsInRegistry(EntityRegistry* registry, const fs::path& deletedPath);
        // Runs transform over every stored asset reference (component textures and cubemap
        // faces, model and sound filenames, text fonts), rewriting the value in place.
        static bool visitAssetPathsInRegistry(EntityRegistry* registry, const std::function<bool(std::string&)>& transform);
        // Same for Lua script entries; C++ ones are build inputs kept project-relative.
        static bool visitLuaPathsInRegistry(EntityRegistry* registry, const std::function<bool(std::string&)>& transform);
        bool visitAssetPathsInMaterialFiles(const std::function<bool(std::string&)>& transform);
        // Runs an asset transform over every scene, entity bundle and material file,
        // marking what changed.
        void applyAssetPathChange(const std::function<bool(std::string&)>& transform);
        // Applies a transform to the customShader of every renderable component (Mesh/UI/
        // Points/Lines/Sky) across all scenes and entity bundles, flagging shader reloads
        // and marking affected scenes/bundles modified. The transform mutates the value in
        // place and returns whether it changed.
        void applyCustomShaderPathChange(const std::function<bool(std::string&)>& transform);
        bool removeMissingChildSceneReferences(SceneProject& sceneProject);
        bool detachChildSceneFromParents(uint32_t childSceneId, const std::set<uint32_t>& skippedSceneIds);

        void finalizeStart(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene>& runtimeScenes);
        void finalizeStop(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene> runtimeScenes);
        void runPlayStartup(const std::shared_ptr<PlaySession>& session, uint32_t sceneId);
        void failPlayStartup(const std::shared_ptr<PlaySession>& session, uint32_t sceneId, const std::string& message,
                     const std::string& alertTitle = "", const std::string& alertMessage = "");
        bool saveSceneFile(SceneProject* sceneProject, const std::filesystem::path& path, bool stopTransientPreviews = true);
        bool saveSceneForPlayStartup(SceneProject* sceneProject);
        bool saveLoadedSceneOnStop(SceneProject* loadedSceneProject, SceneProject* editorScene, bool keepModified);
        bool writeSceneToPath(uint32_t sceneId, const std::filesystem::path& path, bool stopTransientPreviews = true);
        void saveModifiedChildScenes(uint32_t sceneId, std::function<void(bool)> callback = nullptr);
        bool hasSceneUnsavedChangesImpl(uint32_t sceneId, std::unordered_set<uint32_t>& visited) const;
        void saveSceneListSequentially(std::vector<uint32_t> sceneIds, std::function<void(bool)> callback);

        void collectInvolvedScenes(uint32_t sceneId, std::vector<uint32_t>& involvedSceneIds);
        void collectStartActiveScenes(uint32_t sceneId, std::vector<uint32_t>& activeSceneIds);

        uint32_t createNewSceneInternal(std::string sceneName, SceneType type, uint32_t previousSceneId);
        std::vector<std::filesystem::path> findProjectFiles(const std::function<bool(const std::string&)>& matches) const;
        void openSceneInternal(fs::path filepath, uint32_t sceneToClose);

        // The standalone build reads shaders from the editor cache (DORIAX_SHADER_PATH)
        // and cannot compile, so a variant the editor never draws with is missing there.
        void buildStandaloneShaderCache();

    public:
        // Factory defaults, also used by the settings dialog to restore a value
        static constexpr unsigned int defaultCanvasWidth = 1280;
        static constexpr unsigned int defaultCanvasHeight = 720;
        static constexpr Scaling defaultScalingMode = Scaling::FITWIDTH;
        static constexpr TextureStrategy defaultTextureStrategy = TextureStrategy::RESIZE;
        static constexpr bool defaultVSyncEnabled = true;
        static constexpr WindowMode defaultWindowMode = WindowMode::WINDOWED;
        static constexpr unsigned int defaultWindowWidth = defaultCanvasWidth;
        static constexpr unsigned int defaultWindowHeight = defaultCanvasHeight;
        static constexpr bool defaultWindowResizable = true;
        static constexpr const char* defaultWindowTitle = "";
        static constexpr const char* defaultAssetsDir = ".";
        static constexpr const char* defaultLuaDir = ".";
        static constexpr unsigned int defaultCMakeBuildJobs = 0;
        static constexpr bool defaultPackNativeResources = false;

        Project();

        // Live-derive the shader keys a scene needs from its current components
        // (used at scene save and by the Export window's pre-populated list).
        void collectSceneShaderKeys(const SceneProject* sceneProject, std::set<ShaderKey>& shaderKeys) const;

        std::string getName() const;
        void setName(std::string name);

        void setCanvasSize(unsigned int width, unsigned int height);
        unsigned int getCanvasWidth() const;
        unsigned int getCanvasHeight() const;

        void setScalingMode(Scaling scalingMode);
        Scaling getScalingMode() const;

        void setTextureStrategy(TextureStrategy textureStrategy);
        TextureStrategy getTextureStrategy() const;

        void setVSyncEnabled(bool enabled);
        bool isVSyncEnabled() const;

        void setWindowMode(WindowMode windowMode);
        WindowMode getWindowMode() const;

        void setWindowSize(unsigned int width, unsigned int height);
        unsigned int getWindowWidth() const;
        unsigned int getWindowHeight() const;

        void setWindowResizable(bool resizable);
        bool isWindowResizable() const;

        // Stored title; empty means "use the project name"
        void setWindowTitle(const std::string& title);
        std::string getWindowTitle() const;

        void setWindowIcon(const std::filesystem::path& iconPath);
        std::filesystem::path getWindowIcon() const;

        // Window configuration for generated/exported desktop builds,
        // with the title fallback already resolved
        WindowSettings getWindowSettings() const;

        void setAssetsDir(const std::filesystem::path& assetsDir);
        std::filesystem::path getAssetsDir() const;

        void setLuaDir(const std::filesystem::path& luaDir);
        std::filesystem::path getLuaDir() const;

        // Roots added to the C++ build: each is an include directory, and the
        // sources under it compile without a script component referencing them.
        void setScriptDirs(std::vector<std::filesystem::path> scriptDirs);
        const std::vector<std::filesystem::path>& getScriptDirs() const;

        // Absolute roots stored references are relative to: assets for textures, models,
        // sounds and fonts ("asset://"), Lua for script entries ("lua://").
        std::filesystem::path getAssetsPath() const;
        std::filesystem::path getLuaPath() const;

        std::filesystem::path normalizeToProjectRelative(const std::filesystem::path& path) const;
        std::filesystem::path resolveAssetPath(const std::filesystem::path& assetRelative) const;
        std::filesystem::path normalizeToAssetsRelative(const std::filesystem::path& path) const;
        // False for files the assets root only reaches with "..", which the runtime cannot resolve.
        bool isInsideAssetsPath(const std::filesystem::path& path) const;
        std::filesystem::path resolveLuaPath(const std::filesystem::path& luaRelative) const;
        std::filesystem::path normalizeToLuaRelative(const std::filesystem::path& path) const;

        // Applies new roots: referenced files outside them are moved in keeping their
        // layout, then every stored reference is rewritten and saved.
        void changeAssetRoots(const std::filesystem::path& newAssetsDir, const std::filesystem::path& newLuaDir);

        void setCMakeKit(const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator = "");
        std::string getCMakeCCompiler() const;
        std::string getCMakeCxxCompiler() const;
        std::string getCMakeGenerator() const;
        void setCMakeBuildJobs(unsigned int jobs);
        unsigned int getCMakeBuildJobs() const;
        void setPackNativeResources(bool enabled);
        bool shouldPackNativeResources() const;

        uint32_t getStartSceneId() const;
        void setStartSceneId(uint32_t sceneId);

        TerrainEditorSettings& getTerrainEditorSettings();
        const TerrainEditorSettings& getTerrainEditorSettings() const;

        CommandHistory* getProjectCommandHistory();

        bool createTempProject(std::string projectName, bool deleteIfExists = false);
        bool saveProjectToPath(const std::filesystem::path& path);
        void clearTrash();
        void deleteSceneProject(SceneProject* sceneProject);
        void loadSceneProjectData(SceneProject* sceneProject, const YAML::Node& sceneNode);
        bool saveProject(bool userCalled = false, std::function<void()> callback = nullptr);
        bool saveProjectFile();
        bool openProject();

        bool loadProject(const std::filesystem::path path, bool updateLastOpened = true);

        void refreshLinkedMaterials(bool force = false);

        //=== Linked Material part ===

        void linkMaterialFile(uint32_t sceneId, Entity entity, unsigned int submeshIndex, const std::string& filePath);
        bool isMaterialFileLinked(uint32_t sceneId, Entity entity, unsigned int submeshIndex) const;
        std::string getMaterialFilePath(uint32_t sceneId, Entity entity, unsigned int submeshIndex) const;
        void unlinkMaterialFile(uint32_t sceneId, Entity entity, unsigned int submeshIndex);
        void unlinkAllMaterialFiles(uint32_t sceneId, Entity entity);

        //=== end Linked Material part ===

        //=== File path remapping ===

        void remapMaterialFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void remapSceneFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void remapEntityBundleFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void remapScriptFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void remapAssetFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void remapShaderFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        void cleanupMaterialFilePath(const std::filesystem::path& deletedPath);
        void cleanupSceneFilePath(const std::filesystem::path& deletedPath);
        void cleanupEntityBundleFilePath(const std::filesystem::path& deletedPath);
        void cleanupScriptFilePath(const std::filesystem::path& deletedPath);
        void cleanupAssetFilePath(const std::filesystem::path& deletedPath);
        void cleanupShaderFilePath(const std::filesystem::path& deletedPath);

        //=== end File path remapping ===

        void checkUnsavedAndExecute(uint32_t sceneId, std::function<void()> action);

        void saveScene(uint32_t sceneId, std::function<void(bool)> callback = nullptr);
        bool saveSceneToPath(uint32_t sceneId, const std::filesystem::path& path, bool stopTransientPreviews = true);
        void saveSceneToPathAsync(uint32_t sceneId, const std::filesystem::path& path, std::function<void(bool)> callback = nullptr);
        void saveAllScenes(std::function<void(bool)> callback = nullptr);
        void saveLastSelectedScene(std::function<void(bool)> callback = nullptr);

        uint32_t createNewScene(std::string sceneName, SceneType type);
        void loadScene(fs::path filepath, bool opened, bool isNewScene = true, bool loadSceneData = true);
        void openScene(fs::path filepath, bool closePrevious = true);
        void closeScene(uint32_t sceneId, bool systemClose = false);
        void removeScene(uint32_t sceneId);

        std::vector<std::filesystem::path> findSceneFiles() const;
        uint32_t findSceneByPath(const std::filesystem::path& filepath) const;

        bool loadChildSceneInline(uint32_t childSceneId);
        void unloadChildSceneInline(uint32_t childSceneId);

        void addChildScene(uint32_t sceneId, uint32_t childSceneId, bool startActive = true);
        void removeChildScene(uint32_t sceneId, uint32_t childSceneId);
        // Dirties parents of a child scene and forces the Engine layers to rebuild.
        void markParentScenesNeedUpdate(uint32_t childSceneId);
        bool hasChildScene(uint32_t sceneId, uint32_t childSceneId) const;
        bool isChildSceneStartActive(uint32_t sceneId, uint32_t childSceneId) const;
        void setChildSceneStartActive(uint32_t sceneId, uint32_t childSceneId, bool startActive);
        std::vector<uint32_t> getChildScenes(uint32_t sceneId) const;

        Entity findObjectByRay(uint32_t sceneId, float x, float y, uint32_t* outSceneId = nullptr);

        bool selectObjectByRay(uint32_t sceneId, float x, float y, bool shiftPressed);
        bool selectObjectsByRect(uint32_t sceneId, Vector2 start, Vector2 end);

        std::vector<SceneProject>& getScenes();
        const std::vector<SceneProject>& getScenes() const;

        // Flushes compiled forked-shader caches and flags meshes that use a custom
        // shader for reload, so editing a shader source file updates the viewport.
        void invalidateCustomShaders();

        SceneProject* getScene(uint32_t sceneId);
        const SceneProject* getScene(uint32_t sceneId) const;
        SceneProject* getSelectedScene();
        const SceneProject* getSelectedScene() const;

        std::vector<TabEntry>& getTabs();
        const std::vector<TabEntry>& getTabs() const;
        void addTab(TabType type, const std::string& filepath);
        void removeTab(TabType type, const std::string& filepath);
        bool hasTab(TabType type, const std::string& filepath) const;

        void setNextSceneId(uint32_t nextSceneId);
        uint32_t getNextSceneId() const;

        void setSelectedSceneId(uint32_t selectedScene);
        uint32_t getSelectedSceneId() const;

        void setSelectedSceneForProperties(uint32_t selectedScene);
        uint32_t getSelectedSceneForProperties() const;

        bool isTempProject() const;
        bool isTempUnsavedProject() const;
        std::filesystem::path getProjectPath() const;
        std::filesystem::path getProjectInternalPath() const;

        fs::path getTerrainMapsDir() const;
        fs::path getThumbsDir() const;
        fs::path getThumbnailPath(const fs::path& originalPath) const;

        std::vector<Entity> getEntities(uint32_t sceneId) const;

        void replaceSelectedEntities(uint32_t sceneId, std::vector<Entity> selectedEntities);
        void setSelectedEntity(uint32_t sceneId, Entity selectedEntity);
        void addSelectedEntity(uint32_t sceneId, Entity selectedEntity);
        bool isSelectedEntity(uint32_t sceneId, Entity selectedEntity);
        void clearSelectedEntities(uint32_t sceneId);
        void clearAllSelections(uint32_t sceneId);
        std::vector<Entity> getSelectedEntities(uint32_t sceneId) const;
        bool hasSelectedEntities(uint32_t sceneId) const;

        bool hasSelectedSceneUnsavedChanges() const;
        bool hasSelectedSceneUnsavedEntityBundles() const;
        bool hasLocalUnsavedChanges(uint32_t sceneId) const;
        bool hasSceneUnsavedChanges(uint32_t sceneId) const;
        bool hasUnsavedEntityBundles(uint32_t sceneId) const;
        bool hasScenesUnsavedChanges() const;
        bool hasUnsavedEntityBundles() const;

        void updateAllScriptsProperties(uint32_t sceneId);
        // Returns true when a script property definition changed.
        bool updateScriptProperties(SceneProject* sceneProject, Entity entity, std::vector<ScriptEntry>& scripts, const std::string& inMemoryContent = "", const std::string& inMemoryPath = "");

        static std::vector<Entity> getTopLevelEntities(const EntityRegistry* registry, const std::vector<Entity>& orderedEntities);
        // Remaps every entity reference across a set of entities. References to a mapped
        // member are translated. By default references outside the map are cleared so raw
        // IDs cannot leak across a bundle boundary; recovery paths pass clearUnmapped=false
        // to preserve external references while repairing replacement IDs.
        static void remapEntityProperties(EntityRegistry* registry, const std::vector<Entity>& entities, const std::unordered_map<Entity, Entity>& entityMap, bool clearUnmapped = true);
        // Same rule as remapEntityProperties, scoped to one component. When `properties`
        // is non-empty only those are remapped; a request for an aggregate property (e.g.
        // "scripts") also covers the indexed references expanded from it.
        static void remapEntityPropertiesInComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, const std::vector<std::string>& properties, const std::unordered_map<Entity, Entity>& entityMap);
        // Promotes to a per-instance override every member component holding an entity
        // reference that points outside the bundle (cross-scene, or a non-member local
        // entity). Such references cannot be shared, so overriding keeps and persists them.
        static void overrideExternalRefComponents(EntityRegistry* scene, EntityBundle::Instance& instance);

        //=== EntityBundle part ===

        bool createEntityBundle(uint32_t sceneId, fs::path filepath, YAML::Node entityNode);
        bool removeEntityBundle(const std::filesystem::path& filepath);

        const std::vector<std::filesystem::path>& getStandaloneBundles() const;
        void setStandaloneBundles(std::vector<std::filesystem::path> bundlePaths);
        bool isStandaloneBundle(const std::filesystem::path& filepath) const;
        std::vector<std::filesystem::path> findBundleFiles() const;

        void saveEntityBundleToDisk(const std::filesystem::path& filepath);

        EntityBundle* getEntityBundle(const std::filesystem::path& filepath);
        const EntityBundle* getEntityBundle(const std::filesystem::path& filepath) const;
        std::map<std::filesystem::path, const EntityBundle*> getEntityBundles(uint32_t sceneId) const;
        std::filesystem::path findEntityBundlePathFor(uint32_t sceneId, Entity entity) const;

        YAML::Node encodeEntityBundleNode(const std::filesystem::path& filepath) const;

        std::vector<Entity> importEntityBundle(SceneProject* sceneProject, std::vector<Entity>* entities, const std::filesystem::path& filepath, Entity rootEntity, bool needSaveScene = true, const YAML::Node& bundleOverrides = YAML::Node(), const YAML::Node& bundleLocalEntities = YAML::Node(), std::unordered_map<Entity, Entity>* entityRemap = nullptr);
        bool unimportEntityBundle(uint32_t sceneId, const std::filesystem::path& filepath, Entity rootEntity, const std::vector<Entity>& memberEntities);

        bool addEntityToBundle(uint32_t sceneId, Entity entity, Entity parent, bool createItself = true);
        bool addEntityToBundle(uint32_t sceneId, const NodeRecovery& recoveryData, Entity parent,
            bool createItself = true, std::unordered_map<Entity, Entity>* entityRemap = nullptr,
            std::vector<Entity>* restoredEntities = nullptr);
        NodeRecovery removeEntityFromBundle(uint32_t sceneId, Entity entity, bool destroyItself = true);

        bool bundlePropertyChanged(uint32_t sceneId, Entity entity, ComponentType componentType, std::vector<std::string> properties, bool changeItself = false);
        bool bundleNameChanged(uint32_t sceneId, Entity entity, std::string name, bool changeItself = false);
        bool isEntityInBundle(uint32_t sceneId, Entity entity) const;

        bool addComponentToBundle(uint32_t sceneId, Entity entity, ComponentType componentType, bool addToItself = true);
        bool addComponentToBundle(uint32_t sceneId, Entity entity, ComponentType componentType, const ComponentRecovery& recovery, bool addToItself = true);
        ComponentRecovery removeComponentFromBundle(uint32_t sceneId, Entity entity, ComponentType componentType, bool encodeComponent = true, bool removeToItself = true);

        SharedMoveRecovery moveEntityFromBundle(uint32_t sceneId, Entity entity, Entity target, InsertionType type, bool moveItself = true);
        bool undoMoveEntityInBundle(uint32_t sceneId, Entity entity, Entity target, const SharedMoveRecovery& recovery, bool moveItself = true);

        void cleanupEntityBundlesForScene(uint32_t sceneId);

        //=== end EntityBundle part ===

        YAML::Node clearEntitiesNode(YAML::Node node);
        YAML::Node changeEntitiesNode(Entity& firstEntity, YAML::Node node);

        bool isAnyScenePlaying() const;
        bool isPlaySessionActive() const;
        // True only while the active play session's main scene is actually running
        // (PLAYING) — false when paused, loading, or no session is active.
        bool isMainScenePlaying() const;
        bool isAnySceneSaving() const;

        void start(uint32_t sceneId);
        void pause(uint32_t sceneId);
        void resume(uint32_t sceneId);
        void stop(uint32_t sceneId);
        void stopActivePlay();
        void waitForPlaySessionToFinish();

        std::vector<Scene*> getRunningRuntimeLayers(uint32_t sceneId);

        void debugSceneHierarchy();
    };

}
