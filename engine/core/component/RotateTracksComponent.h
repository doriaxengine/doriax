//
// (c) 2026 Eduardo Doria.
//

#ifndef ROTATETRACKS_COMPONENT_H
#define ROTATETRACKS_COMPONENT_H

#include "Engine.h"

namespace doriax{

    struct DORIAX_API RotateTracksComponent{
        std::vector<Quaternion> values;
    };

}

#endif //ROTATETRACKS_COMPONENT_H