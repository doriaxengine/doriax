//
// (c) 2026 Eduardo Doria.
//

#ifndef POSITIONACTION_H
#define POSITIONACTION_H

#include "TimedAction.h"

namespace doriax{
    class DORIAX_API PositionAction: public TimedAction{

    public:
        PositionAction(Scene* scene);

        void setAction(Vector3 startPosition, Vector3 endPosition, float duration, bool loop=false);
    };
}

#endif //POSITIONACTION_H