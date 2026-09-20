// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ActionSystem.h"

#include "Scene.h"
#include "util/Color.h"
#include "util/Angle.h"
#include "math/Interpolation.h"
#include "subsystem/MeshSystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <unordered_set>

using namespace doriax;

float ActionSystem::getParticleInverseScale(float value){
    if (value == 0.0f)
        return 1.0f;

    return 1.0f / value;
}

Vector3 ActionSystem::getParticleDisplayScale(Vector3 scale, Transform* targetTransform){
    if (!targetTransform)
        return scale;

    return Vector3(
        scale.x * getParticleInverseScale(targetTransform->worldScale.x),
        scale.y * getParticleInverseScale(targetTransform->worldScale.y),
        scale.z * getParticleInverseScale(targetTransform->worldScale.z));
}

bool ActionSystem::isParticleWorldSpace(ParticlesComponent& particles, Transform* targetTransform){
    return !particles.localSpace && targetTransform;
}

void ActionSystem::updateParticleTargetTransform(Transform& transform){
    Matrix4 translateMatrix = Matrix4::translateMatrix(transform.position);
    Matrix4 rotationMatrix = transform.rotation.getRotationMatrix();
    Matrix4 scaleMatrix = Matrix4::scaleMatrix(transform.scale);

    transform.localMatrix = translateMatrix * rotationMatrix * scaleMatrix;

    if (transform.parent != NULL_ENTITY){
        Transform* transformParent = scene->findComponent<Transform>(transform.parent);
        if (transformParent){
            updateParticleTargetTransform(*transformParent);

            transform.modelMatrix = transformParent->modelMatrix * transform.localMatrix;
            transform.worldPosition = transformParent->modelMatrix * transform.position;
            transform.worldScale = transformParent->worldScale * transform.scale;
            transform.worldRotation = transformParent->worldRotation * transform.rotation;
            return;
        }
    }

    transform.modelMatrix = transform.localMatrix;
    transform.worldPosition = transform.position;
    transform.worldScale = transform.scale;
    transform.worldRotation = transform.rotation;
}

Vector3 ActionSystem::getParticleSimulationPosition(ParticlesComponent& particles, Transform* targetTransform, Vector3 position){
    if (isParticleWorldSpace(particles, targetTransform))
        return targetTransform->modelMatrix * position;

    return position;
}

Vector3 ActionSystem::getParticleSimulationDirection(ParticlesComponent& particles, Transform* targetTransform, Vector3 direction){
    if (isParticleWorldSpace(particles, targetTransform))
        return targetTransform->worldRotation * direction;

    return direction;
}

Quaternion ActionSystem::getParticleSimulationRotation(ParticlesComponent& particles, Transform* targetTransform, Quaternion rotation){
    if (isParticleWorldSpace(particles, targetTransform))
        return targetTransform->worldRotation * rotation;

    return rotation;
}

Vector3 ActionSystem::getParticleDisplayPosition(ParticlesComponent& particles, Transform* targetTransform, Vector3 position){
    if (isParticleWorldSpace(particles, targetTransform))
        return particles.cachedTargetModelInv * position;

    return position;
}

Quaternion ActionSystem::getParticleDisplayRotation(ParticlesComponent& particles, Transform* targetTransform, Quaternion rotation){
    if (isParticleWorldSpace(particles, targetTransform))
        return particles.cachedTargetWorldRotInv * rotation;

    return rotation;
}

Vector3 ActionSystem::getParticleDisplayScale(ParticlesComponent& particles, Transform* targetTransform, Vector3 scale){
    if (isParticleWorldSpace(particles, targetTransform))
        return Vector3(
            scale.x * particles.cachedTargetInvScale.x,
            scale.y * particles.cachedTargetInvScale.y,
            scale.z * particles.cachedTargetInvScale.z);

    return scale;
}

void ActionSystem::updateParticleTargetCache(ParticlesComponent& particles, Transform* targetTransform){
    if (!isParticleWorldSpace(particles, targetTransform))
        return;

    updateParticleTargetTransform(*targetTransform);

    particles.cachedTargetModelInv = targetTransform->modelMatrix.inverse();
    particles.cachedTargetWorldRotInv = targetTransform->worldRotation.inverse();
    particles.cachedTargetInvScale = Vector3(
        getParticleInverseScale(targetTransform->worldScale.x),
        getParticleInverseScale(targetTransform->worldScale.y),
        getParticleInverseScale(targetTransform->worldScale.z));
}

Vector3 ActionSystem::samplePositionInitializer(ParticlePositionInitializer& init){
    auto frand = [](){ return (float)rand() / (float)RAND_MAX; };
    const float twoPi = 6.28318530717958647692f;
    const float degToRad = 0.01745329251994329577f;
    float outerRadius = std::max(0.0f, init.radius);
    float innerRadius = std::min(std::max(0.0f, init.innerRadius), outerRadius);

    auto sampleVolumeRadius = [&](){
        float inner3 = innerRadius * innerRadius * innerRadius;
        float outer3 = outerRadius * outerRadius * outerRadius;
        return std::pow(inner3 + frand() * (outer3 - inner3), 1.0f / 3.0f);
    };

    auto sampleDiscRadius = [&](){
        float inner2 = innerRadius * innerRadius;
        float outer2 = outerRadius * outerRadius;
        return std::sqrt(inner2 + frand() * (outer2 - inner2));
    };

    switch (init.shape){
        case ParticleEmitterShape::Sphere:{
            float u = frand() * 2.0f - 1.0f;
            float theta = frand() * twoPi;
            float r = sampleVolumeRadius();
            float s = std::sqrt(std::max(0.0f, 1.0f - u*u));
            return Vector3(r * s * std::cos(theta), r * u, r * s * std::sin(theta));
        }
        case ParticleEmitterShape::Hemisphere:{
            float u = frand();
            float theta = frand() * twoPi;
            float r = sampleVolumeRadius();
            float s = std::sqrt(std::max(0.0f, 1.0f - u*u));
            return Vector3(r * s * std::cos(theta), r * u, r * s * std::sin(theta));
        }
        case ParticleEmitterShape::Circle:{
            float theta = frand() * twoPi;
            float r = sampleDiscRadius();
            return Vector3(r * std::cos(theta), 0.0f, r * std::sin(theta));
        }
        case ParticleEmitterShape::Cone:{
            float coneHeight = std::max(0.0f, init.coneHeight);
            float coneAngle = std::min(std::max(0.0f, init.coneAngle), 89.0f);
            float h = coneHeight * std::pow(frand(), 1.0f / 3.0f);
            float angleRad = coneAngle * degToRad;
            float maxR = h * std::tan(angleRad);
            float theta = frand() * twoPi;
            float r = maxR * std::sqrt(frand());
            return Vector3(r * std::cos(theta), h, r * std::sin(theta));
        }
        case ParticleEmitterShape::Box:
        default:
            return getVector3InitializerValue(init.minPosition, init.maxPosition, false);
    }
}

int ActionSystem::sampleBurstCount(ParticleBurst& burst){
    int minCount = std::max(0, burst.minCount);
    int maxCount = std::max(minCount, burst.maxCount);
    if (maxCount <= minCount)
        return minCount;
    int span = maxCount - minCount + 1;
    return minCount + (int)((float)span * (float)rand() / (float)(RAND_MAX + 1.0));
}

void ActionSystem::sortParticleBursts(ParticlesComponent& particles){
    std::sort(particles.bursts.begin(), particles.bursts.end(), [](const ParticleBurst& a, const ParticleBurst& b){
        return a.time < b.time;
    });
}

void ActionSystem::sortParticleColorGradient(ParticlesComponent& particles){
    particles.colorGradient.normalize();
}

Vector3 ActionSystem::sampleParticleColorGradient(const ParticleColorGradient& gradient, float time, float life){
    const std::vector<ParticleColorGradientStop>& stops = gradient.stops;
    if (stops.empty()){
        return Vector3(1,1,1);
    }

    float normalizedTime = (life > 0.0f) ? (time / life) : 0.0f;
    if (normalizedTime < 0.0f) normalizedTime = 0.0f;
    if (normalizedTime > 1.0f) normalizedTime = 1.0f;

    const ParticleColorGradientStop* previousStop = nullptr;
    const ParticleColorGradientStop* nextStop = nullptr;
    float previousTime = 0.0f;
    float nextTime = 0.0f;

    for (const ParticleColorGradientStop& stop : stops){
        float stopTime = stop.time;
        if (stopTime < 0.0f) stopTime = 0.0f;
        if (stopTime > 1.0f) stopTime = 1.0f;

        if (stopTime <= normalizedTime && (!previousStop || stopTime >= previousTime)){
            previousStop = &stop;
            previousTime = stopTime;
        }
        if (stopTime >= normalizedTime && (!nextStop || stopTime <= nextTime)){
            nextStop = &stop;
            nextTime = stopTime;
        }
    }

    Vector3 color;
    if (!previousStop){
        color = nextStop ? nextStop->color : stops.front().color;
    } else if (!nextStop){
        color = previousStop->color;
    } else {
        float span = nextTime - previousTime;
        float value = (span > 0.0f) ? (normalizedTime - previousTime) / span : 0.0f;
        color = previousStop->color + (nextStop->color - previousStop->color) * value;
    }
    if (gradient.useSRGB) color = Color::sRGBToLinear(color);
    return color;
}

float ActionSystem::getParticleCycleDuration(ParticlesComponent& particles){
    float cycleDuration = 0.0f;
    if (particles.rate > 0){
        cycleDuration = std::max(cycleDuration, (float)particles.maxParticles / (float)particles.rate);
    }
    for (ParticleBurst& burst : particles.bursts){
        cycleDuration = std::max(cycleDuration, burst.time);
    }
    if (cycleDuration <= 0.0f && !particles.bursts.empty()){
        cycleDuration = std::max(0.0001f, particles.lifeInitializer.maxLife);
    }
    return cycleDuration;
}

void ActionSystem::syncParticleInstance(size_t idx, ParticlesComponent& particles, InstancedMeshComponent& instmesh, Transform* targetTransform){
    ParticleData& particle = particles.particles[idx];
    InstanceData& instance = instmesh.instances[idx];

    instance.position = getParticleDisplayPosition(particles, targetTransform, particle.position);
    instance.rotation = getParticleDisplayRotation(particles, targetTransform, particle.rotation);
    instance.scale = getParticleDisplayScale(particles, targetTransform, particle.scale);
}

void ActionSystem::syncParticlePoint(size_t idx, ParticlesComponent& particles, PointsComponent& points, Transform* targetTransform){
    points.points[idx].position = getParticleDisplayPosition(particles, targetTransform, particles.particles[idx].position);
}


ActionSystem::ActionSystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<ActionComponent>());
}

void ActionSystem::actionStart(Entity entity){
    ActionComponent& action = scene->getComponent<ActionComponent>(entity);
    Signature signature = scene->getSignature(entity);

    actionComponentStart(action);

    // Reset blend weight to full when an animation starts without a pending fade
    // (a fade-in sets weight/fadeSpeed before triggering start, so preserve those).
    if (signature.test(scene->getComponentId<AnimationComponent>())){
        AnimationComponent& animcomp = scene->getComponent<AnimationComponent>(entity);
        if (animcomp.fadeSpeed <= 0.0f){
            animcomp.weight = 1.0f;
            animcomp.fadeTarget = 1.0f;
            animcomp.stopOnFadeOut = false;
        }
    }

    if (action.target != NULL_ENTITY){
        Signature targetSignature = scene->getSignature(action.target);

        if (signature.test(scene->getComponentId<SpriteAnimationComponent>())){
            SpriteAnimationComponent& spriteanim = scene->getComponent<SpriteAnimationComponent>(entity);
            if (targetSignature.test(scene->getComponentId<SpriteComponent>()) && targetSignature.test(scene->getComponentId<MeshComponent>())){
                SpriteComponent& sprite = scene->getComponent<SpriteComponent>(action.target);
                MeshComponent& mesh = scene->getComponent<MeshComponent>(action.target);

                spriteActionStart(mesh, sprite, spriteanim);

            }
        }

        if (signature.test(scene->getComponentId<ParticlesComponent>())){
            ParticlesComponent& particles = scene->getComponent<ParticlesComponent>(entity);
            if (targetSignature.test(scene->getComponentId<MeshComponent>())) {
                MeshComponent& mesh = scene->getComponent<MeshComponent>(action.target);

                scene->getSystem<MeshSystem>()->createInstancedMesh(action.target);
                targetSignature = scene->getSignature(action.target);

                InstancedMeshComponent& instmesh = scene->getComponent<InstancedMeshComponent>(action.target);

                particleActionStart(particles, instmesh, mesh);
            }
            if (targetSignature.test(scene->getComponentId<PointsComponent>()) ){
                PointsComponent& points = scene->getComponent<PointsComponent>(action.target);

                particleActionStart(particles, points);

            }
        }
    }
}

void ActionSystem::actionComponentStart(ActionComponent& action){
    action.state = ActionState::Running;
    action.startTrigger = false;

    action.onStart.call();
}

void ActionSystem::actionStop(Entity entity){
    if (entity == NULL_ENTITY || !scene->isEntityCreated(entity)){
        return;
    }

    ActionComponent* action = scene->findComponent<ActionComponent>(entity);
    if (!action){
        return;
    }

    // Snapshot everything needed after onStop before invoking user code. A callback
    // may clear animation frames, remove components, or destroy this entity.
    Entity target = action->target;
    std::vector<Entity> animationChildren;
    if (AnimationComponent* animcomp = scene->findComponent<AnimationComponent>(entity)){
        animationChildren = animationChildEntities(*animcomp);

        // Reset runtime fade state before callbacks. A callback may deliberately
        // start/fade the animation again, in which case its newer state is kept.
        animcomp->weight = 0.0f;
        animcomp->fadeTarget = 0.0f;
        animcomp->fadeSpeed = 0.0f;
        animcomp->stopOnFadeOut = false;
    }

    actionComponentStop(*action);
    animationStopChildren(animationChildren);

    if (!scene->isEntityCreated(entity)){
        return;
    }

    if (TimedActionComponent* timedaction = scene->findComponent<TimedActionComponent>(entity)){
        timedActionStop(*timedaction);
    }

    if (target != NULL_ENTITY && scene->isEntityCreated(target)){
        SpriteAnimationComponent* spriteanim = scene->findComponent<SpriteAnimationComponent>(entity);
        SpriteComponent* sprite = scene->findComponent<SpriteComponent>(target);
        MeshComponent* mesh = scene->findComponent<MeshComponent>(target);

        if (spriteanim && sprite && mesh){
            spriteActionStop(*mesh, *sprite, *spriteanim);
        }
    }
}

void ActionSystem::actionComponentStop(ActionComponent& action){
    // Dispatch from a copy so a callback can remove/destroy the component that
    // owns the original subscription list without invalidating call().
    auto onStop = action.onStop;

    action.state = ActionState::Stopped;
    action.stopTrigger = false;
    action.timecount = 0;

    onStop.call();
}

void ActionSystem::actionPause(Entity entity){
    if (entity == NULL_ENTITY || !scene->isEntityCreated(entity)){
        return;
    }

    ActionComponent* action = scene->findComponent<ActionComponent>(entity);
    if (!action){
        return;
    }

    // Snapshot child tracks before onPause callbacks, mirroring actionStop.
    std::vector<Entity> animationChildren;
    if (AnimationComponent* animcomp = scene->findComponent<AnimationComponent>(entity)){
        animationChildren = animationChildEntities(*animcomp);
    }

    actionComponentPause(*action);
    animationPauseChildren(animationChildren);
}

void ActionSystem::actionComponentPause(ActionComponent& action){
    // Dispatch from a copy so a callback can remove/destroy the component that
    // owns the original subscription list without invalidating call().
    auto onPause = action.onPause;

    action.state = ActionState::Paused;
    action.pauseTrigger = false;

    onPause.call();
}

void ActionSystem::actionUpdate(double dt, ActionComponent& action){
    action.timecount += dt * action.speed;

    action.onStep.call();
}

void ActionSystem::animationUpdate(double dt, Entity entity, ActionComponent& action, AnimationComponent& animcomp){
    int totalActionsPassed = 0;
    float actionsEnd = 0; // last frame end, used to wrap a loop

    // Advance the blend weight toward its fade target. Child actions inherit this
    // weight so their sampled poses are combined by flushPoseBlend() into a smooth
    // weighted average, producing crossfades between overlapping animations.
    if (animcomp.fadeSpeed > 0.0f){
        float step = (float)(animcomp.fadeSpeed * dt);
        if (animcomp.weight < animcomp.fadeTarget){
            animcomp.weight = std::min(animcomp.fadeTarget, animcomp.weight + step);
        }else if (animcomp.weight > animcomp.fadeTarget){
            animcomp.weight = std::max(animcomp.fadeTarget, animcomp.weight - step);
        }

        if (animcomp.weight == animcomp.fadeTarget){
            animcomp.fadeSpeed = 0.0f; // fade finished
        }

        if (animcomp.weight <= 0.0f && animcomp.stopOnFadeOut){
            actionStop(entity);
            return;
        }
    }

    for (int i = 0; i < animcomp.actions.size(); i++){

        float timeDiff = action.timecount - animcomp.actions[i].startTime;

        Signature isignature = scene->getSignature(animcomp.actions[i].action);

        if (isignature.test(scene->getComponentId<ActionComponent>())){

            if (timeDiff >= 0) {
                //TODO: Support loop actions
                ActionComponent& iaction = scene->getComponent<ActionComponent>(animcomp.actions[i].action);
                if (iaction.state != ActionState::Running) {
                    actionStart(animcomp.actions[i].action);
                }

                iaction.timecount = timeDiff * iaction.speed;
                iaction.weight = animcomp.weight;

                float frameDuration = getFrameDuration(animcomp.actions[i]) / iaction.speed;
                actionsEnd = std::max(actionsEnd, animcomp.actions[i].startTime + frameDuration);

                if (timeDiff > frameDuration) {
                    totalActionsPassed++;
                }
            }

        }

    }

    bool actionsPassed = totalActionsPassed == animcomp.actions.size();
    bool durationPassed = animcomp.duration >= 0 && action.timecount >= (animcomp.duration / action.speed);

    if (actionsPassed || durationPassed) {
        if (!animcomp.loop) {
            actionStop(entity);
            //onFinish.call(object);
        }else{
            float loopDuration = durationPassed ? (animcomp.duration / action.speed) : actionsEnd;

            // keep the overshoot, or the cycle gets longer at lower frame rates
            if (loopDuration > 0 && action.timecount >= loopDuration){
                action.timecount = std::fmod(action.timecount, loopDuration);
            }else{
                action.timecount = 0;
            }
        }
    }
}

std::vector<Entity> ActionSystem::animationChildEntities(const AnimationComponent& animcomp){
    std::vector<Entity> children;
    children.reserve(animcomp.actions.size());
    for (const ActionFrame& frame : animcomp.actions){
        children.push_back(frame.action);
    }
    return children;
}

void ActionSystem::animationStopChildren(const std::vector<Entity>& children){
    // Child tracks are regular actions and are also visited by the system's
    // all-actions pass. The entity list is a pre-callback snapshot, so callbacks
    // may safely mutate or clear the parent's action frames while children stop.
    for (Entity child : children){
        if (child == NULL_ENTITY || !scene->isEntityCreated(child)){
            continue;
        }

        ActionComponent* childAction = scene->findComponent<ActionComponent>(child);
        if (!childAction){
            continue;
        }

        if (childAction->state == ActionState::Running || childAction->state == ActionState::Paused){
            actionStop(child);
        }
    }
}

void ActionSystem::animationPauseChildren(const std::vector<Entity>& children){
    // Child tracks advance on their own in the all-actions pass, so a paused clip
    // would keep animating unless its children pause with it. animationUpdate
    // restarts them when the parent resumes.
    for (Entity child : children){
        if (child == NULL_ENTITY || !scene->isEntityCreated(child)){
            continue;
        }

        ActionComponent* childAction = scene->findComponent<ActionComponent>(child);
        if (!childAction){
            continue;
        }

        if (childAction->state == ActionState::Running){
            actionPause(child);
        }
    }
}

void ActionSystem::setSpriteTextureRect(MeshComponent& mesh, SpriteComponent& sprite, SpriteAnimationComponent& spriteanim){
    if (mesh.numSubmeshes <= 0 || !spriteanim.frames.validIndex(spriteanim.frameIndex)) {
        return;
    }

    int frameId = spriteanim.frames[spriteanim.frameIndex];
    if (frameId < 0 || (unsigned int)frameId >= sprite.numFramesRect) {
        return;
    }

    MeshSystem::setSpriteFrameRect(mesh, sprite, sprite.framesRect[frameId].rect);
}

void ActionSystem::spriteActionStart(MeshComponent& mesh, SpriteComponent& sprite, SpriteAnimationComponent& spriteanim){
    spriteanim.frameIndex = 0;
    spriteanim.frameTimeIndex = 0;
    spriteanim.spriteFrameCount = 0;

    setSpriteTextureRect(mesh, sprite, spriteanim);
}

void ActionSystem::spriteActionStop(MeshComponent& mesh, SpriteComponent& sprite, SpriteAnimationComponent& spriteanim){
    spriteanim.frameTimeIndex = 0;
    spriteanim.spriteFrameCount = 0;

    setSpriteTextureRect(mesh, sprite, spriteanim);
}

void ActionSystem::spriteActionUpdate(double dt, Entity entity, ActionComponent& action, MeshComponent& mesh, SpriteComponent& sprite, SpriteAnimationComponent& spriteanim){
    spriteanim.spriteFrameCount += dt * 1000;
    while (spriteanim.spriteFrameCount >= spriteanim.framesTime[spriteanim.frameTimeIndex]) {

        spriteanim.spriteFrameCount -= spriteanim.framesTime[spriteanim.frameTimeIndex];

        spriteanim.frameIndex++;
        spriteanim.frameTimeIndex++;

        if (spriteanim.frameIndex == spriteanim.framesSize - 1) {
            if (!spriteanim.loop) {
                actionStop(entity);
            }
        }

        if (spriteanim.frameIndex >= spriteanim.framesSize){
            spriteanim.frameIndex = 0;
            if (spriteanim.loop){
                action.timecount = 0;
            }
        }
        
        if (spriteanim.frameTimeIndex >= spriteanim.framesTimeSize)
            spriteanim.frameTimeIndex = 0;

        setSpriteTextureRect(mesh, sprite, spriteanim);
    }
}

void ActionSystem::timedActionStop(TimedActionComponent& timedaction){
    timedaction.time = 0;
    timedaction.value = 0;
}

void ActionSystem::timedActionUpdate(double dt, Entity entity, ActionComponent& action, TimedActionComponent& timedaction){
    if ((timedaction.time == 1) && !timedaction.loop){
        actionStop(entity);
        //onFinish.call(object);
    } else {
        float duration = timedaction.duration / action.speed;

        if (duration > 0) {

            if (action.timecount >= duration){
                if (!timedaction.loop){
                    action.timecount = duration;
                }else{
                    action.timecount -= duration;
                }
            }

            timedaction.time = action.timecount / duration;
        } else {
            timedaction.time = 1;
        }

        timedaction.value = timedaction.function.call(timedaction.time);
        //Log::debug("step time %f value %f \n", timedaction.time, timedaction.value);
    }
}

void ActionSystem::positionActionUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, PositionActionComponent& posaction, Transform& transform){
    Vector3 position = (posaction.endPosition - posaction.startPosition) * timedaction.value;
    transform.position = posaction.startPosition + position;
    transform.needUpdate = true;
}

void ActionSystem::rotationActionUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, RotationActionComponent& rotaction, Transform& transform){
    transform.rotation = Quaternion::slerp(timedaction.value, rotaction.startRotation, rotaction.endRotation, rotaction.shortestPath);
    transform.needUpdate = true;
}

void ActionSystem::scaleActionUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, ScaleActionComponent& scaleaction, Transform& transform){
    Vector3 scale = (scaleaction.endScale - scaleaction.startScale) * timedaction.value;
    transform.scale = scaleaction.startScale + scale;
    transform.needUpdate = true;
}

void ActionSystem::colorActionMeshUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, ColorActionComponent& coloraction, MeshComponent& mesh){
    Vector3 color = (coloraction.endColor - coloraction.startColor) * timedaction.value;
    if (coloraction.useSRGB){
        mesh.submeshes[0].material.baseColorFactor = Color::sRGBToLinear(coloraction.startColor + color);
    }else{
        mesh.submeshes[0].material.baseColorFactor = coloraction.startColor + color;
    }
}

void ActionSystem::colorActionUIUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, ColorActionComponent& coloraction, UIComponent& ui){
    Vector3 color = (coloraction.endColor - coloraction.startColor) * timedaction.value;
    if (coloraction.useSRGB){
        ui.color = Color::sRGBToLinear(coloraction.startColor + color);
    }else{
        ui.color = coloraction.startColor + color;
    }
}

void ActionSystem::alphaActionMeshUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, AlphaActionComponent& alphaaction, MeshComponent& mesh){
    float alpha = (alphaaction.endAlpha - alphaaction.startAlpha) * timedaction.value;

    mesh.submeshes[0].material.baseColorFactor.w = alphaaction.startAlpha + alpha;
}

void ActionSystem::alphaActionUIUpdate(double dt, ActionComponent& action, TimedActionComponent& timedaction, AlphaActionComponent& alphaaction, UIComponent& ui){
    float alpha = (alphaaction.endAlpha - alphaaction.startAlpha) * timedaction.value;

    ui.color.w = alphaaction.startAlpha + alpha;
}

int ActionSystem::findUnusedParticle(ParticlesComponent& particles){

    for (int i=particles.lastUsedParticle; i<particles.particles.size(); i++){
        if (particles.particles[i].life <= particles.particles[i].time){
            particles.lastUsedParticle = i;
            return i;
        }
    }

    if (particles.loop){
        for (int i=0; i<particles.lastUsedParticle; i++){
            if (particles.particles[i].life <= particles.particles[i].time){
                particles.lastUsedParticle = i;
                return i;
            }
        }
    }

    return -1;
}

float ActionSystem::getFloatInitializerValue(float& min, float& max){
    if (min != max) {
        return min + ((max - min) * (float) rand() / (float) RAND_MAX);
    }
    return max;
}

Vector3 ActionSystem::getVector3InitializerValue(Vector3& min, Vector3& max, bool linearSort){
    if (min != max) {
        if (!linearSort){
            return Vector3( min.x + ((max.x - min.x) * (float) rand() / (float) RAND_MAX),
                            min.y + ((max.y - min.y) * (float) rand() / (float) RAND_MAX),
                            min.z + ((max.z - min.z) * (float) rand() / (float) RAND_MAX));
        }else{
            float factor = (float) rand() / (float) RAND_MAX;

            return Vector3( min.x + ((max.x - min.x) * factor),
                            min.y + ((max.y - min.y) * factor),
                            min.z + ((max.z - min.z) * factor));
        }
    }
    return max;
}

Quaternion ActionSystem::getQuaternionInitializerValue(Quaternion& min, Quaternion& max, bool shortestPath){
    if (min != max) {
        return Quaternion::slerp((float) rand() / (float) RAND_MAX, min, max, shortestPath);
    }
    return max;
}

Rect ActionSystem::getSpriteInitializerValue(std::vector<int>& frames, SpriteComponent& sprite){
    if (frames.size() > 0){
        int id = frames[int(frames.size()*rand()/(RAND_MAX + 1.0))];

        if (sprite.framesRect.validIndex(id) && (unsigned int)id < sprite.numFramesRect){
            return sprite.framesRect[id].rect;
        }
    }

    return Rect(0,0,1,1);
}

Rect ActionSystem::getSpriteInitializerValue(std::vector<int>& frames, PointsComponent& points){
    if (frames.size() > 0){
        int id = frames[int(frames.size()*rand()/(RAND_MAX + 1.0))];

        if (points.framesRect.validIndex(id) && (unsigned int)id < points.numFramesRect){
            return points.framesRect[id].rect;
        }
    }

    return Rect(0,0,1,1);
}

void ActionSystem::applyParticleInitializers(size_t idx, ParticlesComponent& particles, InstancedMeshComponent& instmesh, SpriteComponent* sprite, Transform* targetTransform){
    ParticleLifeInitializer& lifeInit = particles.lifeInitializer;
    particles.particles[idx].life = getFloatInitializerValue(lifeInit.minLife, lifeInit.maxLife);

    ParticlePositionInitializer& posInit = particles.positionInitializer;
    Vector3 position = samplePositionInitializer(posInit);
    particles.particles[idx].position = getParticleSimulationPosition(particles, targetTransform, position);

    ParticleVelocityInitializer& velInit = particles.velocityInitializer;
    Vector3 velocity = getVector3InitializerValue(velInit.minVelocity, velInit.maxVelocity, false);
    particles.particles[idx].velocity = getParticleSimulationDirection(particles, targetTransform, velocity);

    ParticleAccelerationInitializer& accInit = particles.accelerationInitializer;
    Vector3 acceleration = getVector3InitializerValue(accInit.minAcceleration, accInit.maxAcceleration, false);
    particles.particles[idx].acceleration = getParticleSimulationDirection(particles, targetTransform, acceleration);

    ParticleColorInitializer& colInit = particles.colorInitializer;
    instmesh.instances[idx].color = getVector3InitializerValue(colInit.minColor, colInit.maxColor, false);
    if (colInit.useSRGB){
        instmesh.instances[idx].color = Color::sRGBToLinear(instmesh.instances[idx].color);
    }

    ParticleAlphaInitializer& alpInit = particles.alphaInitializer;
    instmesh.instances[idx].color.w = getFloatInitializerValue(alpInit.minAlpha, alpInit.maxAlpha);

    // size initializer is not applicable to instanced meshes

    if (sprite){
        ParticleSpriteInitializer& spriteInit = particles.spriteInitializer;
        instmesh.instances[idx].textureRect = getSpriteInitializerValue(spriteInit.frames, *sprite);
    }

    ParticleRotationInitializer& rotInit = particles.rotationInitializer;
    Quaternion rotation = getQuaternionInitializerValue(rotInit.minRotation, rotInit.maxRotation, rotInit.shortestPath);
    particles.particles[idx].rotation = getParticleSimulationRotation(particles, targetTransform, rotation);

    ParticleScaleInitializer& scaInit = particles.scaleInitializer;
    particles.particles[idx].scale = getVector3InitializerValue(scaInit.minScale, scaInit.maxScale, scaInit.linearSort);

    syncParticleInstance(idx, particles, instmesh, targetTransform);

}

void ActionSystem::applyParticleInitializers(size_t idx, ParticlesComponent& particles, PointsComponent& points, Transform* targetTransform){
    ParticleLifeInitializer& lifeInit = particles.lifeInitializer;
    particles.particles[idx].life = getFloatInitializerValue(lifeInit.minLife, lifeInit.maxLife);

    ParticlePositionInitializer& posInit = particles.positionInitializer;
    Vector3 position = samplePositionInitializer(posInit);
    particles.particles[idx].position = getParticleSimulationPosition(particles, targetTransform, position);

    ParticleVelocityInitializer& velInit = particles.velocityInitializer;
    Vector3 velocity = getVector3InitializerValue(velInit.minVelocity, velInit.maxVelocity, false);
    particles.particles[idx].velocity = getParticleSimulationDirection(particles, targetTransform, velocity);

    ParticleAccelerationInitializer& accInit = particles.accelerationInitializer;
    Vector3 acceleration = getVector3InitializerValue(accInit.minAcceleration, accInit.maxAcceleration, false);
    particles.particles[idx].acceleration = getParticleSimulationDirection(particles, targetTransform, acceleration);

    ParticleColorInitializer& colInit = particles.colorInitializer;
    points.points[idx].color = getVector3InitializerValue(colInit.minColor, colInit.maxColor, false);
    if (colInit.useSRGB){
        points.points[idx].color = Color::sRGBToLinear(points.points[idx].color);
    }

    ParticleAlphaInitializer& alpInit = particles.alphaInitializer;
    points.points[idx].color.w = getFloatInitializerValue(alpInit.minAlpha, alpInit.maxAlpha);

    ParticleSizeInitializer& sizeInit = particles.sizeInitializer;
    points.points[idx].size = getFloatInitializerValue(sizeInit.minSize, sizeInit.maxSize);

    ParticleSpriteInitializer& spriteInit = particles.spriteInitializer;
    points.points[idx].textureRect = getSpriteInitializerValue(spriteInit.frames, points);

    ParticleRotationInitializer& rotInit = particles.rotationInitializer;
    points.points[idx].rotation = Angle::defaultToRad(getQuaternionInitializerValue(rotInit.minRotation, rotInit.maxRotation, rotInit.shortestPath).getRoll());

    syncParticlePoint(idx, particles, points, targetTransform);

    // scale initializer is not applicable to points
}

float ActionSystem::getTimeFromParticleTime(float& time, float& fromTime, float& toTime){
    if ((fromTime != toTime) && (time >= fromTime) && (time <= toTime)) {
        return (time - fromTime) / (toTime - fromTime);
    }

    return -1;
}

bool ActionSystem::getParticleModifierValue(float& particleTime, float& fromTime, float& toTime, Ease& function, float& value){
    float time = getTimeFromParticleTime(particleTime, fromTime, toTime);
    if (time < 0 || time > 1){
        return false;
    }

    value = function.call(time);
    return value >= 0 && value <= 1;
}

float ActionSystem::getFloatModifierValue(float& value, float& fromValue, float& toValue){
    return fromValue + ((toValue - fromValue) * value);
}

Vector3 ActionSystem::getVector3ModifierValue(float& value, Vector3& fromValue, Vector3& toValue){
    return fromValue + ((toValue - fromValue) * value);
}

Quaternion ActionSystem::getQuaternionModifierValue(float& value, Quaternion& fromValue, Quaternion& toValue, bool shortestPath){
    return Quaternion::slerp(value, fromValue, toValue, shortestPath);
}

Rect ActionSystem::getSpriteModifierValue(float& value, std::vector<int>& frames, SpriteComponent& sprite){
    if (frames.size() > 0){
        int id = frames[(int)(frames.size() * value)];

        if (sprite.framesRect.validIndex(id) && (unsigned int)id < sprite.numFramesRect){
            return sprite.framesRect[id].rect;
        }
    }

    return Rect(0,0,1,1);
}

Rect ActionSystem::getSpriteModifierValue(float& value, std::vector<int>& frames, PointsComponent& points){
    if (frames.size() > 0){
        int id = frames[(int)(frames.size() * value)];

        if (points.framesRect.validIndex(id) && (unsigned int)id < points.numFramesRect){
            return points.framesRect[id].rect;
        }
    }

    return Rect(0,0,1,1);
}

void ActionSystem::applyParticleModifiers(size_t idx, ParticlesComponent& particles, InstancedMeshComponent& instmesh, SpriteComponent* sprite, Transform* targetTransform){
    float particleTime = particles.particles[idx].time;
    float value;

    ParticlePositionModifier& posMod = particles.positionModifier;
    if (getParticleModifierValue(particleTime, posMod.fromTime, posMod.toTime, posMod.function, value)){
        particles.particles[idx].position = getParticleSimulationPosition(particles, targetTransform, getVector3ModifierValue(value, posMod.fromPosition, posMod.toPosition));
    }

    ParticleVelocityModifier& velMod = particles.velocityModifier;
    if (getParticleModifierValue(particleTime, velMod.fromTime, velMod.toTime, velMod.function, value)){
        particles.particles[idx].velocity = getParticleSimulationDirection(particles, targetTransform, getVector3ModifierValue(value, velMod.fromVelocity, velMod.toVelocity));
    }

    ParticleAccelerationModifier& accMod = particles.accelerationModifier;
    if (getParticleModifierValue(particleTime, accMod.fromTime, accMod.toTime, accMod.function, value)){
        particles.particles[idx].acceleration = getParticleSimulationDirection(particles, targetTransform, getVector3ModifierValue(value, accMod.fromAcceleration, accMod.toAcceleration));
    }

    if (!particles.colorGradient.stops.empty()){
        instmesh.instances[idx].color = sampleParticleColorGradient(particles.colorGradient, particleTime, particles.particles[idx].life);
    } else {
        ParticleColorModifier& colMod = particles.colorModifier;
        if (getParticleModifierValue(particleTime, colMod.fromTime, colMod.toTime, colMod.function, value)){
            instmesh.instances[idx].color = getVector3ModifierValue(value, colMod.fromColor, colMod.toColor);
            if (colMod.useSRGB){
                instmesh.instances[idx].color = Color::sRGBToLinear(instmesh.instances[idx].color);
            }
        }
    }

    ParticleAlphaModifier& alpMod = particles.alphaModifier;
    if (getParticleModifierValue(particleTime, alpMod.fromTime, alpMod.toTime, alpMod.function, value)){
        instmesh.instances[idx].color.w = getFloatModifierValue(value, alpMod.fromAlpha, alpMod.toAlpha);
    }

    // size modifier is not applicable to instanced meshes

    if (sprite){
        ParticleSpriteModifier& spriteMod = particles.spriteModifier;
        if (getParticleModifierValue(particleTime, spriteMod.fromTime, spriteMod.toTime, spriteMod.function, value)){
            instmesh.instances[idx].textureRect = getSpriteModifierValue(value, spriteMod.frames, *sprite);
        }
    }

    ParticleRotationModifier& rotMod = particles.rotationModifier;
    if (getParticleModifierValue(particleTime, rotMod.fromTime, rotMod.toTime, rotMod.function, value)){
        particles.particles[idx].rotation = getParticleSimulationRotation(particles, targetTransform, getQuaternionModifierValue(value, rotMod.fromRotation, rotMod.toRotation, rotMod.shortestPath));
    }

    ParticleScaleModifier& scaMod = particles.scaleModifier;
    if (getParticleModifierValue(particleTime, scaMod.fromTime, scaMod.toTime, scaMod.function, value)){
        particles.particles[idx].scale = getVector3ModifierValue(value, scaMod.fromScale, scaMod.toScale);
    }

}

void ActionSystem::applyParticleModifiers(size_t idx, ParticlesComponent& particles, PointsComponent& points, Transform* targetTransform){
    float particleTime = particles.particles[idx].time;
    float value;

    ParticlePositionModifier& posMod = particles.positionModifier;
    if (getParticleModifierValue(particleTime, posMod.fromTime, posMod.toTime, posMod.function, value)){
        particles.particles[idx].position = getParticleSimulationPosition(particles, targetTransform, getVector3ModifierValue(value, posMod.fromPosition, posMod.toPosition));
    }

    ParticleVelocityModifier& velMod = particles.velocityModifier;
    if (getParticleModifierValue(particleTime, velMod.fromTime, velMod.toTime, velMod.function, value)){
        particles.particles[idx].velocity = getParticleSimulationDirection(particles, targetTransform, getVector3ModifierValue(value, velMod.fromVelocity, velMod.toVelocity));
    }

    ParticleAccelerationModifier& accMod = particles.accelerationModifier;
    if (getParticleModifierValue(particleTime, accMod.fromTime, accMod.toTime, accMod.function, value)){
        particles.particles[idx].acceleration = getParticleSimulationDirection(particles, targetTransform, getVector3ModifierValue(value, accMod.fromAcceleration, accMod.toAcceleration));
    }

    if (!particles.colorGradient.stops.empty()){
        points.points[idx].color = sampleParticleColorGradient(particles.colorGradient, particleTime, particles.particles[idx].life);
    } else {
        ParticleColorModifier& colMod = particles.colorModifier;
        if (getParticleModifierValue(particleTime, colMod.fromTime, colMod.toTime, colMod.function, value)){
            points.points[idx].color = getVector3ModifierValue(value, colMod.fromColor, colMod.toColor);
            if (colMod.useSRGB){
                points.points[idx].color = Color::sRGBToLinear(points.points[idx].color);
            }
        }
    }

    ParticleAlphaModifier& alpMod = particles.alphaModifier;
    if (getParticleModifierValue(particleTime, alpMod.fromTime, alpMod.toTime, alpMod.function, value)){
        points.points[idx].color.w = getFloatModifierValue(value, alpMod.fromAlpha, alpMod.toAlpha);
    }

    ParticleSizeModifier& sizeMod = particles.sizeModifier;
    if (getParticleModifierValue(particleTime, sizeMod.fromTime, sizeMod.toTime, sizeMod.function, value)){
        points.points[idx].size = getFloatModifierValue(value, sizeMod.fromSize, sizeMod.toSize);
    }

    ParticleSpriteModifier& spriteMod = particles.spriteModifier;
    if (getParticleModifierValue(particleTime, spriteMod.fromTime, spriteMod.toTime, spriteMod.function, value)){
        points.points[idx].textureRect = getSpriteModifierValue(value, spriteMod.frames, points);
    }

    ParticleRotationModifier& rotMod = particles.rotationModifier;
    if (getParticleModifierValue(particleTime, rotMod.fromTime, rotMod.toTime, rotMod.function, value)){
        points.points[idx].rotation = Angle::defaultToRad(getQuaternionModifierValue(value, rotMod.fromRotation, rotMod.toRotation, rotMod.shortestPath).getRoll());
    }

    // scale modifier is not applicable to points
}

void ActionSystem::advanceParticle(size_t idx, float dt, ParticlesComponent& particles, InstancedMeshComponent& instmesh, SpriteComponent* sprite, Transform* targetTransform){
    float life = particles.particles[idx].life;
    float time = particles.particles[idx].time;

    if (life <= time){
        instmesh.instances[idx].visible = false;
        return;
    }

    applyParticleModifiers(idx, particles, instmesh, sprite, targetTransform);

    Vector3 velocity = particles.particles[idx].velocity;
    Vector3 position = particles.particles[idx].position;
    Vector3 acceleration = particles.particles[idx].acceleration;

    velocity += acceleration * dt * 0.5f;
    position += velocity * dt;
    velocity += acceleration * dt * 0.5f;
    time += dt;

    particles.particles[idx].time = time;
    particles.particles[idx].velocity = velocity;
    particles.particles[idx].position = position;

    syncParticleInstance(idx, particles, instmesh, targetTransform);

    instmesh.instances[idx].visible = true;
    instmesh.needUpdateInstances = true;
}

void ActionSystem::advanceParticle(size_t idx, float dt, ParticlesComponent& particles, PointsComponent& points, Transform* targetTransform){
    float life = particles.particles[idx].life;
    float time = particles.particles[idx].time;

    if (life <= time){
        points.points[idx].visible = false;
        return;
    }

    applyParticleModifiers(idx, particles, points, targetTransform);

    Vector3 velocity = particles.particles[idx].velocity;
    Vector3 position = particles.particles[idx].position;
    Vector3 acceleration = particles.particles[idx].acceleration;

    velocity += acceleration * dt * 0.5f;
    position += velocity * dt;
    velocity += acceleration * dt * 0.5f;
    time += dt;

    particles.particles[idx].time = time;
    particles.particles[idx].velocity = velocity;
    particles.particles[idx].position = position;

    syncParticlePoint(idx, particles, points, targetTransform);

    points.points[idx].visible = true;
    points.needUpdate = true;
}

void ActionSystem::particleActionStart(ParticlesComponent& particles, InstancedMeshComponent& instmesh, MeshComponent& mesh){
    // Creating particles
    particles.particles.clear();
    instmesh.instances.clear();
    for (int i = 0; i < particles.maxParticles; i++){
        particles.particles.push_back({});
        particles.particles.back().life = 0;
        particles.particles.back().time = 0;

        instmesh.instances.push_back({});
        instmesh.instances.back().visible = false;

        instmesh.needUpdateInstances = true;
    }

    if (instmesh.maxInstances != particles.maxParticles){
        instmesh.maxInstances = particles.maxParticles;
        if (mesh.loaded)
            mesh.needReload = true;
    }

    particles.emitter = true;
    particles.newParticlesCount = 0;
    particles.lastUsedParticle = 0;
    sortParticleBursts(particles);
    sortParticleColorGradient(particles);
    particles.currentBurst = 0;
}

void ActionSystem::particleActionStart(ParticlesComponent& particles, PointsComponent& points){
    if (particles.sizeInitializer.minSize == 0 && particles.sizeInitializer.maxSize == 0){
        float size = std::max(points.texture.getWidth(), points.texture.getHeight());
        particles.sizeInitializer.minSize = size;
        particles.sizeInitializer.maxSize = size;
    }

    // Creating particles
    particles.particles.clear();
    points.points.clear();
    for (int i = 0; i < particles.maxParticles; i++){
        particles.particles.push_back({});
        particles.particles.back().life = 0;
        particles.particles.back().time = 0;

        points.points.push_back({});
        points.points.back().visible = false;

        points.needUpdate = true;
    }

    if (points.maxPoints != particles.maxParticles){
        points.maxPoints = particles.maxParticles;

        points.needReload = true;
    }

    particles.emitter = true;
    particles.newParticlesCount = 0;
    particles.lastUsedParticle = 0;
    sortParticleBursts(particles);
    sortParticleColorGradient(particles);
    particles.currentBurst = 0;
}

void ActionSystem::particleActionReset(Entity entity){
    ActionComponent* action = scene->findComponent<ActionComponent>(entity);
    ParticlesComponent* particles = scene->findComponent<ParticlesComponent>(entity);
    if (!action || !particles) {
        return;
    }

    action->state = ActionState::Stopped;
    action->timecount = 0.0f;
    action->startTrigger = false;
    action->stopTrigger = false;
    action->pauseTrigger = false;

    particles->particles.clear();
    particles->newParticlesCount = 0.0f;
    particles->lastUsedParticle = 0;
    particles->currentBurst = 0;
    particles->emitter = false;

    if (action->target == NULL_ENTITY) {
        return;
    }

    if (PointsComponent* points = scene->findComponent<PointsComponent>(action->target)) {
        points->points.clear();
        points->renderPoints.clear();
        points->numVisible = 0;
        points->needUpdate = true;
        points->needUpdateBuffer = true;
    }

    if (InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(action->target)) {
        for (InstanceData& instance : instmesh->instances) {
            instance.visible = false;
        }
        instmesh->renderInstances.clear();
        instmesh->numVisible = 0;
        instmesh->needUpdateInstances = true;
    }
}

void ActionSystem::particlesActionUpdate(double dt, Entity entity, Entity target, ActionComponent& action, ParticlesComponent& particles, InstancedMeshComponent& instmesh){
    SpriteComponent* sprite = scene->findComponent<SpriteComponent>(target);
    Transform* targetTransform = scene->findComponent<Transform>(target);
    updateParticleTargetCache(particles, targetTransform);

    bool existParticles = false;
    for(int i=0; i<particles.particles.size(); i++){
        if (particles.particles[i].life > particles.particles[i].time){
            advanceParticle(i, dt, particles, instmesh, sprite, targetTransform);
            existParticles = true;
        }else{
            instmesh.instances[i].visible = false;
        }
    }

    if (particles.emitter){
        // Bursts (process all bursts whose scheduled time has been reached)
        while (particles.currentBurst < (int)particles.bursts.size() &&
               particles.bursts[particles.currentBurst].time <= action.timecount){
            int count = sampleBurstCount(particles.bursts[particles.currentBurst]);
            for (int b = 0; b < count; b++){
                int particleIndex = findUnusedParticle(particles);
                if (particleIndex < 0) break;
                particles.particles[particleIndex].time = 0;
                applyParticleInitializers(particleIndex, particles, instmesh, sprite, targetTransform);
                existParticles = true;
            }
            particles.currentBurst++;
        }

        particles.newParticlesCount += dt * particles.rate;

        int newparticles = (int)particles.newParticlesCount;
        particles.newParticlesCount -= newparticles;
        if (newparticles > particles.maxPerUpdate)
            newparticles = particles.maxPerUpdate;

        for(int i=0; i<newparticles; i++){
            int particleIndex = findUnusedParticle(particles);

            if (particleIndex >= 0){
                particles.particles[particleIndex].time = 0;
                applyParticleInitializers(particleIndex, particles, instmesh, sprite, targetTransform);

                float spawnDt = (float)dt * (float)(newparticles - i) / (float)(newparticles + 1);
                if (spawnDt > 0.0f){
                    advanceParticle(particleIndex, spawnDt, particles, instmesh, sprite, targetTransform);
                }

                existParticles = true;
            }else{
                if (!particles.loop)
                    particles.emitter = false;
                break;
            }
        }
    }

    if (!existParticles && !particles.emitter){
        actionStop(entity);
        //onFinish.call(object);
    } else if (particles.loop) {
        float cycleDuration = getParticleCycleDuration(particles);
        if (cycleDuration > 0.0f){
            while (action.timecount >= cycleDuration) {
                action.timecount -= cycleDuration;
                particles.currentBurst = 0;
            }
        }
    }
}

void ActionSystem::particlesActionUpdate(double dt, Entity entity, Entity target, ActionComponent& action, ParticlesComponent& particles, PointsComponent& points){
    Transform* targetTransform = scene->findComponent<Transform>(target);
    updateParticleTargetCache(particles, targetTransform);

    bool existParticles = false;
    for(int i=0; i<particles.particles.size(); i++){
        if (particles.particles[i].life > particles.particles[i].time){
            advanceParticle(i, dt, particles, points, targetTransform);
            existParticles = true;
        }else{
            points.points[i].visible = false;
        }
    }

    if (particles.emitter){
        // Bursts (process all bursts whose scheduled time has been reached)
        while (particles.currentBurst < (int)particles.bursts.size() &&
               particles.bursts[particles.currentBurst].time <= action.timecount){
            int count = sampleBurstCount(particles.bursts[particles.currentBurst]);
            for (int b = 0; b < count; b++){
                int particleIndex = findUnusedParticle(particles);
                if (particleIndex < 0) break;
                particles.particles[particleIndex].time = 0;
                applyParticleInitializers(particleIndex, particles, points, targetTransform);
                existParticles = true;
            }
            particles.currentBurst++;
        }

        particles.newParticlesCount += dt * particles.rate;

        int newparticles = (int)particles.newParticlesCount;
        particles.newParticlesCount -= newparticles;
        if (newparticles > particles.maxPerUpdate)
            newparticles = particles.maxPerUpdate;

        for(int i=0; i<newparticles; i++){
            int particleIndex = findUnusedParticle(particles);

            if (particleIndex >= 0){
                particles.particles[particleIndex].time = 0;
                applyParticleInitializers(particleIndex, particles, points, targetTransform);

                float spawnDt = (float)dt * (float)(newparticles - i) / (float)(newparticles + 1);
                if (spawnDt > 0.0f){
                    advanceParticle(particleIndex, spawnDt, particles, points, targetTransform);
                }

                existParticles = true;
            }else{
                if (!particles.loop)
                    particles.emitter = false;
                break;
            }
        }
    }

    if (!existParticles && !particles.emitter){
        actionStop(entity);
        //onFinish.call(object);
    } else if (particles.loop) {
        float cycleDuration = getParticleCycleDuration(particles);
        if (cycleDuration > 0.0f){
            while (action.timecount >= cycleDuration) {
                action.timecount -= cycleDuration;
                particles.currentBurst = 0;
            }
        }
    }
}

void ActionSystem::keyframeUpdate(double dt, ActionComponent& action, KeyframeTracksComponent& keyframe){
    if (keyframe.times.size() == 0)
        return;

    float currentTime = action.timecount;

    keyframe.interpolation = 0;

    keyframe.index = 0;
    while (keyframe.index < (keyframe.times.size()-1) && keyframe.times[keyframe.index] < currentTime){
        keyframe.index++;
    }

    float previousTime = 0;
    float nextTime = keyframe.times[keyframe.index];

    if (keyframe.index > 0){
        previousTime = keyframe.times[keyframe.index-1];
    }

    if (nextTime > previousTime){
        keyframe.interpolation = (currentTime - previousTime) / (nextTime - previousTime);
    }

    if (keyframe.interpolation > 1){
        keyframe.interpolation = 1;
    }

    // Per-segment easing: easings[i] shapes the segment from key i to key i+1.
    // Empty easings (e.g. GLTF LINEAR clips) keep pure linear interpolation;
    // GLTF STEP clips use EaseType::STEP here.
    if (keyframe.index > 0){
        size_t segment = (size_t)keyframe.index - 1;
        if (segment < keyframe.easings.size() && keyframe.easings[segment] != EaseType::LINEAR){
            keyframe.interpolation = Ease::getFunction(keyframe.easings[segment])(keyframe.interpolation);
        }
    }
}

// Tangent presence is what marks a track as GLTF CUBICSPLINE; editor-authored
// and GLTF LINEAR/STEP tracks keep the tangent vectors empty.
template<typename T>
static bool hasCubicSplineTangents(const KeyframeTracksComponent& keyframe, const std::vector<T>& values, const std::vector<T>& inTangents, const std::vector<T>& outTangents){
    return keyframe.index > 0 && values.size() > 0 &&
           inTangents.size() == values.size() && outTangents.size() == values.size();
}

// keyframe.index comes from the times array, which the editor can edit out of
// sync with a track's values; a track with fewer values than times must freeze
// instead of reading out of bounds.
template<typename TracksComp>
static bool trackIndexValid(const KeyframeTracksComponent& keyframe, const TracksComp& tracks){
    return !tracks.values.empty() && (size_t)keyframe.index < tracks.values.size();
}

// Sample a vector track at the current playhead: cubic Hermite when tangents
// are present, lerp otherwise.
template<typename TracksComp>
static Vector3 sampleVectorTrack(const KeyframeTracksComponent& keyframe, const TracksComp& tracks){
    Vector3 previous = tracks.values[(keyframe.index > 0) ? keyframe.index - 1 : 0];

    if (hasCubicSplineTangents(keyframe, tracks.values, tracks.inTangents, tracks.outTangents)){
        float td = keyframe.times[keyframe.index] - keyframe.times[keyframe.index-1];
        return Interpolation::cubicSpline(keyframe.interpolation, td,
                previous, tracks.outTangents[keyframe.index-1],
                tracks.values[keyframe.index], tracks.inTangents[keyframe.index]);
    }

    return previous + keyframe.interpolation * (tracks.values[keyframe.index] - previous);
}

void ActionSystem::translateTracksUpdate(KeyframeTracksComponent& keyframe, TranslateTracksComponent& translatetracks, Entity target, float weight){
    if (weight <= 0.0f || !trackIndexValid(keyframe, translatetracks))
        return;

    Vector3 value = sampleVectorTrack(keyframe, translatetracks);

    TransformBlendAccum& accum = transformBlend[target];
    accum.positionSum = accum.positionSum + value * weight;
    accum.positionWeight += weight;
}

void ActionSystem::scaleTracksUpdate(KeyframeTracksComponent& keyframe, ScaleTracksComponent& scaletracks, Entity target, float weight){
    if (weight <= 0.0f || !trackIndexValid(keyframe, scaletracks))
        return;

    Vector3 value = sampleVectorTrack(keyframe, scaletracks);

    TransformBlendAccum& accum = transformBlend[target];
    accum.scaleSum = accum.scaleSum + value * weight;
    accum.scaleWeight += weight;
}

void ActionSystem::rotateTracksUpdate(KeyframeTracksComponent& keyframe, RotateTracksComponent& rotatetracks, Entity target, float weight){
    if (weight <= 0.0f || !trackIndexValid(keyframe, rotatetracks))
        return;

    Quaternion previousRotation = rotatetracks.values[(keyframe.index > 0) ? keyframe.index - 1 : 0];

    Quaternion value;
    if (hasCubicSplineTangents(keyframe, rotatetracks.values, rotatetracks.inTangents, rotatetracks.outTangents)){
        float td = keyframe.times[keyframe.index] - keyframe.times[keyframe.index-1];
        value = Interpolation::cubicSpline(keyframe.interpolation, td,
                previousRotation, rotatetracks.outTangents[keyframe.index-1],
                rotatetracks.values[keyframe.index], rotatetracks.inTangents[keyframe.index]);
        // spec: the interpolated quaternion MUST be normalized before applying.
        // Guard the all-zero result the spec warns exporters about (v_k == -v_k+1):
        // normalizing it would spread NaN through the pose blend.
        if (value.norm() > FLT_EPSILON){
            value.normalize();
        }else{
            value = (keyframe.interpolation < 0.5f) ? previousRotation : rotatetracks.values[keyframe.index];
        }
    }else{
        value = Quaternion::slerp(keyframe.interpolation, previousRotation, rotatetracks.values[keyframe.index]);
    }

    TransformBlendAccum& accum = transformBlend[target];
    // Weighted quaternion average (nlerp-style): align each sample to the first
    // sample's hemisphere so the shortest arc is taken, sum, then normalize on flush.
    if (!accum.hasRotationRef){
        accum.rotationRef = value;
        accum.hasRotationRef = true;
    } else if (accum.rotationRef.dot(value) < 0.0f){
        value = -value;
    }
    accum.rotationSum = accum.rotationSum + value * weight;
    accum.rotationWeight += weight;
}

void ActionSystem::morphTracksUpdate(KeyframeTracksComponent& keyframe, MorphTracksComponent& morpthtracks, Entity target, float weight){
    if (weight <= 0.0f || !trackIndexValid(keyframe, morpthtracks))
        return;

    std::vector<float> previousMorph = morpthtracks.values[(keyframe.index > 0) ? keyframe.index - 1 : 0];

    if ((keyframe.index == 0) || (morpthtracks.values[keyframe.index].size() == morpthtracks.values[keyframe.index-1].size())) {
        bool cubic = hasCubicSplineTangents(keyframe, morpthtracks.values, morpthtracks.inTangents, morpthtracks.outTangents) &&
                     morpthtracks.outTangents[keyframe.index-1].size() == previousMorph.size() &&
                     morpthtracks.inTangents[keyframe.index].size() == morpthtracks.values[keyframe.index].size();
        float td = cubic ? keyframe.times[keyframe.index] - keyframe.times[keyframe.index-1] : 0;
        MorphBlendAccum& accum = morphBlend[target];
        for (int morphIndex = 0; morphIndex < morpthtracks.values[keyframe.index].size() && morphIndex < MAX_MORPHTARGETS; morphIndex++) {
            float value;
            if (cubic){
                value = Interpolation::cubicSpline(keyframe.interpolation, td,
                        previousMorph[morphIndex], morpthtracks.outTangents[keyframe.index-1][morphIndex],
                        morpthtracks.values[keyframe.index][morphIndex], morpthtracks.inTangents[keyframe.index][morphIndex]);
            }else{
                value = previousMorph[morphIndex] + keyframe.interpolation * (morpthtracks.values[keyframe.index][morphIndex] - previousMorph[morphIndex]);
            }
            accum.sums[morphIndex] += value * weight;
            accum.weights[morphIndex] += weight;
        }
    }else{
        Log::error("MorphTrack of index %i is different size than index %i", keyframe.index, keyframe.index-1);
    }
}

void ActionSystem::clearPoseBlend(){
    transformBlend.clear();
    morphBlend.clear();
}

void ActionSystem::flushPoseBlend(){
    for (auto& entry : transformBlend){
        Entity target = entry.first;
        TransformBlendAccum& accum = entry.second;

        if (!scene->isEntityCreated(target))
            continue;

        Transform* transform = scene->findComponent<Transform>(target);
        if (!transform)
            continue;

        bool changed = false;
        if (accum.positionWeight > 0.0f){
            transform->position = accum.positionSum * (1.0f / accum.positionWeight);
            changed = true;
        }
        if (accum.scaleWeight > 0.0f){
            transform->scale = accum.scaleSum * (1.0f / accum.scaleWeight);
            changed = true;
        }
        if (accum.rotationWeight > 0.0f){
            transform->rotation = accum.rotationSum.normalized();
            changed = true;
        }
        if (changed){
            transform->needUpdate = true;
        }
    }

    for (auto& entry : morphBlend){
        Entity target = entry.first;
        MorphBlendAccum& accum = entry.second;

        if (!scene->isEntityCreated(target))
            continue;

        MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
        if (!mesh)
            continue;

        for (int i = 0; i < MAX_MORPHTARGETS; i++){
            if (accum.weights[i] > 0.0f){
                mesh->morphWeights[i] = accum.sums[i] / accum.weights[i];
            }
        }
    }

    clearPoseBlend();
}

float ActionSystem::getDuration(Entity entity) {
    return getDuration(entity, 0);
}

float ActionSystem::getFrameDuration(const ActionFrame& frame) {
    if (frame.duration > 0) {
        return frame.duration;
    }
    return getDuration(frame.action, 0);
}

bool ActionSystem::isAnimationReachable(Entity from, Entity target) {
    std::unordered_set<Entity> visited;
    return isAnimationReachable(from, target, visited);
}

bool ActionSystem::isAnimationReachable(Entity from, Entity target, std::unordered_set<Entity>& visited) {
    if (from == NULL_ENTITY) {
        return false;
    }
    if (from == target) {
        return true;
    }
    if (!visited.insert(from).second) {
        return false; // already walked (protects against pre-existing cycles)
    }

    AnimationComponent* anim = scene->findComponent<AnimationComponent>(from);
    if (!anim) {
        return false;
    }
    for (const ActionFrame& frame : anim->actions) {
        if (isAnimationReachable(frame.action, target, visited)) {
            return true;
        }
    }
    return false;
}

float ActionSystem::getDuration(Entity entity, int depth) {
    // Animations can nest other animations as frames; cap recursion so a cycle
    // of auto-duration frames cannot hang the engine.
    if (entity == NULL_ENTITY || depth > 8) {
        return 0;
    }

    float duration = 0;
    if (TimedActionComponent* timed = scene->findComponent<TimedActionComponent>(entity)) {
        duration = timed->duration;
    } else if (AnimationComponent* anim = scene->findComponent<AnimationComponent>(entity)) {
        if (anim->duration > 0) {
            duration = anim->duration;
        } else {
            for (const ActionFrame& frame : anim->actions) {
                float frameDuration = (frame.duration > 0) ? frame.duration : getDuration(frame.action, depth + 1);
                duration = std::max(duration, frame.startTime + frameDuration);
            }
        }
    } else if (KeyframeTracksComponent* kf = scene->findComponent<KeyframeTracksComponent>(entity)) {
        if (!kf->times.empty()) {
            duration = kf->times.back();
        }
    } else if (SpriteAnimationComponent* spriteanim = scene->findComponent<SpriteAnimationComponent>(entity)) {
        float totalMs = 0;
        for (unsigned int i = 0; i < spriteanim->framesTimeSize; i++) {
            totalMs += spriteanim->framesTime[i];
        }
        duration = totalMs / 1000.0f;
    } else if (ParticlesComponent* parts = scene->findComponent<ParticlesComponent>(entity)) {
        float emitDuration = getParticleCycleDuration(*parts);
        if (emitDuration > 0.0f) {
            if (parts->loop) {
                // One emission cycle: used for timeline wrapping
                duration = emitDuration;
            } else {
                // Full system lifetime: emit phase + last particle dies
                duration = emitDuration + parts->lifeInitializer.maxLife;
            }
        }
    }
    return duration;
}

void ActionSystem::load(){

}

void ActionSystem::resetRunningActions(){
    // Reset actions serialized in Running/Paused state so actionStart fires on the first update tick.
    auto actions = scene->getComponentArray<ActionComponent>();
    for (int i = 0; i < actions->size(); i++) {
        ActionComponent& action = actions->getComponentFromIndex(i);

        if (action.state == ActionState::Running || action.state == ActionState::Paused) {
            action.state = ActionState::Stopped;
            action.timecount = 0;
            action.startTrigger = true;
            action.stopTrigger = false;
            action.pauseTrigger = false;
        }
    }
}

void ActionSystem::destroy(){
    auto actions = scene->getComponentArray<ActionComponent>();
    for (int i = 0; i < actions->size(); i++) {
        ActionComponent &action = actions->getComponentFromIndex(i);
        Entity entity = actions->getEntity(i);
        Signature signature = scene->getSignature(entity);

        action.state = ActionState::Stopped;
        action.timecount = 0;
        action.startTrigger = false;
        action.stopTrigger = false;
        action.pauseTrigger = false;

        if (signature.test(scene->getComponentId<TimedActionComponent>())) {
            TimedActionComponent &timedaction = scene->getComponent<TimedActionComponent>(entity);
            timedaction.time = 0;
            timedaction.value = 0;
        }
    }
}

void ActionSystem::draw(){

}

void ActionSystem::actionStateChange(Entity entity, ActionComponent& action){
    // Action start
    if (action.startTrigger == true && (action.state == ActionState::Stopped || action.state == ActionState::Paused)){
        actionStart(entity);
    }

    // Action stop
    if (action.stopTrigger == true && (action.state == ActionState::Running || action.state == ActionState::Paused)){
        actionStop(entity);
    }

    // Action pause
    if (action.pauseTrigger == true && action.state == ActionState::Running){
        actionPause(entity);
    }
}

void ActionSystem::processRunningAction(double dt, Entity entity, ActionComponent& action){
    Signature signature = scene->getSignature(entity);
    Signature targetSignature;
    if (action.target != NULL_ENTITY){
        targetSignature = scene->getSignature(action.target);
    }

    //Sprite animation
    if (signature.test(scene->getComponentId<SpriteAnimationComponent>())){
        SpriteAnimationComponent& spriteanim = scene->getComponent<SpriteAnimationComponent>(entity);
        if (targetSignature.test(scene->getComponentId<SpriteComponent>()) && targetSignature.test(scene->getComponentId<MeshComponent>())){
            SpriteComponent& sprite = scene->getComponent<SpriteComponent>(action.target);
            MeshComponent& mesh = scene->getComponent<MeshComponent>(action.target);

            spriteActionUpdate(dt, entity, action, mesh, sprite, spriteanim);
            if (action.state != ActionState::Running) return;
        }
    }

    //Particles
    if (signature.test(scene->getComponentId<ParticlesComponent>())){
        ParticlesComponent& particles = scene->getComponent<ParticlesComponent>(entity);

        if (targetSignature.test(scene->getComponentId<InstancedMeshComponent>())){
            InstancedMeshComponent& instmesh = scene->getComponent<InstancedMeshComponent>(action.target);

            particlesActionUpdate(dt, entity, action.target, action, particles, instmesh);
            if (action.state != ActionState::Running) return;
        }
        if (targetSignature.test(scene->getComponentId<PointsComponent>())){
            PointsComponent& points = scene->getComponent<PointsComponent>(action.target);

            particlesActionUpdate(dt, entity, action.target, action, particles, points);
            if (action.state != ActionState::Running) return;
        }
    }

    //keyframe animation
    if (signature.test(scene->getComponentId<KeyframeTracksComponent>())){
        KeyframeTracksComponent& keyframe = scene->getComponent<KeyframeTracksComponent>(entity);

        keyframeUpdate(dt, action, keyframe);
        if (action.state != ActionState::Running) return;

        // Stop when past the last keyframe time
        if (!keyframe.times.empty() && action.timecount >= keyframe.times.back()) {
            keyframe.interpolation = 1;
            keyframe.index = keyframe.times.size() - 1;
        }

        if (signature.test(scene->getComponentId<TranslateTracksComponent>())){
            TranslateTracksComponent& translatetracks = scene->getComponent<TranslateTracksComponent>(entity);

            if (targetSignature.test(scene->getComponentId<Transform>())){
                translateTracksUpdate(keyframe, translatetracks, action.target, action.weight);
            }
        }

        if (signature.test(scene->getComponentId<RotateTracksComponent>())){
            RotateTracksComponent& rotatetracks = scene->getComponent<RotateTracksComponent>(entity);

            if (targetSignature.test(scene->getComponentId<Transform>())){
                rotateTracksUpdate(keyframe, rotatetracks, action.target, action.weight);
            }
        }

        if (signature.test(scene->getComponentId<ScaleTracksComponent>())){
            ScaleTracksComponent& scaletracks = scene->getComponent<ScaleTracksComponent>(entity);

            if (targetSignature.test(scene->getComponentId<Transform>())){
                scaleTracksUpdate(keyframe, scaletracks, action.target, action.weight);
            }
        }

        if (signature.test(scene->getComponentId<MorphTracksComponent>())){
            MorphTracksComponent& morpthtracks = scene->getComponent<MorphTracksComponent>(entity);

            if (targetSignature.test(scene->getComponentId<MeshComponent>())){
                morphTracksUpdate(keyframe, morpthtracks, action.target, action.weight);
            }
        }

        // Stop after applying final values
        if (!keyframe.times.empty() && action.timecount >= keyframe.times.back()) {
            actionStop(entity);
            return;
        }
    }


    if (signature.test(scene->getComponentId<TimedActionComponent>())){
        TimedActionComponent& timedaction = scene->getComponent<TimedActionComponent>(entity);

        timedActionUpdate(dt, entity, action, timedaction);
        if (action.state != ActionState::Running) return;

        //Transform animation
        if (targetSignature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(action.target);

            if (signature.test(scene->getComponentId<PositionActionComponent>())){
                PositionActionComponent& posaction = scene->getComponent<PositionActionComponent>(entity);

                positionActionUpdate(dt, action, timedaction, posaction, transform);
            }

            if (signature.test(scene->getComponentId<RotationActionComponent>())){
                RotationActionComponent& rotaction = scene->getComponent<RotationActionComponent>(entity);

                rotationActionUpdate(dt, action, timedaction, rotaction, transform);
            }

            if (signature.test(scene->getComponentId<ScaleActionComponent>())){
                ScaleActionComponent& scaleaction = scene->getComponent<ScaleActionComponent>(entity);

                scaleActionUpdate(dt, action, timedaction, scaleaction, transform);
            }
        }

        //Color animation
        if (signature.test(scene->getComponentId<ColorActionComponent>())){
            ColorActionComponent& coloraction = scene->getComponent<ColorActionComponent>(entity);

            if (targetSignature.test(scene->getComponentId<MeshComponent>())){
                MeshComponent& mesh = scene->getComponent<MeshComponent>(action.target);

                colorActionMeshUpdate(dt, action, timedaction, coloraction, mesh);
            }
            if (targetSignature.test(scene->getComponentId<UIComponent>())){
                UIComponent& ui = scene->getComponent<UIComponent>(action.target);

                colorActionUIUpdate(dt, action, timedaction, coloraction, ui);
            }
        }

        //Alpha animation
        if (signature.test(scene->getComponentId<AlphaActionComponent>())){
            AlphaActionComponent& alphaaction = scene->getComponent<AlphaActionComponent>(entity);

            if (targetSignature.test(scene->getComponentId<MeshComponent>())){
                MeshComponent& mesh = scene->getComponent<MeshComponent>(action.target);

                alphaActionMeshUpdate(dt, action, timedaction, alphaaction, mesh);
            }
            if (targetSignature.test(scene->getComponentId<UIComponent>())){
                UIComponent& ui = scene->getComponent<UIComponent>(action.target);

                alphaActionUIUpdate(dt, action, timedaction, alphaaction, ui);
            }
        }

    }

    actionUpdate(dt, action);
}

void ActionSystem::updateAnimationPreview(double dt, Entity entity){
    std::vector<Entity> entities;
    entities.push_back(entity);
    updateAnimationPreview(dt, entities);
}

void ActionSystem::updateAnimationPreview(double dt, const std::vector<Entity>& entities){
    // Drives one or more top-level animations into a single pose-blend scope so
    // concurrently running clips crossfade (used by the editor transition preview).
    clearPoseBlend();

    std::unordered_set<Entity> visitedAnimations;

    std::function<void(Entity)> previewAnimation = [&](Entity animationEntity) {
        Signature signature = scene->getSignature(animationEntity);
        if (!signature.test(scene->getComponentId<AnimationComponent>()) ||
            !signature.test(scene->getComponentId<ActionComponent>()) ||
            !visitedAnimations.insert(animationEntity).second) {
            return;
        }

        AnimationComponent& animcomp = scene->getComponent<AnimationComponent>(animationEntity);
        ActionComponent& action = scene->getComponent<ActionComponent>(animationEntity);

        actionStateChange(animationEntity, action);
        if (action.state == ActionState::Running){
            animationUpdate(dt, animationEntity, action, animcomp);
        }

        for (int i = 0; i < (int)animcomp.actions.size(); i++){
            Entity childEntity = animcomp.actions[i].action;
            if (childEntity == NULL_ENTITY || !scene->isEntityCreated(childEntity)) {
                continue;
            }

            Signature childSig = scene->getSignature(childEntity);
            if (!childSig.test(scene->getComponentId<ActionComponent>())) {
                continue;
            }

            ActionComponent& childAction = scene->getComponent<ActionComponent>(childEntity);
            actionStateChange(childEntity, childAction);

            if (childAction.state != ActionState::Running) {
                continue;
            }

            if (childSig.test(scene->getComponentId<AnimationComponent>())) {
                previewAnimation(childEntity);
            } else {
                processRunningAction(dt, childEntity, childAction);
            }
        }

        if (action.state == ActionState::Running){
            actionUpdate(dt, action);
        }
    };

    for (Entity entity : entities){
        if (entity != NULL_ENTITY && scene->isEntityCreated(entity)){
            previewAnimation(entity);
        }
    }

    flushPoseBlend();
}

void ActionSystem::updateActionPreview(double dt, Entity entity){
    if (!scene->isEntityCreated(entity)) return;

    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<ActionComponent>())) return;

    // Animation entities need the full animation preview path
    if (signature.test(scene->getComponentId<AnimationComponent>())) {
        updateAnimationPreview(dt, entity);
        return;
    }

    ActionComponent& action = scene->getComponent<ActionComponent>(entity);

    actionStateChange(entity, action);

    if (action.state == ActionState::Running){
        clearPoseBlend();
        processRunningAction(dt, entity, action);
        flushPoseBlend();
    }
}

void ActionSystem::update(double dt){
    if (paused) {
        return;
    }

    clearPoseBlend();

    //Animations actions
    auto animations = scene->getComponentArray<AnimationComponent>();
    for (int i = 0; i < animations->size(); i++){
        AnimationComponent& animcomp = animations->getComponentFromIndex(i);

        Entity entity = animations->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<ActionComponent>())){
            ActionComponent& action = scene->getComponent<ActionComponent>(entity);

            actionStateChange(entity, action);

            if (action.state == ActionState::Running){
                animationUpdate(dt, entity, action, animcomp);
            }
        }
    }

    //All actions
    auto actions = scene->getComponentArray<ActionComponent>();
    for (int i = 0; i < actions->size(); i++){
		ActionComponent& action = actions->getComponentFromIndex(i);

        actionStateChange(actions->getEntity(i), action);

        // Action update
        if (action.state == ActionState::Running){
            Entity entity = actions->getEntity(i);
            processRunningAction(dt, entity, action);
        }

	}

    // Write the blended (weighted-average) pose to each animated target.
    flushPoseBlend();
}

void ActionSystem::onComponentAdded(Entity entity, ComponentId componentId) {

}

void ActionSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<ActionComponent>()) {
		ActionComponent& action = scene->getComponent<ActionComponent>(entity);

		// Snapshot before stopping children: their onStop callbacks are user code
		// that may add/remove components and invalidate this reference.
		const bool wasPlaying = action.state == ActionState::Running || action.state == ActionState::Paused;
		const bool ownedTarget = action.ownedTarget;
		const Entity target = action.target;

		// A playing animation that loses its ActionComponent can no longer drive or
		// stop its child tracks; stop them now so they don't run on orphaned.
		// A Stopped parent's running children are standalone user-driven actions
		// and are deliberately left alone.
		if (wasPlaying) {
			if (AnimationComponent* animation = scene->findComponent<AnimationComponent>(entity)) {
				animationStopChildren(animationChildEntities(*animation));
			}
		}

		if (ownedTarget && target != NULL_ENTITY && scene->isEntityCreated(target)) {
			scene->destroyEntity(target);
		}
	} else if (componentId == scene->getComponentId<AnimationComponent>()) {
		AnimationComponent& animation = scene->getComponent<AnimationComponent>(entity);

		// Snapshot before callbacks, same reason as above.
		const bool ownedActions = animation.ownedActions;
		const std::vector<Entity> children = animationChildEntities(animation);

		// The clip being removed can no longer drive or stop anything: stop the
		// whole action (children included) so the remaining bare ActionComponent
		// doesn't keep running through the all-actions pass.
		if (ActionComponent* action = scene->findComponent<ActionComponent>(entity)) {
			if (action->state == ActionState::Running || action->state == ActionState::Paused) {
				actionStop(entity);
			}
		}

		if (ownedActions) {
			for (Entity child : children) {
				if (child != NULL_ENTITY && scene->isEntityCreated(child)) {
					scene->destroyEntity(child);
				}
			}
		}
	}
}
