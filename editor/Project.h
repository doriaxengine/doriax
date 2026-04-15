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
#include <tuple>

namespace doriax::editor{

    enum class SceneType{
        SCENE_3D,
        SCENE_2D,
        SCENE_UI
    };

    enum class ScenePlayState {
        STOPPED,
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
        CODE_EDITOR
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
        bool isModified = false;
        bool isVisible = false;
        bool opened = true;
        bool expandedInline = false;
        SceneDisplaySettings displaySettings;
        ScenePlayState playState = ScenePlayState::STOPPED;
        YAML::Node playStateSnapshot;
        SceneMaxValues maxValues;
        std::set<ShaderKey> shaderKeys;
        std::vector<uint32_t> childScenes;
        std::vector<SceneScriptSource> cppScripts;
        std::vector<BundleSceneInfo> bundles;
        YAML::Node editorCameraState;
    };

    struct NodeRecoveryEntry {
        YAML::Node node;
        size_t transformIndex;
    };

    using NodeRecovery = std::map<std::string, NodeRecoveryEntry>;

    struct SharedMoveRecoveryEntry {
        Entity oldParent;
        size_t oldIndex;
        size_t hasTransform;
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
        std::filesystem::path assetsDir;
        std::filesystem::path luaDir;
        std::string cmakeCCompiler;
        std::string cmakeCxxCompiler;
        std::string cmakeGenerator;
        CommandHistory projectHistory;

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

        const std::string libName = "projectlib";

        template<typename T>
        T* findScene(uint32_t sceneId) const;

        Entity createNewEntity(uint32_t sceneId, std::string entityName);
        bool createNewComponent(uint32_t sceneId, Entity entity, ComponentType component);
        void calculateSceneMaxValues(const SceneProject* sceneProject, SceneMaxValues& maxValues) const;
        void collectSceneShaderKeys(const SceneProject* sceneProject, std::set<ShaderKey>& shaderKeys) const;
        void resetEngineConfigs(bool executeViewChanged);
        void resetConfigs();

        void updateSceneCppScripts(SceneProject* sceneProject);
        void updateSceneBundles(SceneProject* sceneProject);

        std::vector<SceneScriptSource> collectAllSceneCppScripts() const;
        std::vector<BundleSceneInfo> collectAllBundles() const;

        void pauseEngineScene(Scene* scene, bool pause) const;

        void copyEngineApiToProject();

        void registerSceneManager();
        void registerBundleManager();

        SceneProject* findSceneProjectByScene(Scene* scene);

        fs::path normalizeToProjectRelative(const fs::path& path) const;
        static bool matchesRelativePath(const fs::path& relativeBase, const fs::path& currentPath);
        static bool matchesRelativeString(const fs::path& relativeBase, const std::string& currentPath);
        static bool remapRelativePath(const fs::path& oldRelative, const fs::path& newRelative,
                          const fs::path& currentPath, fs::path& updatedPath);
        static bool remapRelativeString(const fs::path& oldRelative, const fs::path& newRelative,
                        const std::string& currentPath, std::string& updatedPath);
        static bool remapScriptEntryPaths(ScriptEntry& scriptEntry, const fs::path& oldRelative,
                          const fs::path& newRelative);
        bool remapScriptPathsInRegistry(EntityRegistry* registry, const fs::path& oldRelative,
                        const fs::path& newRelative);
        bool cleanupScriptPathsInRegistry(EntityRegistry* registry, const fs::path& deletedRelative);

        void finalizeStart(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene>& runtimeScenes);
        void finalizeStop(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene> runtimeScenes);

        void collectInvolvedScenes(uint32_t sceneId, std::vector<uint32_t>& involvedSceneIds);

        uint32_t createNewSceneInternal(std::string sceneName, SceneType type, uint32_t previousSceneId);
        void openSceneInternal(fs::path filepath, uint32_t sceneToClose);
        void markParentScenesNeedUpdate(uint32_t childSceneId);

    public:
        Project();

        std::string getName() const;
        void setName(std::string name);

        void setCanvasSize(unsigned int width, unsigned int height);
        unsigned int getCanvasWidth() const;
        unsigned int getCanvasHeight() const;

        void setScalingMode(Scaling scalingMode);
        Scaling getScalingMode() const;

        void setTextureStrategy(TextureStrategy textureStrategy);
        TextureStrategy getTextureStrategy() const;

        void setAssetsDir(const std::filesystem::path& assetsDir);
        std::filesystem::path getAssetsDir() const;

        void setLuaDir(const std::filesystem::path& luaDir);
        std::filesystem::path getLuaDir() const;

        void setCMakeKit(const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator = "");
        std::string getCMakeCCompiler() const;
        std::string getCMakeCxxCompiler() const;
        std::string getCMakeGenerator() const;

        CommandHistory* getProjectCommandHistory();

        bool createTempProject(std::string projectName, bool deleteIfExists = false);
        bool saveProjectToPath(const std::filesystem::path& path);
        void clearTrash();
        void deleteSceneProject(SceneProject* sceneProject);
        void loadSceneProjectData(SceneProject* sceneProject, const YAML::Node& sceneNode);
        bool saveProject(bool userCalled = false, std::function<void()> callback = nullptr);
        bool openProject();

        bool loadProject(const std::filesystem::path path);

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
        void cleanupMaterialFilePath(const std::filesystem::path& deletedPath);
        void cleanupSceneFilePath(const std::filesystem::path& deletedPath);
        void cleanupEntityBundleFilePath(const std::filesystem::path& deletedPath);
        void cleanupScriptFilePath(const std::filesystem::path& deletedPath);

        //=== end File path remapping ===

        void checkUnsavedAndExecute(uint32_t sceneId, std::function<void()> action);

        void saveScene(uint32_t sceneId);
        void saveSceneToPath(uint32_t sceneId, const std::filesystem::path& path);
        void saveAllScenes();
        void saveLastSelectedScene();

        uint32_t createNewScene(std::string sceneName, SceneType type);
        void loadScene(fs::path filepath, bool opened, bool isNewScene = true, bool loadSceneData = true);
        void openScene(fs::path filepath, bool closePrevious = true);
        void closeScene(uint32_t sceneId, bool systemClose = false);
        void removeScene(uint32_t sceneId);

        bool loadChildSceneInline(uint32_t childSceneId);
        void unloadChildSceneInline(uint32_t childSceneId);

        void addChildScene(uint32_t sceneId, uint32_t childSceneId);
        void removeChildScene(uint32_t sceneId, uint32_t childSceneId);
        bool hasChildScene(uint32_t sceneId, uint32_t childSceneId) const;
        std::vector<uint32_t> getChildScenes(uint32_t sceneId) const;

        Entity findObjectByRay(uint32_t sceneId, float x, float y, uint32_t* outSceneId = nullptr);

        bool selectObjectByRay(uint32_t sceneId, float x, float y, bool shiftPressed);
        bool selectObjectsByRect(uint32_t sceneId, Vector2 start, Vector2 end);

        std::vector<SceneProject>& getScenes();
        const std::vector<SceneProject>& getScenes() const;
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
        bool hasSceneUnsavedChanges(uint32_t sceneId) const;
        bool hasUnsavedEntityBundles(uint32_t sceneId) const;
        bool hasScenesUnsavedChanges() const;
        bool hasUnsavedEntityBundles() const;

        void updateAllScriptsProperties(uint32_t sceneId);
        void updateScriptProperties(SceneProject* sceneProject, Entity entity, std::vector<ScriptEntry>& scripts);

        static std::vector<Entity> getTopLevelEntities(const EntityRegistry* registry, const std::vector<Entity>& orderedEntities);
        static void remapEntityProperties(EntityRegistry* registry, const std::vector<Entity>& entities, const std::unordered_map<Entity, Entity>& entityMap);
        static void remapEntityPropertiesInComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, const std::vector<std::string>& properties, const std::unordered_map<Entity, Entity>& entityMap);

        //=== EntityBundle part ===

        bool createEntityBundle(uint32_t sceneId, fs::path filepath, YAML::Node entityNode);
        bool removeEntityBundle(const std::filesystem::path& filepath);

        void saveEntityBundleToDisk(const std::filesystem::path& filepath);

        EntityBundle* getEntityBundle(const std::filesystem::path& filepath);
        const EntityBundle* getEntityBundle(const std::filesystem::path& filepath) const;
        std::map<std::filesystem::path, const EntityBundle*> getEntityBundles(uint32_t sceneId) const;
        std::filesystem::path findEntityBundlePathFor(uint32_t sceneId, Entity entity) const;

        YAML::Node encodeEntityBundleNode(const std::filesystem::path& filepath) const;

        std::vector<Entity> importEntityBundle(SceneProject* sceneProject, std::vector<Entity>* entities, const std::filesystem::path& filepath, Entity rootEntity, bool needSaveScene = true, const YAML::Node& bundleOverrides = YAML::Node(), const YAML::Node& bundleLocalEntities = YAML::Node());
        bool unimportEntityBundle(uint32_t sceneId, const std::filesystem::path& filepath, Entity rootEntity, const std::vector<Entity>& memberEntities);

        bool addEntityToBundle(uint32_t sceneId, Entity entity, Entity parent, bool createItself = true);
        bool addEntityToBundle(uint32_t sceneId, const NodeRecovery& recoveryData, Entity parent, bool createItself = true);
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

        void start(uint32_t sceneId);
        void pause(uint32_t sceneId);
        void resume(uint32_t sceneId);
        void stop(uint32_t sceneId);
        void waitForPlaySessionToFinish();

        void restoreRuntimeLayers(uint32_t sceneId);

        void debugSceneHierarchy();
    };

}
