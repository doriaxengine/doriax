//
// (c) 2026 Eduardo Doria.
//

#include "TranslateTracks.h"

#include "component/KeyframeTracksComponent.h"
#include "component/TranslateTracksComponent.h"

using namespace doriax;

TranslateTracks::TranslateTracks(Scene* scene): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<TranslateTracksComponent>();
}

TranslateTracks::TranslateTracks(Scene* scene, std::vector<float> times, std::vector<Vector3> values): Action(scene){
    addComponent<KeyframeTracksComponent>();
    addComponent<TranslateTracksComponent>();

    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;

    TranslateTracksComponent& translatetracks = getComponent<TranslateTracksComponent>();

    translatetracks.values = values;
}

void TranslateTracks::setTimes(std::vector<float> times){
    KeyframeTracksComponent& keyframe = getComponent<KeyframeTracksComponent>();

    keyframe.times = times;
}

void TranslateTracks::setValues(std::vector<Vector3> values){
    TranslateTracksComponent& translatetracks = getComponent<TranslateTracksComponent>();

    translatetracks.values = values;
}