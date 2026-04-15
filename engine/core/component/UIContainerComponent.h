//
// (c) 2026 Eduardo Doria.
//

#ifndef UI_CONTAINER_COMPONENT_H
#define UI_CONTAINER_COMPONENT_H

#define MAX_CONTAINER_BOXES 30

#include "math/Rect.h"

namespace doriax{

    enum class ContainerType{
        VERTICAL,
        HORIZONTAL,
        VERTICAL_WRAP,
        HORIZONTAL_WRAP
    };

    struct ContainerBox{
        Entity layout = NULL_ENTITY;
        Rect rect = Rect(0,0,0,0);
        bool expand = true;
    };

    struct DORIAX_API UIContainerComponent{
        ContainerType type = ContainerType::VERTICAL;
        bool useAllWrapSpace = false;

        unsigned int wrapCellWidth = 0;  // 0 = auto (use maxWidth from children)
        unsigned int wrapCellHeight = 0; // 0 = auto (use maxHeight from children)

        ContainerBox boxes[MAX_CONTAINER_BOXES];

        unsigned int numBoxes = 0;
        unsigned int fixedWidth = 0;
        unsigned int fixedHeight = 0;
        unsigned int maxWidth = 0;
        unsigned int maxHeight = 0;
        unsigned int numBoxExpand = 0;

        // Intrinsic minimum size from non-anchor-derived content children
        // Used by parent containers to enforce min size for anchor-derived child containers
        unsigned int contentMinWidth = 0;
        unsigned int contentMinHeight = 0;
    };
    
}

#endif //UI_CONTAINER_COMPONENT_H
