//
// (c) 2026 Eduardo Doria.
//

#ifndef TIMEDACTION_H
#define TIMEDACTION_H

#include "Action.h"
#include "Ease.h"

namespace doriax{
    class DORIAX_API TimedAction: public Action{

    protected:
        void setAction(float duration, bool loop);

    public:
        TimedAction(Scene* scene);

        void setFunctionType(EaseType functionType);

        float getValue() const;
        float getTime() const;

        void setDuration(float duration);
        float getDuration() const;

        void setLoop(bool loop);
        bool isLoop() const;
    };
}

#endif //TIMEDACTION_H