//
// (c) 2026 Eduardo Doria.
//

#include "MorphTracks.h"

#include "component/KeyframeTracksComponent.h"
#include "component/MorphTracksComponent.h"

using namespace doriax;

MorphTracks::MorphTracks(Scene* scene): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<MorphTracksComponent>();
}

MorphTracks::MorphTracks(Scene* scene, std::vector<float> times, std::vector<std::vector<float>> values): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<MorphTracksComponent>();

    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;

    MorphTracksComponent& morphtracks = getComponent<MorphTracksComponent>();

    morphtracks.values = values;
}

void MorphTracks::setTimes(std::vector<float> times){
    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;
}

void MorphTracks::setValues(std::vector<std::vector<float>> values){
    MorphTracksComponent& morphtracks = getComponent<MorphTracksComponent>();

    morphtracks.values = values;
}