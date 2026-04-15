//
// (c) 2026 Eduardo Doria.
//

#ifndef ANIMATION_H
#define ANIMATION_H

#include "Action.h"

namespace doriax{
    class DORIAX_API Animation: public Action{

    public:
        Animation(Scene* scene);
        Animation(Scene* scene, Entity entity);
        virtual ~Animation();

        Animation(const Animation& rhs);
        Animation& operator=(const Animation& rhs);

        bool isLoop() const;
        void setLoop(bool loop);

        bool isOwnedActions() const;
        void setOwnedActions(bool ownedActions);

        const std::string &getName() const;
        void setName(const std::string &name);

        const float &getDuration() const;
        void setDuration(const float &duration);

        void addActionFrame(float startTime, float duration, Entity action, Entity target);
        void addActionFrame(float startTime, Entity timedaction, Entity target);
        void addActionFrame(float startTime, float duration, Entity action);
        void addActionFrame(float startTime, Entity timedaction);

        size_t getActionFrameSize() const;
        ActionFrame& getActionFrame(unsigned int index);
        void setActionFrameStartTime(unsigned int index, float startTime);
        void setActionFrameDuration(unsigned int index, float duration);
        void setActionFrameEntity(unsigned int index, Entity action);

        void clearActionFrames();
    };
}

#endif //ANIMATION_H