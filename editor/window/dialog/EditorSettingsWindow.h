// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "AppSettings.h"
#include "Exporter.h"
#include "Generator.h"
#include "ai/AiTypes.h"
#include "imgui.h"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doriax::editor {

    class Project;

    class EditorSettingsWindow {
    public:
        enum class Tab {
            General,
            Desktop,
            Web,
            AI,
            Advanced
        };

    private:
        // std::map keeps buffer addresses stable for ImGui as endpoints come and go.
        struct KeyEntry {
            std::array<char, 512> buffer{};
            bool configured = false;
        };
        struct EndpointEntry {
            std::array<char, 128> label{};
            std::array<char, 512> url{};
        };

        bool m_isOpen = false;
        std::optional<Tab> m_requestedTab;
        // Kept until the AI tab draws, which can be a frame after the tab request
        bool m_expandAiChat = false;

        // Build settings are per project, so the dialog edits the open project's
        Project* m_project = nullptr;

        std::vector<CMakeKit> m_availableKits;
        int m_cmakeKitIndex = 0;
        std::string m_cmakeOverride;
        CMakeInfo m_cmakeInfo;
        std::string m_cmakePickError;
        // Probing runs processes, so it waits for the tab that shows the result
        bool m_kitsDetected = false;
        bool m_emsdkDetected = false;

        std::string m_emsdkOverride;
        EmsdkInfo m_emsdkInfo;
        ScriptEditor m_scriptEditor = ScriptEditor::BUILTIN;
        std::array<char, 512> m_scriptEditorCommand{};
        // detection runs processes, so once per choice
        std::optional<ScriptEditor> m_detectedEditor;
        std::string m_detectedEditorPath;
        ScriptDebugInfo m_debugInfo = ScriptDebugInfo::SYMBOLS;
        bool m_editorVSyncEnabled = true;
        int m_uiScalePercent = 100;
        std::filesystem::path m_defaultExportDirectory;
        std::string m_cacheStatus;

        ai::Settings m_aiSettings;
        std::map<std::string, KeyEntry> m_aiKeys;           // account id -> typed key
        std::map<std::string, EndpointEntry> m_aiEndpoints;  // endpoint id -> typed fields
        // Endpoints removed here. Wiped only on OK, so Cancel restores their keys.
        std::vector<std::string> m_removedEndpoints;
        // Endpoint being renamed in place, and its one-frame focus request.
        std::string m_editingEndpointId;
        bool m_focusEndpointLabel = false;

        ai::McpSettings m_mcpSettings;
        std::string m_mcpToken;

        LocalBuildSettings projectBuildSettings() const;
        void detectKits();
        void refreshCMakeStatus();
        void refreshEmsdkStatus();
        ImGuiTabItemFlags tabFlags(Tab tab) const;
        void drawSettings();
        void drawGeneralSettings();
        void drawCMakeSettings();
        void drawWebSettings();
        void drawAiSettings();
        void drawAiChatSettings();
        void drawAiKeyRow(const std::string& accountId, const std::string& label, bool dimLabel = false);
        void drawAiEndpointRows();
        void drawAddEndpointButton();
        void drawMcpSettings();
        void drawAdvancedSettings();
        void syncAiBuffers();
        void refreshAiKeyState();
        void addAiEndpoint(const std::string& label, const std::string& url);
        void removeAiEndpoint(const std::string& endpointId);
        void applyAiSettings();
        bool applySettings();

    public:
        EditorSettingsWindow() = default;
        ~EditorSettingsWindow() = default;

        // Opens on the given tab, or on the one shown last
        void open(Project* project, std::optional<Tab> tab = std::nullopt, bool expandAiChat = false);
        void show();
        bool isOpen() const { return m_isOpen; }
    };

}
