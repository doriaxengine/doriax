#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "Project.h"
#include "object/Camera.h"
#include "command/CommandHandle.h"
#include "command/type/ScenePropertyCmd.h"
#include "command/type/PropertyCmd.h"
#include "render/gizmo/ViewportGizmo.h"
#include <unordered_map>

namespace doriax::editor {

    enum class ScenePropertyType {
        CHECKBOX,
        DRAG_FLOAT,
        SLIDER_FLOAT,
        COLOR_RGB,
        COLOR_RGBA,
        COMBO
    };

    class SceneWindow {
    private:
        Project* project;
        bool windowFocused;

        bool mouseLeftDown = false;
        Vector2 mouseLeftStartPos;
        Vector2 mouseLeftDragPos;
        bool mouseLeftDraggedInside;

        std::map<uint32_t, bool> draggingMouse;
        std::map<uint32_t, bool> suppressLeftMouseUntilRelease;
        std::map<uint32_t, float> walkSpeed;

        std::map<uint32_t, int> width;
        std::map<uint32_t, int> height;

        std::map<uint32_t, bool> hasNotification;
        std::map<uint32_t, ScenePlayState> lastPlayState;

        std::vector<uint32_t> closeSceneQueue;

        void handleCloseScene(uint32_t sceneId);
        void closeSceneInternal(uint32_t sceneId);
        void sceneEventHandler(SceneProject* sceneProject);
        void handleResourceFileDragDrop(SceneProject* sceneProject);
        void handleTileRectDragDrop(SceneProject* sceneProject);
        bool handleViewportGizmoClick(SceneProject* sceneProject, float canvasX, float canvasY, int canvasWidth, int canvasHeight);
        void snapCameraToDirection(Camera* camera, const Vector3& direction);
        void focusSceneWindow(const SceneProject& sceneProject) const;
        std::string getWindowTitle(const SceneProject& sceneProject) const;
        
    public:
        SceneWindow(Project* project);

        void show();
        bool isFocused() const;
        void clearSceneState(uint32_t sceneId);

        void focusOnEntities(SceneProject* sceneProject, const std::vector<Entity>& entities);

        int getWidth(uint32_t sceneId) const;
        int getHeight(uint32_t sceneId) const;

        template<typename T>
        void drawSceneProperty(SceneProject* sceneProject, const std::string& propertyName, const char* label, ScenePropertyType inputType, float minValue = 0.0f, float maxValue = 1.0f, float col2Size = -1.0f) {
            T value = doriax::editor::Catalog::getSceneProperty<T>(sceneProject->scene, propertyName);
            bool changed = false;

            Command* cmd = nullptr;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", label);
            ImGui::TableSetColumnIndex(1);

            switch (inputType) {
                case ScenePropertyType::CHECKBOX:
                    if constexpr (std::is_same_v<T, bool>) {
                        changed = ImGui::Checkbox(("##" + propertyName).c_str(), &value);
                    }
                    break;
                case ScenePropertyType::DRAG_FLOAT:
                    if constexpr (std::is_same_v<T, float>) {
                        changed = ImGui::DragFloat(("##" + propertyName).c_str(), &value, 0.01f);
                    } else if constexpr (std::is_same_v<T, Vector3>) {
                        changed = ImGui::DragFloat3(("##" + propertyName).c_str(), (float*)&value.x);
                    } else if constexpr (std::is_same_v<T, Vector4>) {
                        changed = ImGui::DragFloat4(("##" + propertyName).c_str(), (float*)&value.x);
                    }
                    break;
                case ScenePropertyType::SLIDER_FLOAT:
                    if constexpr (std::is_same_v<T, float>) {
                        ImGui::SetNextItemWidth(-1);
                        changed = ImGui::SliderFloat(("##" + propertyName).c_str(), &value, minValue, maxValue);
                    }
                    break;
                case ScenePropertyType::COLOR_RGB:
                    if constexpr (std::is_same_v<T, Vector3>) {
                        changed = ImGui::ColorEdit3(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                    }
                    break;
                case ScenePropertyType::COLOR_RGBA:
                    if constexpr (std::is_same_v<T, Vector4>) {
                        changed = ImGui::ColorEdit4(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                    }
                    break;
                case ScenePropertyType::COMBO:
                    if constexpr (std::is_same_v<T, LightState>) {
                        const char* lightStateNames[] = { "Off", "On", "Auto" };
                        int currentItem = static_cast<int>(value);
                        changed = ImGui::Combo(("##" + propertyName).c_str(), &currentItem, lightStateNames, IM_ARRAYSIZE(lightStateNames));
                        if (changed) {
                            value = static_cast<LightState>(currentItem);
                        }
                    }
                    break;
            }

            if (changed) {
                cmd = new ScenePropertyCmd<T>(sceneProject, propertyName, value);
                CommandHandle::get(sceneProject->id)->addCommand(cmd);
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (cmd){
                    cmd->setNoMerge();
                    cmd = nullptr;
                }
            }
        }
    };
}
