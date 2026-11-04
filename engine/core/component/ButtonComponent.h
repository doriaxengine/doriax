// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef BUTTON_COMPONENT_H
#define BUTTON_COMPONENT_H

#include "util/FunctionSubscribe.h"
#include "texture/Texture.h"

namespace doriax{

    struct DORIAX_API ButtonComponent{
        Entity label = NULL_ENTITY;

        Texture textureNormal;
        Texture textureHovered;
        Texture texturePressed;
        Texture textureDisabled;

        Vector4 colorNormal = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color
        Vector4 colorHovered = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color
        Vector4 colorPressed = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color
        Vector4 colorDisabled = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color

        // a hovered or pressed button grows by these, around its pivot
        float scaleHovered = 1.0f;
        float scalePressed = 1.0f;
        float transitionTime = 0.0f; // seconds the color and scale take to change

        FunctionSubscribe<void()> onPress;
        FunctionSubscribe<void()> onRelease;

        bool pressed = false;
        bool hovered = false;
        bool disabled = false;
        bool focusable = true; // keys and gamepads can move the focus to it

        bool needUpdateButton = true;

        float scale = 1.0f;
        Vector3 restScale = Vector3::UNIT_SCALE;
        float transitionLeft = 0.0f;
    };

}

#endif //BUTTON_COMPONENT_H