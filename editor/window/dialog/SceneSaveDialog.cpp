// SceneSaveDialog.cpp
#include "SceneSaveDialog.h"
#include "external/IconsFontAwesome6.h"
#include "Backend.h"
#include <algorithm>
#include <vector>

namespace doriax {
namespace editor {

void SceneSaveDialog::open(const fs::path& projectPath, const std::string& defaultName,
        std::function<void(const fs::path&)> onSave,
        std::function<void()> onCancel) {
    m_isOpen = true;
    m_projectPath = projectPath;
    m_selectedPath = projectPath.string();
    m_onSave = onSave;
    m_onCancel = onCancel;

    // Set default filename
    strncpy(m_fileNameBuffer, defaultName.c_str(), sizeof(m_fileNameBuffer) - 1);
    m_fileNameBuffer[sizeof(m_fileNameBuffer) - 1] = '\0'; // Ensure null termination
}

void SceneSaveDialog::show() {
    if (!m_isOpen) {
        return;
    }

    // Setup modal popup
    ImGui::OpenPopup("Save Scene##SaveSceneModal");

    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // Setup window flags for modal dialog
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    bool popupOpen = ImGui::BeginPopupModal("Save Scene##SaveSceneModal", nullptr, flags);

    if (popupOpen) {
        // Directory browser tree with icons
        if (ImGui::BeginChild("DirectoryBrowser", ImVec2(300, 200), true)) {
            static ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("DirectoryTree", 1, tableFlags)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                // Show project root with special icon
                bool rootOpen = true; // Always keep root open
                ImGui::SetNextItemOpen(rootOpen, ImGuiCond_Always);

                // Project root is always highlighted if selected
                bool isRootSelected = (m_selectedPath == m_projectPath.string());

                // Use empty label for the tree node itself
                if (ImGui::TreeNodeEx("##root", 
                                    ImGuiTreeNodeFlags_OpenOnArrow | 
                                    ImGuiTreeNodeFlags_SpanFullWidth | 
                                    (isRootSelected ? ImGuiTreeNodeFlags_Selected : 0))) {

                    // Display icon separately - always use open folder for root since it's always open
                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", ICON_FA_FOLDER_OPEN);

                    // Display text
                    ImGui::SameLine();
                    ImGui::Text("Project Root");

                    // If clicked, select the project root directory
                    if (ImGui::IsItemClicked() || 
                        (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
                        m_selectedPath = m_projectPath.string();
                    }

                    // Recursively display directory tree
                    displayDirectoryTree(m_projectPath, m_projectPath);

                    ImGui::TreePop();
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        // File name input
        ImGui::Text("Scene File Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##fileName", m_fileNameBuffer, sizeof(m_fileNameBuffer));

        // Show current selected directory path
        ImGui::TextWrapped("Save to: %s", fs::relative(m_selectedPath, m_projectPath).string().c_str());

        // Get filename as string for validation and processing
        std::string fileName = m_fileNameBuffer;

        // Check if the filename has the correct extension
        if (!fileName.empty() && fileName.find(".scene") == std::string::npos) {
            fileName += ".scene";
            // Update the buffer with the extension
            strncpy(m_fileNameBuffer, fileName.c_str(), sizeof(m_fileNameBuffer) - 1);
            m_fileNameBuffer[sizeof(m_fileNameBuffer) - 1] = '\0'; // Ensure null termination
        }

        // Validation
        bool canSave = !fileName.empty();
        fs::path fullPath = fs::path(m_selectedPath) / fileName;
        bool fileExists = fs::exists(fullPath);

        if (fileExists) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: File already exists and will be overwritten!");
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
                m_onSave(fullPath);
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
        // This happens if user presses ESC or clicks outside the modal
        if (m_isOpen) {
            m_isOpen = false;

            // If dialog was closed without explicit button press,
            // we still need to call the cancel callback
            if (m_onCancel) {
                m_onCancel();
            }
        }
    }
}

void SceneSaveDialog::displayDirectoryTree(const fs::path& rootPath, const fs::path& currentPath) {
    try {
        std::vector<fs::path> subDirs;

        // Collect directories first to sort them
        for (const auto& entry : fs::directory_iterator(currentPath)) {
            if (entry.is_directory()) {
                subDirs.push_back(entry.path());
            }
        }

        // Sort directories by name
        std::sort(subDirs.begin(), subDirs.end());

        for (const auto& dirPath : subDirs) {
            // Skip hidden directories (starting with ".")
            if (dirPath.filename().string()[0] == '.') {
                continue;
            }

            // We'll set the icon based on whether the node is open
            // First, push ID to ensure uniqueness for TreeNodeEx
            ImGui::PushID(dirPath.string().c_str());

            // Set tree node flags
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

            // Highlight the selected directory
            bool isSelected = (m_selectedPath == dirPath.string());
            if (isSelected) {
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            }

            // Count subdirectories to determine if we need to show it as a leaf node
            bool hasSubDirs = false;
            try {
                for (const auto& subEntry : fs::directory_iterator(dirPath)) {
                    if (subEntry.is_directory()) {
                        hasSubDirs = true;
                        break;
                    }
                }
            } catch (...) {
                // Ignore directory access errors
            }

            // If directory has no subdirectories, make it a leaf node
            if (!hasSubDirs) {
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;
            }

            // Get directory name relative to project path for display
            std::string displayName = dirPath.filename().string();

            // Create node label with empty name - we'll draw icon and text separately
            bool nodeOpen = ImGui::TreeNodeEx("##dir", nodeFlags);

            // Draw icon, changing based on open state
            ImGui::SameLine(0, 0); // Position right after the tree connector
            if (nodeOpen) {
                // Opened folder icon
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", ICON_FA_FOLDER_OPEN);
            } else {
                // Closed folder icon
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", ICON_FA_FOLDER);
            }

            // Display the folder name next to the icon
            ImGui::SameLine();
            ImGui::Text("%s", displayName.c_str());

            // Handle directory selection on click (include the whole row)
            if (ImGui::IsItemClicked() || 
                (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
                m_selectedPath = dirPath.string();
            }

            // If node is open, recursively display subdirectories
            if (nodeOpen) {
                if (hasSubDirs) {
                    displayDirectoryTree(rootPath, dirPath);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
    }
}

} // namespace editor
} // namespace doriax