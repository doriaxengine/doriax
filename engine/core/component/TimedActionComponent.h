//
// (c) 2026 Eduardo Doria.
//

#ifndef TIMEDACTION_COMPONENT_H
#define TIMEDACTION_COMPONENT_H

#include "action/Ease.h"

namespace doriax{

    struct DORIAX_API TimedActionComponent{
        float time = 0;
        float value = 0;
        
        float duration = 0;
        bool loop = false;

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

}

#endif //TIMEDACTION_COMPONENT_H