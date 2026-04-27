#include "ExportWindow.h"
#include "util/FileDialogs.h"
#include "Backend.h"
#include "window/Widgets.h"
#include "external/IconsFontAwesome6.h"
#include "Out.h"

namespace doriax::editor {

void ExportWindow::open(Project* project) {
    m_isOpen = true;
    m_exporting = false;
    m_project = project;
    m_targetDir.clear();
    m_assetsDir = project->getAssetsDir();
    m_luaDir = project->getLuaDir();
    m_targetDirBuffer[0] = '\0';
    strncpy(m_assetsDirBuffer, m_assetsDir.string().c_str(), sizeof(m_assetsDirBuffer) - 1);
    m_assetsDirBuffer[sizeof(m_assetsDirBuffer) - 1] = '\0';
    strncpy(m_luaDirBuffer, m_luaDir.string().c_str(), sizeof(m_luaDirBuffer) - 1);
    m_luaDirBuffer[sizeof(m_luaDirBuffer) - 1] = '\0';
    m_startSceneIndex = 0;
    uint32_t savedStartSceneId = project->getStartSceneId();
    const auto& startScenes = project->getScenes();
    for (int i = 0; i < (int)startScenes.size(); i++) {
        if (startScenes[i].id == savedStartSceneId) {
            m_startSceneIndex = i;
            break;
        }
    }
    m_selectedShaderIndex = -1;
    m_addShaderOpen = false;

    populateShaderList();
    populatePlatformList();
}

void ExportWindow::populateShaderList() {
    m_shaderEntries.clear();

    if (!m_project) return;

    // Collect all shaderKeys from all scenes (pre-populated list)
    std::set<ShaderKey> addedKeys;
    for (const auto& scene : m_project->getScenes()) {
        for (const auto& key : scene.shaderKeys) {
            if (addedKeys.count(key)) continue;
            addedKeys.insert(key);

            ShaderType type = ShaderPool::getShaderTypeFromKey(key);
            uint32_t props = ShaderPool::getPropertiesFromKey(key);

            ShaderEntry entry;
            entry.key = key;
            entry.type = type;
            entry.properties = props;
            entry.displayName = Exporter::getShaderDisplayName(type, props);
            m_shaderEntries.push_back(entry);
        }
    }

    // Sort by type then properties
    std::sort(m_shaderEntries.begin(), m_shaderEntries.end(), [](const ShaderEntry& a, const ShaderEntry& b) {
        if (a.type != b.type) return (int)a.type < (int)b.type;
        return a.properties < b.properties;
    });
}

void ExportWindow::populatePlatformList() {
    m_platformEntries.clear();

    Platform platforms[] = { Platform::Linux, Platform::Windows, Platform::MacOS, Platform::iOS, Platform::Web, Platform::Android };

    for (Platform p : platforms) {
        PlatformEntry entry;
        entry.platform = p;
        entry.name = Exporter::getPlatformName(p);
        // Default: select current OS
        #if defined(__linux__)
        entry.selected = (p == Platform::Linux);
        #elif defined(_WIN32)
        entry.selected = (p == Platform::Windows);
        #elif defined(__APPLE__)
        entry.selected = (p == Platform::MacOS);
        #else
        entry.selected = false;
        #endif
        m_platformEntries.push_back(entry);
    }
}

void ExportWindow::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Export Project##ExportModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(550, 0),
        ImVec2(550, ImGui::GetMainViewport()->WorkSize.y * 0.9f)
    );

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal |
                             ImGuiWindowFlags_AlwaysAutoResize;

    bool popupOpen = ImGui::BeginPopupModal("Export Project##ExportModal", &m_isOpen, flags);

    if (popupOpen) {
        if (!m_isOpen) {
            ImGui::CloseCurrentPopup();
        } else if (m_exporting) {
            drawProgress();
        } else {
            drawSettings();
        }
        ImGui::EndPopup();
    }
}

void ExportWindow::drawSettings() {
    float tableWidth = ImGui::GetContentRegionAvail().x;

    // --- Target Directory ---
    ImGui::PushItemWidth(-1);
    ImGui::BeginTable("export_settings", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    // Target directory row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Target Directory");
    ImGui::TableNextColumn();
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        Widgets::pathDisplay("##TargetPath", m_targetDir, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##target")) {
            std::string homeDirPath;
            #ifdef _WIN32
            homeDirPath = std::filesystem::path(getenv("USERPROFILE")).string();
            #else
            homeDirPath = std::filesystem::path(getenv("HOME")).string();
            #endif

            std::string selectedPath = FileDialogs::openFileDialog(homeDirPath, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                m_targetDir = selectedPath;
                strncpy(m_targetDirBuffer, selectedPath.c_str(), sizeof(m_targetDirBuffer) - 1);
                m_targetDirBuffer[sizeof(m_targetDirBuffer) - 1] = '\0';
            }
        }
    }

    // Assets directory row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Assets Directory");
    ImGui::TableNextColumn();
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        fs::path assetsDisplay = (m_assetsDir.empty() || m_assetsDir == ".") ? fs::path("<Project root>") : m_assetsDir;
        Widgets::pathDisplay("##AssetsPath", assetsDisplay, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##assets")) {
            std::string defaultPath = m_project ? m_project->getProjectPath().string() : "";
            std::string selectedPath = FileDialogs::openFileDialog(defaultPath, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                std::error_code ec;
                fs::path relPath = fs::relative(fs::path(selectedPath), m_project->getProjectPath(), ec);
                m_assetsDir = (ec || relPath.empty()) ? fs::path(".") : relPath;
                strncpy(m_assetsDirBuffer, m_assetsDir.string().c_str(), sizeof(m_assetsDirBuffer) - 1);
                m_assetsDirBuffer[sizeof(m_assetsDirBuffer) - 1] = '\0';
            }
        }
    }

    // Lua directory row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Lua Directory");
    ImGui::TableNextColumn();
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        fs::path luaDisplay = (m_luaDir.empty() || m_luaDir == ".") ? fs::path("<Project root>") : m_luaDir;
        Widgets::pathDisplay("##LuaPath", luaDisplay, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##lua")) {
            std::string defaultPath = m_project ? m_project->getProjectPath().string() : "";
            std::string selectedPath = FileDialogs::openFileDialog(defaultPath, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                std::error_code ec;
                fs::path relPath = fs::relative(fs::path(selectedPath), m_project->getProjectPath(), ec);
                m_luaDir = (ec || relPath.empty()) ? fs::path(selectedPath) : relPath;
                strncpy(m_luaDirBuffer, m_luaDir.string().c_str(), sizeof(m_luaDirBuffer) - 1);
                m_luaDirBuffer[sizeof(m_luaDirBuffer) - 1] = '\0';
            }
        }
    }

    // Start scene row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Start Scene");
    ImGui::TableNextColumn();
    {
        const auto& scenes = m_project->getScenes();
        if (!scenes.empty()) {
            if (m_startSceneIndex < 0 || m_startSceneIndex >= (int)scenes.size()) {
                m_startSceneIndex = 0;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##StartScene", scenes[m_startSceneIndex].name.c_str())) {
                for (int i = 0; i < (int)scenes.size(); i++) {
                    bool isSelected = (m_startSceneIndex == i);
                    if (ImGui::Selectable(scenes[i].name.c_str(), isSelected)) {
                        m_startSceneIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    ImGui::EndTable();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Shader Selection ---
    ImGui::Text(ICON_FA_WAND_MAGIC_SPARKLES "  Shaders");
    ImGui::SameLine();

    // Align Add/Delete buttons to the right
    float buttonsGroupWidth = ImGui::CalcTextSize(ICON_FA_PLUS " Add").x + ImGui::CalcTextSize(ICON_FA_TRASH " Delete").x + ImGui::GetStyle().ItemSpacing.x * 2 + 30;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonsGroupWidth);

    if (ImGui::Button(ICON_FA_PLUS " Add")) {
        m_addShaderOpen = true;
        m_addShaderTypeIndex = 0;
        memset(m_addShaderProps, 0, sizeof(m_addShaderProps));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectedShaderIndex < 0 || m_selectedShaderIndex >= (int)m_shaderEntries.size());
    if (ImGui::Button(ICON_FA_TRASH " Delete")) {
        m_shaderEntries.erase(m_shaderEntries.begin() + m_selectedShaderIndex);
        m_selectedShaderIndex = -1;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    float shaderListHeight = 160;
    ImGui::BeginChild("ShaderList", ImVec2(0, shaderListHeight), true);
    {
        for (int i = 0; i < (int)m_shaderEntries.size(); i++) {
            const auto& entry = m_shaderEntries[i];
            bool isSelected = (m_selectedShaderIndex == i);

            if (ImGui::Selectable(entry.displayName.c_str(), isSelected)) {
                m_selectedShaderIndex = i;
            }
        }
    }
    ImGui::EndChild();

    drawAddShaderDialog();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Platform Selection ---
    ImGui::Text(ICON_FA_DESKTOP "  Platforms");
    ImGui::Spacing();

    if (ImGui::BeginTable("platforms_table", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("PlatformCol1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("PlatformCol2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("PlatformCol3", ImGuiTableColumnFlags_WidthStretch);
        for (auto& entry : m_platformEntries) {
            ImGui::TableNextColumn();
            std::string checkboxId = "##platform_" + entry.name;
            ImGui::Checkbox((entry.name + checkboxId).c_str(), &entry.selected);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Validation warnings ---
    bool canExport = !m_targetDir.empty();
    std::error_code ec;
    bool targetExists = !m_targetDir.empty() && fs::exists(m_targetDir, ec);
    bool targetNotEmpty = targetExists && !fs::is_empty(m_targetDir, ec);

    if (targetNotEmpty) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Target directory is not empty!");
        canExport = false;
    }

    if (m_shaderEntries.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No shaders in list");
    }

    bool hasSelectedPlatforms = false;
    for (const auto& entry : m_platformEntries) {
        if (entry.selected) { hasSelectedPlatforms = true; break; }
    }
    if (!hasSelectedPlatforms) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No platforms selected");
        canExport = false;
    }

    // --- Buttons ---
    float windowWidth = ImGui::GetWindowSize().x;
    float buttonsWidth = 250;
    ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

    ImGui::BeginDisabled(!canExport);
    if (ImGui::Button("Export", ImVec2(120, 0))) {
        // Build config
        ExportConfig exportConfig;
        exportConfig.targetDir = m_targetDir;
        exportConfig.assetsDir = m_project->getProjectPath() / m_assetsDir;
        if (!m_luaDir.empty()) {
            exportConfig.luaDir = m_project->getProjectPath() / m_luaDir;
        }

        // Set start scene
        const auto& scenes = m_project->getScenes();
        if (m_startSceneIndex >= 0 && m_startSceneIndex < (int)scenes.size()) {
            exportConfig.startSceneId = scenes[m_startSceneIndex].id;
            m_project->setStartSceneId(exportConfig.startSceneId);
        }

        for (const auto& entry : m_shaderEntries) {
            exportConfig.selectedShaderKeys.insert(entry.key);
        }

        for (const auto& entry : m_platformEntries) {
            if (entry.selected) {
                exportConfig.selectedPlatforms.insert(entry.platform);
            }
        }

        m_exporting = true;
        m_exporter.startExport(m_project, exportConfig);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }
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
        // Shader type combo
        const char* typeNames[] = { "Points", "Lines", "Mesh", "Skybox", "Depth", "UI" };
        ShaderType typeValues[] = { ShaderType::POINTS, ShaderType::LINES, ShaderType::MESH, ShaderType::SKYBOX, ShaderType::DEPTH, ShaderType::UI };
        int typeCount = 6;

        ImGui::Text("Type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##shader_type", &m_addShaderTypeIndex, typeNames, typeCount)) {
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
            if (entry.key == newKey) { isDuplicate = true; break; }
        }
        if (isDuplicate) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "This shader already exists in the list");
        }

        // Buttons
        float windowWidth = ImGui::GetWindowSize().x;
        float buttonsWidth = 250;
        ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

        ImGui::BeginDisabled(isDuplicate);
        if (ImGui::Button("Add", ImVec2(120, 0))) {
            ShaderEntry entry;
            entry.key = newKey;
            entry.type = selectedType;
            entry.properties = props;
            entry.displayName = preview;
            m_shaderEntries.push_back(entry);

            // Re-sort
            std::sort(m_shaderEntries.begin(), m_shaderEntries.end(), [](const ShaderEntry& a, const ShaderEntry& b) {
                if (a.type != b.type) return (int)a.type < (int)b.type;
                return a.properties < b.properties;
            });

            m_addShaderOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
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

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::Text("Exporting project...");
    ImGui::Spacing();

    ImGui::ProgressBar(progress.overallProgress, ImVec2(-1.0f, 0.0f));

    ImGui::Spacing();
    ImGui::Text("%s", progress.currentStep.c_str());
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    if (progress.failed) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_FA_CIRCLE_XMARK " Export failed: %s", progress.errorMessage.c_str());
        ImGui::Spacing();

        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - 120) * 0.5f);
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_isOpen = false;
            m_exporting = false;
            ImGui::CloseCurrentPopup();
        }
    } else if (progress.finished) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_FA_CIRCLE_CHECK " Export completed successfully!");
        ImGui::Spacing();

        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - 120) * 0.5f);
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_isOpen = false;
            m_exporting = false;
            ImGui::CloseCurrentPopup();
        }
    }
}

} // namespace doriax::editor
