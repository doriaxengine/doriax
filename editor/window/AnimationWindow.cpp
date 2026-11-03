// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AnimationWindow.h"

#include "imgui_internal.h"
#include "external/IconsFontAwesome6.h"
#include "Catalog.h"
#include "Stream.h"
#include "App.h"
#include "Theme.h"
#include "util/UIUtils.h"
#include "command/CommandHandle.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/CreateTrackKeyCmd.h"
#include "command/type/AddActionFrameCmd.h"

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
#include "component/PositionActionComponent.h"
#include "component/RotationActionComponent.h"
#include "component/ScaleActionComponent.h"
#include "component/ColorActionComponent.h"
#include "component/AlphaActionComponent.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/MeshSystem.h"
#include "action/Animation.h"
#include "util/EntityPayload.h"
#include "util/ProjectUtils.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <unordered_set>

using namespace doriax;

namespace {

struct TimelineMetrics {
    float scale;
    float rulerHeight;
    float trackHeight;
    float trackPadding;
    float labelWidth;
    float trackEndPadding;
    float minBlockWidth;
    float tickMajor;
    float tickMinor;
    float playheadHalf;
    float playheadTip;
    float playheadThickness;
    float targetMajorTickSpacing;
    float inset;
    float textPad;
    float blockRounding;
    float selectionThickness;
    float keyMarkerRadius;
    float keyHitRadius;
    float resizeEdgeZone;
};

TimelineMetrics timelineMetrics() {
    const float s = editor::Theme::dpiScale();
    TimelineMetrics m;
    m.scale = s;
    m.rulerHeight = 20.0f * s;
    m.trackHeight = 24.0f * s;
    m.trackPadding = 2.0f * s;
    m.labelWidth = 80.0f * s;
    m.trackEndPadding = 32.0f * s;
    m.minBlockWidth = 10.0f * s;
    m.tickMajor = 10.0f * s;
    m.tickMinor = 5.0f * s;
    m.playheadHalf = 5.0f * s;
    m.playheadTip = 8.0f * s;
    m.playheadThickness = 2.0f * s;
    m.targetMajorTickSpacing = 80.0f * s;
    m.inset = 2.0f * s;
    m.textPad = 4.0f * s;
    m.blockRounding = 3.0f * s;
    m.selectionThickness = 2.0f * s;
    m.keyMarkerRadius = 3.0f * s;
    m.keyHitRadius = 6.0f * s;
    m.resizeEdgeZone = 5.0f * s;
    return m;
}

// Lane packing converts a minimum *drawn* block width into time. Keep this
// logical so opening the same clip at another DPI does not rewrite tracks.
constexpr float kLogicalMinTimelineBlockWidth = 10.0f;

// Display-only lanes for short blocks that overlap after DPI min-width
// inflation. Does not write ActionFrame::track.
void assignVisualSubLanes(const std::vector<ActionFrame>& actions,
                          const std::vector<float>& visualEndPx,
                          uint32_t numTracks,
                          float pixelsPerSecond,
                          std::vector<int>& outSubLane,
                          std::vector<int>& outLaneCount) {
    outSubLane.assign(actions.size(), 0);
    outLaneCount.assign(numTracks, 1);
    std::vector<std::vector<size_t>> byTrack(numTracks);
    for (size_t i = 0; i < actions.size(); i++) {
        uint32_t track = actions[i].track;
        if (track >= numTracks) {
            continue;
        }
        byTrack[track].push_back(i);
    }
    for (uint32_t t = 0; t < numTracks; t++) {
        auto& indices = byTrack[t];
        if (indices.empty()) {
            continue;
        }
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            float aStart = actions[a].startTime * pixelsPerSecond;
            float bStart = actions[b].startTime * pixelsPerSecond;
            if (aStart != bStart) {
                return aStart < bStart;
            }
            return a < b;
        });
        std::vector<float> laneEnds;
        for (size_t idx : indices) {
            float startPx = actions[idx].startTime * pixelsPerSecond;
            float endPx = visualEndPx[idx];
            int lane = -1;
            for (int l = 0; l < (int)laneEnds.size(); l++) {
                if (startPx >= laneEnds[l]) {
                    lane = l;
                    laneEnds[l] = endPx;
                    break;
                }
            }
            if (lane == -1) {
                lane = (int)laneEnds.size();
                laneEnds.push_back(endPx);
            }
            outSubLane[idx] = lane;
        }
        outLaneCount[t] = std::max(1, (int)laneEnds.size());
    }
}

}

editor::AnimationWindow::AnimationWindow(Project* project){
    this->project = project;

    isPlaying = false;
    currentTime = 0;
    playbackSpeed = 1.0f;

    pixelsPerSecond = 100.0f;
    scrollX = 0;
    scrollY = 0;
    minPixelsPerSecond = 20.0f;
    maxPixelsPerSecond = 500.0f;
    playbackFollowSuspended = false;

    selectedFrameIndex = -1;
    prePlaySelectedFrameIndex = -1;
    isDraggingPlayhead = false;
    isDraggingFrame = false;
    draggingFrameIndex = -1;
    dragStartTime = 0;
    dragStartTrack = 0;
    selectedKeyFrameIndex = -1;
    selectedKeyIndex = -1;
    isDraggingKey = false;
    keyDragStartTime = 0;
    keyDragTime = 0;

    isResizingFrame = false;
    resizingFrameIndex = -1;
    resizeSide = 0;
    resizeStartTime = 0;
    resizeStartDuration = 0;

    snapToGrid = true;
    snapInterval = 0.1f;

    transitionTarget = NULL_ENTITY;
    previewPrimary = NULL_ENTITY;

    hasNotification = false;
    windowOpen = true;
    focusRequested = false;
    isWindowVisible = false;

    isPreviewing = false;
    keyPopupFrameIndex = -1;
    keyPopupTime = 0;
    keyNoticeAt = 0;

    selectedEntity = NULL_ENTITY;
    selectedSceneId = 0;
}


void editor::AnimationWindow::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
        }
        windowOpen = true;
        return;
    }

    windowOpen = false;
    focusRequested = false;
    isWindowVisible = false;

    SceneProject* previewSceneProject = project->getScene(selectedSceneId);
    if (isDraggingKey || isDraggingFrame || isResizingFrame) {
        finishTimelineDrag(previewSceneProject ? previewSceneProject->scene : nullptr,
                           previewSceneProject, false);
    }
    if (isPreviewing) {
        if (previewSceneProject && previewSceneProject->scene) {
            stopPreview(previewSceneProject->scene, previewSceneProject);
        } else {
            previewState.clear();
            isPreviewing = false;
        }
    }

    isPlaying = false;
}

bool editor::AnimationWindow::isOpen() const{
    return windowOpen;
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
    if (!scene->isEntityCreated(actionEntity)) return "Empty";

    Signature sig = scene->getSignature(actionEntity);

    if (sig.test(scene->getComponentId<SpriteAnimationComponent>())) {
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
        if (sig.test(scene->getComponentId<PositionActionComponent>()))
            return "PositionAction";
        if (sig.test(scene->getComponentId<RotationActionComponent>()))
            return "RotationAction";
        if (sig.test(scene->getComponentId<ScaleActionComponent>()))
            return "ScaleAction";
        if (sig.test(scene->getComponentId<ColorActionComponent>()))
            return "ColorAction";
        if (sig.test(scene->getComponentId<AlphaActionComponent>()))
            return "AlphaAction";
        return "TimedAction";
    }

    if (sig.test(scene->getComponentId<AnimationComponent>())) {
        std::string name = scene->getEntityName(actionEntity);
        if (!name.empty()) return name;
        return "Animation";
    }

    return "Action";
}

void editor::AnimationWindow::selectEntity(Entity entity, uint32_t sceneId) {
    if (selectedEntity != entity || selectedSceneId != sceneId) {
        SceneProject* previousSceneProject = project->getScene(selectedSceneId);
        if (isDraggingKey || isDraggingFrame || isResizingFrame) {
            finishTimelineDrag(previousSceneProject ? previousSceneProject->scene : nullptr,
                               previousSceneProject, false);
        }
        // Stop any active preview before changing entity
        if (isPreviewing) {
            if (previousSceneProject && previousSceneProject->scene) {
                stopPreview(previousSceneProject->scene, previousSceneProject);
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
        selectedKeyFrameIndex = -1;
        selectedKeyIndex = -1;
        isDraggingKey = false;
    }
}

Entity editor::AnimationWindow::timelineClip(Scene* scene) const {
    // During a transition preview the timeline follows the clip the preview blended
    // into; selection and authoring stay on selectedEntity.
    if (isPreviewing && scene && previewPrimary != NULL_ENTITY &&
        scene->isEntityCreated(previewPrimary) &&
        scene->findComponent<AnimationComponent>(previewPrimary)) {
        return previewPrimary;
    }
    return selectedEntity;
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

    // Preview playback reads authored key data but does not mutate it. Keeping
    // these components in the restore snapshot would overwrite keys authored
    // while a scrub preview is paused (Snapshot or right-click keying).
    components.remove(Catalog::getComponentName(ComponentType::KeyframeTracksComponent, true));
    components.remove(Catalog::getComponentName(ComponentType::TranslateTracksComponent, true));
    components.remove(Catalog::getComponentName(ComponentType::RotateTracksComponent, true));
    components.remove(Catalog::getComponentName(ComponentType::ScaleTracksComponent, true));
    components.remove(Catalog::getComponentName(ComponentType::MorphTracksComponent, true));
    state.components = components;

    return state;
}

void editor::AnimationWindow::restorePreviewState(Scene* scene) {
    for (PreviewEntityState& state : previewState) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
            continue;
        }

        // settings edited during the preview are kept
        ProjectUtils::updateActionPreviewSnapshot(state.entity, state.components, scene);
        Stream::decodeComponents(state.entity, state.parent, scene, state.components);

        // left out of the snapshot, so its base is dropped here
        if (KeyframeTracksComponent* keyframe = scene->findComponent<KeyframeTracksComponent>(state.entity)) {
            keyframe->hasBase = false;
        }
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
            meshSystem->resetModelToBindPose(modelEntity, *model);
        }
    }
}

float editor::AnimationWindow::effectiveFrameDuration(const ActionFrame& frame, Scene* scene) const {
    return scene->getSystem<ActionSystem>()->getFrameDuration(frame);
}

float editor::AnimationWindow::getAnimationDuration(const AnimationComponent& anim, Scene* scene) const {
    if (anim.duration > 0) {
        return anim.duration;
    }

    float duration = 0.0f;
    for (const ActionFrame& frame : anim.actions) {
        duration = std::max(duration, frame.startTime + effectiveFrameDuration(frame, scene));
    }

    return duration;
}

void editor::AnimationWindow::autoAssignTracks(Entity entity, AnimationComponent& anim, SceneProject* sceneProject) const {
    Scene* scene = sceneProject->scene;
    // Overlap uses the same logical minimum as a 1x display. Drawing still
    // inflates short blocks by DPI, but packing must not depend on it or the
    // clip's tracks change just from opening the window on another monitor.
    const float minimumLayoutDuration = kLogicalMinTimelineBlockWidth / std::max(pixelsPerSecond, 1.0f);
    auto layoutFrameEnd = [&](const ActionFrame& frame) {
        return frame.startTime + std::max(effectiveFrameDuration(frame, scene), minimumLayoutDuration);
    };

    bool hasOverlap = false;
    for (size_t i = 0; i < anim.actions.size() && !hasOverlap; i++) {
        for (size_t j = i + 1; j < anim.actions.size() && !hasOverlap; j++) {
            if (anim.actions[i].track == anim.actions[j].track) {
                float iEnd = layoutFrameEnd(anim.actions[i]);
                float jEnd = layoutFrameEnd(anim.actions[j]);
                if (anim.actions[i].startTime < jEnd && anim.actions[j].startTime < iEnd) {
                    hasOverlap = true;
                }
            }
        }
    }
    if (hasOverlap) {
        // Build sorted indices by (track, startTime, index)
        std::vector<size_t> sorted(anim.actions.size());
        std::iota(sorted.begin(), sorted.end(), 0);
        std::sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
            if (anim.actions[a].track != anim.actions[b].track)
                return anim.actions[a].track < anim.actions[b].track;
            if (anim.actions[a].startTime != anim.actions[b].startTime)
                return anim.actions[a].startTime < anim.actions[b].startTime;
            return a < b;
        });

        // Greedy lane packing
        std::vector<float> laneEnds;
        for (size_t idx : sorted) {
            ActionFrame& frame = anim.actions[idx];
            float frameEnd = layoutFrameEnd(frame);
            int lane = -1;
            for (int l = 0; l < (int)laneEnds.size(); l++) {
                if (frame.startTime >= laneEnds[l]) {
                    lane = l;
                    laneEnds[l] = frameEnd;
                    break;
                }
            }
            if (lane == -1) {
                lane = (int)laneEnds.size();
                laneEnds.push_back(frameEnd);
            }
            frame.track = (uint32_t)lane;
        }
        // A loaded model rebuilds its own animations, only saved ones dirty the scene
        if (std::find(sceneProject->entities.begin(), sceneProject->entities.end(), entity) != sceneProject->entities.end()) {
            sceneProject->isModified = true;
        }
    }
}

namespace {

constexpr float kKeyTimeEpsilon = 0.001f;
// Finite ceiling for authoring beyond the current clip duration. This keeps
// ImGui's drag math well behaved while allowing auto-duration tracks to grow.
constexpr float kAuthoringMaxTime = 3600.0f;

// Insert or update a keyframe on a tracks entity. Updates merge into one undo
// step; inserts keep times sorted, split the segment easing so both halves
// keep the original curve, and keep cubic-spline tangents mirrored with values.
// Passing `batch` collects the writes into that command instead of issuing its
// own, so several tracks can be keyed as a single undo step (snapshot). The
// batch must not receive two writes to the same track in one pass, since each
// write reads the live arrays (callers key each track entity at most once).
template<typename TracksComp, typename ValueT>
void writeTrackKey(editor::Project* project, editor::SceneProject* sceneProject, Scene* scene,
                   Entity trackEntity, editor::ComponentType tracksType, float time, const ValueT& value,
                   editor::MultiPropertyCmd* batch = nullptr){
    KeyframeTracksComponent* kf = scene->findComponent<KeyframeTracksComponent>(trackEntity);
    TracksComp* tracks = scene->findComponent<TracksComp>(trackEntity);
    if (!kf || !tracks) return;

    auto modifiedCb = [sceneProject]() { sceneProject->isModified = true; };

    // update an existing key at (about) this time
    for (size_t i = 0; i < kf->times.size() && i < tracks->values.size(); i++){
        if (std::fabs(kf->times[i] - time) <= kKeyTimeEpsilon){
            std::string valueProp = "values[" + std::to_string(i) + "]";
            if (batch){
                batch->addPropertyCmd<ValueT>(project, sceneProject->id, trackEntity, tracksType,
                    valueProp, value, modifiedCb);
            }else{
                auto* cmd = new editor::PropertyCmd<ValueT>(project, sceneProject->id, trackEntity, tracksType,
                    valueProp, value, modifiedCb);
                editor::CommandHandle::get(sceneProject->id)->addCommand(cmd);
            }
            return;
        }
    }

    // insert a new key keeping times sorted
    size_t oldSize = kf->times.size();
    size_t j = 0;
    while (j < oldSize && kf->times[j] < time) j++;

    std::vector<float> newTimes = kf->times;
    newTimes.insert(newTimes.begin() + (long int)j, time);

    std::vector<ValueT> newValues = tracks->values;
    if (newValues.size() < oldSize){
        newValues.resize(oldSize, value);
    }
    newValues.insert(newValues.begin() + (long int)j, value);

    editor::MultiPropertyCmd* multiCmd = batch ? batch : new editor::MultiPropertyCmd();
    multiCmd->addPropertyCmd<std::vector<float>>(project, sceneProject->id, trackEntity,
        editor::ComponentType::KeyframeTracksComponent, "times", newTimes, modifiedCb);
    multiCmd->addPropertyCmd<std::vector<ValueT>>(project, sceneProject->id, trackEntity,
        tracksType, "values", newValues, nullptr);

    // keep Hermite tangents (GLTF CUBICSPLINE) mirrored with values so cubic
    // playback survives the edit; the new key gets zero tangents (its two
    // adjacent segments flatten toward it, the rest keeps its imported shape)
    if (!tracks->inTangents.empty() || !tracks->outTangents.empty()){
        const ValueT zeroTangent = value * 0.0f;
        auto insertTangent = [&](const std::vector<ValueT>& liveTangents, const char* property){
            std::vector<ValueT> newTangents = liveTangents;
            if (newTangents.size() < oldSize){
                newTangents.resize(oldSize, zeroTangent);
            }
            newTangents.insert(newTangents.begin() + (long int)j, zeroTangent);
            multiCmd->addPropertyCmd<std::vector<ValueT>>(project, sceneProject->id, trackEntity,
                tracksType, property, newTangents, nullptr);
        };
        insertTangent(tracks->inTangents, "inTangents");
        insertTangent(tracks->outTangents, "outTangents");
    }

    // keep per-segment easings aligned with the new key
    if (!kf->easings.empty()){
        std::vector<EaseType> newEasings = kf->easings;
        if (j == 0){
            // new leading segment
            newEasings.insert(newEasings.begin(), EaseType::LINEAR);
        }else if (j < oldSize && j - 1 < newEasings.size()){
            // middle insert splits segment j-1: duplicate its ease
            newEasings.insert(newEasings.begin() + (long int)(j - 1), newEasings[j - 1]);
        }
        // append at the end needs no change (missing entries mean linear)
        if (newEasings != kf->easings){
            multiCmd->addPropertyCmd<std::vector<EaseType>>(project, sceneProject->id, trackEntity,
                editor::ComponentType::KeyframeTracksComponent, "easings", newEasings, nullptr);
        }
    }

    if (!batch){
        multiCmd->setNoMerge();
        editor::CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
    }
}

// The values a key stores for the target's pose. A relative track keys offsets from
// the pose the target had when the track started, or from this one before it starts.
struct KeyValues {
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
};

KeyValues getKeyValues(Scene* scene, Entity trackEntity, Entity target){
    Transform& transform = scene->getComponent<Transform>(target);
    KeyValues values = {transform.position, transform.rotation, transform.scale};

    KeyframeTracksComponent* kf = scene->findComponent<KeyframeTracksComponent>(trackEntity);
    if (!kf || !kf->relative){
        return values;
    }
    if (!kf->hasBase){
        return {Vector3::ZERO, Quaternion(), Vector3::UNIT_SCALE};
    }

    scene->getSystem<ActionSystem>()->getRelativeTrackPose(target, values.position, values.rotation, values.scale);
    values.position = values.position - kf->basePosition;
    values.rotation = kf->baseRotation.inverse() * values.rotation;
    values.scale = Vector3(
        kf->baseScale.x != 0 ? values.scale.x / kf->baseScale.x : 1,
        kf->baseScale.y != 0 ? values.scale.y / kf->baseScale.y : 1,
        kf->baseScale.z != 0 ? values.scale.z / kf->baseScale.z : 1);
    return values;
}

// Where a key goes: keys are stored in the track's local time, so the owning
// frame's startTime must be subtracted from the animation time.
struct TrackLookup {
    Entity entity = NULL_ENTITY;
    float startTime = 0.0f;
    bool skip = false; // matching frames exist, but none spans the requested time
};

// Find an existing frame whose action animates `target` with the wanted track
// type. skip=true means matching tracks exist but none spans animTime (so the
// caller must not create a duplicate); entity=NULL and skip=false means create.
TrackLookup findTrackEntity(Scene* scene, AnimationComponent& anim, Entity target, float animTime,
                            editor::ComponentType tracksType){
    auto hasTracksComp = [&](Entity e) -> bool {
        if (tracksType == editor::ComponentType::TranslateTracksComponent) return scene->findComponent<TranslateTracksComponent>(e) != nullptr;
        if (tracksType == editor::ComponentType::RotateTracksComponent) return scene->findComponent<RotateTracksComponent>(e) != nullptr;
        if (tracksType == editor::ComponentType::ScaleTracksComponent) return scene->findComponent<ScaleTracksComponent>(e) != nullptr;
        return false;
    };

    // Prefer the frame with the greatest startTime not after the playhead, so
    // overlapping tracks for the same target key the block under the playhead
    Entity best = NULL_ENTITY;
    float bestStart = 0.0f;
    bool foundOutOfSpan = false;
    for (const ActionFrame& frame : anim.actions){
        Entity a = frame.action;
        if (a == NULL_ENTITY || !scene->isEntityCreated(a)) continue;
        if (!hasTracksComp(a) || !scene->findComponent<KeyframeTracksComponent>(a)) continue;
        ActionComponent* ac = scene->findComponent<ActionComponent>(a);
        if (ac && ac->target == target){
            bool hasStarted = animTime + kKeyTimeEpsilon >= frame.startTime;
            bool pastExplicitEnd = frame.duration > 0.0f
                && animTime > frame.startTime + frame.duration + kKeyTimeEpsilon;
            if (hasStarted && !pastExplicitEnd){
                if (best == NULL_ENTITY || frame.startTime > bestStart){
                    best = a;
                    bestStart = frame.startTime;
                }
            }else{
                foundOutOfSpan = true;
            }
        }
    }
    if (best != NULL_ENTITY){
        return {best, bestStart, false};
    }
    if (foundOutOfSpan){
        return {NULL_ENTITY, 0.0f, true};
    }
    return {NULL_ENTITY, 0.0f, false};
}

}

void editor::AnimationWindow::finishTimelineDrag(Scene* scene, SceneProject* sceneProject,
                                                 bool refreshPreview) {
    bool timingChanged = false;
    AnimationComponent* anim = scene && sceneProject && selectedEntity != NULL_ENTITY
        && scene->isEntityCreated(selectedEntity)
        ? scene->findComponent<AnimationComponent>(selectedEntity) : nullptr;

    if (isDraggingKey) {
        bool validKey = anim && selectedKeyFrameIndex >= 0
            && selectedKeyFrameIndex < (int)anim->actions.size();
        ActionFrame* keyFrame = validKey ? &anim->actions[selectedKeyFrameIndex] : nullptr;
        KeyframeTracksComponent* keyframes = keyFrame && keyFrame->action != NULL_ENTITY
            && scene->isEntityCreated(keyFrame->action)
            ? scene->findComponent<KeyframeTracksComponent>(keyFrame->action) : nullptr;
        validKey = keyframes && selectedKeyIndex >= 0
            && selectedKeyIndex < (int)keyframes->times.size();

        if (validKey && std::fabs(keyDragTime - keyDragStartTime) > 0.000001f) {
            std::vector<float> newTimes = keyframes->times;
            newTimes[selectedKeyIndex] = keyDragTime;
            auto* cmd = new PropertyCmd<std::vector<float>>(
                project, sceneProject->id, keyFrame->action,
                ComponentType::KeyframeTracksComponent, "times", newTimes,
                [sceneProject]() {
                    sceneProject->isModified = true;
                    sceneProject->needUpdateRender = true;
                });
            CommandHandle::get(sceneProject->id)->addCommandNoMerge(cmd);
            timingChanged = true;
        } else if (!validKey) {
            selectedKeyFrameIndex = -1;
            selectedKeyIndex = -1;
        }
        isDraggingKey = false;
    }

    if (isResizingFrame) {
        bool validFrame = anim && resizingFrameIndex >= 0
            && resizingFrameIndex < (int)anim->actions.size();
        if (validFrame) {
            ActionFrame& frame = anim->actions[resizingFrameIndex];
            bool timeChanged = frame.startTime != resizeStartTime;
            bool durationChanged = frame.duration != resizeStartDuration;
            if (timeChanged || durationChanged) {
                float finalStartTime = frame.startTime;
                float finalDuration = frame.duration;

                // Live frame edits are reverted before constructing commands so
                // undo records the state from the beginning of the drag.
                frame.startTime = resizeStartTime;
                frame.duration = resizeStartDuration;

                if (timeChanged && durationChanged) {
                    auto* multiCmd = new MultiPropertyCmd();
                    multiCmd->addPropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; });
                    multiCmd->addPropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].duration", finalDuration,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
                } else if (durationChanged) {
                    auto* cmd = new PropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].duration", finalDuration,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                } else {
                    auto* cmd = new PropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(resizingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                }
                timingChanged = true;
            }
        }
        isResizingFrame = false;
        resizingFrameIndex = -1;
        resizeSide = 0;
    }

    if (isDraggingFrame) {
        bool validFrame = anim && draggingFrameIndex >= 0
            && draggingFrameIndex < (int)anim->actions.size();
        if (validFrame) {
            ActionFrame& frame = anim->actions[draggingFrameIndex];
            bool timeChanged = frame.startTime != dragStartTime;
            bool trackChanged = frame.track != dragStartTrack;
            if (timeChanged || trackChanged) {
                float finalStartTime = frame.startTime;
                uint32_t finalTrack = frame.track;

                frame.startTime = dragStartTime;
                frame.track = dragStartTrack;

                if (timeChanged && trackChanged) {
                    auto* multiCmd = new MultiPropertyCmd();
                    multiCmd->addPropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(draggingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; });
                    multiCmd->addPropertyCmd<uint32_t>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(draggingFrameIndex) + "].track", finalTrack,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
                } else if (timeChanged) {
                    auto* cmd = new PropertyCmd<float>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(draggingFrameIndex) + "].startTime", finalStartTime,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                } else {
                    auto* cmd = new PropertyCmd<uint32_t>(
                        project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent,
                        "actions[" + std::to_string(draggingFrameIndex) + "].track", finalTrack,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(sceneProject->id)->addCommand(cmd);
                }
                timingChanged = timingChanged || timeChanged;
            }
        }
        isDraggingFrame = false;
        draggingFrameIndex = -1;
    }

    if (refreshPreview && timingChanged && isPreviewing) {
        seekPreview(scene, sceneProject, currentTime, selectedEntity);
    }
}

// Snapshot is available only when at least one supported transform track has a
// valid target and its frame spans the playhead.
bool editor::AnimationWindow::hasSnapshotTracks(Scene* scene, const AnimationComponent& anim) const {
    std::unordered_set<Entity> checkedActions;
    for (const ActionFrame& frame : anim.actions) {
        Entity actionEntity = frame.action;
        if (actionEntity == NULL_ENTITY || !scene->isEntityCreated(actionEntity)) continue;
        if (!scene->findComponent<KeyframeTracksComponent>(actionEntity)) continue;
        if (!checkedActions.insert(actionEntity).second) continue;

        if (currentTime + kKeyTimeEpsilon < frame.startTime) continue;
        if (frame.duration > 0.0f
            && currentTime > frame.startTime + frame.duration + kKeyTimeEpsilon) continue;

        ActionComponent* action = scene->findComponent<ActionComponent>(actionEntity);
        Entity target = action ? action->target : NULL_ENTITY;
        if (target == NULL_ENTITY || !scene->isEntityCreated(target)
            || !scene->findComponent<Transform>(target)) continue;

        if (scene->findComponent<TranslateTracksComponent>(actionEntity)
            || scene->findComponent<RotateTracksComponent>(actionEntity)
            || scene->findComponent<ScaleTracksComponent>(actionEntity)) {
            return true;
        }
    }
    return false;
}

// Key every keyframe track the playhead has reached with its target's current
// transform values, as one undo step. Auto-duration blocks grow to include the
// new key; blocks with an explicit duration are only keyed within their span.
void editor::AnimationWindow::snapshotTracks(Scene* scene, SceneProject* sceneProject, AnimationComponent& anim){
    struct SnapshotTarget {
        Entity action;
        float localTime;
        int channel; // 0 = position, 1 = rotation, 2 = scale
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
    };

    std::vector<SnapshotTarget> pending;
    std::unordered_set<Entity> keyedActions; // one write per track entity per batch
    for (const ActionFrame& frame : anim.actions){
        Entity actionEntity = frame.action;
        if (actionEntity == NULL_ENTITY || !scene->isEntityCreated(actionEntity)) continue;
        if (!scene->findComponent<KeyframeTracksComponent>(actionEntity)) continue;
        if (!keyedActions.insert(actionEntity).second) continue;

        // the playhead must have reached the block; an explicit duration also bounds it
        if (currentTime + kKeyTimeEpsilon < frame.startTime) continue;
        if (frame.duration > 0 && currentTime > frame.startTime + frame.duration + kKeyTimeEpsilon) continue;

        ActionComponent* action = scene->findComponent<ActionComponent>(actionEntity);
        Entity target = action ? action->target : NULL_ENTITY;
        if (target == NULL_ENTITY || !scene->isEntityCreated(target)) continue;
        if (!scene->findComponent<Transform>(target)) continue;

        int channel = -1;
        if (scene->findComponent<TranslateTracksComponent>(actionEntity)){
            channel = 0;
        }else if (scene->findComponent<RotateTracksComponent>(actionEntity)){
            channel = 1;
        }else if (scene->findComponent<ScaleTracksComponent>(actionEntity)){
            channel = 2;
        }
        if (channel < 0) continue;

        KeyValues values = getKeyValues(scene, actionEntity, target);
        pending.push_back({actionEntity, std::max(0.0f, currentTime - frame.startTime), channel,
                           values.position, values.rotation, values.scale});
    }

    if (pending.empty()){
        return;
    }

    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
    for (const SnapshotTarget& entry : pending){
        if (entry.channel == 0){
            writeTrackKey<TranslateTracksComponent, Vector3>(project, sceneProject, scene, entry.action,
                ComponentType::TranslateTracksComponent, entry.localTime, entry.position, multiCmd);
        }else if (entry.channel == 1){
            writeTrackKey<RotateTracksComponent, Quaternion>(project, sceneProject, scene, entry.action,
                ComponentType::RotateTracksComponent, entry.localTime, entry.rotation, multiCmd);
        }else{
            writeTrackKey<ScaleTracksComponent, Vector3>(project, sceneProject, scene, entry.action,
                ComponentType::ScaleTracksComponent, entry.localTime, entry.scale, multiCmd);
        }
    }

    multiCmd->setNoMerge();
    CommandHandle::get(sceneProject->id)->addCommand(multiCmd);
}

// Store one or more of the target's current transform channels at `time`.
// Existing tracks share one MultiPropertyCmd; missing tracks use CreateTrackKeyCmd
// (create + first key + ActionFrame) in that same batch for a single undo step.
void editor::AnimationWindow::keyTargetChannels(Scene* scene, SceneProject* sceneProject, AnimationComponent& anim,
                                                Entity target, float time, const std::vector<int>& channels){
    Transform* transform = scene->findComponent<Transform>(target);
    if (!transform){
        return;
    }

    auto* batch = new MultiPropertyCmd();
    std::vector<CreateTrackKeyCmd*> createdCmds;
    bool queuedKey = false;

    for (int channel : channels){
        ComponentType tracksType;
        EntityCreationType creationType;
        const char* name;
        if (channel == 0){
            tracksType = ComponentType::TranslateTracksComponent;
            creationType = EntityCreationType::TRANSLATE_TRACKS;
            name = "TranslateTracks";
        }else if (channel == 1){
            tracksType = ComponentType::RotateTracksComponent;
            creationType = EntityCreationType::ROTATE_TRACKS;
            name = "RotateTracks";
        }else if (channel == 2){
            tracksType = ComponentType::ScaleTracksComponent;
            creationType = EntityCreationType::SCALE_TRACKS;
            name = "ScaleTracks";
        }else{
            continue;
        }

        TrackLookup track = findTrackEntity(scene, anim, target, time, tracksType);

        if (track.skip){
            std::string targetName = scene->getEntityName(target);
            if (targetName.empty()){
                targetName = "Entity " + std::to_string(target);
            }
            keyNotice = std::string(name) + " for \"" + targetName + "\" has no active frame at this time; key skipped";
            keyNoticeAt = ImGui::GetTime();
            continue;
        }

        if (track.entity != NULL_ENTITY){
            float localTime = std::max(0.0f, time - track.startTime);
            KeyValues values = getKeyValues(scene, track.entity, target);
            if (channel == 0){
                writeTrackKey<TranslateTracksComponent, Vector3>(project, sceneProject, scene, track.entity,
                    tracksType, localTime, values.position, batch);
            }else if (channel == 1){
                writeTrackKey<RotateTracksComponent, Quaternion>(project, sceneProject, scene, track.entity,
                    tracksType, localTime, values.rotation, batch);
            }else{
                writeTrackKey<ScaleTracksComponent, Vector3>(project, sceneProject, scene, track.entity,
                    tracksType, localTime, values.scale, batch);
            }
            queuedKey = true;
            continue;
        }

        // No spanning track: create one with its first key and register the frame.
        // New auto-duration frames start at 0, so local time equals animation time.
        float localTime = std::max(0.0f, time);
        std::unique_ptr<CreateTrackKeyCmd> createCmd;
        if (channel == 1){
            createCmd = std::make_unique<CreateTrackKeyCmd>(
                project, sceneProject->id, selectedEntity, target, creationType, name, localTime,
                transform->rotation);
        }else if (channel == 0){
            createCmd = std::make_unique<CreateTrackKeyCmd>(
                project, sceneProject->id, selectedEntity, target, creationType, name, localTime,
                transform->position);
        }else{
            createCmd = std::make_unique<CreateTrackKeyCmd>(
                project, sceneProject->id, selectedEntity, target, creationType, name, localTime,
                transform->scale);
        }
        createdCmds.push_back(createCmd.get());
        batch->addCommand(std::move(createCmd));
        queuedKey = true;
    }

    if (!queuedKey){
        delete batch;
        return;
    }

    batch->setNoMerge();
    CommandHandle::get(sceneProject->id)->addCommand(batch);

    // New track entities did not exist when this preview began. Save only their
    // runtime state now; buildPreviewEntityState deliberately excludes key data.
    if (isPreviewing){
        for (CreateTrackKeyCmd* cmd : createdCmds){
            Entity trackEntity = cmd->getEntity();
            if (trackEntity != NULL_ENTITY && scene->isEntityCreated(trackEntity)){
                previewState.push_back(buildPreviewEntityState(scene, trackEntity));
            }
        }
    }
}

// Store one target channel as the fine-grained right-click alternative.
void editor::AnimationWindow::keyTargetChannel(Scene* scene, SceneProject* sceneProject, AnimationComponent& anim,
                                               Entity target, float time, int channel){
    keyTargetChannels(scene, sceneProject, anim, target, time, {channel});
}

// Key the exact block selected by the context menu. Unlike empty-area keying,
// this path must never search for a newer overlapping frame of the same type.
void editor::AnimationWindow::keyTrackChannel(Scene* scene, SceneProject* sceneProject, Entity trackEntity,
                                              float localTime, float explicitDuration, int channel){
    if (explicitDuration > 0.0f && localTime > explicitDuration + kKeyTimeEpsilon){
        return;
    }

    ActionComponent* action = scene->findComponent<ActionComponent>(trackEntity);
    Transform* transform = action ? scene->findComponent<Transform>(action->target) : nullptr;
    if (!transform || !scene->findComponent<KeyframeTracksComponent>(trackEntity)){
        return;
    }

    KeyValues values = getKeyValues(scene, trackEntity, action->target);
    auto* batch = new MultiPropertyCmd();
    if (channel == 0 && scene->findComponent<TranslateTracksComponent>(trackEntity)){
        writeTrackKey<TranslateTracksComponent, Vector3>(project, sceneProject, scene, trackEntity,
            ComponentType::TranslateTracksComponent, localTime, values.position, batch);
    }else if (channel == 1 && scene->findComponent<RotateTracksComponent>(trackEntity)){
        writeTrackKey<RotateTracksComponent, Quaternion>(project, sceneProject, scene, trackEntity,
            ComponentType::RotateTracksComponent, localTime, values.rotation, batch);
    }else if (channel == 2 && scene->findComponent<ScaleTracksComponent>(trackEntity)){
        writeTrackKey<ScaleTracksComponent, Vector3>(project, sceneProject, scene, trackEntity,
            ComponentType::ScaleTracksComponent, localTime, values.scale, batch);
    }else{
        delete batch;
        return;
    }

    batch->setNoMerge();
    CommandHandle::get(sceneProject->id)->addCommand(batch);
}

void editor::AnimationWindow::seekPreview(Scene* scene, SceneProject* sceneProject, float time, Entity clipEntity) {
    if (clipEntity == NULL_ENTITY) {
        clipEntity = timelineClip(scene);
    }
    if (!scene || !sceneProject || !canPreviewEntity(clipEntity, scene)) {
        return;
    }

    if (!isPreviewing) {
        startPreview(scene, sceneProject);
    }

    // Scrubbing exits any active transition: reset blend/fade state and preview only the
    // timeline clip (a two-clip crossfade is not meaningfully scrubbable on one timeline).
    for (Entity e : previewAnimations) {
        if (e != NULL_ENTITY && scene->isEntityCreated(e)) {
            if (AnimationComponent* ac = scene->findComponent<AnimationComponent>(e)) {
                ac->weight = 1.0f;
                ac->fadeTarget = 1.0f;
                ac->fadeSpeed = 0.0f;
                ac->stopOnFadeOut = false;
            }
        }
    }
    previewAnimations.clear();
    previewAnimations.push_back(clipEntity);
    previewPrimary = clipEntity;

    AnimationComponent& anim = scene->getComponent<AnimationComponent>(clipEntity);
    float requestedTime = std::max(0.0f, time);
    float previewTime = std::min(requestedTime, getAnimationDuration(anim, scene));
    // Keep the authoring playhead at the requested time even when preview
    // evaluation can only reach the clip's current end. A new key at that time
    // can then extend an auto-duration track.
    currentTime = requestedTime;

    restorePreviewState(scene);
    applyPreviewModelBindPose(scene);

    ActionComponent& action = scene->getComponent<ActionComponent>(clipEntity);
    action.state = ActionState::Stopped;
    action.timecount = 0.0f;
    action.stopTrigger = false;
    action.pauseTrigger = false;
    action.startTrigger = true;

    // Reset all child action entities to Stopped so animationUpdate calls actionStart on
    // them (initializing particle slots, sprite anims, etc.) during seek simulation.
    for (const ActionFrame& frame : anim.actions) {
        if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
            if (ActionComponent* childAction = scene->findComponent<ActionComponent>(frame.action)) {
                childAction->state = ActionState::Stopped;
                childAction->timecount = 0;
                childAction->startTrigger = false;
                childAction->stopTrigger = false;
                childAction->pauseTrigger = false;
            }
        }
    }

    // Simulate to the seek time incrementally for components that require integration (like particles)
    float remainingTime = previewTime;
    float stepSize = 1.0f / 60.0f;
    while (remainingTime > 0.0f) {
        float currentStep = std::min(stepSize, remainingTime);
        scene->getSystem<ActionSystem>()->updateAnimationPreview(currentStep, clipEntity);
        remainingTime -= currentStep;
    }
}

void editor::AnimationWindow::addAnimationToPreview(Scene* scene, Entity animEntity) {
    if (animEntity == NULL_ENTITY || !scene->findComponent<AnimationComponent>(animEntity)) {
        return;
    }

    // Snapshot this animation's action tree (entities not already captured), so it can be
    // restored when the preview ends.
    std::vector<Entity> previewEntities;
    std::unordered_set<Entity> visitedAnimations;
    std::unordered_set<Entity> collectedEntities;
    collectPreviewEntitiesRecursive(scene, animEntity, previewEntities, visitedAnimations, collectedEntities);

    std::unordered_set<Entity> alreadySnapshotted;
    for (const PreviewEntityState& state : previewState) {
        alreadySnapshotted.insert(state.entity);
    }

    for (Entity entity : previewEntities) {
        if (alreadySnapshotted.insert(entity).second) {
            previewState.push_back(buildPreviewEntityState(scene, entity));
        }
    }

    // Reset this animation's child action entities to Stopped so animationUpdate calls
    // actionStart on them (initializing particle systems, sprite animations, etc.)
    // regardless of their saved state in the scene file.
    AnimationComponent& animComp = scene->getComponent<AnimationComponent>(animEntity);
    for (const ActionFrame& frame : animComp.actions) {
        if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
            if (ActionComponent* childAction = scene->findComponent<ActionComponent>(frame.action)) {
                childAction->state = ActionState::Stopped;
                childAction->timecount = 0;
                childAction->startTrigger = false;
                childAction->stopTrigger = false;
                childAction->pauseTrigger = false;
            }
        }
    }
}

void editor::AnimationWindow::startPreview(Scene* scene, SceneProject* sceneProject) {
    if (isPreviewing || !canPreviewEntity(selectedEntity, scene)) {
        return;
    }

    previewState.clear();
    previewAnimations.clear();

    addAnimationToPreview(scene, selectedEntity);
    previewAnimations.push_back(selectedEntity);
    previewPrimary = selectedEntity;

    ActionComponent& action = scene->getComponent<ActionComponent>(selectedEntity);
    action.state = ActionState::Stopped;
    action.timecount = 0;
    action.startTrigger = false;
    action.stopTrigger = false;
    action.pauseTrigger = false;
    Animation(scene, selectedEntity).fadeIn(0.0f);

    isPreviewing = true;
}

void editor::AnimationWindow::triggerTransitionPreview(Scene* scene, SceneProject* sceneProject) {
    if (transitionTarget == NULL_ENTITY || transitionTarget == selectedEntity ||
        !scene->isEntityCreated(transitionTarget) || !canPreviewEntity(transitionTarget, scene)) {
        return;
    }

    if (!isPreviewing) {
        startPreview(scene, sceneProject);
    }
    if (!isPreviewing) {
        return; // current clip could not be previewed
    }

    finishTimelineDrag(scene, sceneProject, true);

    float fadeTime = scene->getComponent<AnimationComponent>(transitionTarget).defaultFadeTime;

    bool alreadyActive = false;
    for (Entity e : previewAnimations) {
        if (e == transitionTarget) { alreadyActive = true; break; }
    }
    if (!alreadyActive) {
        addAnimationToPreview(scene, transitionTarget);
        previewAnimations.push_back(transitionTarget);

        // A newly added clip starts from frame 0. Reversing an already active
        // transition intentionally preserves its current playback time.
        if (ActionComponent* targetAction = scene->findComponent<ActionComponent>(transitionTarget)) {
            targetAction->state = ActionState::Stopped;
            targetAction->timecount = 0.0f;
            targetAction->stopTrigger = false;
            targetAction->pauseTrigger = false;
        }
    }

    // Fade the currently running clip(s) out and the target in over its fade time.
    for (Entity e : previewAnimations) {
        if (e == transitionTarget || !scene->isEntityCreated(e) || !scene->findComponent<AnimationComponent>(e)) {
            continue;
        }
        Animation(scene, e).fadeOut(fadeTime);
    }
    Animation(scene, transitionTarget).fadeIn(fadeTime);

    previewPrimary = transitionTarget;
    // The timeline now follows a different clip, which may have just started
    // from zero. Resume following even when the outgoing clip's scrollbar was
    // moved manually during playback.
    playbackFollowSuspended = false;
    isPlaying = true;
    selectedFrameIndex = -1;
    sceneProject->needUpdateRender = true;
}

void editor::AnimationWindow::stopPreview(Scene* scene, SceneProject* sceneProject, bool applyBindPose) {
    if (!isPreviewing) return;

    // AnimationComponent/key data are intentionally excluded from preview-state
    // restoration, so finalize authored timeline changes before restoring poses.
    finishTimelineDrag(scene, sceneProject, false);

    restorePreviewState(scene);
    if (applyBindPose) {
        applyPreviewModelBindPose(scene);
    }

    // Remove InstancedMeshComponent dynamically added during preview (e.g. particle targets).
    for (const PreviewEntityState& state : previewState) {
        ProjectUtils::removeDynamicInstmesh(state.entity, state.components, scene);
    }

    // Clear any mid-transition blend/fade state left on the previewed animations so it
    // does not carry into a later runtime play (runtime fields are not serialized).
    for (Entity animEntity : previewAnimations) {
        if (animEntity != NULL_ENTITY && scene->isEntityCreated(animEntity)) {
            if (AnimationComponent* animComp = scene->findComponent<AnimationComponent>(animEntity)) {
                animComp->weight = 1.0f;
                animComp->fadeTarget = 1.0f;
                animComp->fadeSpeed = 0.0f;
                animComp->stopOnFadeOut = false;
            }
        }
    }

    previewState.clear();
    previewAnimations.clear();
    previewPrimary = NULL_ENTITY;
    isPreviewing = false;
    isDraggingFrame = false;
    draggingFrameIndex = -1;
    isDraggingKey = false;
    isResizingFrame = false;
    resizingFrameIndex = -1;
    resizeSide = 0;
    if (sceneProject) {
        sceneProject->needUpdateRender = true;
    }
}

void editor::AnimationWindow::clearSceneState(uint32_t sceneId) {
    if (!isPreviewing || selectedSceneId != sceneId) {
        return;
    }

    // The scene is already deleted, drop the snapshots instead of restoring them
    previewState.clear();
    previewAnimations.clear();
    previewPrimary = NULL_ENTITY;
    transitionTarget = NULL_ENTITY;
    isPreviewing = false;
    isPlaying = false;
    isDraggingFrame = false;
    draggingFrameIndex = -1;
    isDraggingKey = false;
    isResizingFrame = false;
    resizingFrameIndex = -1;
    resizeSide = 0;
}

std::string editor::AnimationWindow::getAnimationEntityLabel(Entity entity, Scene* scene) const {
    std::string label = scene->getEntityName(entity);
    if (label.empty()) {
        label = "Animation";
    }

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
    std::string currentLabel = getAnimationEntityLabel(selectedEntity, scene);
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
            std::string label = getAnimationEntityLabel(entity, scene);

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
    EntityCreationType selectedType = EntityCreationType::EMPTY;
    std::string entityName;
    bool selected = false;
    ImGui::BeginDisabled(isPreviewing);
    if (ImGui::Button(ICON_FA_PLUS " Add Action")) {
        ImGui::OpenPopup("##add_action_popup");
    }
    if (ImGui::BeginPopup("##add_action_popup")) {
        if (ImGui::MenuItem(ICON_FA_PLUS "  Empty Action")) {
            selected = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FILM "  Animation")) {
            selectedType = EntityCreationType::ANIMATION;
            entityName = "Animation";
            selected = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_PLAY "  Sprite Animation")) {
            selectedType = EntityCreationType::SPRITE_ANIMATION;
            entityName = "SpriteAnimation";
            selected = true;
        }
        if (ImGui::BeginMenu(ICON_FA_BOLT "  Basic animations")) {
            if (ImGui::MenuItem(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Position Action")) {
                selectedType = EntityCreationType::POSITION_ACTION;
                entityName = "PositionAction";
                selected = true;
            }
            if (ImGui::MenuItem(ICON_FA_ROTATE "  Rotation Action")) {
                selectedType = EntityCreationType::ROTATION_ACTION;
                entityName = "RotationAction";
                selected = true;
            }
            if (ImGui::MenuItem(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "  Scale Action")) {
                selectedType = EntityCreationType::SCALE_ACTION;
                entityName = "ScaleAction";
                selected = true;
            }
            if (ImGui::MenuItem(ICON_FA_PALETTE "  Color Action")) {
                selectedType = EntityCreationType::COLOR_ACTION;
                entityName = "ColorAction";
                selected = true;
            }
            if (ImGui::MenuItem(ICON_FA_EYE "  Alpha Action")) {
                selectedType = EntityCreationType::ALPHA_ACTION;
                entityName = "AlphaAction";
                selected = true;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_ROUTE "  Translate Tracks")) {
            selectedType = EntityCreationType::TRANSLATE_TRACKS;
            entityName = "TranslateTracks";
            selected = true;
        }
        if (ImGui::MenuItem(ICON_FA_ARROWS_SPIN "  Rotate Tracks")) {
            selectedType = EntityCreationType::ROTATE_TRACKS;
            entityName = "RotateTracks";
            selected = true;
        }
        if (ImGui::MenuItem(ICON_FA_MAXIMIZE "  Scale Tracks")) {
            selectedType = EntityCreationType::SCALE_TRACKS;
            entityName = "ScaleTracks";
            selected = true;
        }
        if (ImGui::MenuItem(ICON_FA_SHAPES "  Morph Tracks")) {
            selectedType = EntityCreationType::MORPH_TRACKS;
            entityName = "MorphTracks";
            selected = true;
        }

        ImGui::EndPopup();
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
            finishTimelineDrag(scene, sceneProject, true);
            playbackFollowSuspended = false;
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
        selectedFrameIndex = prePlaySelectedFrameIndex;
        prePlaySelectedFrameIndex = -1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    Entity toolbarClip = timelineClip(scene);
    AnimationComponent& toolbarClipAnim = (toolbarClip != selectedEntity)
        ? scene->getComponent<AnimationComponent>(toolbarClip) : anim;
    float clipDuration = getAnimationDuration(toolbarClipAnim, scene);
    bool holdingEndPose = isPreviewing && currentTime > clipDuration + kKeyTimeEpsilon;

    // Snapshot: key every track reached by the playhead with its target's
    // current transform values (single undo step). A scrub creates a paused
    // preview, which is safe to snapshot; only active playback is blocked.
    bool hasSnapshotTarget = hasSnapshotTracks(scene, anim);
    bool canSnapshot = sceneIsStopped && !isPlaying && hasSnapshotTarget;
    ImGui::BeginDisabled(!canSnapshot);
    if (ImGui::Button(ICON_FA_CAMERA "##anim_snapshot")) {
        snapshotTracks(scene, sceneProject, anim);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!sceneIsStopped) {
            ImGui::SetTooltip("Snapshot is only available while the scene is stopped.");
        } else if (isPlaying) {
            ImGui::SetTooltip("Pause or stop animation playback to snapshot.");
        } else if (!hasSnapshotTarget) {
            ImGui::SetTooltip("No keyframe tracks under the playhead to snapshot");
        } else if (holdingEndPose) {
            ImGui::SetTooltip("Snapshot (%.2fs): the preview can only evaluate through %.2fs, so the object\n"
                              "is holding the clip's end pose. Adjust the pose before Snapshot if you want\n"
                              "different values at this later time.", currentTime, clipDuration);
        } else {
            ImGui::SetTooltip("Snapshot (%.2fs): store the current position, rotation and scale of every\n"
                              "track reached by the playhead as keyframes at this time.\n"
                              "Right-click the timeline to key a single track or channel.", currentTime);
        }
    }
    ImGui::SameLine();

    // Time display. The field scrubs the timeline clip (the blend target during a
    // transition preview), so its range must come from that clip too.
    ImGui::SetNextItemWidth(60);
    float maxTime = sceneIsStopped ? std::max(kAuthoringMaxTime, clipDuration) : clipDuration;
    if (UIUtils::dragFloat("##anim_time", &currentTime, 0.01f, 0.0f, maxTime, "%.2fs")) {
        if (sceneIsStopped && canPreview) {
            seekPreview(scene, sceneProject, currentTime);
        } else {
            currentTime = std::max(0.0f, std::min(currentTime, maxTime));
        }
    }
    ImGui::SameLine();

    // Speed
    ImGui::SetNextItemWidth(50);
    UIUtils::dragFloat("##anim_speed", &playbackSpeed, 0.01f, 0.01f, 10.0f, "%.1fx");
    ImGui::SameLine();

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Transition preview: crossfade the playing clip into another over its Fade time
    // (authored per-animation as "Fade time" in the Properties panel). Lets you see the
    // blend that Model::playAnimation produces at runtime.
    ImGui::Text("Blend to:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    std::string targetLabel = "(select)";
    if (transitionTarget == selectedEntity) {
        transitionTarget = NULL_ENTITY; // can't blend a clip into itself
    }
    if (transitionTarget != NULL_ENTITY && scene->isEntityCreated(transitionTarget)) {
        if (scene->findComponent<AnimationComponent>(transitionTarget)) {
            targetLabel = getAnimationEntityLabel(transitionTarget, scene);
        } else {
            transitionTarget = NULL_ENTITY;
        }
    }
    if (ImGui::BeginCombo("##anim_blend_target", targetLabel.c_str())) {
        auto blendAnims = scene->getComponentArray<AnimationComponent>();
        for (int i = 0; i < (int)blendAnims->size(); i++) {
            Entity blendEntity = blendAnims->getEntity(i);
            if (blendEntity == selectedEntity) continue;
            std::string blendLabel = getAnimationEntityLabel(blendEntity, scene);
            bool blendSelected = (blendEntity == transitionTarget);
            ImGui::PushID((int)blendEntity);
            if (ImGui::Selectable(blendLabel.c_str(), blendSelected)) {
                transitionTarget = blendEntity;
            }
            if (blendSelected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    bool canBlend = isPreviewing && transitionTarget != NULL_ENTITY &&
                    transitionTarget != selectedEntity &&
                    scene->isEntityCreated(transitionTarget) &&
                    canPreviewEntity(transitionTarget, scene);
    ImGui::BeginDisabled(!canBlend);
    if (ImGui::Button(ICON_FA_RIGHT_LEFT "##anim_blend_go")) {
        triggerTransitionPreview(scene, sceneProject);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        if (!isPreviewing) {
            ImGui::SetTooltip("Play an animation first, then crossfade to the selected clip.");
        } else if (transitionTarget == NULL_ENTITY) {
            ImGui::SetTooltip("Select a clip to blend into.");
        } else {
            float ft = scene->findComponent<AnimationComponent>(transitionTarget)
                ? scene->getComponent<AnimationComponent>(transitionTarget).defaultFadeTime : 0.0f;
            ImGui::SetTooltip("Crossfade from the playing clip to the selected clip over its Fade time (%.2fs).", ft);
        }
    }
    ImGui::SameLine();

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Zoom
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    UIUtils::sliderFloat("##anim_zoom", &pixelsPerSecond, minPixelsPerSecond, maxPixelsPerSecond, "%.0f");
    ImGui::SameLine();

    // Snap
    ImGui::Checkbox("Snap", &snapToGrid);
    ImGui::SameLine();
    if (snapToGrid) {
        ImGui::SetNextItemWidth(50);
        UIUtils::dragFloat("##snap_int", &snapInterval, 0.01f, 0.01f, 1.0f, "%.2f");
    }

    if (selectedFrameIndex >= 0 && selectedFrameIndex < (int)anim.actions.size()) {
        ActionFrame& frame = anim.actions[selectedFrameIndex];
        std::string actionLabel = getActionLabel(frame.action, scene);
        float frameDur = effectiveFrameDuration(frame, scene);
        const char* autoTag = (frame.duration > 0) ? "" : " (auto)";

        char infoBuf[256];
        snprintf(infoBuf, sizeof(infoBuf), "Frame %d | Track %u | Start %.2fs | Duration %.2fs%s | %s",
                 selectedFrameIndex, frame.track, frame.startTime, frameDur, autoTag, actionLabel.c_str());

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
            ImGui::SetTooltip("Frame %d\nTrack: %u\nStart: %.2fs\nDuration: %.2fs%s\nAction: %s",
                              selectedFrameIndex, frame.track, frame.startTime, frameDur, autoTag, actionLabel.c_str());
        }
    }

    // Transient keying warnings (e.g. key skipped)
    if (!keyNotice.empty()) {
        if (ImGui::GetTime() - keyNoticeAt < 4.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s", keyNotice.c_str());
        } else {
            keyNotice.clear();
        }
    }

    // Done last: an Animation action adds an AnimationComponent, which can move anim
    if (selected) {
        // If a frame is selected, add to the same track, otherwise track 0
        uint32_t targetTrack = 0;
        if (selectedFrameIndex >= 0 && selectedFrameIndex < (int)anim.actions.size()) {
            targetTrack = anim.actions[selectedFrameIndex].track;
        }
        Entity target = scene->findComponent<ActionComponent>(selectedEntity)
            ? scene->getComponent<ActionComponent>(selectedEntity).target
            : NULL_ENTITY;

        selectedFrameIndex = (int)anim.actions.size();
        CommandHandle::get(sceneProject->id)->addCommandNoMerge(new AddActionFrameCmd(
            project, sceneProject->id, selectedEntity, target, selectedType, entityName, targetTrack));
    }
}

void editor::AnimationWindow::drawTimeRuler(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const TimelineMetrics metrics = timelineMetrics();
    float rulerHeight = metrics.rulerHeight;
    float labelWidth = metrics.labelWidth;

    // Label column header
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + labelWidth, canvasPos.y + rulerHeight),
                            IM_COL32(50, 50, 50, 255));
    drawList->AddText(ImVec2(canvasPos.x + metrics.textPad, canvasPos.y + metrics.inset), IM_COL32(150, 150, 150, 255), "Track");

    // Timeline ruler background
    drawList->AddRectFilled(ImVec2(canvasPos.x + labelWidth, canvasPos.y),
                            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight),
                            IM_COL32(40, 40, 40, 255));

    // Choose a stable 1/2/5 interval that keeps major labels readable at every
    // zoom level. The old fixed thresholds put labels only ~20 px apart at
    // both minimum zoom and immediately above 200 px/s.
    float rawTickInterval = metrics.targetMajorTickSpacing / pixelsPerSecond;
    float intervalMagnitude = std::pow(10.0f, std::floor(std::log10(rawTickInterval)));
    float normalizedInterval = rawTickInterval / intervalMagnitude;
    float niceInterval = normalizedInterval <= 1.0f ? 1.0f
                       : normalizedInterval <= 2.0f ? 2.0f
                       : normalizedInterval <= 5.0f ? 5.0f
                                                    : 10.0f;
    float tickInterval = niceInterval * intervalMagnitude;
    constexpr int subdivisions = 5;
    float subTickInterval = tickInterval / subdivisions;
    ImVec2 timeOrigin(canvasPos.x + labelWidth, canvasPos.y);
    float timeAreaRight = canvasPos.x + canvasSize.x;

    int labelPrecision = tickInterval >= 1.0f
        ? 0
        : std::min(3, std::max(1, (int)std::ceil(-std::log10(tickInterval))));

    // Derive every tick from an integer index instead of accumulating a float;
    // this keeps long timelines aligned and lets us omit minor ticks where a
    // major tick is already drawn.
    long long firstSubTick = (long long)std::floor(timeStart / subTickInterval) - 1;
    long long lastSubTick = (long long)std::ceil(timeEnd / subTickInterval) + 1;
    for (long long tickIndex = firstSubTick; tickIndex <= lastSubTick; tickIndex++) {
        float t = (float)tickIndex * subTickInterval;
        float x = timeToX(t, timeStart, timeOrigin);
        if (x < timeOrigin.x || x > timeAreaRight) {
            continue;
        }

        bool isMajorTick = tickIndex % subdivisions == 0;
        if (isMajorTick) {
            drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - metrics.tickMajor), ImVec2(x, canvasPos.y + rulerHeight),
                              IM_COL32(180, 180, 180, 255));

            char buf[16];
            float roundedTime = std::round(t);
            bool isWholeSecond = std::fabs(t - roundedTime) < 0.0001f;
            snprintf(buf, sizeof(buf), "%.*fs", isWholeSecond ? 0 : labelPrecision,
                     isWholeSecond ? roundedTime : t);
            drawList->AddText(ImVec2(x + metrics.inset, canvasPos.y + metrics.inset), IM_COL32(180, 180, 180, 255), buf);
        } else {
            drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - metrics.tickMinor), ImVec2(x, canvasPos.y + rulerHeight),
                              IM_COL32(100, 100, 100, 255));
        }
    }
}

bool editor::AnimationWindow::drawTracks(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd,
                                         AnimationComponent& anim, SceneProject* sceneProject, bool allowSelection) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Scene* scene = sceneProject->scene;
    const TimelineMetrics metrics = timelineMetrics();
    float rulerHeight = metrics.rulerHeight;
    float trackHeight = metrics.trackHeight;
    float trackPadding = metrics.trackPadding;
    float labelWidth = metrics.labelWidth;

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
        if (sig.test(scene->getComponentId<TimedActionComponent>())) {
            if (sig.test(scene->getComponentId<PositionActionComponent>()))
                return IM_COL32(100, 180, 220, 200);  // Light blue: PositionAction
            if (sig.test(scene->getComponentId<RotationActionComponent>()))
                return IM_COL32(220, 160, 70, 200);   // Amber: RotationAction
            if (sig.test(scene->getComponentId<ScaleActionComponent>()))
                return IM_COL32(180, 100, 200, 200);  // Violet: ScaleAction
            if (sig.test(scene->getComponentId<ColorActionComponent>()))
                return IM_COL32(220, 120, 160, 200);  // Pink: ColorAction
            if (sig.test(scene->getComponentId<AlphaActionComponent>()))
                return IM_COL32(160, 200, 120, 200);  // Lime: AlphaAction
            return IM_COL32(180, 100, 70, 200);       // Warm red: generic TimedAction
        }
        if (sig.test(scene->getComponentId<AnimationComponent>()))
            return IM_COL32(180, 170, 70, 200);  // Gold: Animation

        return IM_COL32(140, 70, 180, 200);      // Purple: generic Action
    };

    // A paused scrub preview is an authoring state: frame move/resize and keying
    // remain available. Active animation playback and scene play lock the timeline.
    bool allowEditing = sceneProject->playState == ScenePlayState::STOPPED && !isPlaying;
    bool allowKeying = allowEditing;
    bool mouseOverFrame = false;

    // Permission can change through external playback/scene controls while the
    // mouse is still held. Finalize first instead of leaving live frame edits
    // outside the undo stack or silently dropping a key-drag preview.
    if ((isDraggingKey || isDraggingFrame || isResizingFrame)
        && (!allowSelection || !allowEditing)) {
        finishTimelineDrag(scene, sceneProject, false);
    }

    // Preview a key move without mutating the component. The PropertyCmd created
    // on release can then capture the real old value for undo.
    if (isDraggingKey) {
        bool validKey = selectedKeyFrameIndex >= 0
            && selectedKeyFrameIndex < (int)anim.actions.size();
        if (validKey) {
            ActionFrame& keyFrame = anim.actions[selectedKeyFrameIndex];
            KeyframeTracksComponent* keyframes = keyFrame.action != NULL_ENTITY
                && scene->isEntityCreated(keyFrame.action)
                ? scene->findComponent<KeyframeTracksComponent>(keyFrame.action) : nullptr;
            validKey = keyframes && selectedKeyIndex >= 0
                && selectedKeyIndex < (int)keyframes->times.size();

            if (validKey && allowKeying && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float animationTime = xToTime(ImGui::GetIO().MousePos.x, timeStart,
                                              ImVec2(canvasPos.x + labelWidth, 0));
                // Snap in timeline space, then convert to the block's local time.
                // This keeps the key on the visible grid when the block itself has
                // been moved to a non-grid-aligned start time.
                float desiredLocalTime = std::max(0.0f,
                    snapTime(std::max(0.0f, animationTime)) - keyFrame.startTime);
                float minTime = 0.0f;
                float maxTime = keyFrame.duration > 0.0f
                    ? keyFrame.duration
                    : std::max(0.0f, kAuthoringMaxTime - keyFrame.startTime);

                // The values/tangents arrays share the times array's ordering, so
                // keep the dragged key between its neighbors instead of reordering
                // only one of those parallel arrays.
                if (selectedKeyIndex > 0) {
                    minTime = std::max(minTime,
                        keyframes->times[selectedKeyIndex - 1] + kKeyTimeEpsilon);
                }
                if (selectedKeyIndex + 1 < (int)keyframes->times.size()) {
                    maxTime = std::min(maxTime,
                        keyframes->times[selectedKeyIndex + 1] - kKeyTimeEpsilon);
                }
                if (minTime <= maxTime) {
                    keyDragTime = std::clamp(desiredLocalTime, minTime, maxTime);
                }
            }
        }

        if (!validKey) {
            isDraggingKey = false;
            selectedKeyFrameIndex = -1;
            selectedKeyIndex = -1;
        }
    }

    // Find highest track
    uint32_t maxTrack = 0;
    for (const auto& frame : anim.actions) {
        if (frame.track > maxTrack) {
            maxTrack = frame.track;
        }
    }
    // Always draw at least 3 tracks, or maxTrack + 2 to give space to drag down
    uint32_t numTracks = std::max((uint32_t)3, maxTrack + 2);

    std::vector<float> visualEndPx(anim.actions.size());
    for (size_t i = 0; i < anim.actions.size(); i++) {
        const ActionFrame& frame = anim.actions[i];
        float frameDur = effectiveFrameDuration(frame, scene);
        if (allowSelection && frame.duration <= 0.0f && isDraggingKey
            && selectedKeyFrameIndex == (int)i) {
            frameDur = std::max(frameDur, keyDragTime);
        }
        float startPx = frame.startTime * pixelsPerSecond;
        visualEndPx[i] = startPx + std::max(frameDur * pixelsPerSecond, metrics.minBlockWidth);
    }
    std::vector<int> subLane;
    std::vector<int> trackLaneCount;
    assignVisualSubLanes(anim.actions, visualEndPx, numTracks, pixelsPerSecond, subLane, trackLaneCount);

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
        drawList->AddText(ImVec2(canvasPos.x + metrics.textPad, trackY + metrics.textPad), IM_COL32(200, 200, 200, 255), label.c_str());
    }

    // 2. Draw blocks
    for (size_t i = 0; i < anim.actions.size(); i++) {
        ActionFrame& frame = anim.actions[i];
        float trackY = canvasPos.y + rulerHeight + frame.track * (trackHeight + trackPadding);
        float frameDur = effectiveFrameDuration(frame, scene);
        if (allowSelection && frame.duration <= 0.0f && isDraggingKey
            && selectedKeyFrameIndex == (int)i) {
            // Auto-duration blocks grow with a key dragged beyond their current end.
            frameDur = std::max(frameDur, keyDragTime);
        }

        int laneCount = (frame.track < numTracks) ? trackLaneCount[frame.track] : 1;
        int lane = subLane[i];
        float innerTop = trackY + metrics.inset;
        float innerBot = trackY + trackHeight - metrics.inset;
        float laneGap = (laneCount > 1) ? metrics.scale : 0.0f;
        float laneH = (innerBot - innerTop - laneGap * (float)(laneCount - 1)) / (float)std::max(laneCount, 1);
        float blockTop = innerTop + (float)lane * (laneH + laneGap);
        float blockBot = blockTop + laneH;

        // Action frame block
        float blockStart = timeToX(frame.startTime, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
        float blockEnd = timeToX(frame.startTime + frameDur, timeStart, ImVec2(canvasPos.x + labelWidth, 0));

        // Keep zero-length frames (auto duration not yet resolvable) visible and clickable
        if (blockEnd - blockStart < metrics.minBlockWidth) {
            blockEnd = blockStart + metrics.minBlockWidth;
        }

        // Clamp to visible area
        float visStart = std::max(blockStart, canvasPos.x + labelWidth);
        float visEnd = std::min(blockEnd, canvasPos.x + canvasSize.x);

        if (visEnd > visStart) {
            ImU32 blockColor = getFrameColor(frame.action);
            drawList->AddRectFilled(ImVec2(visStart, blockTop), ImVec2(visEnd, blockBot),
                                    blockColor, metrics.blockRounding);

            if ((int)i == selectedFrameIndex) {
                drawList->AddRect(ImVec2(visStart, blockTop), ImVec2(visEnd, blockBot),
                                  IM_COL32(255, 220, 120, 255), metrics.blockRounding, 0, metrics.selectionThickness);
            }

            // Keyframe diamonds sit on the block's bottom edge, below the label.
            // Keep them in the track padding so dense keys never cover the text.
            bool mouseOverKey = false;
            if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
                if (KeyframeTracksComponent* kfComp = scene->findComponent<KeyframeTracksComponent>(frame.action)) {
                    const float keyMarkerRadius = metrics.keyMarkerRadius;
                    const float keyHitRadius = metrics.keyHitRadius;
                    float ky = blockBot;
                    int hoveredKeyIndex = -1;
                    float hoveredKeyDistanceSq = 1e9f;
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    drawList->PushClipRect(ImVec2(visStart, blockTop),
                                           ImVec2(visEnd, blockBot + trackPadding), true);
                    for (size_t keyIndex = 0; keyIndex < kfComp->times.size(); keyIndex++) {
                        bool selectedKey = allowSelection && selectedKeyFrameIndex == (int)i
                            && selectedKeyIndex == (int)keyIndex;
                        float kt = selectedKey && isDraggingKey ? keyDragTime : kfComp->times[keyIndex];
                        float kx = timeToX(frame.startTime + kt, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
                        if (kx >= visStart && kx <= visEnd) {
                            float radius = selectedKey ? keyMarkerRadius + metrics.scale : keyMarkerRadius;
                            ImU32 color = selectedKey
                                ? IM_COL32(255, 210, 80, 255)
                                : IM_COL32(255, 255, 255, 230);
                            drawList->AddQuadFilled(ImVec2(kx, ky - radius),
                                                    ImVec2(kx + radius, ky),
                                                    ImVec2(kx, ky + radius),
                                                    ImVec2(kx - radius, ky), color);

                            float dx = mousePos.x - kx;
                            float dy = mousePos.y - ky;
                            float distanceSq = dx * dx + dy * dy;
                            if (allowSelection && allowKeying && std::abs(dx) <= keyHitRadius
                                && std::abs(dy) <= keyHitRadius && distanceSq < hoveredKeyDistanceSq) {
                                hoveredKeyIndex = (int)keyIndex;
                                hoveredKeyDistanceSq = distanceSq;
                            }
                        }
                    }
                    drawList->PopClipRect();

                    if (hoveredKeyIndex >= 0) {
                        mouseOverKey = true;
                        mouseOverFrame = true;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        float localTime = isDraggingKey && selectedKeyFrameIndex == (int)i
                            && selectedKeyIndex == hoveredKeyIndex
                            ? keyDragTime : kfComp->times[hoveredKeyIndex];
                        ImGui::SetTooltip("Key %d | %.2fs (local %.2fs)",
                                          hoveredKeyIndex, frame.startTime + localTime, localTime);

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            selectedFrameIndex = (int)i;
                            selectedKeyFrameIndex = (int)i;
                            selectedKeyIndex = hoveredKeyIndex;
                            keyDragStartTime = kfComp->times[hoveredKeyIndex];
                            keyDragTime = keyDragStartTime;
                            isDraggingKey = true;
                            isDraggingPlayhead = false;
                        }
                    }
                }
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
            float availWidth = blockEnd - blockStart - metrics.textPad * 2.0f;
            const char* displayLabel = nullptr;
            if (ImGui::CalcTextSize(fullLabel.c_str()).x <= availWidth) {
                displayLabel = fullLabel.c_str();
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
                // Keep the text attached to the block's un-clamped start so it
                // scrolls out of view with the block instead of sticking to the
                // left edge. Clip it to the visible block to protect the track
                // label column when the text is partially offscreen.
                drawList->PushClipRect(ImVec2(visStart, blockTop),
                                       ImVec2(visEnd, blockBot), true);
                drawList->AddText(ImVec2(blockStart + metrics.textPad, blockTop + metrics.textPad),
                                  IM_COL32(255, 255, 255, 255), displayLabel);
                drawList->PopClipRect();
            }

            // Interaction: click to select, drag to move/resize
            float edgeZone = metrics.resizeEdgeZone;
            ImVec2 blockMin(visStart, blockTop);
            ImVec2 blockMax(visEnd, blockBot);
            ImGui::SetCursorScreenPos(blockMin);
            ImGui::InvisibleButton(("frame_" + std::to_string(i)).c_str(),
                                   ImVec2(blockMax.x - blockMin.x, blockMax.y - blockMin.y));

            // Detect edge hover for resize cursor
            bool hovered = ImGui::IsItemHovered();
            mouseOverFrame = mouseOverFrame || hovered || ImGui::IsItemActive();
            if (hovered && !mouseOverKey) {
                std::string actionLabel = getActionLabel(frame.action, scene);
                std::string tooltipTitle = std::to_string(i) + ": " + actionLabel;
                if (!targetName.empty()) {
                    tooltipTitle += " | " + targetName;
                }
                ImGui::SetTooltip("%s", tooltipTitle.c_str());
            }
            if (allowEditing && hovered && !mouseOverKey && !isDraggingFrame && !isResizingFrame) {
                float mouseX = ImGui::GetIO().MousePos.x;
                bool onLeftEdge = (mouseX - blockStart) < edgeZone && (mouseX >= blockStart);
                bool onRightEdge = (blockEnd - mouseX) < edgeZone && (mouseX <= blockEnd);
                if (onLeftEdge || onRightEdge) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
            }

            // Selection is index-based against selectedEntity's frame list, so it is
            // disabled while the timeline displays a different clip (transition preview).
            if (allowSelection && !mouseOverKey && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                selectedFrameIndex = (int)i;
                selectedKeyFrameIndex = -1;
                selectedKeyIndex = -1;
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

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered && !mouseOverKey) {
                if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
                    project->setSelectedEntity(selectedSceneId, frame.action);
                }
            }

            // Right-click a keyframe-track block: insert a key at the clicked time
            if (allowSelection && allowKeying && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                bool isTracksBlock = frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)
                    && scene->findComponent<KeyframeTracksComponent>(frame.action) != nullptr;
                if (isTracksBlock) {
                    keyPopupFrameIndex = (int)i;
                    float clickTime = snapTime(xToTime(ImGui::GetIO().MousePos.x, timeStart, ImVec2(canvasPos.x + labelWidth, 0)));
                    keyPopupTime = std::max(frame.startTime, clickTime);
                    ImGui::OpenPopup("##timeline_key_popup");
                }
            }
        }
    }

    // Right-click on the empty tracks area: key the animation's target at that time
    if (allowSelection && allowKeying && !mouseOverFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        && ImGui::IsWindowHovered()) {
        ImVec2 keyMousePos = ImGui::GetIO().MousePos;
        bool inTracksArea = keyMousePos.x >= canvasPos.x + labelWidth && keyMousePos.x <= canvasPos.x + canvasSize.x &&
                            keyMousePos.y >= canvasPos.y + rulerHeight && keyMousePos.y <= canvasPos.y + canvasSize.y;
        if (inTracksArea) {
            keyPopupFrameIndex = -1;
            keyPopupTime = std::max(0.0f, snapTime(xToTime(keyMousePos.x, timeStart, ImVec2(canvasPos.x + labelWidth, 0))));
            ImGui::OpenPopup("##timeline_key_popup");
        }
    }

    // Key-insert context menu: stores the target's current values
    if (ImGui::BeginPopup("##timeline_key_popup")) {
        bool validFrame = keyPopupFrameIndex >= 0 && keyPopupFrameIndex < (int)anim.actions.size();

        ImGui::TextDisabled("At %.2fs", keyPopupTime);
        float previewDuration = getAnimationDuration(anim, scene);
        if (isPreviewing && currentTime > previewDuration + kKeyTimeEpsilon) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                               ICON_FA_TRIANGLE_EXCLAMATION " Preview pose held at %.2fs", previewDuration);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The playhead is at %.2fs, past the evaluable clip. Right-click keying\n"
                                  "stores the currently held end pose unless you adjust it first.", currentTime);
            }
        }
        ImGui::Separator();

        if (validFrame) {
            // key the clicked block's own channel for its own target
            ActionFrame& keyFrame = anim.actions[keyPopupFrameIndex];
            Entity actionEntity = keyFrame.action;

            Entity target = NULL_ENTITY;
            int channel = -1;
            const char* channelLabel = nullptr;
            if (actionEntity != NULL_ENTITY && scene->isEntityCreated(actionEntity)) {
                if (ActionComponent* ac = scene->findComponent<ActionComponent>(actionEntity)) {
                    target = ac->target;
                }
                if (scene->findComponent<TranslateTracksComponent>(actionEntity)) {
                    channel = 0;
                    channelLabel = ICON_FA_KEY "  Insert Position Key";
                } else if (scene->findComponent<RotateTracksComponent>(actionEntity)) {
                    channel = 1;
                    channelLabel = ICON_FA_KEY "  Insert Rotation Key";
                } else if (scene->findComponent<ScaleTracksComponent>(actionEntity)) {
                    channel = 2;
                    channelLabel = ICON_FA_KEY "  Insert Scale Key";
                }
            }

            bool withinExplicitSpan = keyFrame.duration <= 0.0f
                || keyPopupTime <= keyFrame.startTime + keyFrame.duration + kKeyTimeEpsilon;
            bool canKey = allowKeying && withinExplicitSpan && channel >= 0
                && target != NULL_ENTITY && scene->isEntityCreated(target)
                && scene->findComponent<Transform>(target) != nullptr;
            if (channel >= 0) {
                ImGui::BeginDisabled(!canKey);
                if (ImGui::MenuItem(channelLabel)) {
                    float localTime = std::max(0.0f, keyPopupTime - keyFrame.startTime);
                    keyTrackChannel(scene, sceneProject, actionEntity, localTime, keyFrame.duration, channel);
                }
                ImGui::EndDisabled();
                if (!withinExplicitSpan) {
                    ImGui::TextDisabled("The clicked time is outside this block's explicit duration");
                } else if (!canKey) {
                    ImGui::TextDisabled("The track needs a target with a Transform");
                }
            } else {
                ImGui::TextDisabled("No keyable channel on this action");
            }
        } else {
            // empty area: key the animation's own target, creating tracks on demand
            Entity target = NULL_ENTITY;
            if (ActionComponent* ac = scene->findComponent<ActionComponent>(selectedEntity)) {
                target = ac->target;
            }
            bool canKey = allowKeying && target != NULL_ENTITY && scene->isEntityCreated(target)
                && scene->findComponent<Transform>(target) != nullptr;

            ImGui::BeginDisabled(!canKey);
            if (ImGui::MenuItem(ICON_FA_KEY "  Key Position")) {
                keyTargetChannel(scene, sceneProject, anim, target, keyPopupTime, 0);
            }
            if (ImGui::MenuItem(ICON_FA_KEY "  Key Rotation")) {
                keyTargetChannel(scene, sceneProject, anim, target, keyPopupTime, 1);
            }
            if (ImGui::MenuItem(ICON_FA_KEY "  Key Scale")) {
                keyTargetChannel(scene, sceneProject, anim, target, keyPopupTime, 2);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_KEY "  Key Transform")) {
                keyTargetChannels(scene, sceneProject, anim, target, keyPopupTime, {0, 1, 2});
            }
            ImGui::EndDisabled();
            if (!canKey) {
                ImGui::TextDisabled("Set a target on the animation to key it");
            }
        }
        ImGui::EndPopup();
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
        float frameDur = effectiveFrameDuration(frame, scene);

        // Find valid position on a track closest to desiredStart without overlap
        auto findValidPosition = [&](float desiredStart, int track) -> float {
            // Collect occupied intervals on this track (excluding dragged frame)
            std::vector<std::pair<float, float>> occupied;
            for (size_t i = 0; i < anim.actions.size(); i++) {
                if ((int)i == draggingFrameIndex) continue;
                const auto& a = anim.actions[i];
                if ((int)a.track == track) {
                    occupied.push_back({a.startTime, a.startTime + effectiveFrameDuration(a, scene)});
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

        // Resizing an auto (<= 0) duration converts it to an explicit one; the drag
        // math starts from the resolved length. resizeStartDuration stays raw so the
        // undo command records the original auto value.
        float baseDuration = (resizeStartDuration > 0) ? resizeStartDuration
            : scene->getSystem<ActionSystem>()->getDuration(frame.action);

        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        // Find nearest neighbors on the same track to prevent overlap
        float neighborLeftEnd = 0.0f;   // max end time of actions to our left
        float neighborRightStart = 1e9f; // min start time of actions to our right
        float origEnd = resizeStartTime + baseDuration;
        for (size_t i = 0; i < anim.actions.size(); i++) {
            if ((int)i == resizingFrameIndex) continue;
            const auto& a = anim.actions[i];
            if (a.track != frame.track) continue;
            float aEnd = a.startTime + effectiveFrameDuration(a, scene);
            if (aEnd <= resizeStartTime + 0.001f) {
                neighborLeftEnd = std::max(neighborLeftEnd, aEnd);
            }
            if (a.startTime >= origEnd - 0.001f) {
                neighborRightStart = std::min(neighborRightStart, a.startTime);
            }
        }

        if (resizeSide == 1) {
            // Right edge: only change duration, clamp to neighbor on right
            float newDuration = snapTime(baseDuration + timeDelta);
            float maxDuration = neighborRightStart - frame.startTime;
            frame.duration = std::clamp(newDuration, minDuration, maxDuration);
        } else {
            // Left edge: move start and adjust duration (right end stays fixed)
            float newStart = snapTime(resizeStartTime + timeDelta);
            float endTime = resizeStartTime + baseDuration;
            newStart = std::clamp(newStart, neighborLeftEnd, endTime - minDuration);
            frame.startTime = newStart;
            frame.duration = endTime - newStart;
        }
        sceneProject->isModified = true;
    }

    if ((isDraggingKey || isDraggingFrame || isResizingFrame)
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        finishTimelineDrag(scene, sceneProject, true);
    }

    // Accept action entities dragged from the Structure window onto the tracks area
    if (allowSelection && allowEditing) {
        ImRect dropRect(ImVec2(canvasPos.x + labelWidth, canvasPos.y + rulerHeight),
                        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
        if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("##anim_tracks_drop"))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                Entity dropped = entityPayload->entity;

                const char* invalidReason = nullptr;
                if (entityPayload->entitySceneId != 0 && entityPayload->entitySceneId != sceneProject->id) {
                    invalidReason = "Entity belongs to another scene";
                } else if (dropped == NULL_ENTITY || !scene->isEntityCreated(dropped)) {
                    invalidReason = "Invalid entity";
                } else if (!scene->findComponent<ActionComponent>(dropped)) {
                    invalidReason = "Entity has no Action component";
                } else if (scene->getSystem<ActionSystem>()->isAnimationReachable(dropped, selectedEntity)) {
                    invalidReason = "Would create an animation cycle";
                }

                if (invalidReason) {
                    ImGui::SetTooltip("%s", invalidReason);
                } else {
                    ImVec2 dropMousePos = ImGui::GetIO().MousePos;
                    float dropTime = std::max(0.0f, snapTime(xToTime(dropMousePos.x, timeStart, ImVec2(canvasPos.x + labelWidth, 0))));
                    int dropTrack = std::max(0, (int)std::floor((dropMousePos.y - canvasPos.y - rulerHeight) / (trackHeight + trackPadding)));

                    ImGui::SetTooltip(ICON_FA_PLUS " %s | Track %d | %.2fs",
                                      getActionLabel(dropped, scene).c_str(), dropTrack, dropTime);

                    // Ghost block previewing where the frame would land
                    float ghostDur = scene->getSystem<ActionSystem>()->getDuration(dropped);
                    float ghostStart = timeToX(dropTime, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
                    float ghostEnd = timeToX(dropTime + ghostDur, timeStart, ImVec2(canvasPos.x + labelWidth, 0));
                    float ghostMinWidth = metrics.minBlockWidth;
                    if (ghostEnd - ghostStart < ghostMinWidth) {
                        ghostEnd = ghostStart + ghostMinWidth;
                    }
                    float ghostY = canvasPos.y + rulerHeight + dropTrack * (trackHeight + trackPadding);
                    float ghostVisStart = std::max(ghostStart, canvasPos.x + labelWidth);
                    float ghostVisEnd = std::min(ghostEnd, canvasPos.x + canvasSize.x);
                    if (ghostVisEnd > ghostVisStart) {
                        ImU32 ghostColor = getFrameColor(dropped);
                        ImU32 ghostFill = (ghostColor & ~IM_COL32_A_MASK) | ((ImU32)110 << IM_COL32_A_SHIFT);
                        drawList->AddRectFilled(ImVec2(ghostVisStart, ghostY + metrics.inset), ImVec2(ghostVisEnd, ghostY + trackHeight - metrics.inset),
                                                ghostFill, metrics.blockRounding);
                        drawList->AddRect(ImVec2(ghostVisStart, ghostY + metrics.inset), ImVec2(ghostVisEnd, ghostY + trackHeight - metrics.inset),
                                          IM_COL32(255, 255, 255, 180), metrics.blockRounding, 0, 1.5f * metrics.scale);

                        std::string ghostLabel = getActionLabel(dropped, scene);
                        if (ImGui::CalcTextSize(ghostLabel.c_str()).x <= ghostVisEnd - ghostVisStart - metrics.textPad * 2.0f) {
                            drawList->AddText(ImVec2(ghostVisStart + metrics.textPad, ghostY + metrics.textPad),
                                              IM_COL32(255, 255, 255, 200), ghostLabel.c_str());
                        }
                    }

                    if (payload->IsDelivery()) {
                        // Duration 0 = auto: the frame follows the action's own duration.
                        // Overlaps on the target track are re-laned by autoAssignTracks.
                        std::vector<ActionFrame> newActions = anim.actions;
                        newActions.push_back({dropTime, 0.0f, dropped, (uint32_t)dropTrack});
                        auto* cmd = new PropertyCmd<std::vector<ActionFrame>>(
                            project, sceneProject->id, selectedEntity, ComponentType::AnimationComponent, "actions", newActions,
                            [sceneProject]() { sceneProject->isModified = true; });
                        CommandHandle::get(sceneProject->id)->addCommandNoMerge(cmd);
                        selectedFrameIndex = (int)anim.actions.size() - 1;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    return mouseOverFrame;
}

bool editor::AnimationWindow::drawPlayhead(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const TimelineMetrics metrics = timelineMetrics();
    float labelWidth = metrics.labelWidth;

    float playheadX = canvasPos.x + labelWidth + (currentTime - timeStart) * pixelsPerSecond;

    if (playheadX >= canvasPos.x + labelWidth && playheadX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(ImVec2(playheadX, canvasPos.y),
                          ImVec2(playheadX, canvasPos.y + canvasSize.y),
                          IM_COL32(255, 80, 80, 255), metrics.playheadThickness);

        // Playhead triangle at top
        drawList->AddTriangleFilled(
            ImVec2(playheadX - metrics.playheadHalf, canvasPos.y),
            ImVec2(playheadX + metrics.playheadHalf, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + metrics.playheadTip),
            IM_COL32(255, 80, 80, 255));
    }

    ImVec2 timeAreaMin(canvasPos.x + labelWidth, canvasPos.y);
    ImVec2 timeAreaMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseInTimeArea = mousePos.x >= timeAreaMin.x && mousePos.x <= timeAreaMax.x &&
                           mousePos.y >= timeAreaMin.y && mousePos.y <= timeAreaMax.y;

    bool scrubbed = false;
    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (isDraggingPlayhead && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) &&
        ImGui::IsWindowHovered() && mouseInTimeArea && !isDraggingFrame && !isResizingFrame && !isDraggingKey) {
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
    if (!windowOpen) {
        isWindowVisible = false;
        return;
    }

    static bool preventScroll = false;

    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    bool wasOpen = windowOpen;

    if (hasNotification) {
        App::pushTabNotificationStyle();
    }

    ImGuiWindowFlags flags = hasNotification ? ImGuiWindowFlags_UnsavedDocument : 0;
    if (preventScroll) flags |= ImGuiWindowFlags_NoScrollWithMouse;

    isWindowVisible = ImGui::Begin(AnimationWindow::WINDOW_NAME, &windowOpen, flags);
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
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
        return;
    }

    Scene* scene = sceneProject->scene;
    if (!scene) {
        if (isPreviewing) {
            restorePreviewScene();
        }
        ImGui::TextDisabled("No scene available.");
        ImGui::End();
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
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
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
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
    autoAssignTracks(selectedEntity, *animComp, sceneProject);

    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);
    if (isPreviewing && !sceneIsStopped) {
        stopPreview(scene, sceneProject);
        isPlaying = false;
        currentTime = 0;
    }

    // Playback update
    if (isPreviewing && sceneIsStopped) {
        // Engine-driven preview: ActionSystem processes the animation(s). During a
        // transition preview, previewAnimations holds both the outgoing and incoming clip
        // and the engine blends them in one pose scope.
        if (previewAnimations.empty()) {
            previewAnimations.push_back(selectedEntity);
            previewPrimary = selectedEntity;
        }

        if (isPlaying) {
            if (prePlaySelectedFrameIndex == -1) {
                prePlaySelectedFrameIndex = selectedFrameIndex;
            }
            selectedFrameIndex = -1;
            float dt = ImGui::GetIO().DeltaTime * playbackSpeed;
            scene->getSystem<ActionSystem>()->updateAnimationPreview(dt, previewAnimations);
            sceneProject->needUpdateRender = true;
        }

        // Playback continues while any previewed clip is still running.
        bool anyRunning = false;
        for (Entity e : previewAnimations) {
            if (e != NULL_ENTITY && scene->isEntityCreated(e)) {
                if (ActionComponent* ac = scene->findComponent<ActionComponent>(e)) {
                    if (ac->state == ActionState::Running) { anyRunning = true; break; }
                }
            }
        }

        if (isPlaying) {
            if (!anyRunning) {
                isPlaying = false;
            }
            // Drive the playhead from the dominant clip (the transition target once blended).
            Entity timeSource = previewPrimary;
            if (timeSource == NULL_ENTITY || !scene->isEntityCreated(timeSource) ||
                !scene->findComponent<ActionComponent>(timeSource)) {
                timeSource = selectedEntity;
            }
            currentTime = scene->getComponent<ActionComponent>(timeSource).timecount;
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
    // The toolbar can add an AnimationComponent, which can move this one
    animComp = &scene->getComponent<AnimationComponent>(selectedEntity);

    ImGui::Separator();

    // While a transition preview is active the timeline (tracks, duration, playhead)
    // follows the clip the blend targeted; selection and authoring stay on selectedEntity.
    Entity displayEntity = timelineClip(scene);
    AnimationComponent* displayAnim = animComp;
    if (displayEntity != selectedEntity) {
        displayAnim = &scene->getComponent<AnimationComponent>(displayEntity);
        // The blend target may never have been selected here, so its frames (all
        // track 0 when created via the engine API) can still be stacked on one lane.
        autoAssignTracks(displayEntity, *displayAnim, sceneProject);
        ImGui::TextDisabled(ICON_FA_RIGHT_LEFT " Timeline: %s (transition preview)",
                            getAnimationEntityLabel(displayEntity, scene).c_str());
    }

    // The tracks live in their own vertical-scrolling viewport. The horizontal
    // scrollbar is drawn in the parent below it, so reaching the last track is
    // never required to pan through time.
    ImVec2 timelineAvailable = ImGui::GetContentRegionAvail();
    const TimelineMetrics metrics = timelineMetrics();
    const float labelWidth = metrics.labelWidth;
    constexpr float scrollbarGap = 1.0f;
    const float trackEndPadding = metrics.trackEndPadding;
    const float rulerHeight = metrics.rulerHeight;
    const float trackHeight = metrics.trackHeight;
    const float trackPadding = metrics.trackPadding;

    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    // The ruler is part of the vertically scrolling child. Its zoom hit zone
    // must follow that scrolled position instead of remaining at viewport y=0.
    float rulerScreenY = viewportPos.y - scrollY;
    float visibleRulerTop = std::max(viewportPos.y, rulerScreenY);
    float visibleRulerBottom = std::min(viewportPos.y + timelineAvailable.y,
                                       rulerScreenY + rulerHeight);
    bool mouseAboveTracks = ImGui::IsWindowHovered()
        && visibleRulerBottom > visibleRulerTop
        && ImGui::GetMousePos().x >= viewportPos.x
        && ImGui::GetMousePos().x <= viewportPos.x + timelineAvailable.x
        && ImGui::GetMousePos().y >= visibleRulerTop
        && ImGui::GetMousePos().y <= visibleRulerBottom;

    // Handle mouse wheel zoom before measuring horizontal overflow.
    preventScroll = mouseAboveTracks;
    if (mouseAboveTracks && ImGui::GetIO().MouseWheel != 0) {
        float zoomFactor = 1.0f + ImGui::GetIO().MouseWheel * 0.1f;
        pixelsPerSecond = std::clamp(pixelsPerSecond * zoomFactor, minPixelsPerSecond, maxPixelsPerSecond);
    }

    uint32_t maxDisplayTrack = 0;
    float blockContentWidth = 0.0f;
    for (size_t frameIndex = 0; frameIndex < displayAnim->actions.size(); frameIndex++) {
        const ActionFrame& frame = displayAnim->actions[frameIndex];
        maxDisplayTrack = std::max(maxDisplayTrack, frame.track);
        float blockStartPx = frame.startTime * pixelsPerSecond;
        float frameDuration = effectiveFrameDuration(frame, scene);
        if (displayEntity == selectedEntity && frame.duration <= 0.0f && isDraggingKey
            && selectedKeyFrameIndex == (int)frameIndex) {
            // Match drawTracks' live auto-duration stretch while a key is moving.
            frameDuration = std::max(frameDuration, keyDragTime);
        }
        float blockEndPx = (frame.startTime + frameDuration) * pixelsPerSecond;
        // Zero-length blocks still draw with a minimum width.
        blockContentWidth = std::max(blockContentWidth,
            std::max(blockEndPx, blockStartPx + metrics.minBlockWidth));
    }

    // Duration and the authoring playhead remain reachable even when they lie
    // beyond the last block. Apply the same trailing margin to the final
    // effective extent so visibility and scroll range cannot disagree.
    float authoringEndTime = std::max(getAnimationDuration(*displayAnim, scene), currentTime);
    float effectiveContentWidth = std::max(blockContentWidth,
        std::max(0.0f, authoringEndTime) * pixelsPerSecond);
    if (effectiveContentWidth > 0.0f) {
        effectiveContentWidth += trackEndPadding;
    }

    uint32_t displayTrackCount = std::max((uint32_t)3, maxDisplayTrack + 2);
    float trackContentHeight = rulerHeight + displayTrackCount * (trackHeight + trackPadding);

    // Scrollbars reduce the other axis's available space, so resolve their
    // visibility together. Starting hidden and only enabling them makes this
    // monotonic and settles after at most two cross-axis dependencies.
    float scrollbarSize = ImGui::GetStyle().ScrollbarSize;
    bool showHorizontalScrollbar = false;
    bool showVerticalScrollbar = false;
    for (int i = 0; i < 3; i++) {
        float availableTimeWidth = std::max(1.0f,
            timelineAvailable.x - (showVerticalScrollbar ? scrollbarSize : 0.0f) - labelWidth);
        if (effectiveContentWidth > availableTimeWidth + 0.5f) {
            showHorizontalScrollbar = true;
        }

        float availableTrackHeight = std::max(60.0f,
            timelineAvailable.y - (showHorizontalScrollbar ? scrollbarSize + scrollbarGap : 0.0f));
        if (trackContentHeight > availableTrackHeight + 0.5f) {
            showVerticalScrollbar = true;
        }
    }

    float scrollbarHeight = showHorizontalScrollbar ? ImGui::GetStyle().ScrollbarSize : 0.0f;
    float horizontalReserve = showHorizontalScrollbar ? scrollbarHeight + scrollbarGap : 0.0f;
    float viewportHeight = std::max(60.0f, timelineAvailable.y - horizontalReserve);

    ImGuiWindowFlags trackViewportFlags = showVerticalScrollbar
        ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_None;
    if (mouseAboveTracks) {
        trackViewportFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    }

    ImGui::BeginChild("##animation_timeline_vertical", ImVec2(timelineAvailable.x, viewportHeight),
                      ImGuiChildFlags_None, trackViewportFlags);

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    float visibleCanvasHeight = ImGui::GetContentRegionAvail().y;
    ImVec2 canvasSize(ImGui::GetContentRegionAvail().x,
                      std::max(visibleCanvasHeight, trackContentHeight));

    // The label column stays fixed while the time area scrolls. Keep the
    // scrollbar range in pixels so it follows timeline zoom exactly.
    float timeAreaWidth = std::max(1.0f, canvasSize.x - labelWidth);
    float timelineContentWidth = showHorizontalScrollbar
        // Once content overflows, allow its end to reach the viewport center.
        // Smooth playback following can then keep the playhead centered through
        // the final frame instead of exhausting the scroll range early.
        ? std::max(timeAreaWidth, effectiveContentWidth
            + std::max(0.0f, timeAreaWidth * 0.5f - trackEndPadding))
        : timeAreaWidth;
    float maxScroll = std::max(0.0f, timelineContentWidth - timeAreaWidth);
    scrollX = showHorizontalScrollbar ? std::clamp(scrollX, 0.0f, maxScroll) : 0.0f;

    // ScrollbarEx is submitted after the timeline child, so detect its initial
    // mouse-down here. Otherwise automatic following recenters once on the first
    // drag frame before ScrollbarEx can claim the active ID.
    if (showHorizontalScrollbar && isPlaying && !playbackFollowSuspended
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float scrollbarScreenY = viewportPos.y + viewportHeight
            + std::min(ImGui::GetStyle().ItemSpacing.y, scrollbarGap);
        ImRect pendingScrollbarRect(
            ImVec2(viewportPos.x + labelWidth, scrollbarScreenY),
            ImVec2(viewportPos.x + canvasSize.x, scrollbarScreenY + scrollbarHeight));
        if (pendingScrollbarRect.Contains(ImGui::GetMousePos())) {
            playbackFollowSuspended = true;
        }
    }

    // During playback, let the playhead travel to the center and then move the
    // timeline smoothly beneath it. A loop (or a transition to an earlier clip
    // time) puts the playhead left of the viewport and brings the view back too.
    if (showHorizontalScrollbar && isPlaying && !playbackFollowSuspended) {
        float playheadContentX = currentTime * pixelsPerSecond;
        float viewportCenterX = scrollX + timeAreaWidth * 0.5f;
        if (playheadContentX > viewportCenterX || playheadContentX < scrollX) {
            float followedScrollX = playheadContentX - timeAreaWidth * 0.5f;
            scrollX = std::clamp(followedScrollX, 0.0f, maxScroll);
        }
    }

    float timeStart = scrollX / pixelsPerSecond;
    float visibleTime = timeAreaWidth / pixelsPerSecond;
    float timeEnd = timeStart + visibleTime;

    // Draw background
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                            IM_COL32(30, 30, 30, 255));

    drawTimeRuler(canvasPos, canvasSize, timeStart, timeEnd);
    bool mouseOverFrame = drawTracks(canvasPos, canvasSize, timeStart, timeEnd, *displayAnim, sceneProject,
                                     displayEntity == selectedEntity);
    bool scrubbed = drawPlayhead(canvasPos, canvasSize, timeStart, timeEnd);

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseInCanvas = mousePos.x >= canvasPos.x && mousePos.x <= canvasPos.x + canvasSize.x &&
                         mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + canvasSize.y;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && mouseInCanvas &&
        !mouseOverFrame && !isDraggingFrame && !isResizingFrame && !isDraggingKey) {
        selectedFrameIndex = -1;
        selectedKeyFrameIndex = -1;
        selectedKeyIndex = -1;
    }

    if (scrubbed) {
        if (sceneIsStopped && canPreviewEntity(displayEntity, scene)) {
            seekPreview(scene, sceneProject, currentTime, displayEntity);
        } else if (sceneIsStopped) {
            float authoringEnd = std::max(kAuthoringMaxTime, getAnimationDuration(*displayAnim, scene));
            currentTime = std::clamp(currentTime, 0.0f, authoringEnd);
        } else {
            currentTime = std::max(0.0f, std::min(currentTime, getAnimationDuration(*displayAnim, scene)));
        }
    }

    // Restore the child layout cursor after drawTracks positioned interaction
    // items at individual blocks, then establish the vertical content extent.
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::Dummy(canvasSize);
    scrollY = showVerticalScrollbar ? ImGui::GetScrollY() : 0.0f;
    ImGui::EndChild();

    if (showHorizontalScrollbar) {
        // This control belongs to the parent window and therefore stays fixed
        // below the viewport while the child scrolls vertically.
        ImVec2 scrollbarPos = ImGui::GetCursorScreenPos();
        scrollbarPos.y -= std::max(0.0f, ImGui::GetStyle().ItemSpacing.y - scrollbarGap);
        ImGui::SetCursorScreenPos(scrollbarPos);
        ImGui::Dummy(ImVec2(timelineAvailable.x, scrollbarHeight));
        ImRect scrollbarRect(ImVec2(scrollbarPos.x + labelWidth, scrollbarPos.y),
                             ImVec2(scrollbarPos.x + canvasSize.x, scrollbarPos.y + scrollbarHeight));
        ImS64 scrollValue = (ImS64)std::llround(scrollX);
        ImS64 visibleValue = std::max<ImS64>(1, (ImS64)std::llround(timeAreaWidth));
        ImS64 contentValue = std::max(visibleValue, (ImS64)std::llround(timelineContentWidth));
        ImDrawList* parentDrawList = ImGui::GetWindowDrawList();
        parentDrawList->AddRectFilled(scrollbarPos,
                                      ImVec2(scrollbarPos.x + labelWidth, scrollbarPos.y + scrollbarHeight),
                                      ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
        ImGuiID scrollbarId = ImGui::GetID("##animation_timeline_scrollbar");
        ImGui::ScrollbarEx(scrollbarRect, scrollbarId,
                           ImGuiAxis_X, &scrollValue, visibleValue, contentValue);
        if (isPlaying && ImGui::GetActiveID() == scrollbarId) {
            playbackFollowSuspended = true;
        }
        scrollX = std::clamp((float)scrollValue, 0.0f, maxScroll);
    }

    ImGui::End();

    if (wasOpen && !windowOpen) {
        setOpen(false);
    }
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

    finishTimelineDrag(scene, sceneProject, true);

    if (sceneIsStopped && !isPreviewing) {
        currentTime = 0;
        startPreview(scene, sceneProject);
    } else if (sceneIsStopped && isPreviewing) {
        ActionComponent& action = sceneProject->scene->getComponent<ActionComponent>(selectedEntity);
        if (action.state == ActionState::Stopped) {
            // Explicit "play this entity" request: seek it, not the timeline clip.
            seekPreview(scene, sceneProject, 0.0f, selectedEntity);
        }
    }
    playbackFollowSuspended = false;
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
    // External callers scrub the selected clip, not a transition-preview target.
    seekPreview(scene, sceneProject, time, selectedEntity);
}
