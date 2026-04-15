//
// (c) 2026 Eduardo Doria.
//

#ifndef KEYFRAMETRACKS_COMPONENT_H
#define KEYFRAMETRACKS_COMPONENT_H

#include "Engine.h"

namespace doriax{

    struct DORIAX_API KeyframeTracksComponent{
        std::vector<float> times;
        int index = 0;
        float interpolation = 0;
    };

}

#endif //KEYFRAMETRACKS_COMPONENT_H