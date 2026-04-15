//
// (c) 2026 Eduardo Doria.
//

#ifndef ROTATIONACTION_H
#define ROTATIONACTION_H

#include "TimedAction.h"

namespace doriax{
    class DORIAX_API RotationAction: public TimedAction{

    public:
        RotationAction(Scene* scene);

        void setAction(Quaternion startRotation, Quaternion endRotation, float duration, bool loop=false);
    };
}

#endif //ROTATIONACTION_H