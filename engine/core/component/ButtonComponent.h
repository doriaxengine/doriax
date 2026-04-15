//
// (c) 2026 Eduardo Doria.
//

#ifndef BUTTON_COMPONENT_H
#define BUTTON_COMPONENT_H

#include "util/FunctionSubscribe.h"
#include "texture/Texture.h"

namespace doriax{

    struct DORIAX_API ButtonComponent{
        Entity label = NULL_ENTITY;

        Texture textureNormal;
        Texture texturePressed;
        Texture textureDisabled;

        Vector4 colorNormal = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color
        Vector4 colorPressed = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color
        Vector4 colorDisabled = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color

        FunctionSubscribe<void()> onPress;
        FunctionSubscribe<void()> onRelease;

        bool pressed = false;
        bool disabled = false;

        bool needUpdateButton = true;
    };

}

#endif //BUTTON_COMPONENT_H