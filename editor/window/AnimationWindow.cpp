#include "AnimationWindow.h"

#include "imgui_internal.h"
#include "external/IconsFontAwesome6.h"
#include "Catalog.h"
#include "Stream.h"
#include "App.h"
#include "command/CommandHandle.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"

#include "component/ActionComponent.h"
#include "component/AnimationComponent.h"
#include "component/BoneComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/MorphTracksComponent.h"
#include "component/ModelComponent.h"
#include "component/RotateTracksComponent.h"
#include "component/ScaleTracksComponent.h"
#include "component/TranslateTracksComponent.h"
#include "component/SpriteAnimationComponent.h"
#include "component/TimedActionComponent.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/MeshSystem.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

using namespace doriax;

editor::AnimationWindow::AnimationWindow(Project* project){
    this->project = project;

    isPlaying = false;
    currentTime = 0;
    playbackSpeed = 1.0f;

    pixelsPerSecond = 100.0f;
    scrollX = 0;
    minPixelsPerSecond = 20.0f;
    maxPixelsPerSecond = 500.0f;

    selectedFrameIndex = -1;
    isDraggingPlayhead = false;
    isDraggingFrame = false;
    draggingFrameIndex = -1;
    dragStartTime = 0;
    dragStartTrack = 0;

    isResizingFrame = false;
    resizingFrameIndex = -1;
    resizeSide = 0;
    resizeStartTime = 0;
    resizeStartDuration = 0;

    snapToGrid = true;
    snapInterval = 0.1f;

    hasNotification = false;
    isWindowVisible = false;

    isPreviewing = false;

    selectedEntity = NULL_ENTITY;
    selectedSceneId = 0;
}

float editor::AnimationWindow::snapTime(float time) const {
    if (!snapToGrid || snapInterval <= 0) return time;
    return std::round(time / snapInterval) * snapInterval;
}

float editor::AnimationWindow::timeToX(float time, float timeStart, ImVec2 canvasPos) const {
    return canvasPos.x + (time - timeStart) * pixelsPerSecond;
}

float editor::AnimationWindow::xToTime(float x, float timeStart, ImVec2 canvasPos) const {
    return timeStart + (x - canvasPos.x) / pixelsPerSecond;
}

std::string editor::AnimationWindow::getActionLabel(Entity actionEntity, Scene* scene) const {
    if (actionEntity == NULL_ENTITY) return "Empty";

    Signature sig = scene->getSignature(actionEntity);

    if (sig.test(scene->getComponentId<SpriteAnimationComponent>())) {
        SpriteAnimationComponent& sa = scene->getComponent<SpriteAnimationComponent>(actionEntity);
        if (!sa.name.empty()) return sa.name;
        return "SpriteAnimation";
    }

    if (sig.test(scene->getComponentId<TranslateTracksComponent>())) {
        return "TranslateTracks";
    }

    if (sig.test(scene->getComponentId<RotateTracksComponent>())) {
        return "RotateTracks";
    }

    if (sig.test(scene->getComponentId<ScaleTracksComponent>())) {
        return "ScaleTracks";
    }

    if (sig.test(scene->getComponentId<MorphTracksComponent>())) {
        return "MorphTracks";
    }

    if (sig.test(scene->getComponentId<KeyframeTracksComponent>())) {
        return "KeyframeTracks";
    }

    if (sig.test(scene->getComponentId<TimedActionComponent>())) {
        return "TimedAction";
    }

    if (sig.test(scene->getComponentId<AnimationComponent>())) {
        AnimationComponent& a = scene->getComponent<AnimationComponent>(actionEntity);
        if (!a.name.empty()) return a.name;
        return "Animation";
    }

    return "Action";
}

void editor::AnimationWindow::selectEntity(Entity entity, uint32_t sceneId) {
    if (selectedEntity != entity || selectedSceneId != sceneId) {
        // Stop any active preview before changing entity
        if (isPreviewing) {
            SceneProject* sceneProject = project->getScene(selectedSceneId);
            if (sceneProject && sceneProject->scene) {
                stopPreview(sceneProject->scene, sceneProject);
            } else {
                previewState.clear();
                isPreviewing = false;
            }
        }
        selectedEntity = entity;
        selectedSceneId = sceneId;
        currentTime = 0;
        isPlaying = false;
        selectedFrameIndex = -1;
    }
}

bool editor::AnimationWindow::canPreviewEntity(Entity entity, Scene* scene) const {
    if (!scene || entity == NULL_ENTITY || !scene->isEntityCreated(entity)) {
        return false;
    }

    Signature signature = scene->getSignature(entity);
    return signature.test(scene->getComponentId<AnimationComponent>()) &&
           signature.test(scene->getComponentId<ActionComponent>());
}

void editor::AnimationWindow::collectPreviewEntitiesRecursive(Scene* scene, Entity entity, std::vector<Entity>& entities,
                                                              std::unordered_set<Entity>& visitedAnimations,
                                                              std::unordered_set<Entity>& collectedEntities) const {
    if (!canPreviewEntity(entity, scene) || !visitedAnimations.insert(entity).second) {
        return;
    }

    if (collectedEntities.insert(entity).second) {
        entities.push_back(entity);
    }

    const AnimationComponent& animation = scene->getComponent<AnimationComponent>(entity);
    for (const ActionFrame& frame : animation.actions) {
        Entity actionEntity = frame.action;
        if (actionEntity == NULL_ENTITY || !scene->isEntityCreated(actionEntity)) {
            continue;
        }

        Signature actionSignature = scene->getSignature(actionEntity);
        if (collectedEntities.insert(actionEntity).second) {
            entities.push_back(actionEntity);
        }

        if (actionSignature.test(scene->getComponentId<ActionComponent>())) {
            const ActionComponent& action = scene->getComponent<ActionComponent>(actionEntity);
            if (action.target != NULL_ENTITY && scene->isEntityCreated(action.target) &&
                collectedEntities.insert(action.target).second) {
                entities.push_back(action.target);
            }
        }

        if (actionSignature.test(scene->getComponentId<AnimationComponent>()) &&
            actionSignature.test(scene->getComponentId<ActionComponent>())) {
            collectPreviewEntitiesRecursive(scene, actionEntity, entities, visitedAnimations, collectedEntities);
        }
    }
}

editor::AnimationWindow::PreviewEntityState editor::AnimationWindow::buildPreviewEntityState(Scene* scene, Entity entity) const {
    PreviewEntityState state;
    state.entity = entity;

    if (Transform* transform = scene->findComponent<Transform>(entity)) {
        state.parent = transform->parent;
    }

    YAML::Node components = Stream::encodeComponents(entity, scene, scene->getSignature(entity));
    components.remove(Catalog::getComponentName(ComponentType::AnimationComponent, true));
    state.components = components;

    return state;
}

void editor::AnimationWindow::restorePreviewState(Scene* scene) const {
    for (const PreviewEntityState& state : previewState) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
            continue;
        }

        Stream::decodeComponents(state.entity, state.parent, scene, state.components);
    }
}

void editor::AnimationWindow::applyPreviewModelBindPose(Scene* scene) const {
    std::unordered_set<Entity> modelEntities;

    for (const PreviewEntityState& state : previewState) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity)) {
            continue;
        }

        if (BoneComponent* bone = scene->findComponent<BoneComponent>(state.entity)) {
            if (bone->model != NULL_ENTITY) {
                modelEntities.insert(bone->model);
            }
        }

        if (scene->findComponent<ModelComponent>(state.entity)) {
            modelEntities.insert(state.entity);
        }
    }

    auto meshSystem = scene->getSystem<MeshSystem>();
    for (Entity modelEntity : modelEntities) {
        ModelComponent* model = scene->findComponent<ModelComponent>(modelEntity);
        if (model) {
            meshSystem->resetModelToBindPose(*model);
        }
    }
}

float editor::AnimationWindow::getAnimationDuration(const AnimationComponent& anim) const {
    if (anim.duration > 0) {
        return anim.duration;
    }

    float duration = 0.0f;
    for (const ActionFrame& frame : anim.actions) {
        duration = std::max(duration, frame.startTime + frame.duration);
    }

    return duration;
}

void editor::AnimationWindow::seekPreview(Scene* scene, SceneProject* sceneProject, float time) {
    if (!scene || !sceneProject || !canPreviewEntity(selectedEntity, scene)) {
        return;
    }

    if (!isPreviewing) {
        startPreview(scene, sceneProject);
    }

    AnimationComponent& anim = scene->getComponent<AnimationComponent>(selectedEntity);
    currentTime = std::max(0.0f, std::min(time, getAnimationDuration(anim)));

    restorePreviewState(scene);
    applyPreviewModelBindPose(scene);

    ActionComponent& action = scene->getComponent<ActionComponent>(selectedEntity);
    action.timecount = currentTime;
    action.stopTrigger = false;
    action.pauseTrigger = false;
    action.startTrigger = true;

    scene->getSystem<ActionSystem>()->updateAnimationPreview(0.0, selectedEntity);
    sceneProject->needUpdateRender = true;
}

void editor::AnimationWindow::startPreview(Scene* scene, SceneProject* sceneProject) {
    if (isPreviewing || !canPreviewEntity(selectedEntity, scene)) {
        return;
    }

    previewState.clear();

    std::vector<Entity> previewEntities;
    std::unordered_set<Entity> visitedAnimations;
    std::unordered_set<Entity> collectedEntities;
    collectPreviewEntitiesRecursive(scene, selectedEntity, previewEntities, visitedAnimations, collectedEntities);

    previewState.reserve(previewEntities.size());
    for (Entity entity : previewEntities) {
        previewState.push_back(buildPreviewEntityState(scene, entity));
    }

    ActionComponent& action = scene->getComponent<ActionComponent>(selectedEntity);
    action.timecount = 0;
    action.stopTrigger = false;
    action.pauseTrigger = false;
    action.startTrigger = true;

    isPreviewing = true;
}

void editor::AnimationWindow::stopPreview(Scene* scene, SceneProject* sceneProject, bool applyBindPose) {
    if (!isPreviewing) return;

    restorePreviewState(scene);
    if (applyBindPose) {
        applyPreviewModelBindPose(scene);
    }

    previewState.clear();
    isPreviewing = false;
    isDraggingFrame = false;
    draggingFrameIndex = -1;
    isResizingFrame = false;
    resizingFrameIndex = -1;
    resizeSide = 0;
    if (sceneProject) {
        sceneProject->needUpdateRender = true;
    }
}

std::string editor::AnimationWindow::getAnimationEntityLabel(Entity entity, AnimationComponent& anim, Scene* scene) const {
    std::string label = anim.name.empty() ? "Animation" : anim.name;

    ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
    if (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) {
        std::string targetName = scene->getEntityName(actionComp->target);
        if (!targetName.empty()) {
            label += " (" + targetName + ")";
        }
    }

    return label;
}

void editor::AnimationWindow::drawToolbar(float width, AnimationComponent& anim, Scene* scene, SceneProject* sceneProject) {
    // Animation entity combo selector
    auto animations = scene->getComponentArray<AnimationComponent>();
    std::string currentLabel = getAnimationEntityLabel(selectedEntity, anim, scene);
    bool canPreview = canPreviewEntity(selectedEntity, scene);

    float textWidth = ImGui::CalcTextSize(currentLabel.c_str()).x;
    float arrowWidth = ImGui::GetFrameHeight();
    float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
    float minWidth = 150.0f;
    float desiredWidth = textWidth + arrowWidth + padding + 10.0f;

    ImGui::SetNextItemWidth(std::max(minWidth, desiredWidth));
    if (ImGui::BeginCombo("##anim_entity_combo", currentLabel.c_str())) {
        for (int i = 0; i < (int)animations->size(); i++) {
            Entity entity = animations->getEntity(i);
            AnimationComponent& animItem = animations->getComponentFromIndex(i);
            std::string label = getAnimationEntityLabel(entity, animItem, scene);

            bool isSelected = (entity == selectedEntity);
            ImGui::PushID((int)entity);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectEntity(entity, selectedSceneId);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Add Action
    ImGui::BeginDisabled(isPreviewing);
    if (ImGui::Button(ICON_FA_PLUS " Add Action")) {
        // If a frame is selected, add to the same track, otherwise track 0
        uint32_t targetTrack = 0;
        if (selectedFrameIndex >= 0 && selectedFrameIndex < (int)anim.actions.size()) {
            targetTrack = anim.actions[selectedFrameIndex].track;
        }

        ActionFrame newFrame = {0.0f, 1.0f, NULL_ENTITY, targetTrack};

        // Find non-overlapping track
        bool overlap;
        do {
            overlap = false;
            for (const auto& a : anim.actions) {
                if (a.track == newFrame.track) {
                    float startA = a.startTime;
                    float endA = a.startTime + a.duration;
                    float startB = newFrame.startTime;
                    float endB = newFrame.startTime + newFrame.duration;
                    // Strict less-than for overlap means adjoining frames (e.g. 0-1 and 1-2) are OK
                    if (std::max(startA, startB) < std::min(endA, endB)) {
                        overlap = true;
                        newFrame.track++;
                        break;
                    }
                }
            }
        } while (overlap);

        anim.actions.push_back(newFrame);
        selectedFrameIndex = anim.actions.size() - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Play/Pause
    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);
    if (isPlaying) {
        if (ImGui::Button(ICON_FA_PAUSE "##anim_pause")) {
            isPlaying = false;
        }
    } else {
        ImGui::BeginDisabled(sceneIsStopped && !canPreview);
        if (ImGui::Button(ICON_FA_PLAY "##anim_play")) {
            isPlaying = true;
            if (sceneIsStopped && !isPreviewing) {
                currentTime = 0;
                startPreview(scene, sceneProject);
            } else if (sceneIsStopped && isPreviewing) {
                ActionComponent& action = sceneProject->scene->getComponent<ActionComponent>(selectedEntity);
                if (action.state == ActionState::Stopped) {
                    seekPreview(scene, sceneProject, 0.0f);
                }
            }
        }
        ImGui::EndDisabled();
        if (sceneIsStopped && !canPreview && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Animation preview requires an ActionComponent on the selected animation entity.");
        }
    }
    ImGui::SameLine();

    // Stop
    ImGui::BeginDisabled(!isPlaying && !isPreviewing);
    if (ImGui::Button(ICON_FA_STOP "##anim_stop")) {
        isPlaying = false;
        currentTime = 0;
        if (isPreviewing) {
            stopPreview(scene, sceneProject, true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    // Time display
    ImGui::SetNextItemWidth(60);
    float maxTime = getAnimationDuration(anim);
    if (ImGui::DragFloat("##anim_time", &currentTime, 0.01f, 0.0f, maxTime, "%.2fs")) {
        if (sceneIsStopped && canPreview) {
            seekPreview(scene, sceneProject, currentTime);
        } else {
            currentTime = std::max(0.0f, std::min(currentTime, maxTime));
        }
    }
    ImGui::SameLine();

    // Speed
    ImGui::SetNextItemWidth(50);
    ImGui::DragFloat("##anim_speed", &playbackSpeed, 0.01f, 0.01f, 10.0f, "%.1fx");
    ImGui::SameLine();

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Zoom
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##anim_zoom", &pixelsPerSecond, minPixelsPerSecond, maxPixelsPerSecond, "%.0f");
    ImGui::SameLine();

    // Snap
    ImGui::Checkbox("Snap", &snapToGrid);
    ImGui::SameLine();
    if (snapToGrid) {
        ImGui::SetNextItemWidth(50);
        ImGui::DragFloat("##snap_int", &snapInterval, 0.01f, 0.01f, 1.0f, "%.2f");
    }

    if (selectedFrameIndex >= 0 && selectedFrameIndex < (int)anim.actions.size()) {
        ActionFrame& frame = anim.actions[selectedFrameIndex];
        std::string actionLabel = getActionLabel(frame.action, scene);

        char infoBuf[256];
        snprintf(infoBuf, sizeof(infoBuf), "Frame %d | Track %u | Start %.2fs | Duration %.2fs | %s",
                 selectedFrameIndex, frame.track, frame.startTime, frame.duration, actionLabel.c_str());

        float infoWidth = ImGui::CalcTextSize(infoBuf).x;
        float cursorX = ImGui::GetCursorPosX();
        float inlinePadding = 24.0f;
        bool fitsInline = (width - cursorX) > (infoWidth + inlinePadding);

        if (fitsInline) {
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            float minInfoX = ImGui::GetCursorPosX();
            float alignedInfoX = width - infoWidth;
            if (alignedInfoX > minInfoX) {
                ImGui::SetCursorPosX(alignedInfoX);
            }
        } else {
            ImGui::Spacing();
        }

        ImGui::TextDisabled("%s", infoBuf);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Frame %d\nTrack: %u\nStart: %.2fs\nDuration: %.2fs\nAction: %s",
                              selectedFrameIndex, frame.track, frame.startTime, frame.duration, actionLabel.c_str());
        }
    }
}

void editor::AnimationWindow::drawTimeRuler(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float rulerHeight = 20.0f;
    float labelWidth = 120.0f;

    // Label column header
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + labelWidth, canvasPos.y + rulerHeight),
                            IM_COL32(50, 50, 50, 255));
    drawList->AddText(ImVec2(canvasPos.x + 4, canvasPos.y + 3), IM_COL32(150, 150, 150, 255), "Track");

    // Timeline ruler background
    drawList->AddRectFilled(ImVec2(canvasPos.x + labelWidth, canvasPos.y),
                            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight),
                            IM_COL32(40, 40, 40, 255));

    // Determine tick interval based on zoom
    float tickInterval = 1.0f;
    if (pixelsPerSecond > 200) tickInterval = 0.1f;
    else if (pixelsPerSecond > 80) tickInterval = 0.5f;

    float subTickInterval = tickInterval / 5.0f;
    ImVec2 timeOrigin(canvasPos.x + labelWidth, canvasPos.y);
    float timeAreaRight = canvasPos.x + canvasSize.x;

    // Draw ticks
    float t = std::floor(timeStart / tickInterval) * tickInterval;
    while (t <= timeEnd) {
        float x = timeToX(t, timeStart, timeOrigin);
        if (x >= timeOrigin.x && x <= timeAreaRight) {
            drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - 10), ImVec2(x, canvasPos.y + rulerHeight),
                              IM_COL32(180, 180, 180, 255));

            char buf[16];
            snprintf(buf, sizeof(buf), "%.1fs", t);
            drawList->AddText(ImVec2(x + 2, canvasPos.y + 2), IM_COL32(180, 180, 180, 255), buf);
        }
        t += tickInterval;
    }

    // Sub-ticks
    t = std::floor(timeStart / subTickInterval) * subTickInterval;
    while (t <= timeEnd) {
        float x = timeToX(t, timeStart, timeOrigin);
        if (x >= timeOrigin.x && x <= timeAreaRight) {
            drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - 5), ImVec2(x, canvasPos.y + rulerHeight),
                              IM_COL32(100, 100, 100, 255));
        }
        t += subTickInterval;
    }
}

bool editor::AnimationWindow::drawTracks(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd,
                                         AnimationComponent& anim, SceneProject* sceneProject) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Scene* scene = sceneProject->scene;
    float rulerHeight = 20.0f;
    float trackHeight = 24.0f;
    float trackPadding = 2.0f;
    float labelWidth = 120.0f;

    auto getFrameColor = [&](Entity actionEntity) -> ImU32 {
        if (actionEntity == NULL_ENTITY || !scene->isEntityCreated(actionEntity))
            return IM_COL32(120, 120, 120, 200); // Gray: Empty/invalid

        Signature sig = scene->getSignature(actionEntity);

        if (sig.test(scene->getComponentId<SpriteAnimationComponent>()))
            return IM_COL32(70, 180, 100, 200);  // Green: SpriteAnimation
        if (sig.test(scene->getComponentId<TranslateTracksComponent>()))
            return IM_COL32(100, 160, 220, 200); // Light blue: TranslateTracks
        if (sig.test(scene->getComponentId<RotateTracksComponent>()))
            return IM_COL32(220, 130, 70, 200);  // Orange: RotateTracks
        if (sig.test(scene->getComponentId<ScaleTracksComponent>()))
            return IM_COL32(180, 80, 180, 200);  // Magenta: ScaleTracks
        if (sig.test(scene->getComponentId<MorphTracksComponent>()))
            return IM_COL32(80, 190, 190, 200);  // Teal: MorphTracks
        if (sig.test(scene->getComponentId<KeyframeTracksComponent>()))
            return IM_COL32(70, 130, 180, 200);  // Steel blue: KeyframeTracks
        if (sig.test(scene->getComponentId<TimedActionComponent>()))
            return IM_COL32(180, 100, 70, 200);  // Warm red: TimedAction
        if (sig.test(scene->getComponentId<AnimationComponent>()))
            return IM_COL32(180, 170, 70, 200);  // Gold: Animation

        return IM_COL32(140, 70, 180, 200);      // Purple: generic Action
    };

    bool allowEditing = !isPreviewing;
    bool mouseOverFrame = false;

    // Find highest track
    uint32_t maxTrack = 0;
    for (const auto& frame : anim.actions) {
        if (frame.track > maxTrack) {
            maxTrack = frame.track;
        }
    }
    // Always draw at least 3 tracks, or maxTrack + 2 to give space to drag down
    uint32_t numTracks = std::max((uint32_t)3, maxTrack + 2);

    // 1. Draw track backgrounds and labels
    for (uint32_t t = 0; t < numTracks; t++) {
        float trackY = canvasPos.y + rulerHeight + t * (trackHeight + trackPadding);

        // Track background
        ImU32 bgColor = (t % 2 == 0) ? IM_COL32(35, 35, 35, 255) : IM_COL32(45, 45, 45, 255);
        drawList->AddRectFilled(ImVec2(canvasPos.x, trackY),
                                ImVec2(canvasPos.x + canvasSize.x, trackY + trackHeight), bgColor);

        // Label area
        std::string label = "Track " + std::to_string(t);
        drawList->AddRectFilled(ImVec2(canvasPos.x, trackY),
                                ImVec2(canvasPos.x + labelWidth, trackY + trackHeight),
                                IM_COL32(50, 50, 50, 255));
        drawList->AddText(ImVec2(canvasPos.x + 4, trackY + 4), IM_COL32(200, 200, 200, 255), label.c_str());
    }

    // 2. Draw blocks
    for (size_t i = 0; i < anim.actions.size(); i++) {
        ActionFrame& frame = anim.actions[i];
        float trackY = canvasPos.y + rulerHeight + frame.track * (trackHeight + trackPadding);

        // Action frame block
        float blockStart = timeToX(frame.startTime, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
        float blockEnd = timeToX(frame.startTime + frame.duration, timeStart, ImVec2(canvasPos.x + labelWidth, 0));

        // Clamp to visible area
        float visStart = std::max(blockStart, canvasPos.x + labelWidth);
        float visEnd = std::min(blockEnd, canvasPos.x + canvasSize.x);

        if (visEnd > visStart) {
            ImU32 blockColor = getFrameColor(frame.action);
            drawList->AddRectFilled(ImVec2(visStart, trackY + 2), ImVec2(visEnd, trackY + trackHeight - 2),
                                    blockColor, 3.0f);

            if ((int)i == selectedFrameIndex) {
                drawList->AddRect(ImVec2(visStart, trackY + 2), ImVec2(visEnd, trackY + trackHeight - 2),
                                  IM_COL32(255, 220, 120, 255), 3.0f, 0, 2.0f);
            }

            // Block label with progressive truncation
            std::string indexStr = std::to_string(i);
            std::string fullLabel = indexStr + ": " + getActionLabel(frame.action, scene);
            std::string targetName;
            if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
                ActionComponent* frameAction = scene->findComponent<ActionComponent>(frame.action);
                if (frameAction && frameAction->target != NULL_ENTITY && scene->isEntityCreated(frameAction->target)) {
                    targetName = scene->getEntityName(frameAction->target);
                    if (!targetName.empty()) {
                        fullLabel += " | " + targetName;
                    }
                }
            }
            float availWidth = visEnd - visStart - 8;
            const char* displayLabel = nullptr;
            bool isFullLabel = false;
            if (ImGui::CalcTextSize(fullLabel.c_str()).x <= availWidth) {
                displayLabel = fullLabel.c_str();
                isFullLabel = true;
            } else {
                std::string mediumLabel = indexStr + ": " + targetName;
                if (!targetName.empty() && ImGui::CalcTextSize(mediumLabel.c_str()).x <= availWidth) {
                    fullLabel = mediumLabel;
                    displayLabel = fullLabel.c_str();
                } else if (ImGui::CalcTextSize(indexStr.c_str()).x <= availWidth) {
                    displayLabel = indexStr.c_str();
                }
            }
            if (displayLabel) {
                drawList->AddText(ImVec2(visStart + 4, trackY + 4),
                                  IM_COL32(255, 255, 255, 255), displayLabel);
            }

            // Interaction: click to select, drag to move/resize
            float edgeZone = 5.0f;
            ImVec2 blockMin(visStart, trackY);
            ImVec2 blockMax(visEnd, trackY + trackHeight);
            ImGui::SetCursorScreenPos(blockMin);
            ImGui::InvisibleButton(("frame_" + std::to_string(i)).c_str(),
                                   ImVec2(blockMax.x - blockMin.x, blockMax.y - blockMin.y));

            // Detect edge hover for resize cursor
            bool hovered = ImGui::IsItemHovered();
            mouseOverFrame = mouseOverFrame || hovered || ImGui::IsItemActive();
            if (hovered && !isFullLabel) {
                std::string actionLabel = getActionLabel(frame.action, scene);
                std::string tooltip = std::to_string(i) + ": " + actionLabel;
                if (!targetName.empty()) {
                    tooltip += " | " + targetName;
                }
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            if (allowEditing && hovered && !isDraggingFrame && !isResizingFrame) {
                float mouseX = ImGui::GetIO().MousePos.x;
                bool onLeftEdge = (mouseX - blockStart) < edgeZone && (mouseX >= blockStart);
                bool onRightEdge = (blockEnd - mouseX) < edgeZone && (mouseX <= blockEnd);
                if (onLeftEdge || onRightEdge) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                selectedFrameIndex = (int)i;
                if (allowEditing) {
                    float mouseX = ImGui::GetIO().MousePos.x;
                    bool onLeftEdge = (mouseX - blockStart) < edgeZone && (mouseX >= blockStart);
                    bool onRightEdge = (blockEnd - mouseX) < edgeZone && (mouseX <= blockEnd);

                    if (onLeftEdge || onRightEdge) {
                        isResizingFrame = true;
                        resizingFrameIndex = (int)i;
                        resizeSide = onLeftEdge ? -1 : 1;
                        resizeStartTime = frame.startTime;
                        resizeStartDuration = frame.duration;
                    } else {
                        isDraggingFrame = true;
                        draggingFrameIndex = (int)i;
                        dragStartTime = frame.startTime;
                        dragStartTrack = frame.track;
                    }
                }
            }
        }
    }

    // Handle frame dragging
    if (allowEditing && isDraggingFrame && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        draggingFrameIndex >= 0 && draggingFrameIndex < (int)anim.actions.size()) {
        float mouseDragDeltaX = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
        float timeDelta = mouseDragDeltaX / pixelsPerSecond;
        float newStart = dragStartTime + timeDelta;

        float mouseDragDeltaY = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
        int trackDelta = (int)std::round(mouseDragDeltaY / (trackHeight + trackPadding));
        int newTrack = std::max(0, (int)dragStartTrack + trackDelta);

        ActionFrame& frame = anim.actions[draggingFrameIndex];
        float snappedStart = std::max(0.0f, snapTime(newStart));
        float frameDur = frame.duration;

        // Find valid position on a track closest to desiredStart without overlap
        auto findValidPosition = [&](float desiredStart, int track) -> float {
            // Collect occupied intervals on this track (excluding dragged frame)
            std::vector<std::pair<float, float>> occupied;
            for (size_t i = 0; i < anim.actions.size(); i++) {
                if ((int)i == draggingFrameIndex) continue;
                const auto& a = anim.actions[i];
                if ((int)a.track == track) {
                    occupied.push_back({a.startTime, a.startTime + a.duration});
                }
            }

            if (occupied.empty()) return std::max(0.0f, desiredStart);

            std::sort(occupied.begin(), occupied.end());

            float bestStart = desiredStart;
            float bestDist = 1e9f;

            auto tryGap = [&](float gapLo, float gapHi) {
                float maxStart = gapHi - frameDur;
                if (maxStart >= gapLo) {
                    float clamped = std::clamp(desiredStart, gapLo, maxStart);
                    float dist = std::abs(clamped - desiredStart);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestStart = clamped;
                    }
                }
            };

            // Gap before first block
            tryGap(0.0f, occupied[0].first);
            // Gaps between blocks
            for (size_t j = 0; j + 1 < occupied.size(); j++) {
                tryGap(occupied[j].second, occupied[j + 1].first);
            }
            // Gap after last block
            tryGap(occupied.back().second, 1e9f);

            return bestStart;
        };

        float validPos = findValidPosition(snappedStart, newTrack);
        frame.startTime = validPos;
        frame.track = newTrack;
        sceneProject->isModified = true;
    }

    // Handle frame resizing
    if (allowEditing && isResizingFrame && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        resizingFrameIndex >= 0 && resizingFrameIndex < (int)anim.actions.size()) {
        float mouseDragDeltaX = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
        float timeDelta = mouseDragDeltaX / pixelsPerSecond;
        ActionFrame& frame = anim.actions[resizingFrameIndex];
        float minDuration = 0.01f;

        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        // Find nearest neighbors on the same track to prevent overlap
        float neighborLeftEnd = 0.0f;   // max end time of actions to our left
        float neighborRightStart = 1e9f; // min start time of actions to our right
        float origEnd = resizeStartTime + resizeStartDuration;
        for (size_t i = 0; i < anim.actions.size(); i++) {
            if ((int)i == resizingFrameIndex) continue;
            const auto& a = anim.actions[i];
            if (a.track != frame.track) continue;
            float aEnd = a.startTime + a.duration;
            if (aEnd <= resizeStartTime + 0.001f) {
                neighborLeftEnd = std::max(neighborLeftEnd, aEnd);
            }
            if (a.startTime >= origEnd - 0.001f) {
                neighborRightStart = std::min(neighborRightStart, a.startTime);
            }
        }

        if (resizeSide == 1) {
            // Right edge: only change duration, clamp to neighbor on right
            float newDuration = snapTime(resizeStartDuration + timeDelta);
            float maxDuration = neighborRightStart - frame.startTime;
            frame.duration = std::clamp(newDuration, minDuration, maxDuration);
        } else {
            // Left edge: move start and adjust duration (right end stays fixed)
            float newStart = snapTime(resizeStartTime + timeDelta);
            float endTime = resizeStartTime + resizeStartDuration;
            newStart = std::clamp(newStart, neighborLeftEnd, endTime - minDuration);
            frame.startTime = newStart;
            frame.duration = endTime - newStart;
        }
        sceneProject->isModified = true;
    }

    if (allowEditing && isResizingFrame && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (resizingFrameIndex >= 0 && resizingFrameIndex < (int)anim.actions.size()) {
            ActionFrame& frame = anim.actions[resizingFrameIndex];

            bool timeChanged = (frame.startTime != resizeStartTime);
            bool durationChanged = (frame.duration != resizeStartDuration);

            if (timeChanged || durationChanged) {
                float finalStartTime = frame.startTime;
                float finalDuration = frame.duration;

                // Revert so cmd logs exactly old -> new
                frame.startTime = resizeStartTime;
                frame.duration = resizeStartDuration;

                if (timeChanged && durationChanged) {
                    auto* multiCmd = new MultiPropertyCmd();
                    multiCmd->addPropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    multiCmd->addPropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].duration", finalDuration,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
                } else if (durationChanged) {
                    auto* cmd = new PropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].duration", finalDuration,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                } else if (timeChanged) {
                    auto* cmd = new PropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                }
            }
        }

        isResizingFrame = false;
        resizingFrameIndex = -1;
        resizeSide = 0;
    }

    if (allowEditing && isDraggingFrame && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (draggingFrameIndex >= 0 && draggingFrameIndex < (int)anim.actions.size()) {
            ActionFrame& frame = anim.actions[draggingFrameIndex];

            bool timeChanged = (frame.startTime != dragStartTime);
            bool trackChanged = (frame.track != dragStartTrack);

            if (timeChanged || trackChanged) {
                float finalStartTime = frame.startTime;
                uint32_t finalTrack = frame.track;

                // Revert so cmd logs exactly old -> new
                frame.startTime = dragStartTime;
                frame.track = dragStartTrack;

                if (timeChanged && trackChanged) {
                    auto* multiCmd = new MultiPropertyCmd();
                    multiCmd->addPropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent, 
                        "actions[" + std::to_string(draggingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    multiCmd->addPropertyCmd<uint32_t>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent, 
                        "actions[" + std::to_string(draggingFrameIndex) + "].track", finalTrack,
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
                } else if (timeChanged) {
                    auto* cmd = new PropertyCmd<float>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent, 
                        "actions[" + std::to_string(draggingFrameIndex) + "].startTime", finalStartTime, 
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                } else if (trackChanged) {
                    auto* cmd = new PropertyCmd<uint32_t>(
                        project, selectedSceneId, selectedEntity, ComponentType::AnimationComponent, 
                        "actions[" + std::to_string(draggingFrameIndex) + "].track", finalTrack, 
                        [sceneProject]() { sceneProject->isModified = true; }
                    );
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                }
            }
        }

        isDraggingFrame = false;
        draggingFrameIndex = -1;
    }

    return mouseOverFrame;
}

bool editor::AnimationWindow::drawPlayhead(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float labelWidth = 120.0f;

    float playheadX = canvasPos.x + labelWidth + (currentTime - timeStart) * pixelsPerSecond;

    if (playheadX >= canvasPos.x + labelWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(ImVec2(playheadX, canvasPos.y),
                          ImVec2(playheadX, canvasPos.y + canvasSize.y),
                          IM_COL32(255, 80, 80, 255), 2.0f);

        // Playhead triangle at top
        drawList->AddTriangleFilled(
            ImVec2(playheadX - 5, canvasPos.y),
            ImVec2(playheadX + 5, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + 8),
            IM_COL32(255, 80, 80, 255));
    }

    ImVec2 timeAreaMin(canvasPos.x + labelWidth, canvasPos.y);
    ImVec2 timeAreaMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseInTimeArea = mousePos.x >= timeAreaMin.x && mousePos.x <= timeAreaMax.x &&
                           mousePos.y >= timeAreaMin.y && mousePos.y <= timeAreaMax.y;

    bool scrubbed = false;
    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (isDraggingPlayhead && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) &&
        ImGui::IsWindowHovered() && mouseInTimeArea && !isDraggingFrame && !isResizingFrame) {
        float newTime = xToTime(mousePos.x, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
        currentTime = snapTime(std::max(0.0f, newTime));
        isDraggingPlayhead = true;
        scrubbed = true;
    }

    if (isDraggingPlayhead && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDraggingPlayhead = false;
    }

    return scrubbed;
}

void editor::AnimationWindow::show() {
    static bool preventScroll = false;

    if (hasNotification) {
        App::pushTabNotificationStyle();
    }

    ImGuiWindowFlags flags = hasNotification ? ImGuiWindowFlags_UnsavedDocument : 0;
    if (preventScroll) flags |= ImGuiWindowFlags_NoScrollWithMouse;

    isWindowVisible = ImGui::Begin(AnimationWindow::WINDOW_NAME, nullptr, flags);
    if (hasNotification) App::popTabNotificationStyle();

    if (isWindowVisible) {
        hasNotification = false;
    }

    auto restorePreviewScene = [&]() {
        SceneProject* previewSceneProject = project->getScene(selectedSceneId);
        if (previewSceneProject && previewSceneProject->scene) {
            stopPreview(previewSceneProject->scene, previewSceneProject);
        } else {
            previewState.clear();
            isPreviewing = false;
        }

        isPlaying = false;
    };

    SceneProject* sceneProject = project->getSelectedScene();
    if (!sceneProject) {
        if (isPreviewing) {
            restorePreviewScene();
        }
        ImGui::TextDisabled("No scene selected.");
        ImGui::End();
        return;
    }

    Scene* scene = sceneProject->scene;
    if (!scene) {
        if (isPreviewing) {
            restorePreviewScene();
        }
        ImGui::TextDisabled("No scene available.");
        ImGui::End();
        return;
    }

    if (isPreviewing && sceneProject->id != selectedSceneId) {
        restorePreviewScene();
        currentTime = 0;
    }

    // Collect all entities with AnimationComponent in the scene
    auto animations = scene->getComponentArray<AnimationComponent>();
    if (animations->size() == 0) {
        if (isPreviewing) {
            restorePreviewScene();
            currentTime = 0;
        }
        selectedEntity = NULL_ENTITY;
        selectedSceneId = 0;
        ImGui::TextDisabled("No entities with AnimationComponent in this scene.");
        ImGui::End();
        return;
    }

    // Validate current selection still exists
    if (selectedEntity != NULL_ENTITY && !scene->findComponent<AnimationComponent>(selectedEntity)) {
        if (isPreviewing) {
            restorePreviewScene();
            currentTime = 0;
        }
        selectedEntity = NULL_ENTITY;
    }

    // Auto-select from scene selection if user clicked a new animation entity
    // We only update if the selected entities in the project changed to avoid overriding combo selection
    static std::vector<Entity> lastSceneSelectedEntities;
    std::vector<Entity> selectedEntities = project->getSelectedEntities(sceneProject->id);

    if (selectedEntities != lastSceneSelectedEntities) {
        lastSceneSelectedEntities = selectedEntities;
        bool foundAnimation = false;
        for (Entity entity : selectedEntities) {
            if (scene->findComponent<AnimationComponent>(entity)) {
                if (selectedEntity != entity) {
                    selectEntity(entity, sceneProject->id);
                }
                if (!isWindowVisible) {
                    hasNotification = true;
                }
                foundAnimation = true;
                break;
            }
        }
        if (!foundAnimation) {
            hasNotification = false;
        }
    }

    // Default to first animation entity if nothing selected
    if (selectedEntity == NULL_ENTITY) {
        selectedEntity = animations->getEntity(0);
        selectedSceneId = sceneProject->id;
        currentTime = 0;
        isPlaying = false;
        selectedFrameIndex = -1;
    }

    AnimationComponent* animComp = &scene->getComponent<AnimationComponent>(selectedEntity);

    // Auto-assign tracks when overlapping frames are detected on the same track
    {
        bool hasOverlap = false;
        for (size_t i = 0; i < animComp->actions.size() && !hasOverlap; i++) {
            for (size_t j = i + 1; j < animComp->actions.size() && !hasOverlap; j++) {
                if (animComp->actions[i].track == animComp->actions[j].track) {
                    float iEnd = animComp->actions[i].startTime + animComp->actions[i].duration;
                    float jEnd = animComp->actions[j].startTime + animComp->actions[j].duration;
                    if (animComp->actions[i].startTime < jEnd && animComp->actions[j].startTime < iEnd) {
                        hasOverlap = true;
                    }
                }
            }
        }
        if (hasOverlap) {
            // Build sorted indices by (track, startTime, index)
            std::vector<size_t> sorted(animComp->actions.size());
            std::iota(sorted.begin(), sorted.end(), 0);
            std::sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
                if (animComp->actions[a].track != animComp->actions[b].track)
                    return animComp->actions[a].track < animComp->actions[b].track;
                if (animComp->actions[a].startTime != animComp->actions[b].startTime)
                    return animComp->actions[a].startTime < animComp->actions[b].startTime;
                return a < b;
            });

            // Greedy lane packing
            std::vector<float> laneEnds;
            for (size_t idx : sorted) {
                ActionFrame& frame = animComp->actions[idx];
                int lane = -1;
                for (int l = 0; l < (int)laneEnds.size(); l++) {
                    if (frame.startTime >= laneEnds[l]) {
                        lane = l;
                        laneEnds[l] = frame.startTime + frame.duration;
                        break;
                    }
                }
                if (lane == -1) {
                    lane = (int)laneEnds.size();
                    laneEnds.push_back(frame.startTime + frame.duration);
                }
                frame.track = (uint32_t)lane;
            }
            sceneProject->isModified = true;
        }
    }

    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);
    if (isPreviewing && !sceneIsStopped) {
        stopPreview(scene, sceneProject);
        isPlaying = false;
        currentTime = 0;
    }

    // Playback update
    if (isPreviewing && sceneIsStopped) {
        // Engine-driven preview: ActionSystem processes the animation
        if (isPlaying) {
            float dt = ImGui::GetIO().DeltaTime * playbackSpeed;
            scene->getSystem<ActionSystem>()->updateAnimationPreview(dt, selectedEntity);
            sceneProject->needUpdateRender = true;
        }

        // Sync currentTime from engine state
        ActionComponent& action = scene->getComponent<ActionComponent>(selectedEntity);

        // Check if animation finished naturally
        if (action.state == ActionState::Stopped && isPlaying) {
            isPlaying = false;
        } else {
            currentTime = action.timecount;
        }
    } else if (isPlaying) {
        // Fallback: visual-only playback (when scene is playing or no preview)
        float dt = ImGui::GetIO().DeltaTime * playbackSpeed;
        currentTime += dt;

        float duration = animComp->duration;
        if (duration > 0 && currentTime > duration) {
            if (animComp->loop) {
                currentTime = std::fmod(currentTime, duration);
            } else {
                currentTime = duration;
                isPlaying = false;
            }
        }
    }

    // Draw toolbar
    drawToolbar(ImGui::GetContentRegionAvail().x, *animComp, scene, sceneProject);

    ImGui::Separator();

    // Timeline canvas
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.y < 60) canvasSize.y = 60;

    float labelWidth = 120.0f;
    float timeStart = scrollX / pixelsPerSecond;
    float visibleTime = (canvasSize.x - labelWidth) / pixelsPerSecond;
    float timeEnd = timeStart + visibleTime;

    // Handle mouse wheel zoom
    bool mouseAboveTracks = ImGui::IsWindowHovered() && ImGui::GetMousePos().y <= (canvasPos.y + 20.0f);

    preventScroll = mouseAboveTracks;

    if (mouseAboveTracks && ImGui::GetIO().MouseWheel != 0) {
        float zoomFactor = 1.0f + ImGui::GetIO().MouseWheel * 0.1f;
        pixelsPerSecond = std::clamp(pixelsPerSecond * zoomFactor, minPixelsPerSecond, maxPixelsPerSecond);
    }

    // Draw background
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                            IM_COL32(30, 30, 30, 255));

    drawTimeRuler(canvasPos, canvasSize, timeStart, timeEnd);
    bool mouseOverFrame = drawTracks(canvasPos, canvasSize, timeStart, timeEnd, *animComp, sceneProject);
    bool scrubbed = drawPlayhead(canvasPos, canvasSize, timeStart, timeEnd);

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseInCanvas = mousePos.x >= canvasPos.x && mousePos.x <= canvasPos.x + canvasSize.x &&
                         mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + canvasSize.y;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && mouseInCanvas &&
        !mouseOverFrame && !isDraggingFrame && !isResizingFrame) {
        selectedFrameIndex = -1;
    }

    if (scrubbed) {
        if (sceneIsStopped && canPreviewEntity(selectedEntity, scene)) {
            seekPreview(scene, sceneProject, currentTime);
        } else {
            currentTime = std::max(0.0f, std::min(currentTime, getAnimationDuration(*animComp)));
        }
    }

    // Reserve space
    ImGui::Dummy(canvasSize);

    // Horizontal scroll
    float totalTime = 10.0f; // default visible range
    if (animComp->duration > 0) totalTime = animComp->duration * 1.5f;
    float maxScroll = std::max(0.0f, totalTime * pixelsPerSecond - canvasSize.x + labelWidth);
    if (scrollX > maxScroll) scrollX = maxScroll;

    ImGui::End();
}

bool editor::AnimationWindow::isPreviewingEntity(Entity entity, uint32_t sceneId) const {
    return isPreviewing && selectedEntity == entity && selectedSceneId == sceneId;
}

bool editor::AnimationWindow::getIsPlaying() const {
    return isPlaying;
}

bool editor::AnimationWindow::getIsPreviewing() const {
    return isPreviewing;
}

float editor::AnimationWindow::getCurrentTime() const {
    return currentTime;
}

void editor::AnimationWindow::externalPlay(Entity entity, uint32_t sceneId) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return;
    Scene* scene = sceneProject->scene;

    if (selectedEntity != entity || selectedSceneId != sceneId) {
        selectEntity(entity, sceneId);
    }

    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);

    if (sceneIsStopped && !isPreviewing) {
        currentTime = 0;
        startPreview(scene, sceneProject);
    } else if (sceneIsStopped && isPreviewing) {
        ActionComponent& action = sceneProject->scene->getComponent<ActionComponent>(selectedEntity);
        if (action.state == ActionState::Stopped) {
            seekPreview(scene, sceneProject, 0.0f);
        }
    }
    isPlaying = true;
}

void editor::AnimationWindow::externalStop() {
    SceneProject* sceneProject = project->getScene(selectedSceneId);
    if (!sceneProject || !sceneProject->scene) return;

    isPlaying = false;
    currentTime = 0;
    if (isPreviewing) {
        stopPreview(sceneProject->scene, sceneProject);
    }
}

void editor::AnimationWindow::externalPause() {
    isPlaying = false;
}

void editor::AnimationWindow::seekPreviewExternal(Scene* scene, SceneProject* sceneProject, float time) {
    seekPreview(scene, sceneProject, time);
}
