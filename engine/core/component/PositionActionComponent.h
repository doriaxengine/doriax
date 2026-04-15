//
// (c) 2026 Eduardo Doria.
//

#ifndef POSITIONACTION_COMPONENT_H
#define POSITIONACTION_COMPONENT_H

#include "math/Vector3.h"

namespace doriax{

    struct DORIAX_API PositionActionComponent{
        Vector3 endPosition;
        Vector3 startPosition;
    };

}

#endif //POSITIONACTION_COMPONENT_H