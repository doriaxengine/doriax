// ProjectSaveDialog.cpp
#include "ProjectSaveDialog.h"
#include "util/FileDialogs.h"
#include "Backend.h"
#include "window/Widgets.h"
#include "external/IconsFontAwesome6.h"

namespace doriax {
namespace editor {

void ProjectSaveDialog::open(const std::string& defaultName,
        std::function<void(const std::string&, const fs::path&)> onSave,
        std::function<void()> onCancel) {
    m_isOpen = true;
    m_projectName = defaultName;
    m_projectPath.clear();
    m_onSave = onSave;
    m_onCancel = onCancel;

    // Set default project name
    strncpy(m_projectNameBuffer, defaultName.c_str(), sizeof(m_projectNameBuffer) - 1);
    m_projectNameBuffer[sizeof(m_projectNameBuffer) - 1] = '\0'; // Ensure null termination
}

void ProjectSaveDialog::show() {
    if (!m_isOpen) {
        return;
    }

    // Setup modal popup
    ImGui::OpenPopup("Save Project##SaveProjectModal");

    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // Setup window flags for modal dialog
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    bool popupOpen = ImGui::BeginPopupModal("Save Project##SaveProjectModal", nullptr, flags);

    float popupWidth = 300;

    if (popupOpen) {
        // Project name input
        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(popupWidth);
        ImGui::InputText("##projectName", m_projectNameBuffer, sizeof(m_projectNameBuffer));

        // Project path display and button
        ImGui::Text("Path:");

        Vector2 pathDisplaySize = Vector2(popupWidth - ImGui::CalcTextSize("Browse").x - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        Widgets::pathDisplay("##ProjectPath", m_projectPath, pathDisplaySize);

        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
            // Get home directory as default path
            std::string homeDirPath;
            #ifdef _WIN32
            homeDirPath = std::filesystem::path(getenv("USERPROFILE")).string();
            #else
            homeDirPath = std::filesystem::path(getenv("HOME")).string();
            #endif
            
            // Open folder picker dialog
            std::string selectedPath = FileDialogs::openFileDialog(homeDirPath, 0, true);
            
            if (!selectedPath.empty()) {
                m_projectPath = selectedPath;
            }
        }

        // Validation
        std::string projectName = m_projectNameBuffer;
        bool canSave = !projectName.empty() && !m_projectPath.empty();
        
        // Check if the selected directory is empty if it exists
        if (!m_projectPath.empty() && std::filesystem::exists(m_projectPath)) {
            if (!std::filesystem::is_empty(m_projectPath)) {
                canSave = false;
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                                  "Selected directory is not empty.");
            }
        }

        ImGui::Separator();

        // Center the buttons
        float windowWidth = ImGui::GetWindowSize().x;
        float buttonsWidth = 250; // Total width for both buttons and spacing
        ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

        // Buttons
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (m_onSave) {
                m_onSave(projectName, m_projectPath);
            }
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_isOpen = false;
            if (m_onCancel) {
                m_onCancel();
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else {
        // If the popup isn't open anymore but our state says it should be,
        // update our state and make sure to call the cancel callback
        if (m_isOpen) {
            m_isOpen = false;
            if (m_onCancel) {
                m_onCancel();
            }
        }
    }
}

} // namespace editor
} // namespace doriax