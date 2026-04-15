//
// (c) 2026 Eduardo Doria.
//

#include "TimedAction.h"

using namespace doriax;

TimedAction::TimedAction(Scene* scene): Action(scene){
    addComponent<TimedActionComponent>();
}

void TimedAction::setAction(float duration, bool loop){
    ActionComponent& action = getComponent<ActionComponent>();
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    action.timecount = 0;
    timedAction.duration = duration;
    timedAction.loop = loop;
    timedAction.time = 0;
    timedAction.value = 0;
}

void TimedAction::setFunctionType(EaseType functionType){
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    timedAction.function = Ease::getFunction(functionType);
}

float TimedAction::getValue() const{
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    return timedAction.value;
}

float TimedAction::getTime() const{
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    return timedAction.time;
}

void TimedAction::setDuration(float duration){
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    timedAction.duration = duration;
}

float TimedAction::getDuration() const{
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    return timedAction.duration;
}

void TimedAction::setLoop(bool loop){
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    timedAction.loop = loop;
}

bool TimedAction::isLoop() const{
    TimedActionComponent& timedAction = getComponent<TimedActionComponent>();

    return timedAction.loop;
}