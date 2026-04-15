//
// (c) 2026 Eduardo Doria.
//

#ifndef UI_LAYOUT_COMPONENT_H
#define UI_LAYOUT_COMPONENT_H

#include "util/FunctionSubscribe.h"
#include "math/Rect.h"
#include "math/Quaternion.h"
#include "math/Matrix4.h"
#include "math/Vector2.h"

namespace doriax{

    enum class AnchorPreset{
        NONE,
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT,
        CENTER_LEFT,
        CENTER_TOP,
        CENTER_RIGHT,
        CENTER_BOTTOM,
        CENTER,
        LEFT_WIDE,
        TOP_WIDE,
        RIGHT_WIDE,
        BOTTOM_WIDE,
        VERTICAL_CENTER_WIDE,
        HORIZONTAL_CENTER_WIDE,
        FULL_LAYOUT
    };

    struct DORIAX_API UILayoutComponent{
        // UI part
        unsigned int width = 0;
        unsigned int height = 0;

        float anchorPointLeft = 0;
        float anchorPointTop = 0;
        float anchorPointRight = 0;
        float anchorPointBottom = 0;

        int anchorOffsetLeft = 0;
        int anchorOffsetTop = 0;
        int anchorOffsetRight = 0;
        int anchorOffsetBottom = 0;

        Vector2 positionOffset = Vector2(0, 0); // for anchors

        AnchorPreset anchorPreset = AnchorPreset::NONE;
        bool usingAnchors = false;

        Entity panel = NULL_ENTITY;

        int containerBoxIndex = -1;

        Rect scissor = Rect(0, 0, 0, 0);
        bool ignoreScissor = false; // ignore parent scissor
        bool ignoreEvents = false;

        bool needUpdateSizes = false;
        bool needUpdateAnchorOffsets = false;
    };
    
}

#endif //UI_LAYOUT_COMPONENT_H