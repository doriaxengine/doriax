// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AudioSystem.h"

#include "pool/SoundPool.h"
#include "Scene.h"

#include "soloud.h"
#include "soloud_thread.h"
#include "soloud_wav.h"

using namespace doriax;

bool AudioSystem::inited = false;

float AudioSystem::globalVolume = 1.0;
bool AudioSystem::muted = false;

std::vector<unsigned int> AudioSystem::systemPausedVoices;

AudioSystem::AudioSystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<SoundComponent>());

    cameraLastPosition = Vector3(0, 0, 0);
}

SoLoud::Soloud& AudioSystem::getSoloud(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static SoLoud::Soloud* soloud = new SoLoud::Soloud();
    return *soloud;
};

bool AudioSystem::init(){
#ifdef DORIAX_HEADLESS
    // a server plays no sound
    return false;
#endif
    if (!inited) {
        SoLoud::result result = getSoloud().init();
        if (result != SoLoud::SOLOUD_ERRORS::SO_NO_ERROR) {
            Log::error("Failed to initialize audio output");
            return false;
        }

        getSoloud().setGlobalVolume(muted ? 0.0f : globalVolume);

        //Wait for mixing thread
        //SoLoud::Thread::sleep(10);

        inited = true;
    }

    return true;
}

void AudioSystem::deInit(){
    if (inited){
        getSoloud().deinit();

        inited = false;
    }
}

bool AudioSystem::loadSoundSample(SoundComponent& audio){
    if (audio.filename.empty()) {
        return false;
    }

    if (!audio.sample){
        SoundLoadResult result = SoundPool::loadFromFile(audio.filename, audio.filename);
        if (result.state == ResourceLoadState::Loading) {
            return false;
        }
        if (result.state == ResourceLoadState::Failed) {
            audio.loaded = false;
            audio.loadFailed = true;
            audio.length = 0;
            audio.startTrigger = false;
            audio.state = SoundState::Stopped;
            audio.sample.reset();
            return false;
        }

        audio.sample = result.data;
        if (!audio.sample) {
            return false;
        }
    }

    audio.length = audio.sample->getLength();
    audio.loaded = true;
    audio.loadFailed = false;

    return true;
}

bool AudioSystem::loadSound(SoundComponent& audio){
    if (!loadSoundSample(audio)) {
        return false;
    }

    if (!init()) {
        return false;
    }

    return true;
}

void AudioSystem::preloadSoundAssets(){
    auto audios = scene->getComponentArray<SoundComponent>();
    for (int i = 0; i < audios->size(); i++) {
        SoundComponent& audio = audios->getComponentFromIndex(i);

        if (audio.filename.empty()) {
            if (audio.loaded || audio.handle != 0 || audio.sample) {
                destroySound(audio);
            }
            continue;
        }

        if (!audio.loaded) {
            loadSoundSample(audio);
        }
    }
}

void AudioSystem::getLoadCount(size_t& loaded, size_t& total) const{
    loaded = 0;
    total = 0;

    auto audios = scene->getComponentArray<SoundComponent>();
    for (int i = 0; i < audios->size(); i++) {
        const SoundComponent& audio = audios->getComponentFromIndex(i);
        if (audio.filename.empty()) {
            continue;
        }

        total++;
        // a missing file must not hold the load
        if (audio.loaded || audio.loadFailed) {
            loaded++;
        }
    }
}

void AudioSystem::destroySound(SoundComponent& audio){
    if (inited && audio.handle != 0) {
        getSoloud().stop(audio.handle);
    }

    audio.handle = 0;
    audio.playingTime = 0;
    audio.startTrigger = false;
    audio.pauseTrigger = false;
    audio.stopTrigger = false;
    audio.loaded = false;
    audio.loadFailed = false;
    audio.length = 0;
    audio.sample.reset();
    if (!audio.filename.empty()) {
        SoundPool::remove(audio.filename);
    }
}

bool AudioSystem::seekSound(SoundComponent& audio, double time){
    SoLoud::result res = getSoloud().seek(audio.handle, time);

    if (res != SoLoud::SOLOUD_ERRORS::SO_NO_ERROR)
        return false;

    return true;
}

void AudioSystem::setPaused(bool paused) {
    this->paused = paused;
    if (inited) {
        auto audios = scene->getComponentArray<SoundComponent>();
        for (int i = 0; i < audios->size(); i++) {
            SoundComponent& audio = audios->getComponentFromIndex(i);
            if (audio.handle != 0) {
                getSoloud().setPause(audio.handle, paused);
            }
        }
    }
}

void AudioSystem::stopSceneSounds() {
    auto audios = scene->getComponentArray<SoundComponent>();
    for (int i = 0; i < audios->size(); i++) {
        SoundComponent& audio = audios->getComponentFromIndex(i);

        if (inited && audio.handle != 0) {
            getSoloud().stop(audio.handle);
        }

        audio.handle = 0;
        audio.playingTime = 0;
        audio.startTrigger = false;
        audio.pauseTrigger = false;
        audio.stopTrigger = false;
    }
}

void AudioSystem::stopAll(){
    if (inited){
        getSoloud().stopAll();
    }
}

void AudioSystem::pauseAll(){
    if (inited) {
        getSoloud().setPauseAll(true);
    }
}

void AudioSystem::resumeAll(){
    if (inited) {
        getSoloud().setPauseAll(false);
    }
}

void AudioSystem::systemPause(){
    if (!inited) {
        return;
    }

    SoLoud::Soloud& soloud = getSoloud();
    soloud.lockAudioMutex_internal();
    for (unsigned int i = 0; i < soloud.mHighestVoice; i++) {
        if (soloud.mVoice[i] && !(soloud.mVoice[i]->mFlags & SoLoud::AudioSourceInstance::PAUSED)) {
            systemPausedVoices.push_back(soloud.getHandleFromVoice_internal(i));
            soloud.setVoicePause_internal(i, true);
        }
    }
    soloud.unlockAudioMutex_internal();
}

void AudioSystem::systemResume(){
    if (inited) {
        for (unsigned int handle : systemPausedVoices) {
            getSoloud().setPause(handle, false);
        }
    }
    systemPausedVoices.clear();
}

void AudioSystem::checkActive(){
    if (inited) {
        if (getSoloud().getVoiceCount() == 0){
            deInit();
        }
    }
}

// using global volume in a static var to save because init/deinit reset volume value
void AudioSystem::setGlobalVolume(float volume){
    globalVolume = volume;
    getSoloud().setGlobalVolume(muted ? 0.0f : globalVolume);
}

float AudioSystem::getGlobalVolume(){
    //return getSoloud().getGlobalVolume();
    return globalVolume;
}

void AudioSystem::setMuted(bool muted){
    AudioSystem::muted = muted;
    getSoloud().setGlobalVolume(muted ? 0.0f : globalVolume);
}

void AudioSystem::load(){
    // a file that failed before is tried again by each scene that loads it
    auto audios = scene->getComponentArray<SoundComponent>();
    for (int i = 0; i < audios->size(); i++) {
        const SoundComponent& audio = audios->getComponentFromIndex(i);
        if (!audio.filename.empty() && !audio.loaded) {
            SoundPool::retry(audio.filename);
        }
    }

    preloadSoundAssets();
}

void AudioSystem::destroy(){
}

void AudioSystem::update(double dt){
    preloadSoundAssets();

    if (paused) {
        return;
    }

    auto audios = scene->getComponentArray<SoundComponent>();

    // open the output early, a device started by the first play cuts that sound
    if (!outputRequested && audios->size() > 0) {
        outputRequested = true;
        init();
    }

    // 3D sources are placed against the main camera, so without one they play in 2D
    bool hasListener = scene->findComponent<CameraComponent>(scene->getCamera()) &&
                       scene->findComponent<Transform>(scene->getCamera());

    for (int i = 0; i < audios->size(); i++){
		SoundComponent& audio = audios->getComponentFromIndex(i);

        Entity entity = audios->getEntity(i);
        Signature signature = scene->getSignature(entity);
        bool use3DAudio = hasListener && signature.test(scene->getComponentId<Transform>());

        Vector3 worldPosition = Vector3(0, 0, 0);
        if (use3DAudio){
            Transform& transform = scene->getComponent<Transform>(entity);

            worldPosition = transform.worldPosition;
        }

        if (audio.filename.empty()) {
            if (audio.loaded || audio.handle != 0 || audio.sample) {
                destroySound(audio);
            }
            continue;
        }

        bool pendingStart = audio.startTrigger || (audio.state == SoundState::Playing && audio.handle == 0);

        if (audio.state == SoundState::Playing || pendingStart){
            if (!audio.loaded){
                loadSound(audio);
            }
        }

        if (audio.loaded){
            if (audio.pauseTrigger){
                audio.pauseTrigger = false;

                getSoloud().setPause(audio.handle, true);
                audio.state = SoundState::Paused;
            }
            if (audio.stopTrigger){
                audio.stopTrigger = false;

                getSoloud().stop(audio.handle);
                audio.handle = 0;
                audio.playingTime = 0;
                audio.state = SoundState::Stopped;
            }
            if (pendingStart){
                audio.startTrigger = false;

                if (audio.state != SoundState::Paused || audio.handle == 0) {
                    if (!init()) {
                        continue;
                    }

                    if (use3DAudio){
                        if (audio.enableClocked){
                            audio.handle = getSoloud().play3dClocked(Engine::getDeltatime(), *audio.sample, worldPosition.x, worldPosition.y, worldPosition.z);
                        }else{
                            audio.handle = getSoloud().play3d(*audio.sample, worldPosition.x, worldPosition.y, worldPosition.z);
                        }
                    }else{
                        if (audio.enableClocked){
                            audio.handle = getSoloud().playClocked(Engine::getDeltatime(), *audio.sample);
                        }else{
                            audio.handle = getSoloud().play(*audio.sample);
                        }
                    }
                    // a new voice starts with the sample defaults
                    audio.needUpdate = true;
                }else{
                    getSoloud().setPause(audio.handle, false);
                }
                if (use3DAudio){
                    Transform& cameraTransform =  scene->getComponent<Transform>(scene->getCamera());

                    audio.lastPosition = worldPosition;
                    cameraLastPosition = cameraTransform.worldPosition;
                }

                audio.state = SoundState::Playing;
            }

            if (audio.state == SoundState::Playing){
                audio.playingTime = getSoloud().getStreamTime(audio.handle);

                if (audio.needUpdate){
                    getSoloud().setVolume(audio.handle, audio.volume);
                    getSoloud().setRelativePlaySpeed(audio.handle, audio.speed);
                    getSoloud().setPan(audio.handle, audio.pan);
                    getSoloud().setLooping(audio.handle, audio.looping);
                    getSoloud().setLoopPoint(audio.handle, audio.loopingPoint);
                    getSoloud().setProtectVoice(audio.handle, audio.protectVoice);
                    getSoloud().setInaudibleBehavior(audio.handle, audio.inaudibleBehaviorMustTick, audio.inaudibleBehaviorKill);

                    if (use3DAudio){
                        CameraComponent& camera =  scene->getComponent<CameraComponent>(scene->getCamera());
                        Transform& cameraTransform =  scene->getComponent<Transform>(scene->getCamera());

                        Vector3 velocity = audio.lastPosition - worldPosition;

                        Vector3 camWorldPos = cameraTransform.worldPosition;
                        Vector3 camWorldView = camera.worldTarget - camWorldPos;
                        Vector3 camWorldUp = camera.worldUp;
                        Vector3 camVelocity = cameraLastPosition - camWorldPos;

                        unsigned int attModel = SoLoud::AudioSource::NO_ATTENUATION;
                        if (audio.attenuationModel == SoundAttenuation::INVERSE_DISTANCE)
                            attModel = SoLoud::AudioSource::INVERSE_DISTANCE;
                        if (audio.attenuationModel == SoundAttenuation::LINEAR_DISTANCE)
                            attModel = SoLoud::AudioSource::LINEAR_DISTANCE;
                        if (audio.attenuationModel == SoundAttenuation::EXPONENTIAL_DISTANCE)
                            attModel = SoLoud::AudioSource::EXPONENTIAL_DISTANCE;

                        getSoloud().set3dSourceMinMaxDistance(audio.handle, audio.minDistance, audio.maxDistance);
                        getSoloud().set3dSourceAttenuation(audio.handle, attModel, audio.attenuationRolloffFactor);
                        getSoloud().set3dSourceDopplerFactor(audio.handle, audio.dopplerFactor);
                        getSoloud().set3dSourceParameters(
                            audio.handle, 
                            worldPosition.x, worldPosition.y, worldPosition.z, 
                            velocity.x, velocity.y, velocity.z);
                        getSoloud().set3dListenerParameters(
                            camWorldPos.x, camWorldPos.y, camWorldPos.z, 
                            camWorldView.x, camWorldView.y, camWorldView.z, 
                            camWorldUp.x, camWorldUp.y, camWorldUp.z,
                            camVelocity.x, camVelocity.y, camVelocity.z);

                        getSoloud().update3dAudio();

                        audio.lastPosition = worldPosition;
                        cameraLastPosition = camWorldPos;
                    }

                    audio.needUpdate = false;
                }
            }
        }
    }
}

void AudioSystem::draw(){

}

void AudioSystem::onComponentAdded(Entity entity, ComponentId componentId) {

}

void AudioSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<SoundComponent>()) {
		SoundComponent& audio = scene->getComponent<SoundComponent>(entity);
		destroySound(audio);
	}
}