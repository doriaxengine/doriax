//
// (c) 2026 Eduardo Doria.
//

#ifndef TEXTEDIT_COMPONENT_H
#define TEXTEDIT_COMPONENT_H

#include "util/FunctionSubscribe.h"

namespace doriax{

    struct DORIAX_API TextEditComponent{
        Entity text = NULL_ENTITY;
        Entity cursor = NULL_ENTITY;

        float cursorBlink = 0.6f;
        float cursorWidth = 2;
        Vector4 cursorColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f); //linear

        float cursorBlinkTimer = 0;
        bool disabled = false;

        bool needUpdateTextEdit = true;
    };

}

#endif //TEXTEDIT_COMPONENT_H