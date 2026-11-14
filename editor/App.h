// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "imgui.h"

#include "EditorHost.h"
#include "PlatformMenu.h"
#include "Project.h"

#include "window/Properties.h"
#include "window/Structure.h"
#include "window/OutputWindow.h"
#include "window/SceneWindow.h"
#include "window/ResourcesWindow.h"
#include "window/ImageViewerWindow.h"
#include "window/GraphEditorWindow.h"
#include "window/CodeEditor.h"
#include "window/AnimationWindow.h"
#include "window/TerrainEditWindow.h"
#include "window/AiChatWindow.h"

#include "window/LoadingWindow.h"

#include "window/dialog/ProjectSaveDialog.h"
#include "window/dialog/SceneSaveDialog.h"
#include "window/dialog/ExportWindow.h"
#include "window/dialog/EditorSettingsWindow.h"
#include "window/dialog/ProjectSettingsWindow.h"
#include "window/dialog/BundlesWindow.h"
#include "window/dialog/AboutWindow.h"
#include "window/dialog/ScenesWindow.h"

#include "render/SceneRender.h"
#include "util/GitMonitor.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

struct ImGuiDockNode;

namespace doriax::editor{

    namespace ai { class McpServer; }

    enum class AlertType {
        Info,
        Confirm,
        ThreeButton
    };

    struct AlertData{
        bool needShow = false;
        std::string title;
        std::string message;
        std::string note; // optional highlighted note rendered below the message
        AlertType type = AlertType::Info;
        std::function<void()> onYes = nullptr;
        std::function<void()> onNo = nullptr;
        std::function<void()> onCancel = nullptr;
    };

    enum class SaveDialogType {
        Scene,
        Project
    };

    struct SaveDialogQueueItem {
        SaveDialogType type;
        uint32_t sceneId;  // Only used for Scene dialogs
        std::function<void(bool)> callback = nullptr;
    };

    class App : public EditorHost{
    private:
        Project project;

        std::mutex mainThreadTaskMutex;
        std::queue<std::function<void()>> mainThreadTasks;
        std::thread::id mainThreadId;

        // Whether engineRender() drew a scene this frame, and a wake callback the
        // backend loop uses to decide when it can idle.
        bool renderedSceneThisFrame = false;
        std::function<void()> wakeCallback;

        bool redrawRequested = false;
        bool frameRequested = false;

        // Active during startup and project changes; scene editing waits until loading finishes.
        bool projectLoading = true;
        bool pumpingLoading = false;
        std::string loadingStatus = "Starting...";
        std::function<void(bool)> loadingPump;
        std::function<void()> pendingProjectChange;
        std::chrono::steady_clock::time_point lastLoadingFrame{};

        // Footer stats, sampled only from consecutive drawn frames (see engineRender).
        bool renderedScenePrevFrame = false;
        float footerFramerate = 0.0f;
        float footerDeltaMs = 0.0f;

        // Branch in the footer and change marks in the code editor
        GitMonitor gitMonitor;

        bool benchmarkEnabled = false;
        bool benchmarkExit = false;
        bool benchmarkFailed = false;
        bool benchmarkCameraReady = false;
        uint32_t benchmarkSceneId = 0;
        std::filesystem::path benchmarkProject;
        std::string benchmarkScene;
        std::string benchmarkName;
        std::filesystem::path benchmarkOut;
        int benchmarkWarmupFrames = 90;
        int benchmarkMeasureFrames = 180;
        // scene setting overrides: -1 keeps the scene's value
        int benchmarkMeshLod = -1;
        int benchmarkDepthPrepass = -1;
        int benchmarkPhase = 0;
        int benchmarkPhaseFrames = 0;
        std::vector<float> benchmarkFps;
        std::vector<uint32_t> benchmarkDraws;
        std::vector<uint32_t> benchmarkInstances;
        std::vector<uint64_t> benchmarkTris;
        std::chrono::steady_clock::time_point benchmarkWaitStart{};
        std::chrono::steady_clock::time_point benchmarkMeasureStart{};

        ImGuiID dockspace_id;
        ImGuiID dock_id_middle_top;

        static ImFont* codeFont;

        Structure* structureWindow;
        Properties* propertiesWindow;
        OutputWindow* outputWindow;
        SceneWindow* sceneWindow;
        CodeEditor* codeEditor;
        ResourcesWindow* resourcesWindow;
        ImageViewerWindow* imageViewerWindow;
        GraphEditorWindow* graphEditorWindow;
        AnimationWindow* animationWindow;
        TerrainEditWindow* terrainEditWindow;
        AiChatWindow* aiChatWindow;
        ai::McpServer* mcpServer;

        LoadingWindow* loadingWindow;

        bool isInitialized;
        bool dockspaceNeedsRebuild;

        // True only during an explicit "Reset Layout": forces tabs back to their
        // default dock slot even when the ini has a saved position for them.
        bool forceDockTabs = false;

        // captureTabOrder() mirrors the live tab order into project.tabs every
        // frame; these debounce persisting that to project.yaml until the user
        // stops dragging (a reorder triggers no save on its own otherwise).
        bool tabsOrderDirty = false;
        double tabsOrderChangeTime = 0.0;

        // Backing buffer for ImGui's io.IniFilename (must outlive ImGui).
        std::string layoutIniPath;

        // Scale the persisted dock layout was written at, and whether the
        // restored layout has been matched to the current one.
        float layoutUiScale = 0.0f;
        bool layoutScaleApplied = false;

        // Sizes scaleDockChildren() capped, so scaling back down restores them.
        struct CappedDockSize {
            float capped;
            float uncapped;
        };
        std::map<ImGuiID, CappedDockSize> cappedDockSizes;

        AlertData alert;
        ProjectSaveDialog projectSaveDialog;
        SceneSaveDialog sceneSaveDialog;
        ExportWindow exportWindow;
        EditorSettingsWindow editorSettingsWindow;
        ProjectSettingsWindow projectSettingsWindow;
        BundlesWindow bundlesWindow;
        AboutWindow aboutWindow;
        ScenesWindow scenesWindow;

        std::queue<SaveDialogQueueItem> saveDialogQueue;
        bool saveDialogInProgress = false;
        std::filesystem::path lastResourcesProjectPath;

        std::vector<std::string> droppedExternalPaths;
        bool isDroppedExternalPaths;

        uint32_t lastActivatedScene;
        uint32_t pendingResizeScene = NULL_PROJECT_SCENE;

        enum class LastFocusedWindow {
            None,
            AnySceneWindow, // Represents any of the scene-related windows (Scene, Properties, Structure)
            Resources,
            Code,
            Graph,
            AI
        } lastFocusedWindow;

        void saveFunc();
        void saveAllFunc(std::function<void(bool)> callback = nullptr);
        void saveAllAndProject(std::function<void()> onSuccess);
        void openProjectFunc();
        bool requestProjectChange(std::function<void()> change) override;
        bool canEditSelection(bool duplicate);
        void deleteSelection();
        void duplicateSelection();

        PlatformMenuModel buildMenuModel();
        void executeMenuCommand(const PlatformMenuCommand& command);
        void showImGuiMenuItems(const std::vector<PlatformMenuItem>& items);
        void showImGuiMenu(const PlatformMenuModel& menu);
        void showMenu();
        void showAlert();
        void showFooter();
        void showFooterGit();
        void showStyleEditor();
        void buildDockspace(bool resetLayout = false);
        void buildDefaultLayout();
        // Matches a layout restored from the ini to the current UI scale.
        void rescaleRestoredLayout();
        void scaleDockChildren(ImGuiDockNode* node, float ratio);
        void registerLayoutSettings();
        void applyUiScale();
        void dockProjectTabs();
        void dockTabWindow(const std::string& windowName, bool force = false);
        void captureTabOrder();
        std::string tabWindowName(const TabEntry& tab) const;
        ImGuiID getCentralDockId();
        void applyPanelVisibilitySettings();
        void persistPanelVisibilitySettings();
        void processNextSaveDialog();
        bool popSaveDialogQueueItem();

        void closeWindow();
        void parseBenchmarkArgs(int argc, char** argv);
        bool selectBenchmarkScene();
        void failBenchmark(const std::string& message);
        void tickBenchmark();
        void finishBenchmark();

    public:
        void processMainThreadTasks() override;
        bool isMainThread() const override;

    public:

        App();

        void setup();

        void show();

        void engineInit(int argc, char** argv);
        void engineViewLoaded();
        void loadStartupProject();
        void processProjectChange();
        void engineRender();
        void engineViewDestroyed();
        void engineShutdown();

        bool isBenchmarkMode() const { return benchmarkEnabled; }
        bool consumeBenchmarkExit();
        int getExitCode() const { return benchmarkFailed ? 1 : 0; }

        bool isProjectLoading() const { return projectLoading; }
        const std::string& getLoadingStatus() const { return loadingStatus; }
        void setLoadingPump(std::function<void(bool render)> pump);
        void reportLoadingProgress(const std::string& status = {}) override;

        void addNewSceneToDock(uint32_t sceneId) override;
        void addNewCodeWindowToDock(fs::path path, bool force = false);
        void addImageViewerWindowToDock(fs::path path, bool force = false);
        void addGraphEditorWindowToDock(fs::path path, bool force = false);
        void clearSceneWindowState(uint32_t sceneId) override;
        void prepareForProjectSwitch() override;

        void handleExternalDrop(const std::vector<std::string>& paths);
        void handleExternalDragEnter();
        void handleExternalDragLeave();

        void resetLastActivatedScene() override;
        bool shouldSyncEngineApi() const override;
        bool recordsRecentProjects() const override;
        bool locksProjects() const override;
        void updateResourcesPath() override;
        void requestDockspaceRebuild();
        void updateWindowTitle(const std::string& projectName) override;
        void stopTransientPreviews() override;
        void flushSceneMaterialWrites(uint32_t sceneId) override;
        void saveAllCodeEditors() override;
        void requestScenePlayFocus(uint32_t sceneId) override;

        void registerAlert(std::string title, std::string message) override;
        void registerAlert(std::string title, std::string message, std::string note); // note is rendered highlighted below the message
        // A file is only referenceable from inside the assets root
        void registerOutsideAssetsAlert(const std::string& path);
        void registerConfirmAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo = nullptr) override;
        void registerThreeButtonAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo = nullptr, std::function<void()> onCancel = nullptr) override;
        void registerSaveSceneDialog(uint32_t sceneId, std::function<void(bool)> callback = nullptr) override;
        void registerProjectSaveDialog(std::function<void(bool)> callback = nullptr) override;

        // Thread-safe: schedules a task to run on the main/GL thread during the next frame.
        void enqueueMainThreadTask(std::function<void()> task) override;

        bool didRenderScene() const { return renderedSceneThisFrame; }
        bool hasPendingMainThreadTasks();
        // Main thread only: keeps the loop drawing while a worker thread drives the UI.
        void requestRedraw();
        bool consumeRedrawRequest();
        // One frame, without waking the loop: for animation that runs while idle.
        void requestFrame();
        bool consumeFrameRequest();
        // The AI worker gets its own copy: capturing this static App would dangle.
        void setWakeCallback(std::function<void()> cb);

        // Stops background work outliving the main loop; run before backend teardown.
        void shutdownBackgroundWork();

        // Each frame, from the backend loop
        void setWindowFocused(bool focused);

        static std::filesystem::path getUserCacheBaseDir();
        static std::filesystem::path getUserShaderCacheDir();

        // Monospace font used by the code editor
        static ImFont* getCodeFont();

        // ImGui sizes fonts by their line box (ascent-descent) while CSS/VSCode use the em size.
        // JetBrains Mono's line box is (1020 - -300) / 1000upm = 1.32em; multiply a CSS-like
        // font size by this to get the equivalent ImGui font size.
        static constexpr float codeFontEmScale = 1.32f;

        // Tab notification helpers
        static void pushTabNotificationStyle();
        static void popTabNotificationStyle();

        Project* getProject();
        const Project* getProject() const;

        Properties* getPropertiesWindow() const override;
        CodeEditor* getCodeEditor() const override;
        ImageViewerWindow* getImageViewerWindow() const override;
        GraphEditorWindow* getGraphEditorWindow() const override;
        ResourcesWindow* getResourcesWindow() const;
        AnimationWindow* getAnimationWindow() const;
        Structure* getStructureWindow() const;
        TerrainEditWindow* getTerrainEditWindow() const;
        AiChatWindow* getAiChatWindow() const;
        ai::McpServer* getMcpServer() const;
        void openEditorSettings(EditorSettingsWindow::Tab tab, bool expandAiChat = false);

        // Window settings methods. Sizes are physical pixels, converted between
        // the saved scale and the uiScale the backend is opening the window on.
        int getInitialWindowWidth(float uiScale) const;
        int getInitialWindowHeight(float uiScale) const;
        bool getInitialWindowMaximized() const;
        void saveWindowSettings(int width, int height, bool maximized, float uiScale);
        void initializeSettings();
        void exit();
    };

}
