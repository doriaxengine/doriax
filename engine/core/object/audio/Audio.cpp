//
// (c) 2026 Eduardo Doria.
//

#include "Audio.h"
#include "subsystem/AudioSystem.h"

#include "Log.h"

#include <stdexcept>


using namespace doriax;

Audio::Audio(Scene* scene): EntityHandle(scene){
    addComponent<AudioComponent>();
}

Audio::Audio(Scene* scene, bool is3D): EntityHandle(scene){
    addComponent<AudioComponent>();
    if (is3D){
        addComponent<Transform>();
    }
}

Audio::Audio(Scene* scene, Entity entity): EntityHandle(scene, entity){
}


Audio::~Audio(){

}

Object Audio::getObject() const{
    if (!isSound3D()){
        Log::error("Cannot get object from 2D audio: enable 3D sound first");
        throw std::runtime_error("Cannot get object from 2D audio");
    }

    return Object(scene, entity);
}

int Audio::loadAudio(const std::string& filename){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.filename = filename;

    if (Engine::isViewLoaded())
        return scene->getSystem<AudioSystem>()->loadAudio(audio, entity);
    else
        return false;
}

void Audio::destroyAudio(){
    AudioComponent& audio = getComponent<AudioComponent>();

    scene->getSystem<AudioSystem>()->destroyAudio(audio);
}

void Audio::play(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.startTrigger = true;
    audio.pauseTrigger = false;
    audio.stopTrigger = false;
}

void Audio::pause(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.startTrigger = false;
    audio.pauseTrigger = true;
    audio.stopTrigger = false;
}

void Audio::stop(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.startTrigger = false;
    audio.pauseTrigger = false;
    audio.stopTrigger = true;
}

void Audio::seek(double time){
    AudioComponent& audio = getComponent<AudioComponent>();

    scene->getSystem<AudioSystem>()->seekAudio(audio, time);
}

double Audio::getLength(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.length;
}

double Audio::getPlayingTime(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.playingTime;
}

bool Audio::isPlaying(){
    AudioComponent& audio = getComponent<AudioComponent>();

    if ((audio.length > audio.playingTime) && (audio.playingTime != 0) && (audio.state == AudioState::Playing)) {
        return true;
    }
    return false;
}

bool Audio::isPaused(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return (audio.state == AudioState::Paused);
}

bool Audio::isStopped(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return (audio.state == AudioState::Stopped);
}

void Audio::setSound3D(bool sound3D){
    bool hasTransform = scene->findComponent<Transform>(entity) != nullptr;
    if (sound3D && !hasTransform){
        addComponent<Transform>();
    }else if (!sound3D && hasTransform){
        removeComponent<Transform>();
    }

    AudioComponent& audio = getComponent<AudioComponent>();
    audio.needUpdate = true;
}

bool Audio::isSound3D() const{
    return scene->findComponent<Transform>(entity) != nullptr;
}

void Audio::setClockedSound(bool enableClocked){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.enableClocked = enableClocked;
}

bool Audio::isClockedSound() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.enableClocked;
}

void Audio::setVolume(double volume){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.volume != volume){
        audio.volume = volume;
        audio.needUpdate = true;
    }
}

double Audio::getVolume() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.volume;
}

void Audio::setSpeed(float speed){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.speed != speed){
        audio.speed = speed;
        audio.needUpdate = true;
    }
}

float Audio::getSpeed() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.speed;
}

void Audio::setPan(float pan){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.pan != pan){
        audio.pan = pan;
        audio.needUpdate = true;
    }
}

float Audio::getPan() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.pan;
}

void Audio::setLopping(bool lopping){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.looping != lopping){
        audio.looping = lopping;
        audio.needUpdate = true;
    }
}

bool Audio::isLopping() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.looping;
}

void Audio::setLoopingPoint(double loopingPoint){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.loopingPoint != loopingPoint){
        audio.loopingPoint = loopingPoint;
        audio.needUpdate = true;
    }
}

double Audio::getLoopingPoint() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.loopingPoint;
}

void Audio::setProtectVoice(bool protectVoice){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.protectVoice != protectVoice){
        audio.protectVoice = protectVoice;
        audio.needUpdate = true;
    }
}

bool Audio::isProtectVoice() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.protectVoice;
}

void Audio::setInaudibleBehavior(bool mustTick, bool kill){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.inaudibleBehaviorMustTick != mustTick || audio.inaudibleBehaviorKill != kill){
        audio.inaudibleBehaviorMustTick = mustTick;
        audio.inaudibleBehaviorKill = kill;
        audio.needUpdate = true;
    }
}

void Audio::setMinMaxDistance(float minDistance, float maxDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.minDistance != minDistance || audio.maxDistance != maxDistance){
        audio.minDistance = minDistance;
        audio.maxDistance = maxDistance;
        audio.needUpdate = true;
    }
}

void Audio::setMinDistance(float minDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.minDistance != minDistance){
        audio.minDistance = minDistance;
        audio.needUpdate = true;
    }
}

float Audio::getMinDistance() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.minDistance;
}

void Audio::setMaxDistance(float maxDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.maxDistance != maxDistance){
        audio.maxDistance = maxDistance;
        audio.needUpdate = true;
    }
}

float Audio::getMaxDistance() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.maxDistance;
}

void Audio::setAttenuationModel(AudioAttenuation attenuationModel){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.attenuationModel != attenuationModel){
        audio.attenuationModel = attenuationModel;
        audio.needUpdate = true;
    }
}

AudioAttenuation Audio::getAttenuationModel() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.attenuationModel;
}

void Audio::setAttenuationRolloffFactor(float attenuationRolloffFactor){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.attenuationRolloffFactor != attenuationRolloffFactor){
        audio.attenuationRolloffFactor = attenuationRolloffFactor;
        audio.needUpdate = true;
    }
}

float Audio::getAttenuationRolloffFactor() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.attenuationRolloffFactor;
}

void Audio::setDopplerFactor(float dopplerFactor){
    AudioComponent& audio = getComponent<AudioComponent>();

    if (audio.dopplerFactor != dopplerFactor){
        audio.dopplerFactor = dopplerFactor;
        audio.needUpdate = true;
    }
}

float Audio::getDopplerFactor() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.dopplerFactor;
}