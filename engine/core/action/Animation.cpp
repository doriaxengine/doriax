//
// (c) 2026 Eduardo Doria.
//

#include "Animation.h"

#include "component/AnimationComponent.h"

using namespace doriax;

Animation::Animation(Scene* scene): Action(scene){
    addComponent<AnimationComponent>();
}

Animation::Animation(Scene* scene, Entity entity): Action(scene, entity){

}

Animation::~Animation(){

}

Animation::Animation(const Animation& rhs): Action(rhs){
    
}

Animation& Animation::operator=(const Animation& rhs){
    Action::operator =(rhs);

    return *this;
}

bool Animation::isLoop() const{
    AnimationComponent& animation = getComponent<AnimationComponent>();

    return animation.loop;
}

void Animation::setLoop(bool loop){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    animation.loop = loop;
}

bool Animation::isOwnedActions() const{
    AnimationComponent& animation = getComponent<AnimationComponent>();

    return animation.ownedActions;
}

void Animation::setOwnedActions(bool ownedActions){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    animation.ownedActions = ownedActions;
}

const std::string &Animation::getName() const{
    AnimationComponent& animation = getComponent<AnimationComponent>();

    return animation.name;
}

void Animation::setName(const std::string &name) {
    AnimationComponent& animation = getComponent<AnimationComponent>();

    animation.name = name;
}

const float &Animation::getDuration() const{
    AnimationComponent& animation = getComponent<AnimationComponent>();

    return animation.duration;
}

void Animation::setDuration(const float &duration){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    animation.duration = duration;
}

void Animation::addActionFrame(float startTime, float duration, Entity action, Entity target){
    AnimationComponent& animation = getComponent<AnimationComponent>();
    ActionComponent& actioncomp = getComponent<ActionComponent>();

    ActionFrame actionFrame;

    actionFrame.startTime = startTime;
    actionFrame.duration = duration;
    actionFrame.action = action;

    animation.actions.push_back(actionFrame);

    if (target != NULL_ENTITY){
        actioncomp.target = target;

        ActionComponent& childAction = scene->getComponent<ActionComponent>(action);

        childAction.target = target;
    }
}

size_t Animation::getActionFrameSize() const{
    AnimationComponent& animation = getComponent<AnimationComponent>();

    return animation.actions.size();
}

void Animation::addActionFrame(float startTime, Entity timedaction, Entity target){
    TimedActionComponent& timedactioncomp = scene->getComponent<TimedActionComponent>(timedaction);

    addActionFrame(startTime, startTime + timedactioncomp.duration, timedaction, target);
}

void Animation::addActionFrame(float startTime, float duration, Entity action){
    addActionFrame(startTime, duration, action, getTarget());
}

void Animation::addActionFrame(float startTime, Entity timedaction){
    addActionFrame(startTime, timedaction, getTarget());
}

ActionFrame& Animation::getActionFrame(unsigned int index){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    try{
        return animation.actions.at(index);
    }catch (const std::out_of_range& e){
        Log::error("Retrieving non-existent action: %s", e.what());
        throw;
	}
}

void Animation::setActionFrameStartTime(unsigned int index, float startTime){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    try{
        animation.actions.at(index).startTime = startTime;
    }catch (const std::out_of_range& e){
        Log::error("Retrieving non-existent action: %s", e.what());
        throw;
	}
}

void Animation::setActionFrameDuration(unsigned int index, float duration){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    try{
        animation.actions.at(index).duration = duration;
    }catch (const std::out_of_range& e){
        Log::error("Retrieving non-existent action: %s", e.what());
        throw;
	}
}

void Animation::setActionFrameEntity(unsigned int index, Entity action){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    try{
        animation.actions.at(index).action = action;
    }catch (const std::out_of_range& e){
        Log::error("Retrieving non-existent action: %s", e.what());
        throw;
	}
}

void Animation::clearActionFrames(){
    AnimationComponent& animation = getComponent<AnimationComponent>();

    if (animation.ownedActions){
        for (int i = 0; i < animation.actions.size(); i++){
            scene->destroyEntity(animation.actions[i].action);
        }
    }
    animation.actions.clear();
}
