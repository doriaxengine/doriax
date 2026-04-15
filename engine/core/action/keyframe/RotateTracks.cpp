//
// (c) 2026 Eduardo Doria.
//

#include "RotateTracks.h"

#include "component/KeyframeTracksComponent.h"
#include "component/RotateTracksComponent.h"

using namespace doriax;

RotateTracks::RotateTracks(Scene* scene): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<RotateTracksComponent>();
}

RotateTracks::RotateTracks(Scene* scene, std::vector<float> times, std::vector<Quaternion> values): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<RotateTracksComponent>();

    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;

    RotateTracksComponent& rotatetracks = getComponent<RotateTracksComponent>();

    rotatetracks.values = values;
}

void RotateTracks::setTimes(std::vector<float> times){
    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;
}

void RotateTracks::setValues(std::vector<Quaternion> values){
    RotateTracksComponent& rotatetracks = getComponent<RotateTracksComponent>();

    rotatetracks.values = values;
}