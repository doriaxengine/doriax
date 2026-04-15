//
// (c) 2026 Eduardo Doria.
//

#include "Action.h"

using namespace doriax;

Action::Action(Scene* scene): EntityHandle(scene){
    addComponent<ActionComponent>();
}

Action::Action(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

Action::~Action(){
}

void Action::start(){
    ActionComponent& action = getComponent<ActionComponent>();

    action.startTrigger = true;
}

void Action::pause(){
    ActionComponent& action = getComponent<ActionComponent>();

    action.pauseTrigger = true;
}

void Action::stop(){
    ActionComponent& action = getComponent<ActionComponent>();

    action.stopTrigger = true;
}

void Action::setOwnedTarget(bool ownedTarget){
    ActionComponent& action = getComponent<ActionComponent>();

    action.ownedTarget = ownedTarget;
}

bool Action::getOwnedTarget() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return action.ownedTarget;
}

void Action::setTarget(Object* target){
    setTarget(target->getEntity());
}

void Action::setTarget(Entity target){
    ActionComponent& action = getComponent<ActionComponent>();

    // restart
    if (action.state == ActionState::Running){
        action.stopTrigger = true;
        action.startTrigger = true;
    }

    action.target = target;
}

Entity Action::getTarget() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return action.target;
}

void Action::setSpeed(float speed){
    ActionComponent& action = getComponent<ActionComponent>();

    action.speed = speed;
}

float Action::getSpeed() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return action.speed;
}

bool Action::isRunning() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return (action.state == ActionState::Running);
}

bool Action::isStopped() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return (action.state == ActionState::Stopped);
}

bool Action::isPaused() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return (action.state == ActionState::Paused);
}

float Action::getTimeCount() const{
    ActionComponent& action = getComponent<ActionComponent>();

    return action.timecount;
}