//
// (c) 2026 Eduardo Doria.
//

#include "AlphaAction.h"

using namespace doriax;

AlphaAction::AlphaAction(Scene* scene): TimedAction(scene){
    addComponent<AlphaActionComponent>();
}

void AlphaAction::setAction(float startAlpha, float endAlpha, float duration, bool loop){
    TimedAction::setAction(duration, loop);

    AlphaActionComponent& alphaAction = getComponent<AlphaActionComponent>();

    alphaAction.startAlpha = startAlpha;
    alphaAction.endAlpha = endAlpha;
}
