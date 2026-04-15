//
// (c) 2026 Eduardo Doria.
//

#include "RotationAction.h"

using namespace doriax;

RotationAction::RotationAction(Scene* scene): TimedAction(scene){
    addComponent<RotationActionComponent>();
}

void RotationAction::setAction(Quaternion startRotation, Quaternion endRotation, float duration, bool loop){
    TimedAction::setAction(duration, loop);

    RotationActionComponent& rotAction = getComponent<RotationActionComponent>();

    rotAction.startRotation = startRotation;
    rotAction.endRotation = endRotation;
}