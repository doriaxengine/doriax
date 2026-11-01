// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
#include <set>

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
        std::map<uint32_t, bool> subSelectionClickConsumesRelease;
        std::map<uint32_t, bool> resyncLookDelta;
        std::map<uint32_t, bool> lookActive;
        std::map<uint32_t, ImVec2> lookReturnPos;
        std::map<uint32_t, bool> focusCanvasOnNextFrame;

        uint32_t fullscreenSceneId = NULL_PROJECT_SCENE;
        double fullscreenStartTime = 0.0;

        // Engine keys sent down during play but not yet released, and the
        // scene the play session belongs to. Engine Input state is static,
        // so a missed key-up would stay pressed into the next play session.
        std::set<int> playPressedKeys;
        uint32_t playKeysSceneId = 0;

        // Same for mouse buttons, which stop being forwarded as soon as the
        // pointer leaves the viewport, well before the button comes up.
        std::set<int> playPressedMouseButtons;
        uint32_t playMouseSceneId = 0;
        ImVec2 playMousePos = ImVec2(0, 0);

        std::map<uint32_t, int> width;
        std::map<uint32_t, int> height;
        std::map<uint32_t, ImVec2> framebufferScale;
        std::map<uint32_t, float> canvasDpiScale;

        std::map<uint32_t, bool> hasNotification;

        // Entity children hidden below a collapsed Structure node select an ancestor
        // when picked in the viewport. Each maps to its nearest collapsed ancestor.
        std::unordered_map<uint32_t, std::unordered_map<Entity, Entity>> structureSelectionParents;

        std::vector<uint32_t> closeSceneQueue;

        void handleCloseScene(uint32_t sceneId);
        void closeSceneInternal(uint32_t sceneId);
        void showCanvas(SceneProject& sceneProject, bool& gameCursorInSceneRect);
        void showFullscreenCanvas(SceneProject& sceneProject, bool& gameCursorInSceneRect);
        void exitFullscreen();
        void sceneEventHandler(SceneProject* sceneProject);
        void forwardPlayKeyboardInput(ImGuiIO& io, int mods);
        void releasePlayKeys(int mods);
        void releasePlayMouseButtons(int mods);
        void endLook(uint32_t sceneId);
        void handleResourceFileDragDrop(SceneProject* sceneProject);
        Vector3 getModelDropPosition(SceneProject* sceneProject, float x, float y, Entity hitEntity);
        void handleTileRectDragDrop(SceneProject* sceneProject);
        bool handleViewportGizmoClick(SceneProject* sceneProject, float canvasX, float canvasY, int canvasWidth, int canvasHeight);
        void snapCameraToDirection(Camera* camera, const Vector3& direction);
        void focusSceneWindow(const SceneProject& sceneProject) const;
        void requestOpenParentScene(uint32_t childSceneId, uint32_t parentSceneId);
        void drawParentSceneButton(const SceneProject& sceneProject);
        std::string getWindowTitle(const SceneProject& sceneProject) const;
        Entity resolveStructureSelection(uint32_t viewportSceneId, uint32_t sceneId, Entity entity) const;
        Entity findSelectableObjectByRay(uint32_t sceneId, float x, float y, uint32_t* outSceneId = nullptr);
        bool selectObjectByRay(uint32_t sceneId, float x, float y, bool shiftPressed);
        ImVec2 canvasFramebufferScale(uint32_t sceneId) const;
        ImVec2 canvasEngineScale(uint32_t sceneId) const;
        float canvasDpi(uint32_t sceneId) const;
        void toEngineCanvas(uint32_t sceneId, float& x, float& y) const;
        void toEditorCanvas(const SceneProject* sceneProject, float& x, float& y) const;
        ImVec2 getEditorCanvasSize(const SceneProject* sceneProject) const;

    public:
        SceneWindow(Project* project);

        void show();
        bool isFocused() const;
        void resetProjectState();
        void clearSceneState(uint32_t sceneId);
        void requestPlayFocus(uint32_t sceneId);
        void toggleFullscreen(uint32_t sceneId);
        bool isFullscreen() const;
        void beginStructureVisibilityUpdate(uint32_t sceneId);
        void setStructureSelectionParent(uint32_t sceneId, Entity entity, Entity selectionParent);

        void focusOnEntities(SceneProject* sceneProject, const std::vector<Entity>& entities);
        bool viewThroughCamera(uint32_t sceneId, Entity cameraEntity);
        void stopViewingCamera(uint32_t sceneId);

        int getWidth(uint32_t sceneId) const;
        int getHeight(uint32_t sceneId) const;
        float getOverlayScale(uint32_t sceneId) const;

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
                cmd = new ScenePropertyCmd<T>(project, sceneProject->id, propertyName, value);
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
