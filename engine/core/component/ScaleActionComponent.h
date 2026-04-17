//
// (c) 2026 Eduardo Doria.
//

#ifndef SCALEACTION_COMPONENT_H
#define SCALEACTION_COMPONENT_H

#include "math/Vector3.h"

namespace doriax{

    struct DORIAX_API ScaleActionComponent{
        Vector3 endScale = Vector3::UNIT_SCALE;
        Vector3 startScale = Vector3::UNIT_SCALE;
    };

}

#endif //SCALEACTION_COMPONENT_H