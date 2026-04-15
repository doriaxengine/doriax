#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "imgui.h"
#include "external/IconsFontAwesome6.h"

namespace fs = std::filesystem;

namespace doriax::editor{
    class Widgets{

    private:
        inline static std::string shortenPath(const fs::path& path, float maxWidth) {
            std::string fullPath = path.string();
        
            ImVec2 fullPathSize = ImGui::CalcTextSize(fullPath.c_str());
            if (fullPathSize.x <= maxWidth) {
                return fullPath;
            }
        
            std::string filename = path.filename().string();
            std::string parentPath = path.parent_path().string();
        
            ImVec2 filenameSize = ImGui::CalcTextSize(filename.c_str());
            if (filenameSize.x > maxWidth) {
                std::string truncatedFilename = filename;
                while (!truncatedFilename.empty() && ImGui::CalcTextSize((truncatedFilename + "...").c_str()).x > maxWidth) {
                    truncatedFilename.pop_back();
                }
                return truncatedFilename + "...";
            }
        
            float remainingWidth = maxWidth - filenameSize.x - ImGui::CalcTextSize("/").x; // Space for '/' separator
        
            if (parentPath != ".") {
                std::string truncatedParentPath = parentPath;
                while (!truncatedParentPath.empty() && ImGui::CalcTextSize((truncatedParentPath + ".../").c_str()).x > remainingWidth) {
                    truncatedParentPath.pop_back();
                }
        
                return truncatedParentPath + ".../" + filename;
            } else {
                return filename;
            }
        }

    public:
        inline static void pathDisplay(const char* id, fs::path path, const Vector2& size = Vector2::ZERO, fs::path basePath = fs::path()){
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(50, 50, 50, 255));
            ImGui::BeginChild(id, ImVec2(size.x, size.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            
            std::string subPath = path.string();
            if (!basePath.empty()){
                if (subPath.find(basePath.string()) == 0) {
                    subPath = subPath.substr(basePath.string().length());
                    if (subPath.empty()){
                        subPath = "/";
                    }
                }
            }else{
                subPath = subPath.empty() ? "<No path selected>" : subPath;
            }

            std::string shortenedPath = shortenPath(subPath, ImGui::GetContentRegionAvail().x);
        
            ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
            ImGui::Text("%s", ((shortenedPath == ".") ? "" : shortenedPath).c_str());
        
            ImGui::EndChild();
            if (!path.empty()){
                ImGui::SetItemTooltip("%s", path.string().c_str());
            }
            ImGui::PopStyleColor();
        }
    };

}