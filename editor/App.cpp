// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "App.h"

#include "imgui_internal.h"
#include "Platform.h"
#include "Backend.h"

#include "external/IconsFontAwesome6.h"
#include "command/CommandHandle.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "command/type/RemoveChildSceneCmd.h"

#include "util/CrashHandler.h"
#include "util/ProjectUtils.h"
#include "util/Util.h"

#include "EditorVersion.h"
#include "Out.h"
#include "AppSettings.h"
#include "Theme.h"
#include "resources/fonts/fa-solid-900_ttf.h"
#include "resources/fonts/jetbrains-mono-regular_ttf.h"
//#include "recources/fonts/roboto-v20-latin-regular_ttf.h"
#include "util/DefaultFont.h"
#include "util/DefaultFontArabic.h"

#include "shader/ShaderBuilder.h"
#include "subsystem/MeshSystem.h"

#include <filesystem>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

#if defined(_WIN32)
  #include <windows.h>
  #include <shlobj.h>
#endif

using namespace doriax;

namespace {

// A scale no monitor reports means the value is missing or corrupt.
float sanitizeUiScale(float scale) {
    return (scale >= 0.5f && scale <= 8.0f) ? scale : 1.0f;
}

// Saved sizes are physical pixels, so they only mean the same window again at
// the scale they were captured at. Settings from before the editor tracked DPI
// carry no scale and read as 1.0, which is what a DPI-unaware process reported.
int scaleSavedWindowSize(int saved, float uiScale) {
    const float savedScale = sanitizeUiScale(editor::AppSettings::getWindowUiScale());
    const float ratio = sanitizeUiScale(uiScale) / savedScale;
    return std::max(1, static_cast<int>(std::lround(saved * ratio)));
}

// The dockspace lives on the main viewport, so that is the scale it has to match.
float mainViewportScale() {
    return sanitizeUiScale(ImGui::GetMainViewport()->DpiScale);
}

// SizeRef against a central node is read as an absolute size, so it has to grow
// with the text; siblings split by ratio stay proportional under the same factor.
void scaleDockChildren(ImGuiDockNode* node, float ratio) {
    for (ImGuiDockNode* child : node->ChildNodes) {
        if (!child) continue;
        child->Size = ImVec2(child->Size.x * ratio, child->Size.y * ratio);
        child->SizeRef = ImVec2(child->SizeRef.x * ratio, child->SizeRef.y * ratio);
        scaleDockChildren(child, ratio);
    }
}

}

ImFont* editor::App::codeFont = nullptr;

editor::App::App(){
    mainThreadId = std::this_thread::get_id();
    propertiesWindow = new Properties(&project);
    outputWindow = new OutputWindow();
    sceneWindow = new SceneWindow(&project);
    propertiesWindow->setSceneWindow(sceneWindow);
    structureWindow = new Structure(&project, sceneWindow);
    codeEditor = new CodeEditor(&project);
    imageViewerWindow = new ImageViewerWindow(&project);
    resourcesWindow = new ResourcesWindow(&project, codeEditor, imageViewerWindow);
    loadingWindow = new LoadingWindow();
    animationWindow = new AnimationWindow(&project);
    terrainEditWindow = new TerrainEditWindow(&project);
    aiChatWindow = new AiChatWindow(&project, resourcesWindow);

    isInitialized = false;
    dockspaceNeedsRebuild = false;

    lastFocusedWindow = LastFocusedWindow::None;

    Out::setOutputWindow(outputWindow);

    isDroppedExternalPaths = false;

    resetLastActivatedScene();
}

void editor::App::saveFunc(){
    if (lastFocusedWindow == LastFocusedWindow::Code) {
        codeEditor->saveLastFocused();
    }else{
        project.saveLastSelectedScene();
    }
}

void editor::App::saveAllFunc(std::function<void(bool)> callback){
    codeEditor->saveAll();
    project.saveAllScenes(callback);
}

void editor::App::saveAllAndProject(std::function<void()> onSuccess){
    saveAllFunc([this, onSuccess](bool success) {
        if (!success) return;
        project.saveProject(true, onSuccess);
    });
}

void editor::App::openProjectFunc(){
    if (project.isAnyScenePlaying()) {
        registerAlert("Scene Running", "A scene is currently running or stopping. Stop it before opening another project.");
        return;
    }

    if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() || project.isTempUnsavedProject()) {
        Backend::getApp().registerConfirmAlert(
            "Unsaved Changes",
            "There are unsaved changes. Do you want to save them before opening another project?",
            [this]() {
                saveAllAndProject([this]() {
                    this->project.openProject();
                });
            },
            [this]() {
                // No callback - just continue without saving
                project.openProject();
            }
        );
    } else {
        // No unsaved changes, proceed directly
        project.openProject();
    }
}

namespace {

enum class AppMenuCommand : uint32_t {
    NewProject = 1,
    NewScene3D,
    NewScene2D,
    NewSceneUI,
    OpenProject,
    OpenRecentProject,
    ClearRecentProjects,
    SaveProject,
    SaveProjectAs,
    Save,
    SaveAll,
    ExportProject,
    Exit,
    Undo,
    Redo,
    ToggleStructure,
    ToggleProperties,
    ToggleResources,
    ToggleOutput,
    ToggleAnimation,
    ToggleTerrain,
    ToggleAiChat,
    ToggleDetachableWindows,
    ResetLayout,
    ProjectSettings,
    ProjectScenes,
    ProjectBundles,
    ClearTrash,
    ClearShaderCache,
    RunScene,
    PauseScene,
    ResumeScene,
    StopScene,
    RemoveScene,
    About
};

editor::PlatformMenuItem menuCommand(AppMenuCommand command,
                                     std::string label,
                                     bool enabled = true) {
    editor::PlatformMenuItem item;
    item.type = editor::PlatformMenuItemType::Command;
    item.label = std::move(label);
    item.command.id = static_cast<uint32_t>(command);
    item.enabled = enabled;
    return item;
}

editor::PlatformMenuItem menuShortcut(AppMenuCommand command,
                                      std::string label,
                                      std::string shortcut,
                                      bool enabled = true) {
    editor::PlatformMenuItem item = menuCommand(command, std::move(label), enabled);
    item.shortcut = std::move(shortcut);
    return item;
}

editor::PlatformMenuItem menuToggle(AppMenuCommand command,
                                    std::string label,
                                    bool checked) {
    editor::PlatformMenuItem item = menuCommand(command, std::move(label));
    item.checked = checked;
    return item;
}

editor::PlatformMenuItem menuPayloadCommand(AppMenuCommand command,
                                            std::string label,
                                            std::string payload) {
    editor::PlatformMenuItem item = menuCommand(command, std::move(label));
    item.command.payload = std::move(payload);
    return item;
}

editor::PlatformMenuItem menuSeparator() {
    editor::PlatformMenuItem item;
    item.type = editor::PlatformMenuItemType::Separator;
    return item;
}

editor::PlatformMenuItem menuSubmenu(std::string label,
                                     std::vector<editor::PlatformMenuItem> children,
                                     bool enabled = true) {
    editor::PlatformMenuItem item;
    item.type = editor::PlatformMenuItemType::Submenu;
    item.label = std::move(label);
    item.enabled = enabled;
    item.children = std::move(children);
    return item;
}

const char* imguiMenuLabel(const editor::PlatformMenuItem& item) {
    switch (static_cast<AppMenuCommand>(item.command.id)) {
        case AppMenuCommand::NewScene3D: return ICON_FA_CUBES "  3D Scene";
        case AppMenuCommand::NewScene2D: return ICON_FA_CUBES_STACKED "  2D Scene";
        case AppMenuCommand::NewSceneUI: return ICON_FA_WINDOW_RESTORE "  UI Scene";
        default: return item.label.c_str();
    }
}

} // namespace

editor::PlatformMenuModel editor::App::buildMenuModel(){
    SceneProject* selectedScene = project.getSelectedScene();
    uint32_t selectedSceneId = project.getSelectedSceneId();
    bool hasSelectedScene = selectedScene != nullptr;
    bool isProjectBusy = project.isAnyScenePlaying();
    bool isPlaying = hasSelectedScene && selectedScene->playState == ScenePlayState::PLAYING;
    bool isPaused = hasSelectedScene && selectedScene->playState == ScenePlayState::PAUSED;
    bool isLoading = hasSelectedScene && selectedScene->playState == ScenePlayState::LOADING;
    bool isSaving = hasSelectedScene && selectedScene->playState == ScenePlayState::SAVING;
    bool canRun = hasSelectedScene && !isProjectBusy;
    bool canPause = hasSelectedScene && isPlaying;
    bool canResume = hasSelectedScene && isPaused;
    bool canStop = hasSelectedScene && !isSaving && (isPlaying || isPaused || isLoading);
    bool canRemove = hasSelectedScene && !isProjectBusy && project.getScenes().size() > 1;

    bool canSave = lastFocusedWindow == LastFocusedWindow::Code
        ? codeEditor->hasLastFocusedUnsavedChanges()
        : !isProjectBusy && project.hasSelectedSceneUnsavedChanges();
    bool canSaveAll = !isProjectBusy &&
        (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges());

    bool canUndo = false;
    bool canRedo = false;
    if (lastFocusedWindow == LastFocusedWindow::Resources) {
        canUndo = project.getProjectCommandHistory()->canUndo();
        canRedo = project.getProjectCommandHistory()->canRedo();
    } else if (lastFocusedWindow == LastFocusedWindow::Code) {
        canUndo = codeEditor->canUndoLastFocused();
        canRedo = codeEditor->canRedoLastFocused();
    } else if (lastFocusedWindow != LastFocusedWindow::AI && hasSelectedScene) {
        canUndo = CommandHandle::get(selectedSceneId)->canUndo();
        canRedo = CommandHandle::get(selectedSceneId)->canRedo();
    }

    std::vector<PlatformMenuItem> recentItems;
    std::vector<std::filesystem::path> recentProjects = AppSettings::getRecentProjects();
    if (recentProjects.empty()) {
        recentItems.push_back(menuCommand(AppMenuCommand::OpenRecentProject,
                                          "No Recent Projects", false));
    } else {
        for (const auto& path : recentProjects) {
            recentItems.push_back(menuPayloadCommand(
                AppMenuCommand::OpenRecentProject,
                path.filename().string() + " (" + path.string() + ")",
                path.string()));
        }
        recentItems.push_back(menuSeparator());
        recentItems.push_back(menuCommand(AppMenuCommand::ClearRecentProjects,
                                          "Clear Recent Projects"));
    }

    PlatformMenuModel menu;
    menu.menus.push_back(menuSubmenu("File", {
        menuCommand(AppMenuCommand::NewProject, "New Project", !isProjectBusy),
        menuSubmenu("New Scene", {
            menuCommand(AppMenuCommand::NewScene3D, "3D Scene"),
            menuCommand(AppMenuCommand::NewScene2D, "2D Scene"),
            menuCommand(AppMenuCommand::NewSceneUI, "UI Scene")
        }, !isProjectBusy),
        menuSeparator(),
        menuShortcut(AppMenuCommand::OpenProject, "Open Project...", "Ctrl+O",
                     !isProjectBusy),
        menuSubmenu("Recent Projects", std::move(recentItems), !isProjectBusy),
        menuCommand(AppMenuCommand::SaveProject, "Save Project"),
        menuCommand(AppMenuCommand::SaveProjectAs, "Save Project As..."),
        menuSeparator(),
        menuCommand(AppMenuCommand::Save, "Save", canSave),
        menuCommand(AppMenuCommand::SaveAll, "Save All", canSaveAll),
        menuSeparator(),
        menuCommand(AppMenuCommand::ExportProject, "Export Project..."),
        menuSeparator(),
        menuCommand(AppMenuCommand::Exit, "Exit")
    }));

    menu.menus.push_back(menuSubmenu("Edit", {
        menuCommand(AppMenuCommand::Undo, "Undo", canUndo),
        menuCommand(AppMenuCommand::Redo, "Redo", canRedo)
    }));

    menu.menus.push_back(menuSubmenu("View", {
        menuToggle(AppMenuCommand::ToggleStructure, Structure::WINDOW_NAME,
                   structureWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleProperties, Properties::WINDOW_NAME,
                   propertiesWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleResources, ResourcesWindow::WINDOW_NAME,
                   resourcesWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleOutput, OutputWindow::WINDOW_NAME,
                   outputWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleAnimation, AnimationWindow::WINDOW_NAME,
                   animationWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleTerrain, TerrainEditWindow::WINDOW_NAME,
                   terrainEditWindow->isOpen()),
        menuToggle(AppMenuCommand::ToggleAiChat, AiChatWindow::WINDOW_NAME,
                   aiChatWindow->isOpen()),
        menuSeparator(),
        menuToggle(AppMenuCommand::ToggleDetachableWindows, "Detachable Windows",
                   AppSettings::getMultiViewportEnabled()),
        menuSeparator(),
        menuCommand(AppMenuCommand::ResetLayout, "Reset Layout")
    }));

    menu.menus.push_back(menuSubmenu("Project", {
        menuCommand(AppMenuCommand::ProjectSettings, "Project Settings..."),
        menuCommand(AppMenuCommand::ProjectScenes, "Scenes...", !isProjectBusy),
        menuCommand(AppMenuCommand::ProjectBundles, "Bundles..."),
        menuSeparator(),
        menuCommand(AppMenuCommand::ClearTrash, "Clear Trash"),
        menuCommand(AppMenuCommand::ClearShaderCache, "Clear Shader Cache")
    }));

    menu.menus.push_back(menuSubmenu("Scene", {
        menuShortcut(AppMenuCommand::RunScene, "Run", "F5", canRun),
        menuShortcut(AppMenuCommand::PauseScene, "Pause", "F6", canPause),
        menuShortcut(AppMenuCommand::ResumeScene, "Resume", "F5", canResume),
        menuShortcut(AppMenuCommand::StopScene, "Stop", "F7", canStop),
        menuSeparator(),
        menuCommand(AppMenuCommand::RemoveScene, "Remove", canRemove)
    }));

    menu.menus.push_back(menuSubmenu("Help", {
        menuCommand(AppMenuCommand::About, "About Doriax")
    }));
    return menu;
}

void editor::App::executeMenuCommand(const PlatformMenuCommand& command){
    const auto action = static_cast<AppMenuCommand>(command.id);
    switch (action) {
        case AppMenuCommand::NewProject: {
            if (project.isAnyScenePlaying()) return;
            std::string projectName = "MyDoriaxProject";
            auto startFreshProject = [this, projectName]() {
                if (project.createTempProject(projectName, true)) aiChatWindow->startNewChat();
            };
            if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() ||
                project.isTempUnsavedProject()) {
                registerConfirmAlert(
                    "Unsaved Changes",
                    "There are unsaved changes. Do you want to save them before creating a new project?",
                    [this, startFreshProject]() { saveAllAndProject(startFreshProject); },
                    startFreshProject);
            } else {
                startFreshProject();
            }
            break;
        }
        case AppMenuCommand::NewScene3D:
            if (!project.isAnyScenePlaying())
                project.createNewScene("New Scene", SceneType::SCENE_3D);
            break;
        case AppMenuCommand::NewScene2D:
            if (!project.isAnyScenePlaying())
                project.createNewScene("New Scene", SceneType::SCENE_2D);
            break;
        case AppMenuCommand::NewSceneUI:
            if (!project.isAnyScenePlaying())
                project.createNewScene("New Scene", SceneType::SCENE_UI);
            break;
        case AppMenuCommand::OpenProject:
            openProjectFunc();
            break;
        case AppMenuCommand::OpenRecentProject: {
            if (command.payload.empty() || project.isAnyScenePlaying()) return;
            const std::filesystem::path path(command.payload);
            if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() ||
                project.isTempUnsavedProject()) {
                registerConfirmAlert(
                    "Unsaved Changes",
                    "There are unsaved changes. Do you want to save them before opening another project?",
                    [this, path]() {
                        saveAllAndProject([this, path]() { project.loadProject(path); });
                    },
                    [this, path]() { project.loadProject(path); });
            } else {
                project.loadProject(path);
            }
            break;
        }
        case AppMenuCommand::ClearRecentProjects:
            AppSettings::clearRecentProjects();
            break;
        case AppMenuCommand::SaveProject:
            project.saveProject(true);
            break;
        case AppMenuCommand::SaveProjectAs:
            registerProjectSaveDialog([](){});
            break;
        case AppMenuCommand::Save:
            saveFunc();
            break;
        case AppMenuCommand::SaveAll:
            saveAllFunc();
            break;
        case AppMenuCommand::ExportProject:
            exportWindow.open(&project);
            break;
        case AppMenuCommand::Exit:
            exit();
            break;
        case AppMenuCommand::Undo:
            if (lastFocusedWindow == LastFocusedWindow::Resources) {
                project.getProjectCommandHistory()->undo();
                resourcesWindow->refreshCurrentDirectory();
            } else if (lastFocusedWindow == LastFocusedWindow::Code) {
                codeEditor->undoLastFocused();
            } else if (lastFocusedWindow != LastFocusedWindow::AI && project.getSelectedScene()) {
                CommandHandle::get(project.getSelectedSceneId())->undo();
            }
            break;
        case AppMenuCommand::Redo:
            if (lastFocusedWindow == LastFocusedWindow::Resources) {
                project.getProjectCommandHistory()->redo();
                resourcesWindow->refreshCurrentDirectory();
            } else if (lastFocusedWindow == LastFocusedWindow::Code) {
                codeEditor->redoLastFocused();
            } else if (lastFocusedWindow != LastFocusedWindow::AI && project.getSelectedScene()) {
                CommandHandle::get(project.getSelectedSceneId())->redo();
            }
            break;
        case AppMenuCommand::ToggleStructure:
            structureWindow->setOpen(!structureWindow->isOpen());
            break;
        case AppMenuCommand::ToggleProperties:
            propertiesWindow->setOpen(!propertiesWindow->isOpen());
            break;
        case AppMenuCommand::ToggleResources:
            resourcesWindow->setOpen(!resourcesWindow->isOpen());
            break;
        case AppMenuCommand::ToggleOutput:
            outputWindow->setOpen(!outputWindow->isOpen());
            break;
        case AppMenuCommand::ToggleAnimation:
            animationWindow->setOpen(!animationWindow->isOpen());
            break;
        case AppMenuCommand::ToggleTerrain:
            terrainEditWindow->setOpen(!terrainEditWindow->isOpen());
            break;
        case AppMenuCommand::ToggleAiChat:
            aiChatWindow->setOpen(!aiChatWindow->isOpen());
            break;
        case AppMenuCommand::ToggleDetachableWindows: {
            const bool multiViewport = !AppSettings::getMultiViewportEnabled();
            AppSettings::setMultiViewportEnabled(multiViewport);
            AppSettings::saveSettings();
            if (multiViewport && !Backend::isRunningOnWayland())
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            else
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
#ifdef __linux__
            if (multiViewport) {
                if (Backend::isRunningOnWayland())
                    registerAlert("Restart Required",
                        "Detachable windows will take effect after you restart the editor.",
                        "Note: This feature may encounter issues on Linux.");
                else
                    registerAlert("Experimental Feature",
                        "Detachable windows are now enabled.",
                        "Note: This feature may encounter issues on Linux.");
            }
#endif
            break;
        }
        case AppMenuCommand::ResetLayout:
            structureWindow->setOpen(true);
            propertiesWindow->setOpen(true);
            resourcesWindow->setOpen(true);
            outputWindow->setOpen(true);
            animationWindow->setOpen(true);
            terrainEditWindow->setOpen(false);
            aiChatWindow->setOpen(true);
            buildDockspace(true);
            break;
        case AppMenuCommand::ProjectSettings:
            projectSettingsWindow.open(&project);
            break;
        case AppMenuCommand::ProjectScenes:
            if (!project.isAnyScenePlaying())
                scenesWindow.open(&project);
            break;
        case AppMenuCommand::ProjectBundles:
            bundlesWindow.open(&project);
            break;
        case AppMenuCommand::ClearTrash:
            project.clearTrash();
            break;
        case AppMenuCommand::ClearShaderCache: {
            const std::filesystem::path cacheDir = getUserShaderCacheDir();
            if (std::filesystem::exists(cacheDir)) std::filesystem::remove_all(cacheDir);
            break;
        }
        case AppMenuCommand::RunScene:
            if (project.getSelectedScene() && !project.isAnyScenePlaying())
                project.start(project.getSelectedSceneId());
            break;
        case AppMenuCommand::PauseScene:
            if (SceneProject* scene = project.getSelectedScene();
                scene && scene->playState == ScenePlayState::PLAYING)
                project.pause(project.getSelectedSceneId());
            break;
        case AppMenuCommand::ResumeScene:
            if (SceneProject* scene = project.getSelectedScene();
                scene && scene->playState == ScenePlayState::PAUSED)
                project.resume(project.getSelectedSceneId());
            break;
        case AppMenuCommand::StopScene:
            if (SceneProject* scene = project.getSelectedScene(); scene &&
                scene->playState != ScenePlayState::SAVING &&
                (scene->playState == ScenePlayState::PLAYING ||
                 scene->playState == ScenePlayState::PAUSED ||
                 scene->playState == ScenePlayState::LOADING))
                project.stop(project.getSelectedSceneId());
            break;
        case AppMenuCommand::RemoveScene: {
            const uint32_t sceneId = project.getSelectedSceneId();
            if (project.getSelectedScene() && !project.isAnyScenePlaying() &&
                project.getScenes().size() > 1) {
                project.checkUnsavedAndExecute(sceneId, [this, sceneId]() {
                    project.removeScene(sceneId);
                });
            }
            break;
        }
        case AppMenuCommand::About:
            registerAlert("About Doriax",
                "Doriax Engine\n\nVersion: " DORIAX_EDITOR_VERSION "\n\nDeveloped by Eduardo Doria");
            break;
    }
}

void editor::App::showImGuiMenuItems(const std::vector<PlatformMenuItem>& items){
    for (const PlatformMenuItem& item : items) {
        if (item.type == PlatformMenuItemType::Separator) {
            ImGui::Separator();
        } else if (item.type == PlatformMenuItemType::Submenu) {
            if (ImGui::BeginMenu(item.label.c_str(), item.enabled)) {
                showImGuiMenuItems(item.children);
                ImGui::EndMenu();
            }
        } else if (ImGui::MenuItem(
                       imguiMenuLabel(item),
                       item.shortcut.empty() ? nullptr : item.shortcut.c_str(),
                       item.checked, item.enabled)) {
            executeMenuCommand(item.command);
        }
    }
}

void editor::App::showImGuiMenu(const PlatformMenuModel& menu){
    if (ImGui::BeginMainMenuBar()) {
        for (const PlatformMenuItem& topLevel : menu.menus) {
            if (ImGui::BeginMenu(topLevel.label.c_str(), topLevel.enabled)) {
                showImGuiMenuItems(topLevel.children);
                ImGui::EndMenu();
            }
        }
        ImGui::EndMainMenuBar();
    }
}

void editor::App::showMenu(){
    PlatformMenuModel menu = buildMenuModel();
    const float nativeMenuHeight = Backend::setMainMenu(
        menu, [this](const PlatformMenuCommand& command) {
            executeMenuCommand(command);
        });
    if (nativeMenuHeight < 0.0f)
        return;
    if (nativeMenuHeight > 0.0f) {
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav;
        ImGui::BeginViewportSideBar("##NativeMainMenuSpace", ImGui::GetMainViewport(),
                                    ImGuiDir_Up, nativeMenuHeight, flags);
        ImGui::End();
        return;
    }
    showImGuiMenu(menu);
}

void editor::App::showFooter(){
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    const float fontScale = 0.9f;
    const float footerHeight = (ImGui::GetTextLineHeight() * fontScale) + Theme::dpi(10.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoDocking |
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoNav |
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoFocusOnAppearing;

    // Position at the bottom of the main viewport
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - footerHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, footerHeight));
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(8.0f, 6.0f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.00f));

    if (ImGui::Begin("##Footer", nullptr, flags)) {
        ImGui::SetWindowFontScale(fontScale);

        const float fps = footerFramerate;
        const float deltaMs = footerDeltaMs;
        const size_t queuedResources = Engine::getQueuedResourceCount();
        SceneProject* selectedScene = project.getSelectedScene();
        const bool hasSelectedScene = selectedScene != nullptr;
        const bool isPlaying = hasSelectedScene && selectedScene->playState == ScenePlayState::PLAYING;
        const bool isPaused = hasSelectedScene && selectedScene->playState == ScenePlayState::PAUSED;
        const bool isSaving = hasSelectedScene && selectedScene->playState == ScenePlayState::SAVING;
        const bool isLoading = hasSelectedScene && selectedScene->playState == ScenePlayState::LOADING;
        const bool isStopped = !hasSelectedScene || selectedScene->playState == ScenePlayState::STOPPED;
        const bool isCancelling = hasSelectedScene && selectedScene->playState == ScenePlayState::CANCELLING;
        const bool isAnySaving = project.isAnySceneSaving();
        const bool canPlayPause = hasSelectedScene && !isSaving && !isLoading && !isCancelling && (isPlaying || isPaused || (isStopped && !project.isAnyScenePlaying()));
        const bool canStop = hasSelectedScene && !isSaving && !isStopped && !isCancelling;
        const ImVec4 footerButtonHovered = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
        const ImVec4 footerButtonActive = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);

        auto footerActionButton = [&](const char* label) -> bool {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, footerButtonHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, footerButtonActive);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Theme::dpi(ImVec2(6.0f, 1.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

            bool pressed = ImGui::Button(label);

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            return pressed;
        };

        // Left side: Status
        bool statusShown = false;
        if (isSaving || isAnySaving) {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), ICON_FA_FLOPPY_DISK " Saving scene");
            statusShown = true;
        } else if (isLoading) {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), ICON_FA_SPINNER " Loading scene");
            statusShown = true;
        } else if (isCancelling) {
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), ICON_FA_STOP " Stopping");
            statusShown = true;
        } else if (queuedResources > 0) {
            ImGui::Text(ICON_FA_SPINNER " Loading resources: %zu", queuedResources);
            statusShown = true;
        } else {
             if (selectedScene) {
                if (selectedScene->playState == ScenePlayState::PLAYING){
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), ICON_FA_PLAY " Playing");
                    statusShown = true;
                } else if (selectedScene->playState == ScenePlayState::PAUSED){
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), ICON_FA_PAUSE " Paused");
                    statusShown = true;
                }
             }
        }

        if (!statusShown) {
            ImGui::TextDisabled(ICON_FA_CHECK " Ready");
        }

        // Middle: Scene Info & Selection
        if (selectedScene) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("Scene: %s", selectedScene->name.c_str());

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            size_t selectionCount = selectedScene->selectedEntities.size();
            if (selectionCount == 0) {
                ImGui::TextDisabled("No Selection");
            } else if (selectionCount == 1) {
                ImGui::Text("Selected: 1 Entity");
            } else {
                ImGui::Text("Selected: %zu Entities", selectionCount);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            ImGui::BeginDisabled(!canPlayPause);
            if (footerActionButton(isPlaying ? ICON_FA_PAUSE " Pause" : (isPaused ? ICON_FA_PLAY " Resume" : ICON_FA_PLAY " Play"))) {
                if (isPlaying) {
                    project.pause(selectedScene->id);
                } else if (isPaused) {
                    project.resume(selectedScene->id);
                } else {
                    project.start(selectedScene->id);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!canStop);
            if (footerActionButton(ICON_FA_STOP " Stop")) {
                project.stop(selectedScene->id);
            }
            ImGui::EndDisabled();
        }

        // Right side: Performance stats
        char fpsText[32];
        char msText[32];
        sprintf(fpsText, ICON_FA_GAUGE_HIGH " %.1f FPS", fps);
        sprintf(msText, ICON_FA_CLOCK " %.2f ms", deltaMs);

        // Calculate width to position at right
        float statsWidth = ImGui::CalcTextSize(fpsText).x + ImGui::CalcTextSize(msText).x + 30.0f;
        float statsStartX = ImGui::GetWindowContentRegionMax().x - statsWidth;
        if (statsStartX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(statsStartX);
        } else {
            ImGui::SameLine();
        }

        ImGui::Text("%s", fpsText);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%s", msText);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    // Reserve space from the viewport work area so dockspace doesn't overlap
    viewport->WorkSize.y -= footerHeight;
}

void editor::App::showAlert(){
    if (alert.needShow) {
        ImGui::OpenPopup((alert.title + "##AlertModal").c_str());

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_Modal;

        if (ImGui::BeginPopupModal((alert.title + "##AlertModal").c_str(), nullptr, flags)) {
            ImGui::Text("%s", alert.message.c_str());
            if (!alert.note.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(Theme::Colors::WarningText, "%s", alert.note.c_str());
            }
            ImGui::Separator();

            if (alert.type == AlertType::Info) {
                // For info alerts, just show OK button
                ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 120) * 0.5f);
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                }
            } else if (alert.type == AlertType::Confirm) {
                // For confirmation alerts, show Yes and No buttons
                float windowWidth = ImGui::GetWindowSize().x;
                float buttonsWidth = 250; // Total width for both buttons and spacing
                ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

                if (ImGui::Button("Yes", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                    if (alert.onYes) {
                        alert.onYes();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                    if (alert.onNo) {
                        alert.onNo();
                    }
                }
            } else if (alert.type == AlertType::ThreeButton) {
                // For three-button alerts, show Yes, No and Cancel buttons
                float windowWidth = ImGui::GetWindowSize().x;
                float buttonsWidth = 370; // Width for three buttons
                ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

                if (ImGui::Button("Yes", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                    if (alert.onYes) {
                        alert.onYes();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                    if (alert.onNo) {
                        alert.onNo();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    alert.needShow = false;
                    ImGui::CloseCurrentPopup();
                    if (alert.onCancel) {
                        alert.onCancel();
                    }
                }
            }
            ImGui::EndPopup();
        }
    }
}

void editor::App::buildDockspace(bool resetLayout){
    // A reset forces every tab back to its default slot even if the ini has a
    // customized position saved for it (see dockTabWindow()).
    forceDockTabs = resetLayout;

    // Keep an existing layout (restored from the persisted ini) so the user's
    // customizations survive; only recover the scene node we dock tabs into.
    // A reset rebuilds the default skeleton from scratch instead.
    if (!resetLayout && ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        dock_id_middle_top = getCentralDockId();
    } else {
        buildDefaultLayout();
        // Built from the current font size, so it needs no DPI correction.
        layoutUiScale = mainViewportScale();
        layoutScaleApplied = true;
    }

    dockProjectTabs();

    ImGui::DockBuilderFinish(dockspace_id);

    forceDockTabs = false;
}

// The ini stores dock sizes in absolute pixels and ImGui never converts them, so
// a layout saved at 100% stays narrow at 150% on every launch.
void editor::App::rescaleRestoredLayout(){
    if (layoutScaleApplied) return;

    // Nothing persisted: buildDefaultLayout() builds at the current scale below.
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id);
    if (!root) return;

    const float currentScale = mainViewportScale();
    const float savedScale = sanitizeUiScale(layoutUiScale);
    layoutUiScale = currentScale;
    layoutScaleApplied = true;

    const float ratio = currentScale / savedScale;
    if (ratio > 0.99f && ratio < 1.01f) return;

    // The root node is pinned to the viewport, so only its descendants move.
    scaleDockChildren(root, ratio);
}

void editor::App::buildDefaultLayout(){
    // WorkSize is what DockSpaceOverViewport() sizes the root node to, so the
    // caps below measure against the room the panels actually get.
    const ImVec2 viewport = ImGui::GetMainViewport()->WorkSize;
    ImGuiID dock_id_left, dock_id_left_top, dock_id_left_bottom, dock_id_right, dock_id_middle, dock_id_middle_bottom;
    // dock_id_middle_top is a member: dockTabWindow() reads it after this runs.

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport);

    // Sizes in font sizes hold the same text at any DPI, but can add up to more
    // than the window has, leaving a sibling with no room at all.
    auto preferredSize = [](float fontSizes, float available, float maxShare){
        return std::min(fontSizes * ImGui::GetFontSize(), available * maxShare);
    };

    // Structure on the left, Resources split off its bottom.
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.0f, &dock_id_left, &dock_id_middle);
    ImGui::DockBuilderSetNodeSize(dock_id_left, ImVec2(preferredSize(14, viewport.x, 0.3f), viewport.y));
    ImGui::DockBuilderDockWindow(Structure::WINDOW_NAME, dock_id_left);

    ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.0f, &dock_id_left_bottom, &dock_id_left_top);
    ImGui::DockBuilderSetNodeSize(dock_id_left_bottom, ImVec2(viewport.x, preferredSize(50, viewport.y, 0.65f)));
    ImGui::DockBuilderDockWindow(ResourcesWindow::WINDOW_NAME, dock_id_left_bottom);

    // Properties on the right.
    ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Right, 0.0f, &dock_id_right, &dock_id_middle);
    ImGui::DockBuilderSetNodeSize(dock_id_right, ImVec2(preferredSize(19, viewport.x, 0.3f), viewport.y));
    ImGui::DockBuilderDockWindow(Properties::WINDOW_NAME, dock_id_right);
    ImGui::DockBuilderDockWindow(AiChatWindow::WINDOW_NAME, dock_id_right);

    auto stampPanelDockOrder = [](const char* windowName, short order) {
        ImGuiID windowId = ImHashStr(windowName);
        ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(windowId);
        if (!settings) settings = ImGui::CreateNewWindowSettings(windowName);
        settings->DockOrder = order;
        if (ImGuiWindow* window = ImGui::FindWindowByName(windowName)) {
            window->DockOrder = order;
        }
    };
    stampPanelDockOrder(Properties::WINDOW_NAME, 0);
    stampPanelDockOrder(AiChatWindow::WINDOW_NAME, 1);

    // Output/Animation across the bottom; scenes fill the remaining centre.
    ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Down, 0.0f, &dock_id_middle_bottom, &dock_id_middle_top);
    ImGui::DockBuilderSetNodeSize(dock_id_middle_bottom, ImVec2(viewport.x, preferredSize(10, viewport.y, 0.4f)));
    ImGui::DockBuilderDockWindow(OutputWindow::WINDOW_NAME, dock_id_middle_bottom);
    ImGui::DockBuilderDockWindow(AnimationWindow::WINDOW_NAME, dock_id_middle_bottom);

    // Mark the scene area as the central node. CentralNode is a saved flag, so it
    // persists in the ini and lets getCentralDockId() recover this node on the
    // next launch without rebuilding the layout.
    if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(dock_id_middle_top)) {
        centerNode->SetLocalFlags(centerNode->LocalFlags | ImGuiDockNodeFlags_CentralNode);
    }
}

void editor::App::dockProjectTabs(){
    // Re-open document tabs and dock scenes in their saved order. dockTabWindow()
    // leaves already-known windows where the ini put them (unless resetting).
    const std::vector<TabEntry>& tabs = project.getTabs();

    auto dockSceneTab = [&](const TabEntry& tab) {
        for (auto& sceneProject : project.getScenes()) {
            if (sceneProject.opened && sceneProject.filepath.string() == tab.filepath) {
                addNewSceneToDock(sceneProject.id);
                break;
            }
        }
    };

    auto dockCodeTab = [&](const TabEntry& tab) {
        fs::path fullPath = project.getProjectPath() / tab.filepath;
        if (fs::exists(fullPath)) {
            if (!codeEditor->isFileOpen(tab.filepath)) {
                codeEditor->openFile(tab.filepath);
            } else {
                dockTabWindow("###" + tab.filepath);
            }
        }
    };

    auto dockImageTab = [&](const TabEntry& tab) {
        fs::path fullPath = project.getProjectPath() / tab.filepath;
        if (fs::exists(fullPath)) {
            if (!imageViewerWindow->isFileOpen(tab.filepath)) {
                imageViewerWindow->openFile(tab.filepath, false);
            } else {
                dockTabWindow(ImageViewerWindow::getWindowId(tab.filepath));
            }
        }
    };

    auto dockOrphanScenes = [&]() {
        for (auto& sceneProject : project.getScenes()) {
            if (!sceneProject.opened) continue;
            if (!project.hasTab(TabType::SCENE, sceneProject.filepath.string())) {
                addNewSceneToDock(sceneProject.id);
            }
        }
    };

    if (forceDockTabs) {
        // Reset layout: scenes first, then code and image documents.
        for (const auto& tab : tabs) {
            if (tab.type == TabType::SCENE) dockSceneTab(tab);
        }
        dockOrphanScenes();
        for (const auto& tab : tabs) {
            if (tab.type == TabType::CODE_EDITOR) dockCodeTab(tab);
        }
        for (const auto& tab : tabs) {
            if (tab.type == TabType::IMAGE_VIEWER) dockImageTab(tab);
        }
    } else {
        for (const auto& tab : tabs) {
            if (tab.type == TabType::SCENE) dockSceneTab(tab);
            else if (tab.type == TabType::CODE_EDITOR) dockCodeTab(tab);
            else if (tab.type == TabType::IMAGE_VIEWER) dockImageTab(tab);
        }
        dockOrphanScenes();
    }

    // Make the saved tab list authoritative for tab ordering. Scene window ids
    // (###Scene<id>) are reassigned on every load, so the per-window DockOrder
    // ImGui keeps in its ini can't be matched back to a scene. Stamp a sequential
    // DockOrder onto each tab's window settings from project.tabs order so the
    // shared central node restores its tabs in the order the user left them
    // (captured live by captureTabOrder()). ImGui copies this onto the window's
    // DockOrder when it is created, and tabs appearing on the same frame are
    // sorted by it.
    short dockOrder = 0;
    auto stampDockOrder = [&](const std::string& windowName) {
        if (windowName.empty()) return;
        ImGuiID windowId = ImHashStr(windowName.c_str());
        ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(windowId);
        if (!settings) settings = ImGui::CreateNewWindowSettings(windowName.c_str());
        settings->DockOrder = dockOrder;
        if (ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str())) {
            window->DockOrder = dockOrder;
        }
        dockOrder++;
    };

    if (forceDockTabs) {
        for (const auto& tab : tabs) {
            if (tab.type == TabType::SCENE) stampDockOrder(tabWindowName(tab));
        }
        for (auto& sceneProject : project.getScenes()) {
            if (!sceneProject.opened) continue;
            if (!project.hasTab(TabType::SCENE, sceneProject.filepath.string())) {
                stampDockOrder("###Scene" + std::to_string(sceneProject.id));
            }
        }
        for (const auto& tab : tabs) {
            if (tab.type == TabType::CODE_EDITOR) stampDockOrder(tabWindowName(tab));
        }
        for (const auto& tab : tabs) {
            if (tab.type == TabType::IMAGE_VIEWER) stampDockOrder(tabWindowName(tab));
        }
    } else {
        for (const auto& tab : tabs) {
            stampDockOrder(tabWindowName(tab));
        }
    }
}

std::string editor::App::tabWindowName(const TabEntry& tab) const {
    if (tab.type == TabType::SCENE) {
        for (const auto& sceneProject : project.getScenes()) {
            if (sceneProject.opened && sceneProject.filepath.string() == tab.filepath) {
                return "###Scene" + std::to_string(sceneProject.id);
            }
        }
        return {};
    }
    if (tab.type == TabType::IMAGE_VIEWER) {
        return ImageViewerWindow::getWindowId(tab.filepath);
    }
    // CodeEditor docks its windows as "###<relative-filepath>" (see getWindowTitle()).
    return "###" + tab.filepath;
}

void editor::App::captureTabOrder() {
    // Mirror the live ImGui tab order back into project.tabs so a user's
    // drag-reordering of scene/code/image tabs survives a save and the next launch.
    // ImGui keeps each window's visual position in its DockNode as DockOrder;
    // we reorder the (filepath-keyed, stable) tab list to match. Reordering a
    // tab triggers no save on its own, so we persist the change ourselves,
    // debounced until the user stops dragging.
    std::vector<TabEntry>& tabs = project.getTabs();
    if (tabs.size() >= 2) {
        struct OrderedTab { TabEntry entry; int order; };
        std::vector<OrderedTab> ordered;
        ordered.reserve(tabs.size());
        for (const TabEntry& tab : tabs) {
            ImGuiWindow* window = ImGui::FindWindowByName(tabWindowName(tab).c_str());
            // Windows that aren't currently docked (DockOrder < 0) or not yet
            // instantiated keep their relative position via the stable sort.
            int order = (window && window->DockOrder >= 0) ? window->DockOrder
                                                           : std::numeric_limits<int>::max();
            ordered.push_back({tab, order});
        }

        std::stable_sort(ordered.begin(), ordered.end(),
            [](const OrderedTab& a, const OrderedTab& b){ return a.order < b.order; });

        bool changed = false;
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (tabs[i].type != ordered[i].entry.type || tabs[i].filepath != ordered[i].entry.filepath) {
                tabs[i] = ordered[i].entry;
                changed = true;
            }
        }

        if (changed) {
            tabsOrderDirty = true;
            tabsOrderChangeTime = ImGui::GetTime();
        }
    }

    // Persist a short moment after the last reorder so a drag results in one
    // write rather than one per frame while the tab slides past its neighbours.
    if (tabsOrderDirty && ImGui::GetTime() - tabsOrderChangeTime > 0.75) {
        project.saveProjectFile();
        tabsOrderDirty = false;
    }
}

void editor::App::dockTabWindow(const std::string& windowName, bool force){
    // Auto-dock only a window the layout ini has never seen, so we never override
    // a position the user chose (docked or intentionally floating). A layout reset
    // (forceDockTabs) and explicit user opens (force) snap the tab to the centre.
    if (force || forceDockTabs || !ImGui::FindWindowSettingsByID(ImHashStr(windowName.c_str()))) {
        dock_id_middle_top = getCentralDockId();
        ImGui::DockBuilderDockWindow(windowName.c_str(), dock_id_middle_top);
    }
}

ImGuiID editor::App::getCentralDockId(){
    // The scene area carries ImGuiDockNodeFlags_CentralNode (a saved flag, so it
    // survives in the ini). We scan for it rather than calling
    // DockBuilderGetCentralNode(), which reads root->CentralNode — only populated
    // once DockSpace() has processed the dockspace, not yet true on the first
    // frame after the layout is restored from the ini.
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    for (const ImGuiStoragePair& pair : ctx->DockContext.Nodes.Data) {
        ImGuiDockNode* node = static_cast<ImGuiDockNode*>(pair.val_p);
        if (node && node->IsCentralNode() && ImGui::DockNodeGetRootNode(node)->ID == dockspace_id) {
            return node->ID;
        }
    }

    // No central node yet (e.g. a pre-existing ini without one): fall back to the
    // dockspace root so windows still attach somewhere sensible.
    return dockspace_id;
}

void editor::App::showStyleEditor(){
    ImGui::Begin("Dear ImGui Style Editor", nullptr);
    {
        // Get the current IO object to access display size
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize = ImGui::GetWindowSize();

        // Calculate the centered position at the top
        float windowX = (io.DisplaySize.x - windowSize.x - 200) / 2;
        float windowY = 0.0f; // Top of the screen

        // Set the window position
        ImGui::SetWindowPos(ImVec2(windowX, windowY), ImGuiCond_Once);
        ImGui::SetWindowCollapsed(true, ImGuiCond_Once);

        ImGui::ShowStyleEditor();
    }
    ImGui::End();
}

ImFont* editor::App::getCodeFont() {
    return codeFont ? codeFont : ImGui::GetIO().Fonts->Fonts[0];
}

void editor::App::setup() {
    mainThreadId = std::this_thread::get_id();

    // Initialize application settings
    initializeSettings();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking
    // Activate only where it works: on Wayland the backend can't reposition windows,
    // so the feature stays off this session until a restart forces the X11 backend.
    if (AppSettings::getMultiViewportEnabled() && !Backend::isRunningOnWayland())
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Multi-Viewport / Platform Windows (toggle in View menu)

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Persist the docking layout next to settings.yaml at a stable absolute
    // path, so it survives restarts regardless of the working directory or
    // build type. (initializeSettings() above has already set the config dir.)
    layoutIniPath = (AppSettings::getConfigDirectory() / "editor_layout.ini").string();
    io.IniFilename = layoutIniPath.c_str();

    // Separate from the window scale: the layout keeps the scale it was built at,
    // while the window is measured on whichever monitor it closes on.
    layoutUiScale = AppSettings::getLayoutUiScale();

    io.Fonts->AddFontDefault();

    ImFontConfig config;
    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false;
    // Merge size 0.0f: inherit the implicit reference size of AddFontDefault().
    // Since 1.92, merging with an explicit size into an implicit-ref-size font asserts,
    // and specifying GlyphMinAdvanceX (monospacing) requires a non-zero reference size,
    // so it is omitted here. The visible icon font is font1 below, which keeps monospacing.
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, fa_solid_900_ttf_len, 0.0f, &config, icon_ranges);

    ImFontConfig config1;
    strcpy(config1.Name, "roboto-v20-latin-regular (16 px)");
    config1.FontDataOwnedByAtlas = false;
    config1.OversampleH = 2;
    config1.OversampleV = 2;
    config1.RasterizerMultiply = 1.5f;
    ImFont* font1 = io.Fonts->AddFontFromMemoryTTF(roboto_v20_latin_regular_ttf, roboto_v20_latin_regular_ttf_len, 16.0f, &config1);

    ImFontConfig config2;
    config2.MergeMode = true;
    config2.GlyphMinAdvanceX = 16.0f; // Use if you want to make the icon monospaced
    config2.FontDataOwnedByAtlas = false;
    static const ImWchar icon_ranges2[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, fa_solid_900_ttf_len, 16.0f, &config2, icon_ranges2);

    // Same Arabic subset the engine renders text with, so property fields and the scene
    // tree show Arabic instead of missing-glyph boxes. Since 1.92 the atlas loads glyphs
    // on demand, it only needs a font that carries them.
    ImFontConfig configArabic;
    configArabic.MergeMode = true;
    configArabic.FontDataOwnedByAtlas = false;
    //same rasterizer settings as the font it merges into, or Arabic comes out thinner
    //and fainter than the Latin beside it
    configArabic.OversampleH = 2;
    configArabic.OversampleV = 2;
    configArabic.RasterizerMultiply = 1.5f;
    io.Fonts->AddFontFromMemoryTTF(noto_sans_arabic_ttf, noto_sans_arabic_ttf_len, 16.0f, &configArabic);

    ImFontConfig config3;
    strcpy(config3.Name, "jetbrains-mono-regular");
    config3.FontDataOwnedByAtlas = false;
    config3.OversampleH = 2;
    config3.OversampleV = 2;
    codeFont = io.Fonts->AddFontFromMemoryTTF(jetbrains_mono_regular_ttf, jetbrains_mono_regular_ttf_len, 16.0f, &config3);

    //scripts can hold Arabic string literals
    ImFontConfig configCodeArabic;
    configCodeArabic.MergeMode = true;
    configCodeArabic.FontDataOwnedByAtlas = false;
    configCodeArabic.OversampleH = 2;
    configCodeArabic.OversampleV = 2;
    io.Fonts->AddFontFromMemoryTTF(noto_sans_arabic_ttf, noto_sans_arabic_ttf_len, 16.0f, &configCodeArabic);

    io.FontDefault = font1;

    io.ConfigDragClickToInputText = true;

    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    //ImGui::StyleColorsDark();
    Theme::apply();

    // Fonts follow each viewport's monitor DPI. Style paddings are global, so
    // App::show() re-scales them from the main viewport when that DPI changes.
    //
    // MacOS/Apple Silicon: disable Dear ImGui's automatic DPI scaling to fix Retina scaling issues.
    #ifdef __APPLE__
        io.ConfigDpiScaleFonts = false;
        io.ConfigDpiScaleViewports = false;
    #else
        io.ConfigDpiScaleFonts = true;
        io.ConfigDpiScaleViewports = true;
    #endif

    float dpiScale = 1.0f;
    if (ImGui::GetPlatformIO().Monitors.Size > 0) {
        dpiScale = ImGui::GetPlatformIO().Monitors[0].DpiScale;
    }
    Theme::applyDpiScale(dpiScale);
}

void editor::App::show(){
    float dpiScale = 1.0f;
    if (const ImGuiViewport* mainViewport = ImGui::GetMainViewport()) {
        dpiScale = mainViewport->DpiScale;
    }
    Theme::applyDpiScale(dpiScale);

    if (resourcesWindow->isFocused()) {
        lastFocusedWindow = LastFocusedWindow::Resources;
    } else if (codeEditor->isFocused()) {
        lastFocusedWindow = LastFocusedWindow::Code;
    } else if (aiChatWindow->isFocused()) {
        lastFocusedWindow = LastFocusedWindow::AI;
    }else{
        lastFocusedWindow = LastFocusedWindow::AnySceneWindow;
    }

    ImGuiIO& io = ImGui::GetIO();
    bool isUndo = (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift);
    bool isRedo = (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift);

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (ImGui::GetIO().KeyShift) {
            // CTRL+SHIFT+S saves all files
            saveAllFunc();
        } else {
            saveFunc();
        }
    }

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        // CTRL+O opens a project
        openProjectFunc();
    }

    // Play/Pause/Stop shortcuts
    {
        SceneProject* selectedScene = project.getSelectedScene();
        uint32_t selectedSceneId = project.getSelectedSceneId();
        bool hasSelectedScene = selectedScene != nullptr;
        bool isPlaying = hasSelectedScene && selectedScene->playState == ScenePlayState::PLAYING;
        bool isPaused = hasSelectedScene && selectedScene->playState == ScenePlayState::PAUSED;
        bool isLoading = hasSelectedScene && selectedScene->playState == ScenePlayState::LOADING;

        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            if (hasSelectedScene && !project.isAnyScenePlaying()) {
                project.start(selectedSceneId);
            } else if (isPaused) {
                project.resume(selectedSceneId);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
            if (isPlaying) {
                project.pause(selectedSceneId);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F7)) {
            if (isPlaying || isPaused || isLoading) {
                project.stop(selectedSceneId);
            }
        }
    }

    if (isDroppedExternalPaths) {
        isDroppedExternalPaths = false;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            std::vector<char> payload = Util::encodeStringsPayload(droppedExternalPaths);
            ImGui::SetDragDropPayload("external_files", payload.data(), payload.size());
            ImGui::EndDragDropSource();
        }
    }

    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()){
        // space to keys events
    }

    if (!resourcesWindow->isFocused() && !codeEditor->isFocused() && !aiChatWindow->isFocused()){
        uint32_t sceneId = project.getSelectedSceneId();

        // Update the Undo and Redo button logic:
        if (isUndo) {
            CommandHandle::get(sceneId)->undo();
        }
        if (isRedo) {
            CommandHandle::get(sceneId)->redo();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete)){
            uint32_t selectedSceneForProperties = project.getSelectedSceneForProperties();
            uint32_t targetSceneId = sceneId;
            if (selectedSceneForProperties != NULL_PROJECT_SCENE &&
                (selectedSceneForProperties == sceneId || project.hasChildScene(sceneId, selectedSceneForProperties))) {
                targetSceneId = selectedSceneForProperties;
            }

            // Check if a tile is selected — delete it instead of the entity
            SceneProject* sp = project.getScene(targetSceneId);
            bool tileDeleted = false;
            bool instanceDeleted = false;
            if (sp && sp->sceneRender) {
                int tileIdx = sp->sceneRender->getSelectedTileIndex();
                Entity tileEntity = sp->sceneRender->getSelectedTileEntity();
                if (tileIdx >= 0) {
                    Command* deleteCmd = ProjectUtils::buildDeleteTileCmd(&project, targetSceneId, tileEntity, (unsigned int)tileIdx);
                    if (deleteCmd) {
                        CommandHandle::get(sceneId)->addCommand(deleteCmd);
                        sp->sceneRender->clearTileSelection();
                        tileDeleted = true;
                    }
                }

                int instIdx = sp->sceneRender->getSelectedInstanceIndex();
                Entity instEntity = sp->sceneRender->getSelectedInstanceEntity();
                if (!tileDeleted && instIdx >= 0) {
                    Command* deleteCmd = ProjectUtils::buildDeleteInstanceCmd(&project, targetSceneId, instEntity, (unsigned int)instIdx);
                    if (deleteCmd) {
                        CommandHandle::get(sceneId)->addCommand(deleteCmd);
                        sp->sceneRender->clearInstanceSelection();
                        instanceDeleted = true;
                    }
                }
            }

            if (!tileDeleted && !instanceDeleted) {
            const std::vector<Entity>& selectedEntities = project.getSelectedEntities(targetSceneId);

            Command* lastCmd = nullptr;
            if (!selectedEntities.empty()) {
                for (const Entity& entity : selectedEntities){
                    lastCmd = new DeleteEntityCmd(&project, targetSceneId, entity);
                    CommandHandle::get(sceneId)->addCommand(lastCmd);
                }
            } else {
                if (selectedSceneForProperties != NULL_PROJECT_SCENE &&
                    selectedSceneForProperties != sceneId &&
                    project.hasChildScene(sceneId, selectedSceneForProperties)) {
                        lastCmd = new RemoveChildSceneCmd(&project, sceneId, selectedSceneForProperties);
                        CommandHandle::get(sceneId)->addCommand(lastCmd);
                }
            }
            if (lastCmd) {
                lastCmd->setNoMerge();
            }
            }
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)){
            uint32_t targetSceneId = sceneId;
            SceneProject* sp = project.getScene(targetSceneId);
            bool tileDuplicated = false;
            if (sp && sp->sceneRender) {
                int tileIdx = sp->sceneRender->getSelectedTileIndex();
                Entity tileEntity = sp->sceneRender->getSelectedTileEntity();
                if (tileIdx >= 0) {
                    Command* dupCmd = ProjectUtils::buildDuplicateTileCmd(&project, targetSceneId, tileEntity, (unsigned int)tileIdx);
                    if (dupCmd) {
                        CommandHandle::get(sceneId)->addCommand(dupCmd);
                        TilemapComponent* tilemap = sp->scene->findComponent<TilemapComponent>(tileEntity);
                        if (tilemap) {
                            sp->sceneRender->selectTile(tileEntity, (int)tilemap->numTiles - 1);
                        }
                        tileDuplicated = true;
                    }
                }
            }
            if (!tileDuplicated) {
                const std::vector<Entity>& selectedEntities = project.getSelectedEntities(targetSceneId);
                if (!selectedEntities.empty()){
                    CommandHandle::get(sceneId)->addCommandNoMerge(new DuplicateEntityCmd(&project, targetSceneId, selectedEntities));
                }
            }
        }
    }

    if (resourcesWindow->isFocused()) {
        if (isUndo) {
            project.getProjectCommandHistory()->undo();
            resourcesWindow->refreshCurrentDirectory();
        }
        if (isRedo) {
            project.getProjectCommandHistory()->redo();
            resourcesWindow->refreshCurrentDirectory();
        }
    }

    dockspace_id = ImGui::GetID("MyDockspace");

    showMenu();
    showFooter();

    isInitialized = true;

    // Before DockSpaceOverViewport() below lays the restored sizes out.
    rescaleRestoredLayout();

    if (dockspaceNeedsRebuild || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        buildDockspace();
        dockspaceNeedsRebuild = false;
    }

    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    #ifdef SHOW_STYLE_WINDOW
    showStyleEditor();
    #endif

    showAlert();

    sceneSaveDialog.show();
    projectSaveDialog.show();
    exportWindow.show();
    projectSettingsWindow.show();
    bundlesWindow.show();
    scenesWindow.show();

    if (!saveDialogInProgress && !sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen() && !saveDialogQueue.empty()) {
        processNextSaveDialog();
    }

    structureWindow->show();
    resourcesWindow->show();
    imageViewerWindow->show();
    outputWindow->show();
    animationWindow->show();
    terrainEditWindow->show();
    propertiesWindow->show();
    aiChatWindow->show();
    codeEditor->show();
    sceneWindow->show();

    loadingWindow->show();

    // Keep the persisted tab list in sync with the live tab order so a user's
    // drag-reordering of scene/code tabs is saved (and restored on next launch).
    captureTabOrder();

    // Closing a panel through its title-bar button changes the same state as the
    // View menu, so mirror the live state after all panels have rendered.
    persistPanelVisibilitySettings();
}

void editor::App::engineInit(int argc, char** argv) {
    Engine::systemInit(argc, argv, new editor::Platform(&project));

    // Check if there's a last opened project
    std::filesystem::path lastProjectPath = AppSettings::getLastProjectPath();

    if (!lastProjectPath.empty() && std::filesystem::exists(lastProjectPath)) {
        // Try to load the last project
        if (project.loadProject(lastProjectPath)) {
            Out::info("Loaded last opened project: \"%s\"", lastProjectPath.string().c_str());
        } else {
            // If loading fails, create a new temp project
            project.createTempProject("MyDoriaxProject");
        }
    } else {
        // No last project, create a new temp project
        project.createTempProject("MyDoriaxProject");
    }

    Engine::pauseGameEvents(true);

    // Any edit/undo/redo flags its scene for redraw, so the loop can idle otherwise.
    CommandHistory::onSceneModified = [this](size_t sceneId, bool structural){
        if (SceneProject* sceneProject = project.getScene((uint32_t)sceneId)){
            sceneProject->needUpdateRender = true;
            // Only structural commands, so a gizmo drag doesn't rebuild the tree each frame.
            if (structural){
                sceneProject->structureVersion++;
            }
        }
    };

    ShaderPool::setShaderBuilder([this](doriax::ShaderKey shaderKey) -> doriax::ShaderBuildResult {
        static doriax::editor::ShaderBuilder builder;  // Make static to reuse
        return builder.buildShader(shaderKey, &project);
    });

    Engine::setAsyncLoading(true);
}

void editor::App::engineViewLoaded(){
    Engine::systemViewLoaded();
}

void editor::App::engineRender(){
    processMainThreadTasks();
    project.refreshLinkedMaterials();
    renderedSceneThisFrame = false;
    const uint32_t selectedSceneId = project.getSelectedSceneId();

    // Redraw the selected scene only when it changed (needUpdateRender) or while a
    // continuous state (play/load/preview/thumbnail) is active, so the loop can idle.
    const bool thumbnailsPending = resourcesWindow && resourcesWindow->hasPendingThumbnailWork();

    // Drop targets preview a dragged resource directly, with no command to flag the scene.
    const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
    const bool draggingResourceFile = dragPayload && dragPayload->IsDataType("resource_files");

    // Keep drawing a few frames after the last change so late async uploads and
    // shader builds still get presented.
    const int RENDER_SETTLE_FRAMES = 30;

    auto sceneNeedsRender = [&](SceneProject& sp, bool isSelected) -> bool {
        bool active = sp.needUpdateRender;
        bool gaugeAnimating = false;
        // A running play session must advance and redraw its simulation every frame.
        if (sp.playState == ScenePlayState::PLAYING) active = true;
        // A drag preview redraws until the settle frames cover its restore.
        if (draggingResourceFile) active = true;
        if (isSelected && sp.scene && sp.sceneRender) {
            // Still loading a model, previewing a camera, or capturing a thumbnail.
            auto ms = sp.scene->getSystem<MeshSystem>();
            if (ms && ms->hasPendingAsyncModelLoads()) active = true;
            // A parsed model still has per-mesh work that only runs while drawing.
            auto rs = sp.scene->getSystem<RenderSystem>();
            if (rs && !rs->isAllLoaded()) active = true;
            if (sp.sceneRender->isPreviewCameraActive()) active = true;
            gaugeAnimating = sp.sceneRender->getUILayer()->isCameraGaugeAnimating();
            if (thumbnailsPending) active = true;
            // Preview scenes (shape, material, direction) only draw inside systemDraw().
            if (Engine::hasScenesToExecuteOnce()) active = true;
        }

        if (active) {
            sp.renderSettleFrames = RENDER_SETTLE_FRAMES;
            return true;
        }
        if (gaugeAnimating) {
            if (sp.renderSettleFrames > 0) sp.renderSettleFrames--;
            return true;
        }
        if (sp.renderSettleFrames > 0) {
            sp.renderSettleFrames--;
            return true;
        }
        return false;
    };

    for (auto& sceneProject : project.getScenes()) {
        if (!sceneProject.opened) continue;
        if (!sceneProject.scene || !sceneProject.sceneRender) continue;
        if (sceneProject.playState == ScenePlayState::SAVING || sceneProject.playState == ScenePlayState::LOADING || sceneProject.playState == ScenePlayState::CANCELLING) continue;

        const bool isSelected = (sceneProject.id == selectedSceneId);

        auto meshSystem = sceneProject.scene->getSystem<MeshSystem>();
        bool hasPendingModelLoads = meshSystem && meshSystem->hasPendingAsyncModelLoads();
        if (hasPendingModelLoads && !sceneProject.needUpdateRender && !isSelected) {
            meshSystem->update(0);
            continue;
        }

        if (sceneNeedsRender(sceneProject, isSelected)){
            int width = sceneWindow->getWidth(sceneProject.id);
            int height = sceneWindow->getHeight(sceneProject.id);

            // Not laid out yet (background tab): nothing to draw, and activating it here
            // would thrash lastActivatedScene. Stays dirty until SceneWindow gives it a size.
            if (width == 0 || height == 0) continue;

            SceneRender* sceneRender = sceneProject.sceneRender;

            bool sceneChanged = false;

            // Collect loaded child scene layers (skip if playing — runtime manages its own layers)
            std::vector<Scene*> childLayers;
            if (sceneProject.playState == ScenePlayState::STOPPED) {
                for (const ChildSceneRef& childSceneRef : sceneProject.childScenes) {
                    uint32_t childId = childSceneRef.id;
                    const SceneProject* childScene = project.getScene(childId);
                    if (childScene && childScene->expandedInline && childScene->scene) {
                        childLayers.push_back(childScene->scene);
                    }
                }
            }
            sceneProject.sceneRender->setChildSceneLayers(childLayers);

            // activate() reloads this scene and every layer, so it runs only on a scene
            // change. Anything else needing it calls resetLastActivatedScene().
            if (lastActivatedScene != sceneProject.id){
                // Collected before activate(): it drops every engine layer, and a dropped
                // layer no longer counts as running, so this would come back empty.
                std::vector<Scene*> runtimeLayers = project.getRunningRuntimeLayers(sceneProject.id);
                sceneProject.sceneRender->activate();

                for (Scene* runtimeLayer : runtimeLayers) {
                    Engine::addSceneLayer(runtimeLayer);
                }

                lastActivatedScene = sceneProject.id;
                pendingResizeScene = sceneProject.id;
                sceneChanged = true;
                #ifdef _DEBUG
                printf("DEBUG: Activated scene %u\n", lastActivatedScene);
                #endif
            }

            if (pendingResizeScene == sceneProject.id && width != 0 && height != 0) {
                sceneChanged = true;
                pendingResizeScene = NULL_PROJECT_SCENE;
            }

            if (width != 0 && height != 0){
                sceneRender->setOverlayScale(sceneWindow->getOverlayScale(sceneProject.id));
                Framebuffer* framebuffer = Engine::getFramebuffer();
                const bool framebufferMismatch = !framebuffer
                    || !framebuffer->isCreated()
                    || framebuffer->getWidth() != static_cast<unsigned int>(width)
                    || framebuffer->getHeight() != static_cast<unsigned int>(height);

                if (Platform::setSizes(width, height) || sceneChanged || framebufferMismatch){
                    Engine::systemViewChanged();
                    sceneRender->updateSize(width, height);
                    sceneChanged = false;
                }

                // Update child scene render systems so their entities display correctly
                for (const ChildSceneRef& childSceneRef : sceneProject.childScenes) {
                    uint32_t childId = childSceneRef.id;
                    SceneProject* childScene = project.getScene(childId);
                    if (childScene && childScene->expandedInline && childScene->scene) {
                        if (childScene->sceneRender) {
                            childScene->sceneRender->hideAllGizmos();
                        }
                        childScene->scene->getSystem<MeshSystem>()->update(0);
                        childScene->scene->getSystem<UISystem>()->update(0);
                        childScene->scene->getSystem<RenderSystem>()->update(0);
                        childScene->needUpdateRender = false;
                    }
                }

                // to avoid delay when move objects with gizmo
                sceneRender->updateRenderSystem();

                //TODO: avoid calling every frame
                sceneRender->update(project.getSelectedEntities(sceneProject.id), project.getEntities(sceneProject.id), sceneProject.mainCamera, sceneProject.displaySettings);

                Engine::systemDraw();

                resourcesWindow->processMaterialThumbnails();
                resourcesWindow->processModelThumbnails();

                // Only a real draw counts as activity, or the loop never idles.
                renderedSceneThisFrame = true;
                sceneProject.needUpdateRender = false;
            }
        }
    }

    // A frame resuming after an idle gap measures the gap, not a frame time.
    if (renderedSceneThisFrame && renderedScenePrevFrame){
        footerFramerate = Engine::getFramerate();
        footerDeltaMs = (float)(Engine::getDeltatime() * 1000.0);
    }else{
        footerFramerate = 0.0f;
        footerDeltaMs = 0.0f;
    }
    renderedScenePrevFrame = renderedSceneThisFrame;
}

void editor::App::enqueueMainThreadTask(std::function<void()> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
        mainThreadTasks.push(std::move(task));
    }
    // Wake an idle backend loop so cross-thread work runs promptly.
    if (wakeCallback) wakeCallback();
}

bool editor::App::hasPendingMainThreadTasks() {
    std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
    return !mainThreadTasks.empty();
}

// Counts as activity, so the loop also keeps drawing for the idle delay past the
// last request: enough for ImGui to auto-resize a window whose content just grew.
void editor::App::requestRedraw() {
    redrawRequested = true;
}

bool editor::App::consumeRedrawRequest() {
    const bool requested = redrawRequested;
    redrawRequested = false;
    return requested;
}

void editor::App::setWakeCallback(std::function<void()> cb) {
    wakeCallback = cb;
    // The AI service keeps its own copy, so nothing it holds points back here.
    if (aiChatWindow) aiChatWindow->setWakeCallback(std::move(cb));
}

void editor::App::shutdownBackgroundWork() {
    // Before glfwTerminate/SDL_Quit, or a late reply posts to a dead window system.
    if (aiChatWindow) aiChatWindow->shutdown();
    wakeCallback = nullptr;
}

void editor::App::engineViewDestroyed(){
    imageViewerWindow->closeAll();
    Engine::systemViewDestroyed();
}

void editor::App::engineShutdown(){
    Engine::systemShutdown();
}

void editor::App::addNewSceneToDock(uint32_t sceneId){
    if (isInitialized){
        const std::string windowName = "###Scene" + std::to_string(sceneId);
        const SceneProject* sceneProject = project.getScene(sceneId);
        const bool isUnsavedScene = sceneProject && sceneProject->filepath.empty();

        dockTabWindow(windowName, isUnsavedScene);
    }
}

void editor::App::clearSceneWindowState(uint32_t sceneId) {
    if (sceneWindow) {
        sceneWindow->clearSceneState(sceneId);
    }

    if (lastActivatedScene == sceneId) {
        resetLastActivatedScene();
    }
}

void editor::App::prepareForProjectSwitch() {
    imageViewerWindow->closeAll();
    resourcesWindow->cancelThumbnailWork();
    // Thumbnail cancellation prevents new preview loads from being queued;
    // now quiesce any remaining scene/model jobs before pool cleanup.
    MeshSystem::cancelAllAsyncModelLoads();
    codeEditor->closeAll();
    outputWindow->clear();
    sceneWindow->resetProjectState();
    dockspaceNeedsRebuild = true;
}

void editor::App::requestScenePlayFocus(uint32_t sceneId) {
    if (!isMainThread()) {
        enqueueMainThreadTask([this, sceneId]() {
            requestScenePlayFocus(sceneId);
        });
        return;
    }

    if (sceneWindow) {
        sceneWindow->requestPlayFocus(sceneId);
    }
}

void editor::App::addNewCodeWindowToDock(fs::path path, bool force){
    if (isInitialized){
        dockTabWindow("###" + path.string(), force);
    }
}

void editor::App::addImageViewerWindowToDock(fs::path path, bool force){
    if (isInitialized){
        dockTabWindow(ImageViewerWindow::getWindowId(path), force);
    }
}

void editor::App::handleExternalDrop(const std::vector<std::string>& paths) {
    isDroppedExternalPaths = true;
    droppedExternalPaths = paths;
}

void editor::App::handleExternalDragEnter() {
    resourcesWindow->handleExternalDragEnter();
}

void editor::App::handleExternalDragLeave() {
    resourcesWindow->handleExternalDragLeave();
}

void editor::App::resetLastActivatedScene(){
    lastActivatedScene = NULL_PROJECT_SCENE;
    pendingResizeScene = NULL_PROJECT_SCENE;
}

bool editor::App::shouldSyncEngineApi() const {
    return true;
}

void editor::App::updateResourcesPath(){
    std::filesystem::path currentProjectPath = project.getProjectPath().lexically_normal();
    bool projectPathChanged = lastResourcesProjectPath != currentProjectPath;

    if (isInitialized){
        if (projectPathChanged) {
            resourcesWindow->notifyProjectPathChange();
        } else {
            resourcesWindow->refreshCurrentDirectory();
        }
    }

    // Initial project loading happens before the Resources window is initialized,
    // but prepareForProjectSwitch() still suspends its worker.
    resourcesWindow->resumeThumbnailWork();

    lastResourcesProjectPath = currentProjectPath;
    resourcesWindow->cleanupThumbnails();
}

void editor::App::requestDockspaceRebuild() {
    dockspaceNeedsRebuild = true;
}

void editor::App::updateWindowTitle(const std::string& projectName) {
    Backend::updateWindowTitle(projectName);
}

void editor::App::stopTransientPreviews() {
    if (propertiesWindow) {
        propertiesWindow->stopTransientPreviews();
    }
}

void editor::App::saveAllCodeEditors() {
    if (codeEditor) {
        codeEditor->saveAll();
    }
}

void editor::App::registerAlert(std::string title, std::string message) {
    registerAlert(title, message, "");
}

void editor::App::registerAlert(std::string title, std::string message, std::string note) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
    alert.note = note;
    alert.type = AlertType::Info;
    alert.onYes = nullptr;
    alert.onNo = nullptr;
}

void editor::App::registerOutsideAssetsAlert(const std::string& path) {
    registerAlert(
        "File outside the assets directory",
        "Only files inside \"" + project.getAssetsDir().generic_string() + "\" can be used.\n"
        "Move or copy it there and drop it again.",
        project.normalizeToProjectRelative(path).generic_string());
}

void editor::App::registerConfirmAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
    alert.note.clear();
    alert.type = AlertType::Confirm;
    alert.onYes = onYes;
    alert.onNo = onNo;
}

void editor::App::registerThreeButtonAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo, std::function<void()> onCancel) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
    alert.note.clear();
    alert.type = AlertType::ThreeButton;
    alert.onYes = onYes;
    alert.onNo = onNo;
    alert.onCancel = onCancel;
}

void editor::App::registerSaveSceneDialog(uint32_t sceneId, std::function<void()> callback) {
    // Add scene to the save dialog queue with callback
    SaveDialogQueueItem item = {SaveDialogType::Scene, sceneId, callback};
    saveDialogQueue.push(item);

    // If this is the only item in the queue, process it immediately
    if (saveDialogQueue.size() == 1 && !saveDialogInProgress && !sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen()) {
        processNextSaveDialog();
    }
    // If queue has more items or another dialog is open, they'll be processed later
}

void editor::App::registerProjectSaveDialog(std::function<void()> callback) {
    // Add project save to the dialog queue with callback
    SaveDialogQueueItem item = {SaveDialogType::Project, 0, callback};  // sceneId is unused for Project saves
    saveDialogQueue.push(item);

    // If this is the only item in the queue, process it immediately
    if (saveDialogQueue.size() == 1 && !saveDialogInProgress && !sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen()) {
        processNextSaveDialog();
    }
    // If queue has more items or another dialog is open, they'll be processed later
}

std::filesystem::path editor::App::getUserCacheBaseDir() {
    // Cache the result to avoid repeated syscalls/env lookups
    static std::filesystem::path cached = []() -> std::filesystem::path {
    #if defined(_WIN32)
        // Ensure COM is initialized for SHGetKnownFolderPath
        // Using COINIT_APARTMENTTHREADED is safe for most GUI apps
        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        // hrInit == S_OK means we initialized, RPC_E_CHANGED_MODE means already initialized differently (OK)
        // We don't call CoUninitialize here since we want COM available for the app lifetime

        PWSTR widePath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &widePath)) && widePath) {
            std::filesystem::path p(widePath);
            CoTaskMemFree(widePath);
            return p; // e.g. C:\Users\<you>\AppData\Local
        }
        // Fallback to environment variable
        if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && *localAppData) {
            return std::filesystem::path(localAppData);
        }
        return std::filesystem::temp_directory_path();

    #elif defined(__APPLE__)
        // Conventional macOS cache location: ~/Library/Caches
        const char* home = std::getenv("HOME");
        if (home && *home) {
            return std::filesystem::path(home) / "Library" / "Caches";
        }
        return std::filesystem::temp_directory_path();

    #else
        // Linux / other Unix: XDG Base Dir spec
        // Prefer $XDG_CACHE_HOME, fallback to ~/.cache
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        if (xdg && *xdg) {
            return std::filesystem::path(xdg);
        }
        const char* home = std::getenv("HOME");
        if (home && *home) {
            return std::filesystem::path(home) / ".cache";
        }
        return std::filesystem::temp_directory_path();
    #endif
    }();

    return cached;
}

std::filesystem::path editor::App::getUserShaderCacheDir(){
    // Bump this version on any shader source change to invalidate stale cached
    // .sdat (the cache is keyed by shaderType+properties, not source content).
    // v4: mirror (USE_MIRROR) variants changed the u_fs_mirror block guard.
    // v5: SSAO (mesh.frag USE_SSAO + viewportInfo, new ssao/ssao_blur shaders).
    // v6: SSR (G-buffer geometry pass + ssr/ssr_blur/composite fullscreen shaders;
    //     energy-conserving SSR-over-IBL, glossy roughness blur, debug modes).
    // v7: second UV set (HAS_UV_SET2) — per-texture UV selection via u_fs_texCoordSets.
    // v8: directional/spot shadows share u_shadowAtlas (replaces u_shadowMap1..6),
    //     3x3 atlas, SSAO on terrain (USE_SSAO + depth HAS_TERRAIN).
    // v9: dual shadow atlases — u_shadowAtlas (directional/spot, projective) and
    //     u_shadowPointAtlas (point cube faces); removes the u_shadowCubeMap* samplers
    //     and keeps each atlas within the GPU max texture size.
    // v10: 2D lighting — mesh USE_LIGHT2D/USE_SHADOWS_2D variants (u_fs_lighting2d,
    //      u_normalTexture on unlit, v_position guard) and the shadow2d 1D polar pass.
    // v11: uniform-driven shadow PCF (ShadowQuality) — USE_SHADOWS_PCF variant removed,
    //      shadows.glsl/lighting2d.glsl loop by radius from cameraDir.w / atlasInfo.w.
    // v12: fixed game resolution — new blit fullscreen shader (blit.frag).
    // v13: box-projected local reflection probes in the mesh IBL path.
    // v14: G-buffer (SSR) terrain fix — gbuffer.vert no longer declares a_texcoord1
    //      for terrain (generates the base-tile UV in-shader), so the terrain G-buffer
    //      pipeline's vertex layout is continuous and passes sokol validation.
    // v15: skinning capacity increased to 128 bones; every skinned shader uniform block changed.
    // v16: projected spotlight masks add spotUp_maskAspect[MAX_LIGHTS] to u_fs_lighting
    //      and add the shared u_spotMaskAtlas sampler to punctual mesh variants,
    //      using the engine's native texture Y orientation.
    // v17: projective shadows use a depth-only atlas with a comparison sampler.
    // v18: vec3 vertex colors on meshes — v_color is promoted to vec4 under instancing.
    // v19: Vulkan shadow atlas lookups and screen-space depth/texture orientation fixes,
    //      later widened from IS_VULKAN to every non-GL backend (Metal, D3D11) across
    //      the depth, G-buffer and fullscreen passes: same shaders, so still v19.
    // v20: dynamically sized skinning uses storage buffers, or an unfilterable bone
    //      texture with a nonfiltering sampler on GL/GLES.
    // v21: backend-specific storage-buffer bindings are preserved in reflection data.
    // v22: composite.frag samples the scene color and reflection in destination space
    //      (params.w), so SSR can composite to the swapchain in exported builds.
    // v23: explicit SSR LOD sampling prevents D3D11 loop-unrolling failures.
    return App::getUserCacheBaseDir() / "doriax" / "shaders" / "v23";
}

void editor::App::pushTabNotificationStyle(){
    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4(0.22f, 0.30f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmed,   ImVec4(0.22f, 0.30f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,  ImVec4(0.26f, 0.35f, 0.46f, 1.00f));
}

void editor::App::popTabNotificationStyle(){
    ImGui::PopStyleColor(3);
}

editor::Project* editor::App::getProject(){
    return &project;
}

const editor::Project* editor::App::getProject() const{
    return &project;
}

editor::Properties* editor::App::getPropertiesWindow() const{
    return propertiesWindow;
}

editor::CodeEditor* editor::App::getCodeEditor() const{
    return codeEditor;
}

editor::ImageViewerWindow* editor::App::getImageViewerWindow() const{
    return imageViewerWindow;
}

editor::ResourcesWindow* editor::App::getResourcesWindow() const{
    return resourcesWindow;
}

bool editor::App::popSaveDialogQueueItem() {
    if (saveDialogQueue.empty()) {
        Out::warning("Attempted to pop an empty save dialog queue.");
        return false;
    }

    saveDialogQueue.pop();
    return true;
}

editor::AnimationWindow* editor::App::getAnimationWindow() const{
    return animationWindow;
}

editor::TerrainEditWindow* editor::App::getTerrainEditWindow() const{
    return terrainEditWindow;
}

void editor::App::processNextSaveDialog() {
    // Check if there's anything to process and no dialogs are currently open
    if (saveDialogInProgress || saveDialogQueue.empty() || sceneSaveDialog.isOpen() || projectSaveDialog.isOpen()) {
        return;
    }

    // Get the next item from the queue
    SaveDialogQueueItem item = saveDialogQueue.front();
    // Store the callback to use it later
    std::function<void()> completionCallback = item.callback;

    if (item.type == SaveDialogType::Scene) {
        // Process scene save dialog
        uint32_t sceneId = item.sceneId;
        SceneProject* sceneProject = project.getScene(sceneId);
        if (!sceneProject) {
            // Invalid scene, remove from queue and try next item
            popSaveDialogQueueItem();
            processNextSaveDialog();
            return;
        }

        // Set default filename
        std::string defaultName = sceneProject->name + ".scene";
        fs::path initialDirectory = project.getProjectPath();
        if (resourcesWindow) {
            fs::path resourcesPath = resourcesWindow->getCurrentPath();
            if (!resourcesPath.empty()) {
                initialDirectory = resourcesPath;
            }
        }

        saveDialogInProgress = true;

        // Open dialog for the current scene
        sceneSaveDialog.open(
            project.getProjectPath(),
            initialDirectory,
            defaultName,
            // Save callback
            [this, sceneId, completionCallback](const fs::path& fullPath) {
                bool saveStarted = false;
                SceneProject* sceneProject = project.getScene(sceneId);
                if (sceneProject) {
                    // Create directory if it doesn't exist
                    std::filesystem::create_directories(fullPath.parent_path());

                    // Save the scene
                    std::error_code ec;
                    fs::path relPath = fs::relative(fullPath, project.getProjectPath(), ec);
                    if (ec || relPath.empty()) {
                        Out::error("Scene filepath must be relative to project path: %s", fullPath.string().c_str());
                        Backend::getApp().registerAlert("Error", "Scene file must be inside the project folder.");
                    } else {
                        sceneProject->filepath = relPath;
                        saveStarted = true;
                        project.saveSceneToPathAsync(sceneId, fullPath, [this, completionCallback](bool success) {
                            saveDialogInProgress = false;
                            popSaveDialogQueueItem();

                            if (success && completionCallback) {
                                completionCallback();
                            }

                            processNextSaveDialog();
                        });
                    }
                }

                if (!saveStarted) {
                    saveDialogInProgress = false;
                    popSaveDialogQueueItem();
                    processNextSaveDialog();
                }
            },
            // Cancel callback
            [this, completionCallback]() {
                // Remove the current item from the queue without saving
                saveDialogInProgress = false;
                popSaveDialogQueueItem();

                // Process the next item if available
                processNextSaveDialog();
            }
        );
    }
    else if (item.type == SaveDialogType::Project) {
        // Process project save dialog
        std::string defaultName = project.getName();
        if (defaultName.empty()) {
            defaultName = "MyProject";
        }

        saveDialogInProgress = true;

        projectSaveDialog.open(
            defaultName,
            // Save callback
            [this, completionCallback](const std::string& projectName, const fs::path& projectPath) {
                // Set the project name if provided
                if (!projectName.empty()) {
                    project.setName(projectName);
                }

                // Save the project to the selected path
                project.saveProjectToPath(projectPath);

                // Remove this item from the queue
                saveDialogInProgress = false;
                popSaveDialogQueueItem();

                // Execute the completion callback if provided
                if (completionCallback) {
                    completionCallback();
                }

                // Process the next item if available
                processNextSaveDialog();
            },
            // Cancel callback
            [this, completionCallback]() {
                // Remove the current item from the queue without saving
                saveDialogInProgress = false;
                popSaveDialogQueueItem();

                // Process the next item if available
                processNextSaveDialog();
            }
        );
    }
}

void editor::App::processMainThreadTasks() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
        std::swap(tasks, mainThreadTasks);
    }

    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        if (task) task();
    }
}

bool editor::App::isMainThread() const {
    return std::this_thread::get_id() == mainThreadId;
}

void editor::App::initializeSettings() {
    AppSettings::initialize();

    // Next to settings.yaml, where a user can find it to report a crash.
    const std::filesystem::path logFile = AppSettings::getConfigDirectory() / "editor.log";
    Out::setLogFile(logFile);
    CrashHandler::install(logFile);

    applyPanelVisibilitySettings();
}

void editor::App::applyPanelVisibilitySettings() {
    const PanelVisibilitySettings visibility = AppSettings::getPanelVisibility();
    structureWindow->setOpen(visibility.structure);
    propertiesWindow->setOpen(visibility.properties);
    resourcesWindow->setOpen(visibility.resources);
    outputWindow->setOpen(visibility.output);
    animationWindow->setOpen(visibility.animation);
    terrainEditWindow->setOpen(visibility.terrain);
    aiChatWindow->setOpen(visibility.aiChat);
}

void editor::App::persistPanelVisibilitySettings() {
    PanelVisibilitySettings visibility;
    visibility.structure = structureWindow->isOpen();
    visibility.properties = propertiesWindow->isOpen();
    visibility.resources = resourcesWindow->isOpen();
    visibility.output = outputWindow->isOpen();
    visibility.animation = animationWindow->isOpen();
    visibility.terrain = terrainEditWindow->isOpen();
    visibility.aiChat = aiChatWindow->isOpen();

    if (visibility != AppSettings::getPanelVisibility()) {
        AppSettings::setPanelVisibility(visibility);
    }
}

int editor::App::getInitialWindowWidth(float uiScale) const {
    return scaleSavedWindowSize(AppSettings::getWindowWidth(), uiScale);
}

int editor::App::getInitialWindowHeight(float uiScale) const {
    return scaleSavedWindowSize(AppSettings::getWindowHeight(), uiScale);
}

bool editor::App::getInitialWindowMaximized() const {
    return AppSettings::getIsMaximized();
}

void editor::App::saveWindowSettings(int width, int height, bool maximized, float uiScale) {
    AppSettings::setWindowWidth(width);
    AppSettings::setWindowHeight(height);
    AppSettings::setIsMaximized(maximized);
    AppSettings::setWindowUiScale(sanitizeUiScale(uiScale));
    // The ini is written at the scale the layout has held all session, not the
    // monitor scale above, which is read fresh and may have changed since.
    if (layoutScaleApplied) {
        AppSettings::setLayoutUiScale(sanitizeUiScale(layoutUiScale));
    }
    AppSettings::saveSettings();
}

void editor::App::exit() {
    // Check if any modal popup is currently open (including ComponentAddDialog, ScriptCreateDialog, etc.)
    ImGuiWindow* modal = ImGui::GetTopMostAndVisiblePopupModal();
    if (modal != nullptr) {
        // A modal is open - close it first
        ImGui::CloseCurrentPopup();
        return;  // Don't proceed with exit yet, user needs to click close again
    }

    // Also check if the scene save dialog is specifically open
    if (sceneSaveDialog.isOpen()) {
        sceneSaveDialog.close();
        return;
    }

    if (project.isAnySceneSaving()) {
        registerAlert("Saving Scene", "Wait for the current scene save to finish before exiting.");
        return;
    }

    if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() || project.isTempUnsavedProject()) {
        registerThreeButtonAlert(
            "Unsaved Changes",
            "There are unsaved changes. Do you want to save them before exiting?",
            [this]() {
                saveAllAndProject([this]() {
                    closeWindow();
                });
            },
            [this]() {
                // No callback - just exit without saving
                closeWindow();
            },
            []() {
                // Cancel callback - do nothing, just close the dialog
            }
        );
    } else {
        // No unsaved changes, proceed with exit
        closeWindow();
    }
}

void editor::App::closeWindow(){
    // Flush a still-pending tab reorder (debounced in captureTabOrder()) so it
    // isn't lost when quitting right after dragging a tab.
    if (tabsOrderDirty){
        project.saveProjectFile();
        tabsOrderDirty = false;
    }

    // Stop all playing scenes before shutdown to properly cleanup script instances
    for (auto& sceneProject : project.getScenes()) {
        if (sceneProject.playState == ScenePlayState::PLAYING ||
            sceneProject.playState == ScenePlayState::PAUSED ||
            sceneProject.playState == ScenePlayState::LOADING) {
            project.stop(sceneProject.id);
        }
    }

    project.waitForPlaySessionToFinish();

    project.clearTrash();

    editor::ShaderBuilder::requestShutdown();
    Backend::closeWindow();
}
