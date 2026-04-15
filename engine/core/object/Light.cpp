//
// (c) 2026 Eduardo Doria.
//

#include "Light.h"

#include "util/Angle.h"
#include "util/Color.h"
#include "subsystem/RenderSystem.h"
#include <math.h>

using namespace doriax;

Light::Light(Scene* scene): Object(scene){
    addComponent<LightComponent>();
}

Light::Light(Scene* scene, Entity entity): Object(scene, entity){
}

Light::~Light(){
}

void Light::setType(LightType type){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.type != type){
        lightcomp.type = type;

        lightcomp.needUpdateShadowCamera = true;
        lightcomp.needUpdateShadowMap = true;
        scene->getSystem<RenderSystem>()->needReloadMeshes();
    }
}

LightType Light::getType() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.type;
}

void Light::setDirection(Vector3 direction){
    LightComponent& lightcomp = getComponent<LightComponent>();
    Transform& transform = getComponent<Transform>();

    if (lightcomp.direction != direction){
        lightcomp.direction = direction;

        transform.needUpdate = true;
    }
}

void Light::setDirection(const float x, const float y, const float z){
    setDirection(Vector3(x,y,z));
}

Vector3 Light::getDirection() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.direction;
}

void Light::setColor(Vector3 color){
    LightComponent& lightcomp = getComponent<LightComponent>();

    lightcomp.color = Color::sRGBToLinear(color);
}

void Light::setColor(const float r, const float g, const float b){
    setColor(Vector3(r,g,b));
}

Vector3 Light::getColor() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.color;
}

void Light::setRange(float range){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.range != range){
        lightcomp.range = range;

        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getRange() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.range;
}

void Light::setIntensity(float intensity){
    LightComponent& lightcomp = getComponent<LightComponent>();
    Transform& transform = getComponent<Transform>();

    if (intensity > 0 && lightcomp.intensity == 0){
        lightcomp.needUpdateShadowCamera = true;
    }

    lightcomp.intensity = intensity;
}

float  Light::getIntensity() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.intensity;
}

void Light::setConeAngle(float inner, float outer){
    LightComponent& lightcomp = getComponent<LightComponent>();

    float innerConeCos = cos(Angle::defaultToRad(inner / 2));
    float outerConeCos = cos(Angle::defaultToRad(outer / 2));

    if (lightcomp.innerConeCos != innerConeCos || lightcomp.outerConeCos != outerConeCos){
        lightcomp.innerConeCos = innerConeCos;
        lightcomp.outerConeCos = outerConeCos;

        lightcomp.needUpdateShadowCamera = true;
    }
}

void Light::setInnerConeAngle(float inner){
    LightComponent& lightcomp = getComponent<LightComponent>();

    float innerConeCos = cos(Angle::defaultToRad(inner / 2));

    if (lightcomp.innerConeCos != innerConeCos){
        lightcomp.innerConeCos = innerConeCos;

        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getInnerConeAngle() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.innerConeCos;
}

void Light::setOuterConeAngle(float outer){
    LightComponent& lightcomp = getComponent<LightComponent>();

    float outerConeCos = cos(Angle::defaultToRad(outer / 2));

    if (lightcomp.outerConeCos != outerConeCos){
        lightcomp.outerConeCos = outerConeCos;

        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getOuterConeAngle() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.outerConeCos;
}

void Light::setShadows(bool shadows){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.shadows != shadows){
        lightcomp.shadows = shadows;

        lightcomp.needUpdateShadowCamera = true;
        scene->getSystem<RenderSystem>()->needReloadMeshes();
    }
}

bool Light::isShadows() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.shadows;
}

void Light::setBias(float bias){
    LightComponent& lightcomp = getComponent<LightComponent>();

    lightcomp.shadowBias = bias;
}

float Light::getBias() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.shadowBias;
}

void Light::setShadowMapSize(unsigned int size){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.mapResolution != size){
        lightcomp.mapResolution = size;

        lightcomp.needUpdateShadowMap = true;
        scene->getSystem<RenderSystem>()->needReloadMeshes();
    }
}

unsigned int Light::getShadowMapSize() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.mapResolution;
}

void Light::setShadowCameraNearFar(float nearValue, float farValue){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.shadowCameraNearFar != Vector2(nearValue, farValue)){
        lightcomp.shadowCameraNearFar = Vector2(nearValue, farValue);

        lightcomp.automaticShadowCamera = false;
        lightcomp.needUpdateShadowCamera = true;
    }
}

void Light::setCameraNear(float nearValue){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.shadowCameraNearFar.x != nearValue){
        lightcomp.shadowCameraNearFar.x = nearValue;

        lightcomp.automaticShadowCamera = false;
        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getCameraNear() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.shadowCameraNearFar.x;
}

void Light::setCameraFar(float farValue){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.shadowCameraNearFar.y != farValue){
        lightcomp.shadowCameraNearFar.y = farValue;

        lightcomp.automaticShadowCamera = false;
        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getCameraFar() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.shadowCameraNearFar.y;
}

void Light::setAutomaticShadowCamera(bool automatic){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.automaticShadowCamera != automatic){
        lightcomp.automaticShadowCamera = automatic;

        lightcomp.needUpdateShadowCamera = true;
    }
}

bool Light::isAutomaticShadowCamera() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.automaticShadowCamera;
}

void Light::setNumCascades(unsigned int numCascades){
    LightComponent& lightcomp = getComponent<LightComponent>();

    if (lightcomp.numShadowCascades != numCascades){
        lightcomp.numShadowCascades = numCascades;

        lightcomp.needUpdateShadowCamera = true;
    }
}

float Light::getNumCascades() const{
    LightComponent& lightcomp = getComponent<LightComponent>();

    return lightcomp.numShadowCascades;
}