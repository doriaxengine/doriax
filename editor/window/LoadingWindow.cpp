#include "LoadingWindow.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <vector>

using namespace doriax::editor;

LoadingWindow::LoadingWindow() {
}

LoadingWindow::~LoadingWindow() {
}

void LoadingWindow::show() {
    bool hasBuilds = ResourceProgress::hasActiveBuilds();

    bool dragDropActive = ImGui::IsDragDropActive();

    if (hasBuilds && !dragDropActive) {
        if (!wasShowing) {
            ImGui::OpenPopup("Loading");
            wasShowing = true;
        }

        OverallBuildProgress overallProgress = ResourceProgress::getOverallProgress();
        drawProgressModal(overallProgress);
    } else {
        if (wasShowing) {
            ImGui::CloseCurrentPopup();
            wasShowing = false;
        }
    }
}

void LoadingWindow::drawProgressModal(const OverallBuildProgress& progress) {
    // Center the modal
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

    // Set transparency
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_Modal | 
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::BeginPopupModal("Loading", nullptr, flags)) {
        if (progress.hasActiveBuilds) {
            // Create a formatted string first, then use it with TreeNode
            char buildingText[256];
            snprintf(buildingText, sizeof(buildingText), "Building %d resources", progress.totalBuilds);

            if (ImGui::TreeNode("resource_list", "%s", buildingText)) {
                // Get all active builds for the dropdown
                std::vector<ResourceBuildInfo> allBuilds = ResourceProgress::getAllActiveBuilds();

                for (const auto& build : allBuilds) {
                    ImGui::Bullet();
                    ImGui::Text("%s - %s - %.1f%%", 
                        ResourceProgress::getResourceTypeName(build.type).c_str(),
                        build.name.c_str(),
                        build.progress * 100.0f);
                }
                ImGui::TreePop();
            }

            ImGui::Spacing();

            // Overall progress bar
            ImGui::ProgressBar(progress.totalProgress, ImVec2(-1.0f, 0.0f), "");

            // Progress text
            ImGui::Text("Progress: %.1f%%", progress.totalProgress * 100.0f);

            ImGui::Spacing();
        }

        ImGui::EndPopup();
    }
}