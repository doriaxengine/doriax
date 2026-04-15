//
// (c) 2026 Eduardo Doria.
//

#ifndef SCALEACTION_H
#define SCALEACTION_H

#include "TimedAction.h"

namespace doriax{
    class DORIAX_API ScaleAction: public TimedAction{

    public:
        ScaleAction(Scene* scene);

        void setAction(Vector3 startScale, Vector3 endScale, float duration, bool loop=false);
    };
}

#endif //SCALEACTION_H