//
// (c) 2026 Eduardo Doria.
//

#include "ScaleTracks.h"

#include "component/KeyframeTracksComponent.h"
#include "component/ScaleTracksComponent.h"

using namespace doriax;

ScaleTracks::ScaleTracks(Scene* scene): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<ScaleTracksComponent>();
}

ScaleTracks::ScaleTracks(Scene* scene, std::vector<float> times, std::vector<Vector3> values): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<ScaleTracksComponent>();

    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;

    ScaleTracksComponent& scaletracks = getComponent<ScaleTracksComponent>();

    scaletracks.values = values;
}

void ScaleTracks::setTimes(std::vector<float> times){
    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;
}

void ScaleTracks::setValues(std::vector<Vector3> values){
    ScaleTracksComponent& scaletracks = getComponent<ScaleTracksComponent>();

    scaletracks.values = values;
}