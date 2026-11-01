// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ExportWindow.h"
#include "util/FileDialogs.h"
#include "AppSettings.h"
#include "Backend.h"
#include "Generator.h"
#include "pool/ShaderPool.h"
#include "window/Widgets.h"
#include "external/IconsFontAwesome6.h"
#include "Out.h"
#include "Theme.h"

namespace doriax::editor {

namespace {
    // Desktop exports build for the current host, so only expose backends
    // supported by that host's standalone CMake configuration.
    #if defined(_WIN32)
    constexpr ShaderBackend desktopGraphicBackends[] = {
        ShaderBackend::GLCore, ShaderBackend::Vulkan, ShaderBackend::D3D11
    };
    #elif defined(__APPLE__)
    // Vulkan is left out: MoltenVK has no VK_EXT_descriptor_buffer, which the
    // renderer binds through, so an export would build and then fail at startup
    constexpr ShaderBackend desktopGraphicBackends[] = {
        ShaderBackend::MetalMacOS, ShaderBackend::GLCore
    };
    #else
    constexpr ShaderBackend desktopGraphicBackends[] = {
        ShaderBackend::GLCore, ShaderBackend::Vulkan
    };
    #endif

    constexpr int desktopGraphicBackendCount =
        static_cast<int>(sizeof(desktopGraphicBackends) / sizeof(desktopGraphicBackends[0]));

    float textButtonWidth(const char* label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    void beginSettingsRow(const char* label) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
    }

    bool isHostBackend(ShaderBackend backend) {
        for (ShaderBackend hostBackend : desktopGraphicBackends) {
            if (hostBackend == backend) return true;
        }
        return false;
    }
}

void ExportWindow::open(Project* project) {
    m_isOpen = true;
    m_step = Step::ModeSelect;
    if (m_project != project) m_projectSettingsDirty = false;
    m_project = project;
    m_targetDir.clear();
    m_targetDirFromDefault = false;
    m_targetDirBuffer[0] = '\0';
    m_startSceneId = project->getStartSceneId();
    const SceneProject* startScene = project->getScene(m_startSceneId);
    if (!startScene || startScene->filepath.empty()) {
        m_startSceneId = NULL_PROJECT_SCENE;
        for (const auto& scene : project->getScenes()) {
            if (!scene.filepath.empty()) {
                m_startSceneId = scene.id;
                break;
            }
        }
    }
    m_selectedShaderIndex = -1;
    m_shaderAdditions.clear();
    m_shaderExclusions.clear();
    m_addShaderOpen = false;
    m_emsdkOverride.clear();
    m_emsdkInfo = EmsdkInfo();
    m_missingBuildTools.clear();
    m_graphicBackendIndex = 0;
    m_sourceBackendsConfigured = false;
    m_desktopBackendConfigured = false;

    populateBackendList();
}

void ExportWindow::populateShaderList() {
    m_shaderEntries.clear();

    if (!m_project) return;

    // Collect all shaderKeys from all scenes (pre-populated list). The stored
    // keys are refreshed on scene save, so union them with a live collection —
    // otherwise a component added since the last save (e.g. a shadow-casting
    // Light2D) would be missing here and from the export.
    std::set<ShaderKey> addedKeys;
    for (const auto& scene : m_project->getScenes()) {
        std::set<ShaderKey> sceneKeys = scene.shaderKeys;
        m_project->collectSceneShaderKeys(&scene, sceneKeys);
        for (const auto& key : sceneKeys) {
            if (addedKeys.count(key)) continue;
            addedKeys.insert(key);

            ShaderType type = ShaderPool::getShaderTypeFromKey(key);
            uint32_t props = ShaderPool::getPropertiesFromKey(key);
            uint16_t customId = ShaderPool::getCustomIdFromKey(key);

            ShaderEntry entry;
            entry.key = key;
            entry.type = type;
            entry.properties = props;
            entry.displayName = Exporter::getShaderDisplayName(type, props, customId);
            m_shaderEntries.push_back(entry);
        }
    }

    // Sort by type then properties
    std::sort(m_shaderEntries.begin(), m_shaderEntries.end(), [](const ShaderEntry& a, const ShaderEntry& b) {
        if (a.type != b.type) return (int)a.type < (int)b.type;
        return a.properties < b.properties;
    });
}

void ExportWindow::populateShaderListFromKeys(const std::vector<ShaderKey>& shaderKeys) {
    m_shaderEntries.clear();

    for (ShaderKey key : shaderKeys) {
        ShaderKey normalizedKey = ShaderPool::normalizeKey(key);

        ShaderEntry entry;
        entry.key = normalizedKey;
        entry.type = ShaderPool::getShaderTypeFromKey(normalizedKey);
        entry.properties = ShaderPool::getPropertiesFromKey(normalizedKey);
        uint16_t customId = ShaderPool::getCustomIdFromKey(normalizedKey);
        entry.displayName = Exporter::getShaderDisplayName(entry.type, entry.properties, customId);
        m_shaderEntries.push_back(entry);
    }

    std::sort(m_shaderEntries.begin(), m_shaderEntries.end(), [](const ShaderEntry& a, const ShaderEntry& b) {
        if (a.type != b.type) return (int)a.type < (int)b.type;
        return a.properties < b.properties;
    });
}

const char* ExportWindow::localExportMode() const {
    return m_mode == ExportMode::SourceCode ? "sourceCode" : m_mode == ExportMode::Desktop ? "desktop" : "web";
}

void ExportWindow::refreshShaderSelection() {
    populateShaderList();
    std::set<ShaderKey> sceneKeys;
    for (const auto& entry : m_shaderEntries) sceneKeys.insert(ShaderPool::normalizeKey(entry.key));
    std::set<ShaderKey> keys = sceneKeys;
    keys.insert(m_shaderAdditions.begin(), m_shaderAdditions.end());
    keys.insert(m_shaderExclusions.begin(), m_shaderExclusions.end());
    populateShaderListFromKeys(std::vector<ShaderKey>(keys.begin(), keys.end()));
    for (auto& entry : m_shaderEntries) entry.fromScene = sceneKeys.count(entry.key) > 0;
    m_selectedShaderIndex = -1;
}

void ExportWindow::includeShader(ShaderKey key) {
    m_shaderExclusions.erase(key);
    const bool fromScene = std::any_of(m_shaderEntries.begin(), m_shaderEntries.end(), [key](const ShaderEntry& entry) {
        return entry.key == key && entry.fromScene;
    });
    if (!fromScene) m_shaderAdditions.insert(key);
    refreshShaderSelection();
    saveCurrentSettingsToProject();
}

void ExportWindow::populateBackendList() {
    m_backendEntries.clear();

    for (ShaderBackend backend : ShaderPool::getShaderBackends()) {
        BackendEntry entry;
        entry.backend = backend;
        entry.name = ShaderPool::getShaderBackendName(backend);
        entry.selected = isHostBackend(backend);
        m_backendEntries.push_back(entry);
    }
}

void ExportWindow::refreshEmsdkStatus() {
    m_emsdkInfo = Exporter::detectEmsdk(m_emsdkOverride);
}

void ExportWindow::selectMode(ExportMode mode) {
    m_mode = mode;
    loadSettingsFromProject();
    // Both probes spawn processes, so run them once here rather than per-frame.
    if (mode == ExportMode::Desktop) {
        m_missingBuildTools = Generator::checkBuildTools();
    } else if (mode == ExportMode::Web) {
        refreshEmsdkStatus();
    }
    m_step = Step::Settings;
}

void ExportWindow::loadSettingsFromProject() {
    if (!m_project) return;

    m_targetDir.clear();
    m_targetDirFromDefault = false;
    m_targetDirBuffer[0] = '\0';
    m_selectedShaderIndex = -1;
    m_sourceBackendsConfigured = false;
    m_selectSourceBackendTab = true;
    m_sourcePlatformWindows = false;
    m_sourcePlatformLinux = false;
    m_sourcePlatformMacOS = false;
    m_sourcePlatformIOS = false;
    m_sourcePlatformAndroid = false;
    m_sourcePlatformWeb = false;
    m_sourcePresetBackends.clear();
    m_desktopBackendConfigured = false;

    const ShaderOverrides& shaders = m_project->getShaderOverrides();
    m_shaderAdditions = shaders.additions;
    m_shaderExclusions = shaders.exclusions;
    refreshShaderSelection();

    if (m_mode == ExportMode::SourceCode) {
        const SourceCodeExportSettings& settings = m_project->getSourceCodeExportSettings();
        populateBackendList();
        m_sourceBackendsConfigured = settings.graphicBackendsConfigured;
        if (!settings.graphicBackendsConfigured) {
            #if defined(_WIN32)
            m_sourcePlatformWindows = true;
            #elif defined(__APPLE__)
            m_sourcePlatformMacOS = true;
            #else
            m_sourcePlatformLinux = true;
            #endif
        }
        auto addPresetBackends = [&](std::initializer_list<ShaderBackend> backends) {
            m_sourcePresetBackends.insert(backends.begin(), backends.end());
        };
        if (m_sourcePlatformWindows) addPresetBackends({ShaderBackend::GLCore, ShaderBackend::Vulkan, ShaderBackend::D3D11});
        if (m_sourcePlatformLinux) addPresetBackends({ShaderBackend::GLCore, ShaderBackend::Vulkan});
        if (m_sourcePlatformMacOS) addPresetBackends({ShaderBackend::MetalMacOS, ShaderBackend::GLCore});
        if (m_sourcePlatformIOS) addPresetBackends({ShaderBackend::MetalIOS});
        // Android builds only support OpenGL ES 3
        if (m_sourcePlatformAndroid) addPresetBackends({ShaderBackend::GLES3});
        if (m_sourcePlatformWeb) addPresetBackends({ShaderBackend::GLES3});
        if (settings.graphicBackendsConfigured) {
            for (auto& entry : m_backendEntries) {
                entry.selected = settings.graphicBackends.count(entry.backend) > 0;
            }
        }
    } else if (m_mode == ExportMode::Desktop) {
        const DesktopExportSettings& settings = m_project->getDesktopExportSettings();
        m_graphicBackendIndex = 0;
        m_desktopBackendConfigured = settings.graphicBackendConfigured;
        if (settings.graphicBackendConfigured) {
            for (int i = 0; i < desktopGraphicBackendCount; ++i) {
                if (desktopGraphicBackends[i] == settings.graphicBackend) {
                    m_graphicBackendIndex = i;
                    break;
                }
            }
        }
    } else if (m_mode == ExportMode::Web) {
        m_emsdkOverride = AppSettings::getEmsdkPath();
    }

    const auto file = m_project->getProjectPath() / "project.yaml";
    m_targetDir = AppSettings::getExportTargetDir(file, localExportMode());

    m_buildSettings = AppSettings::getBuildSettings(file);
    const unsigned int buildJobs = m_buildSettings.buildJobs;
    m_desktopBuildJobs = static_cast<int>(std::min(
        buildJobs ? buildJobs : Generator::getAutomaticParallelBuildJobs(), Generator::MAX_SUPPORTED_PARALLEL_BUILD_JOBS));
    if (m_targetDir.empty()) {
        m_targetDir = AppSettings::getDefaultExportDirectory();
        m_targetDirFromDefault = !m_targetDir.empty();
    }

    if (!m_targetDir.empty()) {
        std::string targetDirString = m_targetDir.string();
        strncpy(m_targetDirBuffer, targetDirString.c_str(), sizeof(m_targetDirBuffer) - 1);
        m_targetDirBuffer[sizeof(m_targetDirBuffer) - 1] = '\0';
    }
}

bool ExportWindow::saveCurrentSettingsToProject(bool saveProjectFile) {
    if (!m_project) return false;

    const auto file = m_project->getProjectPath() / "project.yaml";
    // A directory taken from the editor-wide default is not the project's own
    fs::path targetDir = m_targetDirFromDefault ? AppSettings::getExportTargetDir(file, localExportMode()) : m_targetDir;
    if (!AppSettings::setExportTargetDir(file, localExportMode(), targetDir)) {
        Out::error("Could not save local export settings.");
        return false;
    }

    if (m_mode == ExportMode::Desktop) {
        // Only the job count belongs to this dialog, so the rest is re-read: the
        // compiler may have changed in Editor Settings since the mode was selected
        const unsigned int jobs = static_cast<unsigned int>(m_desktopBuildJobs);
        m_buildSettings = AppSettings::getBuildSettings(file);
        m_buildSettings.buildJobs = jobs == Generator::getAutomaticParallelBuildJobs() ? 0 : jobs;
        if (!AppSettings::setBuildSettings(file, m_buildSettings)) {
            Out::error("Could not save local build settings.");
            return false;
        }
    }

    bool changed = false;
    if (m_mode == ExportMode::SourceCode && m_sourceBackendsConfigured) {
        auto& settings = m_project->getSourceCodeExportSettings();
        std::set<ShaderBackend> backends;
        for (const auto& entry : m_backendEntries) if (entry.selected) backends.insert(entry.backend);
        changed = !settings.graphicBackendsConfigured || settings.graphicBackends != backends;
        settings.graphicBackends = backends;
        settings.graphicBackendsConfigured = true;
    } else if (m_mode == ExportMode::Desktop && m_desktopBackendConfigured) {
        auto& settings = m_project->getDesktopExportSettings();
        const auto backend = desktopGraphicBackends[m_graphicBackendIndex];
        changed = !settings.graphicBackendConfigured || settings.graphicBackend != backend;
        settings.graphicBackend = backend;
        settings.graphicBackendConfigured = true;
    }

    ShaderOverrides& shaders = m_project->getShaderOverrides();
    changed = changed || shaders.additions != m_shaderAdditions || shaders.exclusions != m_shaderExclusions;
    shaders.additions = m_shaderAdditions;
    shaders.exclusions = m_shaderExclusions;
    m_projectSettingsDirty = m_projectSettingsDirty || changed;
    if (saveProjectFile && m_projectSettingsDirty) {
        if (!m_project->saveProjectFile()) {
            Out::error("Could not save project export settings.");
            return false;
        }
        m_projectSettingsDirty = false;
    }
    return true;
}

void ExportWindow::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Export Project##ExportModal");

    float width = Theme::dpi((m_step == Step::ModeSelect) ? 640.0f : 550.0f);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(width, 0),
        ImVec2(width, ImGui::GetMainViewport()->WorkSize.y * 0.9f)
    );

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal |
                             ImGuiWindowFlags_AlwaysAutoResize;

    bool popupOpen = ImGui::BeginPopupModal("Export Project##ExportModal", &m_isOpen, flags);

    if (popupOpen) {
        if (!m_isOpen) {
            // Closed via the title-bar X: kill a still-running export first.
            if (m_step == Step::Progress && m_exporter.isRunning()) {
                m_exporter.cancelExport();
            }
            ImGui::CloseCurrentPopup();
        } else if (m_step == Step::Progress) {
            drawProgress();
        } else if (m_step == Step::Settings) {
            drawSettings();
        } else {
            drawModeSelect();
        }
        ImGui::EndPopup();
    }
}

bool ExportWindow::drawModeCard(const char* id, const char* icon, const char* title, const char* description,
                                const ImVec2& size, const char* disabledText) {
    ImVec2 cardPos = ImGui::GetCursorPos();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.235f, 0.314f, 0.471f, 0.392f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.314f, 0.392f, 0.549f, 0.588f));
    bool clicked = ImGui::Button(id, size);
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);

    // NOTE: sizes derive from FontSizeBase, not GetFontSize() — passing the
    // latter to PushFont would apply the global font scale twice.
    const float baseSize = ImGui::GetStyle().FontSizeBase;
    const float padX = Theme::dpi(12.0f);

    // Large centered icon
    ImGui::PushFont(ImGui::GetFont(), baseSize * 2.6f);
    ImVec2 iconSize = ImGui::CalcTextSize(icon);
    ImGui::SetCursorPos(ImVec2(cardPos.x + (size.x - iconSize.x) * 0.5f, cardPos.y + Theme::dpi(22.0f)));
    ImGui::TextUnformatted(icon);
    ImGui::PopFont();

    // Title
    ImGui::PushFont(ImGui::GetFont(), baseSize * 1.15f);
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    float titleY = cardPos.y + Theme::dpi(22.0f) + iconSize.y + Theme::dpi(14.0f);
    ImGui::SetCursorPos(ImVec2(cardPos.x + (size.x - titleSize.x) * 0.5f, titleY));
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    // Description
    const float descriptionY = titleY + titleSize.y + Theme::dpi(8.0f);
    const float wrapWidth = size.x - padX * 2.0f;
    ImGui::SetCursorPos(ImVec2(cardPos.x + padX, descriptionY));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushTextWrapPos(cardPos.x + size.x - padX);
    ImGui::TextWrapped("%s", description);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    if (disabledText) {
        const float descriptionHeight = ImGui::CalcTextSize(description, nullptr, false, wrapWidth).y;
        ImGui::SetCursorPos(ImVec2(cardPos.x + padX, descriptionY + descriptionHeight + Theme::dpi(4.0f)));
        ImGui::PushTextWrapPos(cardPos.x + size.x - padX);
        ImGui::TextDisabled("%s", disabledText);
        ImGui::PopTextWrapPos();
    }

    // Re-anchor the layout cursor to the card rect so SameLine() places the
    // next card correctly after the overlay drawing above.
    ImGui::SetCursorPos(cardPos);
    ImGui::Dummy(size);

    return clicked;
}

void ExportWindow::drawModeSelect() {
    ImGui::Spacing();
    ImGui::TextDisabled("Choose how to export your project:");
    ImGui::Spacing();

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    const ImVec2 cardSize(cardWidth, Theme::dpi(185.0f));

    if (drawModeCard("##mode_source", ICON_FA_CODE, "Source Code",
                     "C++ engine source for every supported platform:", cardSize,
                     "Android \xc2\xb7 iOS \xc2\xb7 Web \xc2\xb7 Windows \xc2\xb7 Mac \xc2\xb7 Linux")) {
        selectMode(ExportMode::SourceCode);
    }
    ImGui::SameLine();
    if (drawModeCard("##mode_desktop", ICON_FA_DESKTOP, "Desktop",
                     "Build a ready-to-run native executable for this computer", cardSize)) {
        selectMode(ExportMode::Desktop);
    }
    ImGui::SameLine();
    if (drawModeCard("##mode_web", ICON_FA_GLOBE, "Web",
                     "Build an HTML and WebAssembly version using Emscripten", cardSize)) {
        selectMode(ExportMode::Web);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    float windowWidth = ImGui::GetWindowSize().x;
    float cancelWidth = Theme::dpi(120.0f);
    ImGui::SetCursorPosX((windowWidth - cancelWidth) * 0.5f);
    if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }
}

void ExportWindow::drawOutputDirRow(const char* label) {
    beginSettingsRow(label);
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        Widgets::pathDisplay("##TargetPath", m_targetDir, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##target")) {
            std::string homeDirPath;
            #ifdef _WIN32
            const char* homeDir = getenv("USERPROFILE");
            #else
            const char* homeDir = getenv("HOME");
            #endif
            if (homeDir) homeDirPath = std::filesystem::path(homeDir).string();

            std::string startDir = m_targetDir.empty() ? homeDirPath : m_targetDir.string();
            std::string selectedPath = FileDialogs::openFileDialog(startDir, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                m_targetDir = selectedPath;
                m_targetDirFromDefault = false;
                strncpy(m_targetDirBuffer, selectedPath.c_str(), sizeof(m_targetDirBuffer) - 1);
                m_targetDirBuffer[sizeof(m_targetDirBuffer) - 1] = '\0';
                saveCurrentSettingsToProject();
            }
        }
    }
}

void ExportWindow::drawStartSceneRow() {
    beginSettingsRow("Start Scene");
    {
        const auto& scenes = m_project->getScenes();
        const SceneProject* selectedScene = m_project->getScene(m_startSceneId);
        if (!selectedScene || selectedScene->filepath.empty()) {
            selectedScene = nullptr;
            m_startSceneId = NULL_PROJECT_SCENE;
            for (const auto& scene : scenes) {
                if (!scene.filepath.empty()) {
                    selectedScene = &scene;
                    m_startSceneId = scene.id;
                    break;
                }
            }
        }

        if (selectedScene) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##StartScene", selectedScene->name.c_str())) {
                for (const auto& scene : scenes) {
                    if (scene.filepath.empty()) continue;

                    bool isSelected = m_startSceneId == scene.id;
                    if (ImGui::Selectable(scene.name.c_str(), isSelected)) {
                        m_startSceneId = scene.id;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("No saved scenes");
        }
    }
}

void ExportWindow::drawGraphicBackendRow() {
    beginSettingsRow("Graphic Backend");

    if (m_mode == ExportMode::Web) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("WebGL 2 (OpenGL ES 3)");
        ImGui::SetItemTooltip("Web exports currently use the WebGL 2 backend");
        return;
    }

    if (m_graphicBackendIndex < 0 || m_graphicBackendIndex >= desktopGraphicBackendCount) {
        m_graphicBackendIndex = 0;
    }

    const std::string preview = ShaderPool::getShaderBackendName(desktopGraphicBackends[m_graphicBackendIndex]);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##GraphicBackend", preview.c_str())) {
        for (int i = 0; i < desktopGraphicBackendCount; ++i) {
            const bool selected = (m_graphicBackendIndex == i);
            if (ImGui::Selectable(ShaderPool::getShaderBackendName(desktopGraphicBackends[i]).c_str(), selected)) {
                m_graphicBackendIndex = i;
                m_desktopBackendConfigured = true;
                saveCurrentSettingsToProject();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void ExportWindow::drawDesktopKitRows() {
    const std::string& cCompiler = m_buildSettings.cCompiler;
    const std::string& cxxCompiler = m_buildSettings.cxxCompiler;
    const std::string& generator = m_buildSettings.generator;

    std::string kitDisplay;
    if (cCompiler.empty() && cxxCompiler.empty() && generator.empty()) {
        kitDisplay = "Default (system toolchain)";
    } else {
        kitDisplay = !cxxCompiler.empty() ? cxxCompiler : cCompiler;
        if (!generator.empty()) {
            kitDisplay += kitDisplay.empty() ? generator : " (" + generator + ")";
        }
    }

    beginSettingsRow("Compiler");
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", kitDisplay.c_str());
    ImGui::SetItemTooltip("Change in Editor Settings");

    beginSettingsRow("Build Jobs");
    ImGui::SetNextItemWidth(-1);
    const int previousJobs = m_desktopBuildJobs;
    if (ImGui::InputInt("##DesktopBuildJobs", &m_desktopBuildJobs, 1, 8)) {
        m_desktopBuildJobs = std::clamp(m_desktopBuildJobs, 1, static_cast<int>(Generator::MAX_SUPPORTED_PARALLEL_BUILD_JOBS));
        if (m_desktopBuildJobs != previousJobs) {
            saveCurrentSettingsToProject();
        }
    }
    ImGui::SetItemTooltip("Parallel build jobs. Default: %u (automatic maximum).", Generator::getAutomaticParallelBuildJobs());
}

void ExportWindow::drawShaderSection() {
    ImGui::Text(ICON_FA_WAND_MAGIC_SPARKLES "  Shaders");
    ImGui::SameLine();

    const char* addLabel = ICON_FA_PLUS " Add";
    const bool hasSelection = m_selectedShaderIndex >= 0 && m_selectedShaderIndex < (int)m_shaderEntries.size();
    const bool selectionExcluded = hasSelection && m_shaderExclusions.count(m_shaderEntries[m_selectedShaderIndex].key) > 0;
    const char* deleteLabel = selectionExcluded ? ICON_FA_ROTATE_LEFT " Restore" : ICON_FA_TRASH " Delete";
    float buttonsGroupWidth = textButtonWidth(addLabel) + textButtonWidth(deleteLabel) + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonsGroupWidth);

    if (ImGui::Button(addLabel)) {
        m_addShaderOpen = true;
        m_addShaderTypeIndex = 0;
        memset(m_addShaderProps, 0, sizeof(m_addShaderProps));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button(deleteLabel)) {
        const auto key = m_shaderEntries[m_selectedShaderIndex].key;
        if (selectionExcluded) {
            includeShader(key);
        } else {
            m_shaderAdditions.erase(key);
            m_shaderExclusions.insert(key);
            refreshShaderSelection();
            saveCurrentSettingsToProject();
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginChild("ShaderList", ImVec2(0, Theme::dpi(160.0f)), true);
    {
        for (int i = 0; i < (int)m_shaderEntries.size(); i++) {
            const auto& entry = m_shaderEntries[i];
            bool isSelected = (m_selectedShaderIndex == i);
            const bool isOverride = m_shaderAdditions.count(entry.key) > 0;
            const bool isExcluded = m_shaderExclusions.count(entry.key) > 0;
            const char* icon = isExcluded ? ICON_FA_TRASH : isOverride ? ICON_FA_CIRCLE_PLUS : ICON_FA_CUBES;
            const std::string label = std::string(icon) + "  " + entry.displayName +
                                      (isExcluded ? " (Excluded)" : "") + "###shader_" + std::to_string(entry.key);

            // Excluded shaders remain selectable for Restore, but use disabled text.
            if (isExcluded) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                m_selectedShaderIndex = i;
            }
            if (isExcluded) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", isExcluded ? "Excluded from the export. Select this shader and click Restore to include it again."
                                       : isOverride ? "Shader override: manually added to the export."
                                                    : "Collected from project scenes.");
        }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(m_shaderAdditions.empty() && m_shaderExclusions.empty());
    if (ImGui::Button("Reset Shader Overrides")) {
        m_shaderAdditions.clear();
        m_shaderExclusions.clear();
        refreshShaderSelection();
        saveCurrentSettingsToProject();
    }
    ImGui::EndDisabled();
    drawAddShaderDialog();
}

void ExportWindow::applySourcePlatformPresets() {
    std::set<ShaderBackend> required;
    auto add = [&](std::initializer_list<ShaderBackend> backends) {
        required.insert(backends.begin(), backends.end());
    };
    if (m_sourcePlatformWindows) add({ShaderBackend::GLCore, ShaderBackend::Vulkan, ShaderBackend::D3D11});
    if (m_sourcePlatformLinux) add({ShaderBackend::GLCore, ShaderBackend::Vulkan});
    if (m_sourcePlatformMacOS) add({ShaderBackend::MetalMacOS, ShaderBackend::GLCore});
    if (m_sourcePlatformIOS) add({ShaderBackend::MetalIOS});
    if (m_sourcePlatformAndroid) add({ShaderBackend::GLES3});
    if (m_sourcePlatformWeb) add({ShaderBackend::GLES3});

    for (auto& entry : m_backendEntries) {
        if (required.count(entry.backend) > 0) {
            // Preserve manual choices for backends already covered by a preset.
            if (m_sourcePresetBackends.count(entry.backend) == 0) {
                entry.selected = true;
            }
        } else if (m_sourcePresetBackends.count(entry.backend) > 0) {
            entry.selected = false;
        }
    }
    m_sourcePresetBackends = required;
    m_sourceBackendsConfigured = true;
}

void ExportWindow::drawBackendSection() {
    ImGui::Text(ICON_FA_MICROCHIP "  Graphic Backends");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted("Use platform presets to select compatible shader backends, "
                               "then fine-tune the selection in Custom Backends. "
                               "Switching tabs keeps your current selection. "
                               "The selected backends below are included in the export.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::Spacing();

    if (ImGui::BeginTabBar("source_backend_tabs")) {
        // Only backend choices are persisted, so show those directly when reloading.
        const bool selectPresets = m_selectSourceBackendTab && !m_sourceBackendsConfigured;
        const bool selectCustom = m_selectSourceBackendTab && m_sourceBackendsConfigured;
        m_selectSourceBackendTab = false;
        if (ImGui::BeginTabItem("Platform Presets", nullptr, selectPresets ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (ImGui::BeginTable("source_platforms_table", 3, ImGuiTableFlags_SizingStretchSame)) {
                auto platformCheck = [&](const char* label, bool& value) {
                    ImGui::TableNextColumn();
                    if (ImGui::Checkbox(label, &value)) {
                        applySourcePlatformPresets();
                        saveCurrentSettingsToProject();
                    }
                };
                platformCheck("Windows", m_sourcePlatformWindows);
                platformCheck("Linux", m_sourcePlatformLinux);
                platformCheck("macOS", m_sourcePlatformMacOS);
                platformCheck("iOS", m_sourcePlatformIOS);
                platformCheck("Android", m_sourcePlatformAndroid);
                platformCheck("Web", m_sourcePlatformWeb);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Custom Backends", nullptr, selectCustom ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (ImGui::BeginTable("backends_table", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("BackendCol1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("BackendCol2", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("BackendCol3", ImGuiTableColumnFlags_WidthStretch);
                for (auto& entry : m_backendEntries) {
                    ImGui::TableNextColumn();
                    std::string checkboxId = "##backend_" + entry.name;
                    if (ImGui::Checkbox((entry.name + checkboxId).c_str(), &entry.selected)) {
                        m_sourceBackendsConfigured = true;
                        saveCurrentSettingsToProject();
                    }
                    ImGui::SetItemTooltip("%s", ShaderPool::getShaderLangStr(entry.backend).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    std::string selectedBackends;
    for (const auto& entry : m_backendEntries) {
        if (!entry.selected) continue;
        if (!selectedBackends.empty()) selectedBackends += ", ";
        selectedBackends += entry.name;
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Selected backends: %s", selectedBackends.empty() ? "None" : selectedBackends.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void ExportWindow::drawSettings() {
    // Mode header
    const char* modeIcon = ICON_FA_CODE;
    const char* modeTitle = "Source Code Export";
    if (m_mode == ExportMode::Desktop) {
        modeIcon = ICON_FA_DESKTOP;
        modeTitle = "Desktop Export";
    } else if (m_mode == ExportMode::Web) {
        modeIcon = ICON_FA_GLOBE;
        modeTitle = "Web Export";
    }
    ImGui::Text("%s  %s", modeIcon, modeTitle);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushItemWidth(-1);
    ImGui::BeginTable("export_settings", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, Theme::dpi(140.0f));
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    drawOutputDirRow(m_mode == ExportMode::SourceCode ? "Target Directory" : "Destination Directory");
    drawStartSceneRow();
    if (m_mode == ExportMode::Desktop) {
        drawGraphicBackendRow();
        drawDesktopKitRows();
    } else if (m_mode == ExportMode::Web) {
        drawGraphicBackendRow();
    }

    ImGui::EndTable();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawShaderSection();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_mode == ExportMode::SourceCode) {
        drawBackendSection();
    }

    // --- Validation warnings ---
    bool canExport = !m_targetDir.empty();
    std::error_code ec;
    bool targetExists = !m_targetDir.empty() && fs::exists(m_targetDir, ec);
    bool targetNotEmpty = targetExists && !fs::is_empty(m_targetDir, ec);

    if (targetNotEmpty) {
        const char* dirLabel = (m_mode == ExportMode::SourceCode) ? "Target directory" : "Destination";
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s is not empty: existing files will be overwritten", dirLabel);
    }

    bool hasSavedScenes = false;
    for (const auto& scene : m_project->getScenes()) {
        if (!scene.filepath.empty()) {
            hasSavedScenes = true;
            break;
        }
    }

    if (!hasSavedScenes) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No saved scenes in project");
        canExport = false;
    } else if (std::none_of(m_shaderEntries.begin(), m_shaderEntries.end(), [this](const ShaderEntry& entry) {
        return m_shaderExclusions.count(entry.key) == 0;
    })) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No shaders selected for export");
    }

    if (m_mode == ExportMode::SourceCode) {
        bool hasSelectedBackends = false;
        for (const auto& entry : m_backendEntries) {
            if (entry.selected) { hasSelectedBackends = true; break; }
        }
        if (!hasSelectedBackends) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No graphic backends selected");
            canExport = false;
        }
    } else if (m_mode == ExportMode::Desktop) {
        if (!m_missingBuildTools.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Missing build tools:\n%s", m_missingBuildTools.c_str());
            ImGui::PopStyleColor();
            canExport = false;
        }
    } else if (m_mode == ExportMode::Web) {
        if (!m_emsdkInfo.found) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Emscripten SDK not found. Configure it in Editor Settings > Web.");
            ImGui::PopStyleColor();
            canExport = false;
        }
    }

    // --- Buttons ---
    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_ARROW_LEFT " Back")) {
        m_step = Step::ModeSelect;
    }

    ImGui::SameLine();
    const float actionWidth = Theme::dpi(120.0f);
    float buttonsWidth = actionWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonsWidth);

    ImGui::BeginDisabled(!canExport);
    if (ImGui::Button("Export", ImVec2(actionWidth, 0))) {
        if (targetNotEmpty) {
            ImGui::OpenPopup("Directory Not Empty##ExportOverwrite");
        } else {
            startConfiguredExport(false);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(actionWidth, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }

    drawOverwriteConfirmDialog();
}

void ExportWindow::drawOverwriteConfirmDialog() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(Theme::dpi(ImVec2(440.0f, 0.0f)), ImVec2(Theme::dpi(440.0f), FLT_MAX));

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    if (!ImGui::BeginPopupModal("Directory Not Empty##ExportOverwrite", nullptr, flags)) return;

    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " This directory already contains files:");
    ImGui::Spacing();

    Vector2 pathSize = Vector2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
    Widgets::pathDisplay("##OverwritePath", m_targetDir, pathSize);

    ImGui::Spacing();
    ImGui::PushTextWrapPos();
    ImGui::Text("Files with the same name will be overwritten.");
    if (m_mode == ExportMode::SourceCode) {
        ImGui::TextDisabled("Files left from an earlier export are not removed and may break the build.");
    }
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float windowWidth = ImGui::GetWindowSize().x;
    const float actionWidth = Theme::dpi(120.0f);
    float buttonsWidth = actionWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

    if (ImGui::Button("Overwrite", ImVec2(actionWidth, 0))) {
        ImGui::CloseCurrentPopup();
        startConfiguredExport(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(actionWidth, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ExportWindow::startConfiguredExport(bool overwriteTarget) {
    ExportConfig exportConfig;
    exportConfig.mode = m_mode;
    exportConfig.overwriteTarget = overwriteTarget;

    if (m_mode == ExportMode::SourceCode) {
        exportConfig.targetDir = m_targetDir;
        exportConfig.packNativeResources = m_project->shouldPackNativeResources();
        for (const auto& entry : m_backendEntries) {
            if (entry.selected) {
                exportConfig.selectedBackends.insert(entry.backend);
            }
        }
    } else {
        exportConfig.destinationDir = m_targetDir;

        // A built export runs on a single backend, driving both its shaders and its CMake
        ShaderBackend backend = ShaderBackend::GLES3;
        if (m_mode == ExportMode::Desktop) {
            const int backendIndex =
                (m_graphicBackendIndex >= 0 && m_graphicBackendIndex < desktopGraphicBackendCount)
                    ? m_graphicBackendIndex
                    : 0;
            backend = desktopGraphicBackends[backendIndex];
        }
        exportConfig.selectedBackends.insert(backend);
        exportConfig.graphicBackend = Exporter::getCMakeGraphicBackend(backend);

        if (m_mode == ExportMode::Desktop) {
            const LocalBuildSettings build = AppSettings::getBuildSettings(m_project->getProjectPath() / "project.yaml");
            exportConfig.cmakeCCompiler = build.cCompiler;
            exportConfig.cmakeCxxCompiler = build.cxxCompiler;
            exportConfig.cmakeGenerator = build.generator;
            exportConfig.buildJobs = static_cast<unsigned int>(m_desktopBuildJobs);
            exportConfig.packNativeResources = m_project->shouldPackNativeResources();
        } else {
            exportConfig.emsdkPath = m_emsdkOverride;
        }
    }

    // Stored references are relative to these roots, set in the project settings
    exportConfig.assetsDir = m_project->getAssetsPath();
    exportConfig.luaDir = m_project->getLuaPath();

    // Set start scene
    const SceneProject* startScene = m_project->getScene(m_startSceneId);
    if (startScene && !startScene->filepath.empty()) {
        exportConfig.startSceneId = startScene->id;
        m_projectSettingsDirty = m_projectSettingsDirty || m_project->getStartSceneId() != exportConfig.startSceneId;
        m_project->setStartSceneId(exportConfig.startSceneId);
    }

    // Only the overrides: the exporter resolves them once every scene is loaded,
    // so closed scenes still contribute and an exclusion cannot empty the list early
    exportConfig.shaderAdditions = m_shaderAdditions;
    exportConfig.shaderExclusions = m_shaderExclusions;

    if (!saveCurrentSettingsToProject()) return;
    m_step = Step::Progress;
    m_exporter.startExport(m_project, exportConfig);
}

void ExportWindow::drawAddShaderDialog() {
    if (!m_addShaderOpen) return;

    ImGui::OpenPopup("Add Shader##AddShaderModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    if (ImGui::BeginPopupModal("Add Shader##AddShaderModal", &m_addShaderOpen, flags)) {
        // Shader type combo. Keep in sync with the ShaderType enum
        // (engine/core/render/Render.h); names come from ShaderPool so the
        // labels never drift.
        static const ShaderType typeValues[] = {
            ShaderType::POINTS, ShaderType::LINES, ShaderType::MESH, ShaderType::SKYBOX,
            ShaderType::DEPTH, ShaderType::GBUFFER, ShaderType::UI,
            ShaderType::SSAO, ShaderType::SSAO_BLUR,
            ShaderType::SSR, ShaderType::SSR_BLUR, ShaderType::COMPOSITE,
            ShaderType::SHADOW2D, ShaderType::BLIT, ShaderType::POSTPROCESS,
            ShaderType::WATER, ShaderType::UNDERWATER, ShaderType::PROBE_FILTER
        };
        constexpr int typeCount = (int)(sizeof(typeValues) / sizeof(typeValues[0]));
        std::vector<std::string> typeNameStrs;
        std::vector<const char*> typeNames;
        for (ShaderType type : typeValues) {
            typeNameStrs.push_back(ShaderPool::getShaderTypeName(type, false));
        }
        for (const std::string& name : typeNameStrs) {
            typeNames.push_back(name.c_str());
        }

        ImGui::Text("Type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##shader_type", &m_addShaderTypeIndex, typeNames.data(), typeCount)) {
            // Reset properties when type changes
            memset(m_addShaderProps, 0, sizeof(m_addShaderProps));
        }

        // Property checkboxes
        ShaderType selectedType = typeValues[m_addShaderTypeIndex];
        int propCount = ShaderPool::getShaderPropertyCount(selectedType);

        if (propCount > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Properties:");
            ImGui::Spacing();

            // Determine column count based on properties
            int columns = 1;
            if (propCount >= 6) columns = 2;
            if (propCount >= 12) columns = 3;

            if (ImGui::BeginTable("shader_props_table", columns, ImGuiTableFlags_NoBordersInBody)) {
                for (int i = 0; i < propCount; i++) {
                    ImGui::TableNextColumn();
                    std::string abbrev = ShaderPool::getShaderPropertyName(selectedType, i, true);
                    std::string fullName = ShaderPool::getShaderPropertyName(selectedType, i, false);
                    std::string label = fullName + " (" + abbrev + ")##prop_" + std::to_string(i);
                    ImGui::Checkbox(label.c_str(), &m_addShaderProps[i]);
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Preview
        uint32_t props = 0;
        for (int i = 0; i < propCount; i++) {
            if (m_addShaderProps[i]) props |= (1 << i);
        }
        std::string preview = Exporter::getShaderDisplayName(selectedType, props);
        ImGui::Text("Preview: %s", preview.c_str());

        ImGui::Spacing();

        // Check for duplicate
        ShaderKey newKey = ShaderPool::getShaderKey(selectedType, props);
        bool isDuplicate = false;
        for (const auto& entry : m_shaderEntries) {
            if (entry.key == newKey && m_shaderExclusions.count(newKey) == 0) { isDuplicate = true; break; }
        }
        if (isDuplicate) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "This shader already exists in the list");
        }

        // Buttons
        float windowWidth = ImGui::GetWindowSize().x;
        const float actionWidth = Theme::dpi(120.0f);
        float buttonsWidth = actionWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

        ImGui::BeginDisabled(isDuplicate);
        if (ImGui::Button("Add", ImVec2(actionWidth, 0))) {
            includeShader(newKey);
            m_addShaderOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(actionWidth, 0))) {
            m_addShaderOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        m_addShaderOpen = false;
    }
}

void ExportWindow::drawProgress() {
    ExportProgress progress = m_exporter.getProgress();

    // The export thread is not input: without this the dialog would only advance on
    // mouse moves, and the finished layout would wait for one.
    if (!progress.finished && !progress.failed) {
        Backend::getApp().requestRedraw();
    }

    ImGui::Dummy(ImVec2(0.0f, Theme::dpi(6.0f)));
    ImGui::Text("Exporting project...");
    ImGui::Spacing();

    ImGui::ProgressBar(progress.overallProgress, ImVec2(-1.0f, 0.0f));

    ImGui::Spacing();
    ImGui::Text("%s", progress.currentStep.c_str());

    // Last build-output line, clipped to a single line so the modal keeps its size.
    // No background, as an empty one looked like a second progress bar.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##BuildDetail", ImVec2(0, ImGui::GetTextLineHeight() + Theme::dpi(2.0f)), false, ImGuiWindowFlags_NoScrollbar);
    if (!progress.detailLine.empty()) {
        ImGui::TextDisabled("%s", progress.detailLine.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, Theme::dpi(6.0f)));

    if (progress.failed) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped(ICON_FA_CIRCLE_XMARK " Export failed: %s", progress.errorMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        float windowWidth = ImGui::GetWindowSize().x;
        float closeWidth = Theme::dpi(120.0f);
        ImGui::SetCursorPosX((windowWidth - closeWidth) * 0.5f);
        if (ImGui::Button("Close", ImVec2(closeWidth, 0))) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
    } else if (progress.finished) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_FA_CIRCLE_CHECK " Export completed successfully!");
        ImGui::Spacing();

        float windowWidth = ImGui::GetWindowSize().x;
        float closeWidth = Theme::dpi(120.0f);
        ImGui::SetCursorPosX((windowWidth - closeWidth) * 0.5f);
        if (ImGui::Button("Close", ImVec2(closeWidth, 0))) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        // Still running: cancelling also terminates the compiler subprocess.
        float windowWidth = ImGui::GetWindowSize().x;
        float cancelWidth = Theme::dpi(120.0f);
        ImGui::SetCursorPosX((windowWidth - cancelWidth) * 0.5f);
        if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0))) {
            m_exporter.cancelExport();
        }
    }
}

} // namespace doriax::editor
