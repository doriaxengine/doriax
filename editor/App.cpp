#include "App.h"

#include "imgui_internal.h"
#include "Platform.h"
#include "Doriax.h"
#include "Backend.h"

#include "external/IconsFontAwesome6.h"
#include "command/CommandHandle.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "command/type/RemoveChildSceneCmd.h"

#include "util/ProjectUtils.h"

#include "Out.h"
#include "AppSettings.h"
#include "resources/fonts/fa-solid-900_ttf.h"
//#include "recources/fonts/roboto-v20-latin-regular_ttf.h"
#include "util/DefaultFont.h"

#include "shader/ShaderBuilder.h"

#include <filesystem>
#include <cstdlib>

#if defined(_WIN32)
  #include <windows.h>
  #include <shlobj.h>
#endif

using namespace doriax;

ImVec4 editor::App::ThemeColors::ButtonActivated;
ImVec4 editor::App::ThemeColors::FileCardBackground;
ImVec4 editor::App::ThemeColors::FileCardBackgroundHovered;
ImVec4 editor::App::ThemeColors::SubtleText;
ImVec4 editor::App::ThemeColors::filenameLabel;
ImVec4 editor::App::ThemeColors::ExtEntityButton;
ImVec4 editor::App::ThemeColors::ExtEntityButtonHovered;
ImVec4 editor::App::ThemeColors::ExtEntityButtonActive;

editor::App::App(){
    propertiesWindow = new Properties(&project);
    outputWindow = new OutputWindow();
    sceneWindow = new SceneWindow(&project);
    structureWindow = new Structure(&project, sceneWindow);
    codeEditor = new CodeEditor(&project);
    resourcesWindow = new ResourcesWindow(&project, codeEditor);
    loadingWindow = new LoadingWindow();
    animationWindow = new AnimationWindow(&project);
    terrainEditWindow = new TerrainEditWindow(&project);

    isInitialized = false;

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

void editor::App::saveAllFunc(){
    project.saveAllScenes();
    codeEditor->saveAll();
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
                // Yes callback - save all and then continue
                saveAllFunc();
                project.saveProject(true,
                    [this]() {
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

void editor::App::showMenu(){
    SceneProject* selectedScene = project.getSelectedScene();
    uint32_t selectedSceneId = project.getSelectedSceneId();
    bool hasSelectedScene = selectedScene != nullptr;
    bool isProjectBusy = project.isAnyScenePlaying();
    bool isPlaying = hasSelectedScene && selectedScene->playState == ScenePlayState::PLAYING;
    bool isPaused = hasSelectedScene && selectedScene->playState == ScenePlayState::PAUSED;
    bool canRun = hasSelectedScene && !isProjectBusy;
    bool canPause = hasSelectedScene && isPlaying;
    bool canResume = hasSelectedScene && isPaused;
    bool canStop = hasSelectedScene && (isPlaying || isPaused);
    bool canRemove = hasSelectedScene && !isProjectBusy && project.getScenes().size() > 1;

    // Remove menu bar border
    //ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    //ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    // Create the main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::BeginDisabled(isProjectBusy);
            if (ImGui::MenuItem("New Project")) {
                std::string projectName = "MyDoriaxProject";
                if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() || project.isTempUnsavedProject()) {
                    Backend::getApp().registerConfirmAlert(
                        "Unsaved Changes",
                        "There are unsaved changes. Do you want to save them before creating a new project?",
                        [this, projectName]() {
                            // Yes callback - save all and then reset
                            saveAllFunc();
                            project.saveProject(true,
                                [this, projectName]() {
                                project.createTempProject(projectName, true);
                            });
                        },
                        [this, projectName]() {
                            // No callback - just reset without saving
                            project.createTempProject(projectName, true);
                        }
                    );
                } else {
                    // No unsaved changes, just reset
                    project.createTempProject(projectName, true);
                }
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(isProjectBusy);
            if (ImGui::BeginMenu("New Scene")) {
                if (ImGui::MenuItem(ICON_FA_CUBES "  3D Scene")) {
                    project.createNewScene("New Scene", SceneType::SCENE_3D);
                }
                if (ImGui::MenuItem(ICON_FA_CUBES_STACKED "  2D Scene")) {
                    project.createNewScene("New Scene", SceneType::SCENE_2D);
                }
                if (ImGui::MenuItem(ICON_FA_WINDOW_RESTORE "  UI Scene")) {
                    project.createNewScene("New Scene", SceneType::SCENE_UI);
                }
                ImGui::EndMenu();
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            ImGui::BeginDisabled(isProjectBusy);
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                openProjectFunc();
            }
            if (ImGui::BeginMenu("Recent Projects")) {
                std::vector<std::filesystem::path> recentProjects = AppSettings::getRecentProjects();
                if (recentProjects.empty()) {
                    ImGui::MenuItem("No Recent Projects", nullptr, false, false);
                } else {
                    for (const auto& path : recentProjects) {
                        std::string label = path.filename().string() + " (" + path.string() + ")";
                        if (ImGui::MenuItem(label.c_str())) {
                            if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() || project.isTempUnsavedProject()) {
                                registerConfirmAlert(
                                    "Unsaved Changes",
                                    "There are unsaved changes. Do you want to save them before opening another project?",
                                    [this, path]() {
                                        // Yes callback - save all and then contin
                                        saveAllFunc();
                                        project.saveProject(true,
                                            [this, path]() {
                                                this->project.loadProject(path);
                                        });
                                    },
                                    [this, path]() {
                                        // No callback - just continue without saving
                                        this->project.loadProject(path);
                                    }
                                );
                            } else {
                                project.loadProject(path);
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Projects")) {
                        AppSettings::clearRecentProjects();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndDisabled();

            if (ImGui::MenuItem("Save Project")) {
                project.saveProject(true);
            }
            if (ImGui::MenuItem("Save Project As...")) {
                registerProjectSaveDialog([](){});
            }
            ImGui::Separator();
            bool canSave = false;
            if (lastFocusedWindow == LastFocusedWindow::Code) {
                canSave = codeEditor->hasLastFocusedUnsavedChanges();
            }else{
                canSave = project.hasSelectedSceneUnsavedChanges();
            }
            bool canSaveAll = project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges();

            ImGui::BeginDisabled(!canSave);
            if (ImGui::MenuItem("Save")) {
                saveFunc();
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!canSaveAll);
            if (ImGui::MenuItem("Save All")) {
                saveAllFunc();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Export Project...")) {
                exportWindow.open(&project);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                exit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            bool canUndo = false;
            bool canRedo = false;
            if (lastFocusedWindow == LastFocusedWindow::Resources) {
                canUndo = project.getProjectCommandHistory()->canUndo();
                canRedo = project.getProjectCommandHistory()->canRedo();
            } else if (lastFocusedWindow == LastFocusedWindow::Code) {
                canUndo = codeEditor->canUndoLastFocused();
                canRedo = codeEditor->canRedoLastFocused();
            } else {
                canUndo = CommandHandle::get(project.getSelectedSceneId())->canUndo();
                canRedo = CommandHandle::get(project.getSelectedSceneId())->canRedo();
            }

            ImGui::BeginDisabled(!canUndo);
            if (ImGui::MenuItem("Undo")) {
                if (lastFocusedWindow == LastFocusedWindow::Resources) {
                    project.getProjectCommandHistory()->undo();
                    resourcesWindow->refreshCurrentDirectory();
                } else if (lastFocusedWindow == LastFocusedWindow::Code) {
                    codeEditor->undoLastFocused();
                } else {
                    CommandHandle::get(project.getSelectedSceneId())->undo();
                }
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!canRedo);
            if (ImGui::MenuItem("Redo")) {
                if (lastFocusedWindow == LastFocusedWindow::Resources) {
                    project.getProjectCommandHistory()->redo();
                    resourcesWindow->refreshCurrentDirectory();
                } else if (lastFocusedWindow == LastFocusedWindow::Code) {
                    codeEditor->redoLastFocused();
                } else {
                    CommandHandle::get(project.getSelectedSceneId())->redo();
                }
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                 buildDockspace();
            }
             ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Project Settings...")) {
                projectSettingsWindow.open(&project);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Trash")) {
                project.clearTrash();
            }
            if (ImGui::MenuItem("Clear Shader Cache")) {
                std::filesystem::path cacheDir = getUserShaderCacheDir();
                if (std::filesystem::exists(cacheDir)) {
                    std::filesystem::remove_all(cacheDir);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            ImGui::BeginDisabled(!canRun);
            if (ImGui::MenuItem("Run", "F5")) {
                project.start(selectedSceneId);
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!canPause);
            if (ImGui::MenuItem("Pause", "F6")) {
                project.pause(selectedSceneId);
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!canResume);
            if (ImGui::MenuItem("Resume", "F5")) {
                project.resume(selectedSceneId);
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!canStop);
            if (ImGui::MenuItem("Stop", "F7")) {
                project.stop(selectedSceneId);
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            ImGui::BeginDisabled(!canRemove);
            if (ImGui::MenuItem("Remove")) {
                project.checkUnsavedAndExecute(selectedSceneId, [this, selectedSceneId]() {
                    project.removeScene(selectedSceneId);
                });
            }
            ImGui::EndDisabled();

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Doriax")) {
                registerAlert("About Doriax", "Doriax Engine Editor\n\nVersion: 0.1.0\n\nDeveloped by Doriax Team");
            }
            if (ImGui::MenuItem("Documentation")) {
                // TODO: Open URL
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Restore previous style
    //ImGui::PopStyleVar(2);
}

void editor::App::showFooter(){
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    const float fontScale = 0.8f;
    const float footerHeight = (ImGui::GetTextLineHeight() * fontScale) + 6.0f;
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.00f));

    if (ImGui::Begin("##Footer", nullptr, flags)) {
        ImGui::SetWindowFontScale(fontScale);

        const float fps = Engine::getFramerate();
        const float deltaMs = Engine::getDeltatime() * 1000.0f;
        const size_t queuedResources = Engine::getQueuedResourceCount();
        SceneProject* selectedScene = project.getSelectedScene();
        const bool hasSelectedScene = selectedScene != nullptr;
        const bool isPlaying = hasSelectedScene && selectedScene->playState == ScenePlayState::PLAYING;
        const bool isPaused = hasSelectedScene && selectedScene->playState == ScenePlayState::PAUSED;
        const bool isStopped = !hasSelectedScene || selectedScene->playState == ScenePlayState::STOPPED;
        const bool isCancelling = hasSelectedScene && selectedScene->playState == ScenePlayState::CANCELLING;
        const bool canPlayPause = hasSelectedScene && !isCancelling && (isPlaying || isPaused || (!isStopped || !project.isAnyScenePlaying()));
        const bool canStop = hasSelectedScene && !isStopped && !isCancelling;
        const ImVec4 footerButtonHovered = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
        const ImVec4 footerButtonActive = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);

        auto footerActionButton = [&](const char* label) -> bool {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, footerButtonHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, footerButtonActive);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

            bool pressed = ImGui::Button(label);

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            return pressed;
        };

        // Left side: Status
        bool statusShown = false;
        if (queuedResources > 0) {
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

void editor::App::buildDockspace(){
    ImGuiID dock_id_left, dock_id_left_top, dock_id_left_bottom, dock_id_right, dock_id_middle, dock_id_middle_bottom;
    float size;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    // Split the dockspace into left and middle
    size = 14*ImGui::GetFontSize();
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.0f, &dock_id_left, &dock_id_middle);
    ImGui::DockBuilderSetNodeSize(dock_id_left, ImVec2(size, ImGui::GetMainViewport()->Size.y)); // Set left node size
    ImGui::DockBuilderDockWindow(Structure::WINDOW_NAME, dock_id_left);

    size = 50*ImGui::GetFontSize();
    ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.0f, &dock_id_left_bottom, &dock_id_left_top);
    ImGui::DockBuilderSetNodeSize(dock_id_left_bottom, ImVec2(ImGui::GetMainViewport()->Size.x, size));
    ImGui::DockBuilderDockWindow(ResourcesWindow::WINDOW_NAME, dock_id_left_bottom);

    // Split the middle into right and remaining middle
    size = 19*ImGui::GetFontSize();
    ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Right, 0.0f, &dock_id_right, &dock_id_middle);
    ImGui::DockBuilderSetNodeSize(dock_id_right, ImVec2(size, ImGui::GetMainViewport()->Size.y)); // Set right node size
    ImGui::DockBuilderDockWindow(Properties::WINDOW_NAME, dock_id_right);

    // Split the remaining middle into top and bottom
    size = 10*ImGui::GetFontSize();
    ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Down, 0.0f, &dock_id_middle_bottom, &dock_id_middle_top);
    ImGui::DockBuilderSetNodeSize(dock_id_middle_bottom, ImVec2(ImGui::GetMainViewport()->Size.x, size)); // Set bottom node size
    ImGui::DockBuilderDockWindow(OutputWindow::WINDOW_NAME, dock_id_middle_bottom);
    ImGui::DockBuilderDockWindow(AnimationWindow::WINDOW_NAME, dock_id_middle_bottom);

    // Dock tabs in their saved order
    for (const auto& tab : project.getTabs()) {
        if (tab.type == TabType::SCENE) {
            // Find scene by filepath
            for (auto& sceneProject : project.getScenes()) {
                if (sceneProject.opened && sceneProject.filepath.string() == tab.filepath) {
                    addNewSceneToDock(sceneProject.id);
                    break;
                }
            }
        } else if (tab.type == TabType::CODE_EDITOR) {
            fs::path fullPath = project.getProjectPath() / tab.filepath;
            if (fs::exists(fullPath)) {
                codeEditor->openFile(tab.filepath);
            }
        }
    }

    // Dock any opened scenes that weren't in tabs (fallback)
    for (auto& sceneProject : project.getScenes()) {
        if (!sceneProject.opened) continue;
        if (!project.hasTab(TabType::SCENE, sceneProject.filepath.string())) {
            addNewSceneToDock(sceneProject.id);
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
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

void editor::App::setup() {
    // Initialize application settings
    initializeSettings();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    #ifdef _DEBUG
    io.IniFilename = nullptr;  // Disable saving to ini file
    #endif

    io.Fonts->AddFontDefault();

    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = 16.0f;
    config.FontDataOwnedByAtlas = false;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryTTF(fa_solid_900_ttf, fa_solid_900_ttf_len, 16.0f, &config, icon_ranges);

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

    io.FontDefault = font1;

    io.ConfigDragClickToInputText = true;

    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    //ImGui::StyleColorsDark();
    kewtStyleTheme();
}

void editor::App::show(){
    if (resourcesWindow->isFocused()) {
        lastFocusedWindow = LastFocusedWindow::Resources;
    } else if (codeEditor->isFocused()) {
        lastFocusedWindow = LastFocusedWindow::Code;
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
            if (isPlaying || isPaused) {
                project.stop(selectedSceneId);
            }
        }
    }

    if (isDroppedExternalPaths) {
        isDroppedExternalPaths = false;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            ImGui::SetDragDropPayload("external_files", &droppedExternalPaths, sizeof(std::vector<std::string>));
            ImGui::EndDragDropSource();
        }
    }

    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()){
        // space to keys events
    }

    if (!resourcesWindow->isFocused() && !codeEditor->isFocused()){
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

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        buildDockspace();
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

    if (!sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen() && !saveDialogQueue.empty()) {
        processNextSaveDialog();
    }

    structureWindow->show();
    resourcesWindow->show();
    outputWindow->show();
    animationWindow->show();
    terrainEditWindow->show();
    propertiesWindow->show();
    codeEditor->show();
    sceneWindow->show();

    loadingWindow->show();
}

void editor::App::engineInit(int argc, char** argv) {
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

    Engine::systemInit(argc, argv, new editor::Platform(&project));
    Engine::pauseGameEvents(true);

    ShaderPool::setShaderBuilder([this](doriax::ShaderKey shaderKey) -> doriax::ShaderBuildResult {
        static doriax::editor::ShaderBuilder builder;  // Make static to reuse
        return builder.buildShader(shaderKey, &project);
    });

    TextureDataPool::setAsyncLoading(true);
}

void editor::App::engineViewLoaded(){
    Engine::systemViewLoaded();
}

void editor::App::engineRender(){
    processMainThreadTasks();
    project.refreshLinkedMaterials();

    for (auto& sceneProject : project.getScenes()) {
        if (!sceneProject.opened) continue;

        if (sceneProject.needUpdateRender || sceneProject.id == project.getSelectedSceneId()){
            int width = sceneWindow->getWidth(sceneProject.id);
            int height = sceneWindow->getHeight(sceneProject.id);

            SceneRender* sceneRender = sceneProject.sceneRender;

            bool sceneChanged = false;

            // Collect loaded child scene layers (skip if playing — runtime manages its own layers)
            std::vector<Scene*> childLayers;
            if (sceneProject.playState == ScenePlayState::STOPPED) {
                for (uint32_t childId : sceneProject.childScenes) {
                    const SceneProject* childScene = project.getScene(childId);
                    if (childScene && childScene->expandedInline && childScene->scene) {
                        childLayers.push_back(childScene->scene);
                    }
                }
            }
            sceneProject.sceneRender->setChildSceneLayers(childLayers);

            if (lastActivatedScene != sceneProject.id || sceneProject.needUpdateRender){
                sceneProject.sceneRender->activate();
                project.restoreRuntimeLayers(sceneProject.id);
                if (lastActivatedScene != sceneProject.id) {
                    lastActivatedScene = sceneProject.id;
                    sceneChanged = true;
                    #ifdef _DEBUG
                    printf("DEBUG: Activated scene %u\n", lastActivatedScene);
                    #endif
                }
            }

            if (width != 0 && height != 0){
                if (Platform::setSizes(width, height) || sceneChanged){
                    Engine::systemViewChanged();
                    sceneRender->updateSize(width, height);
                    sceneChanged = false;
                }

                // Update child scene render systems so their entities display correctly
                for (uint32_t childId : sceneProject.childScenes) {
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

                sceneProject.needUpdateRender = false;
            }
        }
    }
}

void editor::App::enqueueMainThreadTask(std::function<void()> task) {
    if (!task) return;
    std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
    mainThreadTasks.push(std::move(task));
}

void editor::App::engineViewDestroyed(){
    Engine::systemViewDestroyed();
}

void editor::App::engineShutdown(){
    Engine::systemShutdown();
}

void editor::App::addNewSceneToDock(uint32_t sceneId){
    if (isInitialized){
        ImGui::DockBuilderDockWindow(("###Scene" + std::to_string(sceneId)).c_str(), dock_id_middle_top);
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

void editor::App::addNewCodeWindowToDock(fs::path path){
    if (isInitialized){
        ImGui::DockBuilderDockWindow(("###" + path.string()).c_str(), dock_id_middle_top);
    }
}

void editor::App::kewtStyleTheme(){
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                   = ImVec4(0.75f, 0.73f, 0.73f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.16f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.16f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.11f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.11f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.11f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.16f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.16f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.28f, 0.33f, 0.41f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.28f, 0.33f, 0.41f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.39f, 0.45f, 0.55f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.28f, 0.33f, 0.41f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.34f, 0.40f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.39f, 0.45f, 0.55f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_TabSelected]            = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.23f, 0.51f, 0.96f, 0.78f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.36f, 0.43f, 0.48f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.27f, 0.25f, 0.24f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.47f, 0.44f, 0.42f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.34f, 0.33f, 0.31f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink]               = ImVec4(0.23f, 0.51f, 0.96f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.23f, 0.51f, 0.96f, 0.38f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.23f, 0.51f, 0.96f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    ThemeColors::ButtonActivated = ImLerp(colors[ImGuiCol_Button], colors[ImGuiCol_NavHighlight], 0.4f);
    ThemeColors::FileCardBackground = ImLerp(colors[ImGuiCol_WindowBg], colors[ImGuiCol_FrameBg], 0.5f);
    ThemeColors::FileCardBackgroundHovered = ImLerp(colors[ImGuiCol_WindowBg], colors[ImGuiCol_HeaderHovered], 0.25f);
    ThemeColors::SubtleText = ImLerp(colors[ImGuiCol_Text], colors[ImGuiCol_NavHighlight], 0.4f);
    ThemeColors::filenameLabel = ImVec4(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f);
    ThemeColors::ExtEntityButton = ImVec4(colors[ImGuiCol_Button].x * 0.85f, colors[ImGuiCol_Button].y * 0.9f, colors[ImGuiCol_Button].z * 1.15f, colors[ImGuiCol_Button].w);
    ThemeColors::ExtEntityButtonHovered = ImVec4(colors[ImGuiCol_ButtonHovered].x * 0.85f, colors[ImGuiCol_ButtonHovered].y * 0.9f, colors[ImGuiCol_ButtonHovered].z * 1.15f, colors[ImGuiCol_ButtonHovered].w);
    ThemeColors::ExtEntityButtonActive = ImVec4(colors[ImGuiCol_ButtonActive].x * 0.85f, colors[ImGuiCol_ButtonActive].y * 0.9f, colors[ImGuiCol_ButtonActive].z * 1.15f, colors[ImGuiCol_ButtonActive].w);

    // main
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(10, 4);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(8, 8);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 16;
    style.ScrollbarSize = 12;
    style.GrabRounding = 12;

    // borders
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 0;
    style.FrameBorderSize = 0;
    style.TabBorderSize = 0;
    style.TabBarBorderSize = 1;
    style.TabBarOverlineSize = 2;

    // rounding
    style.WindowRounding = 2;
    style.ChildRounding = 2;
    style.FrameRounding = 2;
    style.PopupRounding = 2;
    style.ScrollbarRounding = 2;
    style.GrabRounding = 2;
    style.TabRounding = 2;

    // tables
    style.CellPadding = ImVec2(8, 4);
    style.TableAngledHeadersAngle = 0.61; //35 deg
    style.TableAngledHeadersTextAlign = ImVec2(0.50, 0.00);

    // widgets
    style.WindowTitleAlign = ImVec2(0.00, 0.50);
    style.WindowMenuButtonPosition = ImGuiDir::ImGuiDir_Right;
    style.ColorButtonPosition = ImGuiDir::ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.50, 0.50);
    style.SelectableTextAlign = ImVec2(0.00, 0.00);
    style.SeparatorTextBorderSize = 1;
    style.SeparatorTextAlign = ImVec2(0.00, 0.50);
    style.SeparatorTextPadding = ImVec2(16, 0);
    style.LogSliderDeadzone = 4;

    // docking
    style.DockingSeparatorSize = 6;
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
}

void editor::App::updateResourcesPath(){
    if (isInitialized){
        resourcesWindow->notifyProjectPathChange();
    }
    resourcesWindow->cleanupThumbnails();
}

void editor::App::registerAlert(std::string title, std::string message) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
    alert.type = AlertType::Info;
    alert.onYes = nullptr;
    alert.onNo = nullptr;
}

void editor::App::registerConfirmAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
    alert.type = AlertType::Confirm;
    alert.onYes = onYes;
    alert.onNo = onNo;
}

void editor::App::registerThreeButtonAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo, std::function<void()> onCancel) {
    alert.needShow = true;
    alert.title = title;
    alert.message = message;
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
    if (saveDialogQueue.size() == 1 && !sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen()) {
        processNextSaveDialog();
    }
    // If queue has more items or another dialog is open, they'll be processed later
}

void editor::App::registerProjectSaveDialog(std::function<void()> callback) {
    // Add project save to the dialog queue with callback
    SaveDialogQueueItem item = {SaveDialogType::Project, 0, callback};  // sceneId is unused for Project saves
    saveDialogQueue.push(item);

    // If this is the only item in the queue, process it immediately
    if (saveDialogQueue.size() == 1 && !sceneSaveDialog.isOpen() && !projectSaveDialog.isOpen()) {
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
    return App::getUserCacheBaseDir() / "doriax" / "shaders" / "v1";
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

editor::CodeEditor* editor::App::getCodeEditor() const{
    return codeEditor;
}

editor::ResourcesWindow* editor::App::getResourcesWindow() const{
    return resourcesWindow;
}

editor::AnimationWindow* editor::App::getAnimationWindow() const{
    return animationWindow;
}

editor::TerrainEditWindow* editor::App::getTerrainEditWindow() const{
    return terrainEditWindow;
}

void editor::App::processNextSaveDialog() {
    // Check if there's anything to process and no dialogs are currently open
    if (saveDialogQueue.empty() || sceneSaveDialog.isOpen() || projectSaveDialog.isOpen()) {
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
            saveDialogQueue.pop();
            processNextSaveDialog();
            return;
        }

        // Set default filename
        std::string defaultName = sceneProject->name + ".scene";

        // Open dialog for the current scene
        sceneSaveDialog.open(
            project.getProjectPath(), 
            defaultName,
            // Save callback
            [this, sceneId, completionCallback](const fs::path& fullPath) {
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
                        project.saveSceneToPath(sceneId, fullPath);
                    }
                }

                // Remove this item from the queue
                saveDialogQueue.pop();

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
                saveDialogQueue.pop();

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
                saveDialogQueue.pop();

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
                saveDialogQueue.pop();

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

void editor::App::initializeSettings() {
    AppSettings::initialize();
}

int editor::App::getInitialWindowWidth() const {
    return AppSettings::getWindowWidth();
}

int editor::App::getInitialWindowHeight() const {
    return AppSettings::getWindowHeight();
}

bool editor::App::getInitialWindowMaximized() const {
    return AppSettings::getIsMaximized();
}

void editor::App::saveWindowSettings(int width, int height, bool maximized) {
    AppSettings::setWindowWidth(width);
    AppSettings::setWindowHeight(height);
    AppSettings::setIsMaximized(maximized);
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

    if (project.hasScenesUnsavedChanges() || codeEditor->hasUnsavedChanges() || project.isTempUnsavedProject()) {
        registerThreeButtonAlert(
            "Unsaved Changes",
            "There are unsaved changes. Do you want to save them before exiting?",
            [this]() {
                // Yes callback - save all and exit when done
                saveAllFunc();
                project.saveProject(true,
                    [this]() {
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
    // Stop all playing scenes before shutdown to properly cleanup script instances
    for (auto& sceneProject : project.getScenes()) {
        if (sceneProject.playState == ScenePlayState::PLAYING || 
            sceneProject.playState == ScenePlayState::PAUSED) {
            project.stop(sceneProject.id);
        }
    }

    project.waitForPlaySessionToFinish();

    project.clearTrash();

    editor::ShaderBuilder::requestShutdown();
    Backend::closeWindow();
}