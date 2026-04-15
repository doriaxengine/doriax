//
// (c) 2026 Eduardo Doria.
//

#include "Particles.h"

using namespace doriax;

Particles::Particles(Scene* scene): Action(scene){
    addComponent<ParticlesComponent>();
}

Particles::~Particles(){

}

void Particles::setMaxParticles(unsigned int maxParticles){
    ParticlesComponent& particomp = getComponent<ParticlesComponent>();

    if (particomp.maxParticles != maxParticles){
        particomp.maxParticles = maxParticles;

        //particomp.needReload = true;
    }
}

unsigned int Particles::getMaxParticles() const{
    ParticlesComponent& particomp = getComponent<ParticlesComponent>();

    return particomp.maxParticles;
}

void Particles::setRate(int rate){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();
    
    partAnim.rate = rate;
}

int Particles::getRate() const{
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();
    
    return partAnim.rate;
}

void Particles::setEmitter(bool emitter){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.emitter = emitter;
}

bool Particles::isEmitter() const{
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    return partAnim.emitter;
}

void Particles::setLoop(bool loop){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.loop = loop;
}

bool Particles::isLoop() const{
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    return partAnim.loop;
}

void Particles::setMaxPerUpdate(int maxPerUpdate){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();
    
    partAnim.maxPerUpdate = maxPerUpdate;
}

int Particles::getMaxPerUpdate() const{
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();
    
    return partAnim.maxPerUpdate;
}

void Particles::setLifeInitializer(float life){
    setLifeInitializer(life, life);
}

void Particles::setLifeInitializer(float minLife, float maxLife){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.lifeInitializer.minLife = minLife;
    partAnim.lifeInitializer.maxLife = maxLife;
}

void Particles::setPositionInitializer(Vector3 position){
    setPositionInitializer(position, position);
}

void Particles::setPositionInitializer(Vector3 minPosition, Vector3 maxPosition){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.positionInitializer.minPosition = minPosition;
    partAnim.positionInitializer.maxPosition = maxPosition;
}

void Particles::setPositionModifier(float fromTime, float toTime, Vector3 fromPosition, Vector3 toPosition){
    setPositionModifier(fromTime, toTime, fromPosition, toPosition, EaseType::LINEAR);
}

void Particles::setPositionModifier(float fromTime, float toTime, Vector3 fromPosition, Vector3 toPosition, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.positionModifier.fromTime = fromTime;
    partAnim.positionModifier.toTime = toTime;
    partAnim.positionModifier.fromPosition = fromPosition;
    partAnim.positionModifier.toPosition = toPosition;
    partAnim.positionModifier.function = Ease::getFunction(functionType);
}

void Particles::setVelocityInitializer(Vector3 velocity){
    setVelocityInitializer(velocity, velocity);
}

void Particles::setVelocityInitializer(Vector3 minVelocity, Vector3 maxVelocity){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.velocityInitializer.minVelocity = minVelocity;
    partAnim.velocityInitializer.maxVelocity = maxVelocity;
}

void Particles::setVelocityModifier(float fromTime, float toTime, Vector3 fromVelocity, Vector3 toVelocity){
    setVelocityModifier(fromTime, toTime, fromVelocity, toVelocity, EaseType::LINEAR);
}

void Particles::setVelocityModifier(float fromTime, float toTime, Vector3 fromVelocity, Vector3 toVelocity, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.velocityModifier.fromTime = fromTime;
    partAnim.velocityModifier.toTime = toTime;
    partAnim.velocityModifier.fromVelocity = fromVelocity;
    partAnim.velocityModifier.toVelocity = toVelocity;
    partAnim.velocityModifier.function = Ease::getFunction(functionType);
}

void Particles::setAccelerationInitializer(Vector3 acceleration){
    setAccelerationInitializer(acceleration, acceleration);
}

void Particles::setAccelerationInitializer(Vector3 minAcceleration, Vector3 maxAcceleration){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.accelerationInitializer.minAcceleration = minAcceleration;
    partAnim.accelerationInitializer.maxAcceleration = maxAcceleration;
}

void Particles::setAccelerationModifier(float fromTime, float toTime, Vector3 fromAcceleration, Vector3 toAcceleration){
    setAccelerationModifier(fromTime, toTime, fromAcceleration, toAcceleration, EaseType::LINEAR);
}

void Particles::setAccelerationModifier(float fromTime, float toTime, Vector3 fromAcceleration, Vector3 toAcceleration, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.accelerationModifier.fromTime = fromTime;
    partAnim.accelerationModifier.toTime = toTime;
    partAnim.accelerationModifier.fromAcceleration = fromAcceleration;
    partAnim.accelerationModifier.toAcceleration = toAcceleration;
    partAnim.accelerationModifier.function = Ease::getFunction(functionType);
}

void Particles::setColorInitializer(Vector3 color){
    setColorInitializer(color, color);
}

void Particles::setColorInitializer(Vector3 minColor, Vector3 maxColor){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.colorInitializer.minColor = minColor;
    partAnim.colorInitializer.maxColor = maxColor;
}

void Particles::setColorModifier(float fromTime, float toTime, Vector3 fromColor, Vector3 toColor){
    setColorModifier(fromTime, toTime, fromColor, toColor, EaseType::LINEAR);
}

void Particles::setColorModifier(float fromTime, float toTime, Vector3 fromColor, Vector3 toColor, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.colorModifier.fromTime = fromTime;
    partAnim.colorModifier.toTime = toTime;
    partAnim.colorModifier.fromColor = fromColor;
    partAnim.colorModifier.toColor = toColor;
    partAnim.colorModifier.function = Ease::getFunction(functionType);
}

void Particles::setAlphaInitializer(float alpha){
    setAlphaInitializer(alpha, alpha);
}

void Particles::setAlphaInitializer(float minAlpha, float maxAlpha){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.alphaInitializer.minAlpha = minAlpha;
    partAnim.alphaInitializer.maxAlpha = maxAlpha;
}

void Particles::setAlphaModifier(float fromTime, float toTime, float fromAlpha, float toAlpha){
    setAlphaModifier(fromTime, toTime, fromAlpha, toAlpha, EaseType::LINEAR);
}

void Particles::setAlphaModifier(float fromTime, float toTime, float fromAlpha, float toAlpha, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.alphaModifier.fromTime = fromTime;
    partAnim.alphaModifier.toTime = toTime;
    partAnim.alphaModifier.fromAlpha = fromAlpha;
    partAnim.alphaModifier.toAlpha = toAlpha;
    partAnim.alphaModifier.function = Ease::getFunction(functionType);
}

void Particles::setSizeInitializer(float size){
    setSizeInitializer(size, size);
}

void Particles::setSizeInitializer(float minSize, float maxSize){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.sizeInitializer.minSize = minSize;
    partAnim.sizeInitializer.maxSize = maxSize;
}

void Particles::setSizeModifier(float fromTime, float toTime, float fromSize, float toSize){
    setSizeModifier(fromTime, toTime, fromSize, toSize, EaseType::LINEAR);
}

void Particles::setSizeModifier(float fromTime, float toTime, float fromSize, float toSize, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.sizeModifier.fromTime = fromTime;
    partAnim.sizeModifier.toTime = toTime;
    partAnim.sizeModifier.fromSize = fromSize;
    partAnim.sizeModifier.toSize = toSize;
    partAnim.sizeModifier.function = Ease::getFunction(functionType);
}

void Particles::setSpriteIntializer(std::vector<int> frames){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.spriteInitializer.frames = frames;
}

void Particles::setSpriteIntializer(int minFrame, int maxFrame){
    if (minFrame <= maxFrame){
        ParticlesComponent& partAnim = getComponent<ParticlesComponent>();
        partAnim.spriteInitializer.frames.clear();
        for (int i = minFrame; i <= maxFrame; i++){
            partAnim.spriteInitializer.frames.push_back(i);
        }
    }
}

void Particles::setSpriteModifier(float fromTime, float toTime, std::vector<int> frames){
    setSpriteModifier(fromTime, toTime, frames, EaseType::LINEAR);
}

void Particles::setSpriteModifier(float fromTime, float toTime, std::vector<int> frames, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.spriteModifier.fromTime = fromTime;
    partAnim.spriteModifier.toTime = toTime;
    partAnim.spriteModifier.frames = frames;
    partAnim.spriteModifier.function = Ease::getFunction(functionType);
}

void Particles::setRotationInitializer(Quaternion rotation){
    setRotationInitializer(rotation, rotation);
}

void Particles::setRotationInitializer(float rotation){
    setRotationInitializer(rotation, rotation);
}

void Particles::setRotationInitializer(Quaternion minRotation, Quaternion maxRotation){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.rotationInitializer.minRotation = minRotation;
    partAnim.rotationInitializer.maxRotation = maxRotation;
}

void Particles::setRotationInitializer(float minRotation, float maxRotation){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.rotationInitializer.minRotation = Quaternion(0, 0, minRotation);
    partAnim.rotationInitializer.maxRotation = Quaternion(0, 0, maxRotation);
}

void Particles::setRotationModifier(float fromTime, float toTime, float fromRotation, float toRotation){
    setRotationModifier(fromTime, toTime, fromRotation, toRotation, EaseType::LINEAR);
}

void Particles::setRotationModifier(float fromTime, float toTime, Quaternion fromRotation, Quaternion toRotation){
    setRotationModifier(fromTime, toTime, fromRotation, toRotation, EaseType::LINEAR);
}

void Particles::setRotationModifier(float fromTime, float toTime, float fromRotation, float toRotation, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.rotationModifier.fromTime = fromTime;
    partAnim.rotationModifier.toTime = toTime;
    partAnim.rotationModifier.fromRotation = Quaternion(0, 0, fromRotation);
    partAnim.rotationModifier.toRotation = Quaternion(0, 0, toRotation);;
    partAnim.rotationModifier.function = Ease::getFunction(functionType);
}

void Particles::setRotationModifier(float fromTime, float toTime, Quaternion fromRotation, Quaternion toRotation, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.rotationModifier.fromTime = fromTime;
    partAnim.rotationModifier.toTime = toTime;
    partAnim.rotationModifier.fromRotation = fromRotation;
    partAnim.rotationModifier.toRotation = toRotation;
    partAnim.rotationModifier.function = Ease::getFunction(functionType);
}

void Particles::setScaleInitializer(float scale){
    setScaleInitializer(scale, scale);
}

void Particles::setScaleInitializer(Vector3 scale){
    setScaleInitializer(scale, scale);
}

void Particles::setScaleInitializer(float minScale, float maxScale){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.scaleInitializer.minScale = Vector3(minScale, minScale, minScale);
    partAnim.scaleInitializer.maxScale = Vector3(maxScale, maxScale, maxScale);
}

void Particles::setScaleInitializer(Vector3 minScale, Vector3 maxScale){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.scaleInitializer.minScale = minScale;
    partAnim.scaleInitializer.maxScale = maxScale;
}

void Particles::setScaleModifier(float fromTime, float toTime, float fromScale, float toScale){
    setScaleModifier(fromTime, toTime, Vector3(fromScale, fromScale, fromScale), Vector3(toScale, toScale, toScale), EaseType::LINEAR);
}

void Particles::setScaleModifier(float fromTime, float toTime, Vector3 fromScale, Vector3 toScale){
    setScaleModifier(fromTime, toTime, fromScale, toScale, EaseType::LINEAR);
}

void Particles::setScaleModifier(float fromTime, float toTime, Vector3 fromScale, Vector3 toScale, EaseType functionType){
    ParticlesComponent& partAnim = getComponent<ParticlesComponent>();

    partAnim.scaleModifier.fromTime = fromTime;
    partAnim.scaleModifier.toTime = toTime;
    partAnim.scaleModifier.fromScale = fromScale;
    partAnim.scaleModifier.toScale = toScale;
    partAnim.scaleModifier.function = Ease::getFunction(functionType);
}

