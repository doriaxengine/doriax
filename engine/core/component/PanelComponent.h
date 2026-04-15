//
// (c) 2026 Eduardo Doria.
//

#ifndef PANEL_COMPONENT_H
#define PANEL_COMPONENT_H

#include "util/FunctionSubscribe.h"

namespace doriax{

    enum class PanelEdge{
        NONE,
        RIGHT,
        RIGHT_BOTTOM,
        BOTTOM,
        LEFT_BOTTOM,
        LEFT
    };

    struct DORIAX_API PanelComponent{
        Entity headerimage = NULL_ENTITY;
        Entity headercontainer = NULL_ENTITY;
        Entity headertext = NULL_ENTITY;

        AnchorPreset titleAnchorPreset = AnchorPreset::CENTER;

        unsigned int minWidth = 0;
        unsigned int minHeight = 0;

        int headerMarginLeft = 0;
        int headerMarginTop = 0;
        int headerMarginRight = 0;
        int headerMarginBottom = 0;

        bool defaultHeaderMargin = true;

        int resizeMargin = 5;

        bool canMove = true;
        bool canResize = true;
        bool canBringToFront = true;

        bool headerPointerDown = false;
        PanelEdge edgePointerDown = PanelEdge::NONE;

        FunctionSubscribe<void()> onMove;
        FunctionSubscribe<void(int, int)> onResize;

        bool needUpdatePanel = true;
    };

}

#endif //PANEL_COMPONENT_H