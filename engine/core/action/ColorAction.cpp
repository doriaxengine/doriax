//
// (c) 2026 Eduardo Doria.
//

#include "ColorAction.h"
#include "util/Color.h"

using namespace doriax;

ColorAction::ColorAction(Scene* scene): TimedAction(scene){
    addComponent<ColorActionComponent>();
}

void ColorAction::setAction(Vector3 startColor, Vector3 endColor, float duration, bool loop){
    TimedAction::setAction(duration, loop);

    ColorActionComponent& colorAction = getComponent<ColorActionComponent>();

    colorAction.startColor = startColor;
    colorAction.endColor = endColor;
}

void ColorAction::setAction(Vector4 startColor, Vector4 endColor, float duration, bool loop){
    setAction(Vector3(startColor.x, startColor.y, startColor.z), Vector3(endColor.x, endColor.y, endColor.z), duration, loop);
}