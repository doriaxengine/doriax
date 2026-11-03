// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "EditorSettingsWindow.h"

#include "AppSettings.h"
#include "App.h"
#include "Backend.h"
#include "Project.h"
#include "Theme.h"
#include "ai/McpServer.h"
#include "ai/SecretStore.h"
#include "external/IconsFontAwesome6.h"
#include "util/FileDialogs.h"
#include "util/UIUtils.h"
#include "window/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace doriax::editor {

namespace {
    namespace fs = std::filesystem;

    static constexpr float dialogWidth = 700.0f;
    static constexpr float dialogHeight = 620.0f;
    static constexpr float settingsLabelWidth = 190.0f;
    static constexpr float settingsPanelPadding = 12.0f;
    static constexpr float settingsButtonWidth = 120.0f;
    static constexpr ImGuiWindowFlags noScrollFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    const ImVec4 keySetColor(0.55f, 0.85f, 0.55f, 1.0f);

    // One scrolling child per tab, like the Project Settings panels
    bool beginSettingsPanel(const char* panelId) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(settingsPanelPadding, settingsPanelPadding)));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, Theme::dpi(6.0f)));
        return ImGui::BeginChild(panelId, ImVec2(0, 0), ImGuiChildFlags_Borders);
    }

    void endSettingsPanel() {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
    }

    bool beginSettingsTable(const char* tableId) {
        float labelWidth = std::min(Theme::dpi(settingsLabelWidth), ImGui::GetContentRegionAvail().x * 0.4f);
        if (!ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            return false;
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    template <typename DrawContents>
    void drawSettingsPanel(const char* panelId, DrawContents drawContents) {
        if (beginSettingsPanel(panelId) && beginSettingsTable("##EditorSettingsTable")) {
            drawContents();
            ImGui::EndTable();
        }
        endSettingsPanel();
    }

    // Section text that belongs outside the row table
    void drawSectionNote(const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    void drawSectionTitle(const char* title) {
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::SeparatorText(title);
        ImGui::PopStyleVar();
    }

    void setBuffer(char* buffer, size_t size, const std::string& value) {
        std::snprintf(buffer, size, "%s", value.c_str());
    }

    // Borderless dim icon after a label
    bool inlineIconButton(const char* icon, const std::string& id, const char* tooltip) {
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        bool clicked = ImGui::Button((std::string(icon) + "##" + id).c_str());
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::SetItemTooltip("%s", tooltip);
        return clicked;
    }

    // Read-only value styled like Widgets::pathDisplay; the button copies `copied`
    void drawCopyField(const char* id, const std::string& shown, const std::string& copied, const char* tooltip) {
        const float buttonSize = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(50, 50, 50, 255));
        ImGui::BeginChild(id, ImVec2(width, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        ImGui::TextUnformatted(shown.c_str());
        ImGui::EndChild();
        ImGui::SetItemTooltip("%s", shown.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (Widgets::iconButton((std::string(id) + "Copy").c_str(), ICON_FA_COPY, ImVec2(buttonSize, buttonSize))) {
            ImGui::SetClipboardText(copied.c_str());
        }
        ImGui::SetItemTooltip("%s", tooltip);
    }

    bool beginSettingsRow(const char* label, const char* tooltip = nullptr, bool defChanged = false) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip) ImGui::SetItemTooltip("%s", tooltip);

        bool reset = defChanged && inlineIconButton(ICON_FA_ROTATE_LEFT, label, "Restore default");

        ImGui::TableNextColumn();
        return reset;
    }

    void showDisabledItemTooltip(const std::string& text) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    float buttonVisibleWidth(const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    void drawPathPicker(
        const char* id,
        const char* browseId,
        const char* autoId,
        fs::path displayPath,
        bool canAuto,
        bool selectDirectory,
        const std::function<void(const std::string&)>& onBrowse,
        const std::function<void()>& onAuto
    ) {
        const ImGuiStyle& style = ImGui::GetStyle();
        float browseWidth = buttonVisibleWidth(browseId);
        float autoWidth = buttonVisibleWidth(autoId);
        float pathWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - browseWidth - autoWidth - style.ItemSpacing.x * 2.0f);

        Widgets::pathDisplay(id, displayPath, Vector2(pathWidth, ImGui::GetFrameHeight()));

        ImGui::SameLine();
        if (ImGui::Button(browseId)) {
            std::string startDir;
            if (!displayPath.empty() && displayPath.string().rfind("<", 0) != 0) {
                startDir = displayPath.parent_path().string();
            }
            std::string selectedPath = FileDialogs::openFileDialog(startDir, FILE_DIALOG_ALL, selectDirectory);
            if (!selectedPath.empty()) {
                onBrowse(selectedPath);
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!canAuto);
        if (ImGui::Button(autoId)) {
            onAuto();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Clear the override and use auto-detect again.");
    }
}

void EditorSettingsWindow::open(Project* project, std::optional<Tab> tab, bool expandAiChat) {
    m_isOpen = true;
    m_requestedTab = tab;
    m_expandAiChat = expandAiChat;
    m_project = project;
    m_cacheStatus.clear();

    m_kitsDetected = false;
    m_cmakeOverride = AppSettings::getCMakePath();
    m_cmakePickError.clear();

    m_emsdkDetected = false;
    m_emsdkOverride = AppSettings::getEmsdkPath();
    m_editorVSyncEnabled = AppSettings::getEditorVSyncEnabled();
    m_uiScalePercent = static_cast<int>(std::lround(AppSettings::getUiScale() * 100.0f));
    m_defaultExportDirectory = AppSettings::getDefaultExportDirectory();

    m_aiSettings = AppSettings::getAiSettings();
    m_aiKeys.clear();
    m_aiEndpoints.clear();
    m_removedEndpoints.clear();
    m_editingEndpointId.clear();
    m_focusEndpointLabel = false;
    syncAiBuffers();

    m_mcpSettings = AppSettings::getMcpSettings();
    m_mcpToken = ai::McpServer::storedToken();
}

void EditorSettingsWindow::detectKits() {
    m_availableKits = Generator::detectAvailableKits();
    m_cmakeKitIndex = 0;
    refreshCMakeStatus();

    const LocalBuildSettings currentBuild = projectBuildSettings();
    const std::string& currentCxx = currentBuild.cxxCompiler;
    const std::string& currentGen = currentBuild.generator;
    if (!currentCxx.empty() || !currentGen.empty()) {
        for (size_t i = 0; i < m_availableKits.size(); i++) {
            if (m_availableKits[i].available && m_availableKits[i].cxxCompiler == currentCxx && m_availableKits[i].generator == currentGen) {
                m_cmakeKitIndex = static_cast<int>(i + 1);
                break;
            }
        }
    }
    m_kitsDetected = true;
}

LocalBuildSettings EditorSettingsWindow::projectBuildSettings() const {
    if (!m_project) return LocalBuildSettings();
    return AppSettings::getBuildSettings(m_project->getProjectPath() / "project.yaml");
}

void EditorSettingsWindow::refreshCMakeStatus() {
    m_cmakeInfo = Generator::detectCMake();
}

void EditorSettingsWindow::refreshEmsdkStatus() {
    m_emsdkInfo = Exporter::detectEmsdk(m_emsdkOverride);
}

ImGuiTabItemFlags EditorSettingsWindow::tabFlags(Tab tab) const {
    return m_requestedTab == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
}

// Adds buffers for new rows and drops gone ones, keeping typed text intact.
void EditorSettingsWindow::syncAiBuffers() {
    std::map<std::string, KeyEntry> keys;
    for (const ai::ProviderAccount& account : ai::listAccounts(m_aiSettings)) {
        auto existing = m_aiKeys.find(account.id);
        keys[account.id] = existing != m_aiKeys.end() ? existing->second : KeyEntry{};
    }
    m_aiKeys = std::move(keys);

    std::map<std::string, EndpointEntry> endpoints;
    for (const ai::CustomEndpoint& endpoint : m_aiSettings.customEndpoints) {
        auto existing = m_aiEndpoints.find(endpoint.id);
        if (existing != m_aiEndpoints.end()) {
            endpoints[endpoint.id] = existing->second;
            continue;
        }
        EndpointEntry entry;
        setBuffer(entry.label.data(), entry.label.size(), endpoint.label);
        setBuffer(entry.url.data(), entry.url.size(), endpoint.url);
        endpoints[endpoint.id] = entry;
    }
    m_aiEndpoints = std::move(endpoints);

    refreshAiKeyState();
}

void EditorSettingsWindow::refreshAiKeyState() {
    for (auto& entry : m_aiKeys) {
        entry.second.configured = ai::SecretStore::hasApiKey(entry.first);
    }
}

void EditorSettingsWindow::addAiEndpoint(const std::string& label, const std::string& url) {
    ai::CustomEndpoint endpoint;
    endpoint.id = ai::makeEndpointId(m_aiSettings, label);
    endpoint.label = label;
    endpoint.url = url;
    m_aiSettings.customEndpoints.push_back(endpoint);
    syncAiBuffers();
}

void EditorSettingsWindow::removeAiEndpoint(const std::string& endpointId) {
    m_removedEndpoints.push_back(endpointId);
    auto& endpoints = m_aiSettings.customEndpoints;
    endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end(),
                                   [&](const ai::CustomEndpoint& endpoint) {
                                       return endpoint.id == endpointId;
                                   }),
                    endpoints.end());
    if (m_editingEndpointId == endpointId) {
        m_editingEndpointId.clear();
    }
    syncAiBuffers();
}

void EditorSettingsWindow::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Editor Settings##EditorSettingsModal");

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetWorkCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImVec2 size(
        std::min(Theme::dpi(dialogWidth), viewport->WorkSize.x * 0.9f),
        std::min(Theme::dpi(dialogHeight), viewport->WorkSize.y * 0.9f)
    );
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal |
                             ImGuiWindowFlags_NoResize |
                             noScrollFlags;

    bool popupOpen = ImGui::BeginPopupModal("Editor Settings##EditorSettingsModal", &m_isOpen, flags);

    if (popupOpen) {
        if (!m_isOpen) {
            ImGui::CloseCurrentPopup();
        } else {
            drawSettings();
        }
        ImGui::EndPopup();
    }
}

void EditorSettingsWindow::drawSettings() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerY = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight();
    float tabRegionHeight = std::max(1.0f, footerY - style.ItemSpacing.y - ImGui::GetCursorPosY());

    ImGui::BeginChild(
        "##EditorSettingsTabRegion",
        ImVec2(0, tabRegionHeight),
        ImGuiChildFlags_None,
        noScrollFlags
    );

    if (ImGui::BeginTabBar("##EditorSettingsTabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
        // A requested tab only shows from the next frame. Until then the last one
        // is still drawn, and the toolchain tabs would probe for nothing.
        const bool switchingTab = m_requestedTab.has_value();

        if (ImGui::BeginTabItem("General", nullptr, tabFlags(Tab::General))) {
            drawGeneralSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Desktop", nullptr, tabFlags(Tab::Desktop))) {
            if (!switchingTab) drawCMakeSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Web", nullptr, tabFlags(Tab::Web))) {
            if (!switchingTab) drawWebSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("AI", nullptr, tabFlags(Tab::AI))) {
            drawAiSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Advanced", nullptr, tabFlags(Tab::Advanced))) {
            drawAdvancedSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        m_requestedTab.reset();
    }

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(style.WindowPadding.x, footerY - style.ItemSpacing.y));
    ImGui::Separator();

    float footerWidth = std::max(1.0f, ImGui::GetWindowWidth() - style.WindowPadding.x * 2.0f);
    float buttonWidth = std::clamp((footerWidth - style.ItemSpacing.x) * 0.5f, 1.0f, Theme::dpi(settingsButtonWidth));
    float buttonsWidth = buttonWidth * 2.0f + style.ItemSpacing.x;
    float buttonX = style.WindowPadding.x + std::max(0.0f, (footerWidth - buttonsWidth) * 0.5f);
    ImGui::SetCursorPos(ImVec2(buttonX, footerY));

    if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
        if (applySettings()) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }
}

void EditorSettingsWindow::drawGeneralSettings() {
    drawSettingsPanel("##EditorGeneralSettingsPanel", [this]() {
        if (beginSettingsRow("Default Export Directory",
                             "Used by Export Project when the current project has no saved export directory.",
                             !m_defaultExportDirectory.empty())) {
            m_defaultExportDirectory.clear();
        }
        fs::path exportDisplay = m_defaultExportDirectory.empty()
            ? fs::path("<Not set>")
            : m_defaultExportDirectory;
        drawPathPicker(
            "##EditorDefaultExportDirectory", "Browse##editor_default_export_dir", "Clear##editor_default_export_dir",
            exportDisplay, !m_defaultExportDirectory.empty(), true,
            [this](const std::string& selectedPath) {
                m_defaultExportDirectory = selectedPath;
            },
            [this]() {
                m_defaultExportDirectory.clear();
            }
        );

        if (beginSettingsRow("Editor VSync",
                             "VSync for the editor UI. Play mode and exported builds use the project VSync setting.",
                             m_editorVSyncEnabled != true)) {
            m_editorVSyncEnabled = true;
        }
        ImGui::Checkbox("##EditorVSync", &m_editorVSyncEnabled);

        if (beginSettingsRow("UI Scale",
                             "Size of the editor text and panels, on top of the display scale the system reports.",
                             m_uiScalePercent != 100)) {
            m_uiScalePercent = 100;
        }
        static constexpr int uiScalePercents[] = { 50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };
        ImGui::SetNextItemWidth(Theme::dpi(100.0f));
        if (ImGui::BeginCombo("##EditorUiScale", (std::to_string(m_uiScalePercent) + "%").c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int percent : uiScalePercents) {
                const bool selected = percent == m_uiScalePercent;
                if (ImGui::Selectable((std::to_string(percent) + "%").c_str(), selected)) {
                    m_uiScalePercent = percent;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    });
}

void EditorSettingsWindow::drawCMakeSettings() {
    if (!m_kitsDetected) {
        detectKits();
    }
    drawSettingsPanel("##EditorCMakeSettingsPanel", [this]() {
        beginSettingsRow("CMake", "Path to cmake executable. Empty means auto-detect from PATH.", !m_cmakeOverride.empty());
        fs::path cmakeDisplay = m_cmakeOverride.empty()
            ? fs::path(m_cmakeInfo.found ? m_cmakeInfo.path : "<Not found on PATH>")
            : fs::path(m_cmakeOverride);
        drawPathPicker(
            "##EditorCMakePath", "Browse##editor_cmake", "Auto##editor_cmake",
            cmakeDisplay, !m_cmakeOverride.empty(), false,
            [this](const std::string& selectedPath) {
                const std::string resolved = Generator::resolveCMakePath(selectedPath);
                const std::string version = resolved.empty() ? std::string() : Generator::probeCMakeVersion(resolved);

                if (version.empty()) {
                    m_cmakePickError = resolved.empty()
                        ? "No CMake executable in: " + selectedPath
                        : "Not a working CMake: " + resolved;
                } else {
                    m_cmakeOverride = resolved;
                    m_cmakePickError.clear();
                }
                refreshCMakeStatus();
            },
            [this]() {
                m_cmakeOverride.clear();
                m_cmakePickError.clear();
                refreshCMakeStatus();
            }
        );

        if (m_cmakeInfo.found) {
            ImGui::TextDisabled("Detected: %s", m_cmakeInfo.version.empty() ? m_cmakeInfo.path.c_str() : m_cmakeInfo.version.c_str());
        }
        if (!m_cmakePickError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::TextWrapped("%s", m_cmakePickError.c_str());
            ImGui::PopStyleColor();
        }

        if (beginSettingsRow("Compiler", "Compiler kit used to build C++ scripts, both when playing a scene and when exporting. "
                "Set the language standard in Project Settings > Build > C++ Standard.", m_cmakeKitIndex != 0)) {
            m_cmakeKitIndex = 0;
        }
        if (m_cmakeKitIndex < 0 || m_cmakeKitIndex > static_cast<int>(m_availableKits.size())) {
            m_cmakeKitIndex = 0;
        }

        const char* currentLabel = m_cmakeKitIndex == 0
            ? "Default"
            : m_availableKits[m_cmakeKitIndex - 1].displayName.c_str();

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##EditorCMakeKit", currentLabel)) {
            bool selected = m_cmakeKitIndex == 0;
            if (ImGui::Selectable("Default", selected)) m_cmakeKitIndex = 0;
            if (selected) ImGui::SetItemDefaultFocus();

            for (size_t i = 0; i < m_availableKits.size(); i++) {
                const auto& kit = m_availableKits[i];
                if (!kit.available) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable((kit.displayName + "  (unavailable)").c_str(), false);
                    ImGui::EndDisabled();
                    showDisabledItemTooltip(kit.unavailableReason);
                    continue;
                }

                selected = m_cmakeKitIndex == static_cast<int>(i + 1);
                if (ImGui::Selectable(kit.displayName.c_str(), selected)) {
                    m_cmakeKitIndex = static_cast<int>(i + 1);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (m_cmakeKitIndex > 0) {
            const auto& kit = m_availableKits[m_cmakeKitIndex - 1];
            if (!kit.cCompiler.empty() || !kit.cxxCompiler.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("C: %s\nCXX: %s", kit.cCompiler.c_str(), kit.cxxCompiler.c_str());
                ImGui::PopStyleColor();
            }
        } else {
            // The combo fell back to "Default" because the stored kit was not detected
            const LocalBuildSettings stored = projectBuildSettings();
            if (!stored.cxxCompiler.empty() || !stored.generator.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Builds still use \"%s\", which is no longer detected. Apply to switch to the default toolchain.",
                    stored.cxxCompiler.empty() ? stored.generator.c_str() : stored.cxxCompiler.c_str());
                ImGui::PopStyleColor();
            }
        }

    });
}

void EditorSettingsWindow::drawWebSettings() {
    if (!m_emsdkDetected) {
        refreshEmsdkStatus();
        m_emsdkDetected = true;
    }
    drawSettingsPanel("##EditorWebSettingsPanel", [this]() {
        beginSettingsRow("Emscripten SDK", "Path to emsdk root. Empty means EMSDK/PATH auto-detect.", !m_emsdkOverride.empty());

        fs::path emsdkDisplay = m_emsdkOverride.empty()
            ? fs::path("<Auto-detect>")
            : fs::path(m_emsdkOverride);
        drawPathPicker(
            "##EditorEmsdkPath", "Browse##editor_emsdk", "Auto##editor_emsdk",
            emsdkDisplay, !m_emsdkOverride.empty(), true,
            [this](const std::string& selectedPath) {
                m_emsdkOverride = selectedPath;
                refreshEmsdkStatus();
            },
            [this]() {
                m_emsdkOverride.clear();
                refreshEmsdkStatus();
            }
        );

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Status");
        ImGui::TableNextColumn();
        if (m_emsdkInfo.found) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_FA_CIRCLE_CHECK " Found %s", m_emsdkInfo.description.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Not found");
        }
    });
}

void EditorSettingsWindow::drawAiSettings() {
    if (beginSettingsPanel("##EditorAiSettingsPanel")) {
        if (m_expandAiChat) {
            ImGui::SetScrollY(0.0f);
            ImGui::SetNextItemOpen(true);
            m_expandAiChat = false;
        }
        if (ImGui::CollapsingHeader("AI Chat")) {
            drawAiChatSettings();
        }
        if (ImGui::CollapsingHeader("MCP Server", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawMcpSettings();
        }
    }
    endSettingsPanel();
}

void EditorSettingsWindow::drawAiChatSettings() {
    drawSectionNote("Add a key for any provider the AI chat should use. Keys are stored obfuscated in "
                    "your user config folder, and the chat's model picker lists the configured providers.");

    if (beginSettingsTable("##AiProviderKeys")) {
        for (const ai::ProviderAccount& account : ai::listAccounts(m_aiSettings)) {
            if (account.provider == ai::ProviderId::OpenAICompatible) continue;
            drawAiKeyRow(account.id, account.label);
        }
        ImGui::EndTable();
    }

    drawSectionTitle("Custom endpoints");
    ImGui::SetItemTooltip("OpenAI-compatible Chat Completions URLs. Each keeps its own key and "
                          "model list, so several can be configured at the same time.");

    // Skipped when empty: a zero-row table still costs its padding.
    if (!m_aiSettings.customEndpoints.empty() && beginSettingsTable("##AiEndpoints")) {
        drawAiEndpointRows();
        ImGui::EndTable();
    }
    drawAddEndpointButton();

    drawSectionTitle("Limits");

    if (beginSettingsTable("##AiLimits")) {
        const ai::Settings defaults;

        if (beginSettingsRow("Request Timeout (s)",
                             "Maximum time to wait for each AI model response. Raise this for slow local models.",
                             m_aiSettings.requestTimeoutSeconds != defaults.requestTimeoutSeconds)) {
            m_aiSettings.requestTimeoutSeconds = defaults.requestTimeoutSeconds;
        }
        ImGui::SetNextItemWidth(-1);
        UIUtils::inputInt("##AiRequestTimeout", &m_aiSettings.requestTimeoutSeconds, 30, 60);
        m_aiSettings.requestTimeoutSeconds = std::clamp(m_aiSettings.requestTimeoutSeconds, 1, 3600);

        if (beginSettingsRow("Max Output Tokens", nullptr,
                             m_aiSettings.maxOutputTokens != defaults.maxOutputTokens)) {
            m_aiSettings.maxOutputTokens = defaults.maxOutputTokens;
        }
        ImGui::SetNextItemWidth(-1);
        UIUtils::inputInt("##AiMaxOutput", &m_aiSettings.maxOutputTokens, 256, 1024);
        m_aiSettings.maxOutputTokens = std::clamp(m_aiSettings.maxOutputTokens, 256, 16000);

        if (beginSettingsRow("Max Tool Steps",
                             "How many model turns that request tools are allowed per user message. "
                             "Raise this if you see \"Reached the tool-step limit\" on long tasks.",
                             m_aiSettings.maxToolRounds != defaults.maxToolRounds)) {
            m_aiSettings.maxToolRounds = defaults.maxToolRounds;
        }
        ImGui::SetNextItemWidth(-1);
        UIUtils::inputInt("##AiMaxToolRounds", &m_aiSettings.maxToolRounds, 1, 4);
        m_aiSettings.maxToolRounds = std::clamp(m_aiSettings.maxToolRounds, 1, 100);

        ImGui::EndTable();
    }
}

void EditorSettingsWindow::drawAiKeyRow(const std::string& accountId, const std::string& label, bool dimLabel) {
    KeyEntry& entry = m_aiKeys[accountId];
    float clearWidth = ImGui::GetFrameHeight();
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (dimLabel) {
        ImGui::TextDisabled("%s", label.c_str());
    } else {
        ImGui::TextUnformatted(label.c_str());
    }
    if (entry.configured) {
        ImGui::SameLine();
        ImGui::TextColored(keySetColor, ICON_FA_CIRCLE_CHECK);
        ImGui::SetItemTooltip("Key configured");
    }

    ImGui::TableNextColumn();
    ImGui::PushID(accountId.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth - spacing);
    ImGui::InputTextWithHint("##Key", entry.configured ? "key set - type to replace" : "paste API key",
                             entry.buffer.data(), entry.buffer.size(),
                             ImGuiInputTextFlags_Password);
    if (ImGui::BeginPopupContextItem("##KeyContext")) {
        if (ImGui::MenuItem(ICON_FA_CLIPBOARD " Paste")) {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard) {
                setBuffer(entry.buffer.data(), entry.buffer.size(), clipboard);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!entry.configured && entry.buffer[0] == '\0');
    if (Widgets::iconButton("##ClearKey", ICON_FA_TRASH, ImVec2(clearWidth, ImGui::GetFrameHeight()))) {
        ai::SecretStore::clearApiKey(accountId);
        entry.buffer.fill('\0');
        refreshAiKeyState();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Clear this key");
    ImGui::PopID();
}

void EditorSettingsWindow::drawAiEndpointRows() {
    std::string pendingRemoval;
    for (const ai::CustomEndpoint& endpoint : m_aiSettings.customEndpoints) {
        auto it = m_aiEndpoints.find(endpoint.id);
        if (it == m_aiEndpoints.end()) continue;
        EndpointEntry& fields = it->second;

        float clearWidth = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;

        ImGui::PushID(endpoint.id.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        // A plain label until the pencil turns it into a field.
        if (m_editingEndpointId == endpoint.id) {
            ImGui::SetNextItemWidth(-1);
            if (m_focusEndpointLabel) {
                ImGui::SetKeyboardFocusHere();
                m_focusEndpointLabel = false;
            }
            if (ImGui::InputText("##Label", fields.label.data(), fields.label.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivated()) {
                m_editingEndpointId.clear();
            }
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fields.label[0] != '\0' ? fields.label.data() : endpoint.id.c_str());
            if (inlineIconButton(ICON_FA_PENCIL, "EditLabel", "Rename this endpoint")) {
                m_editingEndpointId = endpoint.id;
                m_focusEndpointLabel = true;
            }
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth - spacing);
        ImGui::InputTextWithHint("##Url", "https://host/v1/chat/completions",
                                 fields.url.data(), fields.url.size());
        ImGui::SameLine();
        if (Widgets::iconButton("##RemoveEndpoint", ICON_FA_XMARK,
                                ImVec2(clearWidth, ImGui::GetFrameHeight()))) {
            pendingRemoval = endpoint.id;
        }
        ImGui::SetItemTooltip("Remove this endpoint and its key");
        ImGui::PopID();

        drawAiKeyRow(ai::accountKey(ai::ProviderId::OpenAICompatible, endpoint.id), "API key", true);
    }

    // Deferred so the endpoint list is not mutated mid-iteration.
    if (!pendingRemoval.empty()) {
        removeAiEndpoint(pendingRemoval);
    }
}

// Outside the tables, centered under the section title.
void EditorSettingsWindow::drawAddEndpointButton() {
    const float width = Theme::dpi(160.0f);
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowSize().x - width) * 0.5f));
    if (ImGui::Button(ICON_FA_PLUS "  Add endpoint", ImVec2(width, 0))) {
        ImGui::OpenPopup("##AddEndpoint");
    }
    if (ImGui::BeginPopup("##AddEndpoint")) {
        // The presets below only prefill a URL that could be typed here instead.
        if (ImGui::MenuItem("OpenAI compatible")) {
            addAiEndpoint("OpenAI compatible", "");
        }
        ImGui::Separator();
        for (const ai::EndpointPreset& preset : ai::endpointPresets()) {
            if (ImGui::MenuItem(preset.label.c_str())) {
                addAiEndpoint(preset.label, preset.url);
            }
        }
        ImGui::EndPopup();
    }
}

void EditorSettingsWindow::drawMcpSettings() {
    // Stored at once, so a copied command still works after Cancel
    if (m_mcpToken.empty()) {
        m_mcpToken = ai::McpServer::newToken();
    }
    const std::string url = ai::McpServer::endpointUrl(m_mcpSettings.port);

    drawSectionNote("Lets AI agents outside the editor, such as Claude Code, Codex or Gemini CLI, use the "
                    "tools of the AI chat on the open project. The agent brings its own model.");

    if (beginSettingsTable("##McpSettingsTable")) {
        const ai::McpSettings defaults;
        const ai::McpServer* server = Backend::getApp().getMcpServer();

        beginSettingsRow("Enable Server", "Listen for MCP clients on this computer only (127.0.0.1).");
        ImGui::Checkbox("##McpEnabled", &m_mcpSettings.enabled);

        beginSettingsRow("Status");
        const std::string error = server->getError();
        if (server->isRunning()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_FA_CIRCLE_CHECK " Running");
        } else if (!error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " %s", error.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("Stopped");
        }

        if (beginSettingsRow("Port", nullptr, m_mcpSettings.port != defaults.port)) {
            m_mcpSettings.port = defaults.port;
        }
        ImGui::SetNextItemWidth(-1);
        UIUtils::inputInt("##McpPort", &m_mcpSettings.port, 0, 0);
        m_mcpSettings.port = std::clamp(m_mcpSettings.port, 1024, 65535);

        if (beginSettingsRow("Allow Changes", "Off: agents only get the read-only tools, which inspect and search the project.",
                             m_mcpSettings.allowChanges != defaults.allowChanges)) {
            m_mcpSettings.allowChanges = defaults.allowChanges;
        }
        ImGui::Checkbox("##McpAllowChanges", &m_mcpSettings.allowChanges);

        beginSettingsRow("Token", "Agents send it as \"Authorization: Bearer <token>\". "
                                  "Anyone who has it can change your project, so keep it private.");
        if (ImGui::Button("Copy##McpToken")) {
            ImGui::SetClipboardText(m_mcpToken.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace##McpToken")) {
            m_mcpToken = ai::McpServer::newToken();
        }
        ImGui::SetItemTooltip("Connected agents need the new token from the next request on.");

        beginSettingsRow("URL", "The MCP endpoint, for clients set up by hand. They also need the token header.");
        drawCopyField("##McpUrl", url, url, "Copy the URL");

        ImGui::EndTable();
    }

    drawSectionTitle("Clients");
    ImGui::SetItemTooltip("Run one of these in a terminal to add the editor to that agent.");

    if (beginSettingsTable("##McpClientsTable")) {
        const std::string masked = "********";

        const std::string claude = "claude mcp add --transport http doriax " + url + " --header \"Authorization: Bearer ";
        beginSettingsRow("Claude Code");
        drawCopyField("##McpClaudeCommand", claude + masked + "\"", claude + m_mcpToken + "\"",
                      "Copy the command, token included");

        const std::string codex = "codex mcp add doriax --url " + url + " --bearer-token-env-var DORIAX_MCP_TOKEN";
        beginSettingsRow("Codex", "Codex reads the token from DORIAX_MCP_TOKEN when it connects, "
                                  "so set that variable to the token where Codex runs.");
        drawCopyField("##McpCodexCommand", codex, codex, "Copy the command");

        const std::string gemini = "gemini mcp add -s user --transport http --header \"Authorization: Bearer ";
        beginSettingsRow("Gemini CLI");
        drawCopyField("##McpGeminiCommand", gemini + masked + "\" doriax " + url, gemini + m_mcpToken + "\" doriax " + url,
                      "Copy the command, token included");

        ImGui::EndTable();
    }
}

void EditorSettingsWindow::drawAdvancedSettings() {
    drawSettingsPanel("##EditorAdvancedSettingsPanel", [this]() {
        beginSettingsRow("Shader Cache", "Shared by projects for this editor version. Shaders are rebuilt when needed.");
        ImGui::BeginDisabled(m_project && m_project->isAnyScenePlaying());
        if (ImGui::Button("Clear Shader Cache")) {
            std::error_code ec;
            std::filesystem::remove_all(App::getUserShaderCacheDir(), ec);
            m_cacheStatus = ec ? "Could not clear the shader cache: " + ec.message() : "Shader cache cleared.";
        }
        ImGui::EndDisabled();
        if (!m_cacheStatus.empty()) {
            ImGui::TextWrapped("%s", m_cacheStatus.c_str());
        }
    });
}

bool EditorSettingsWindow::applySettings() {
    AppSettings::setEditorVSyncEnabled(m_editorVSyncEnabled);
    AppSettings::setUiScale(m_uiScalePercent / 100.0f);
    AppSettings::setDefaultExportDirectory(m_defaultExportDirectory);
    AppSettings::setCMakePath(m_cmakeOverride);
    // Unless the Desktop tab ran detection, the stored kit stays
    if (m_kitsDetected) {
        LocalBuildSettings build;
        if (m_cmakeKitIndex > 0) {
            const auto& kit = m_availableKits[m_cmakeKitIndex - 1];
            build.cCompiler = kit.cCompiler;
            build.cxxCompiler = kit.cxxCompiler;
            build.generator = kit.generator;
        }
        if (m_project) {
            const auto file = m_project->getProjectPath() / "project.yaml";
            build.buildJobs = AppSettings::getBuildSettings(file).buildJobs;
            AppSettings::setBuildSettings(file, build);
        }
        // Also the editor-wide default, so a new project starts from it
        AppSettings::setLastCMakeKit(build.cCompiler, build.cxxCompiler, build.generator);
    }
    AppSettings::setEmsdkPath(m_emsdkOverride);
    applyAiSettings();
    AppSettings::setMcpSettings(m_mcpSettings);
    Backend::getApp().getMcpServer()->applySettings(m_mcpSettings);
    return AppSettings::saveSettings();
}

void EditorSettingsWindow::applyAiSettings() {
    // The id never changes, so a rename keeps the endpoint's stored key.
    for (ai::CustomEndpoint& endpoint : m_aiSettings.customEndpoints) {
        auto it = m_aiEndpoints.find(endpoint.id);
        if (it == m_aiEndpoints.end()) continue;
        endpoint.label = it->second.label.data();
        endpoint.url = it->second.url.data();
    }

    // The model, the endpoint in use and the approval mode belong to the chat
    ai::Settings settings = AppSettings::getAiSettings();
    settings.customEndpoints = m_aiSettings.customEndpoints;
    if (!ai::findEndpoint(settings, settings.endpointId)) {
        settings.endpointId.clear();
    }
    settings.requestTimeoutSeconds = m_aiSettings.requestTimeoutSeconds;
    settings.maxOutputTokens = m_aiSettings.maxOutputTokens;
    settings.maxToolRounds = m_aiSettings.maxToolRounds;
    AppSettings::setAiSettings(settings);

    // Deleted endpoints lose their key, unless the same id was re-added before OK.
    for (const std::string& endpointId : m_removedEndpoints) {
        if (!ai::findEndpoint(settings, endpointId)) {
            ai::SecretStore::clearApiKey(ai::accountKey(ai::ProviderId::OpenAICompatible, endpointId));
        }
    }
    m_removedEndpoints.clear();

    // Persist any keys the user typed (one per account). Keys are stored
    // obfuscated in the user config dir, never alongside the project.
    for (auto& entry : m_aiKeys) {
        if (entry.second.buffer[0] != '\0') {
            ai::SecretStore::setApiKey(entry.first, entry.second.buffer.data());
            entry.second.buffer.fill('\0');
        }
    }
    refreshAiKeyState();

    Backend::getApp().getAiChatWindow()->reloadSettings();
}

} // namespace doriax::editor
