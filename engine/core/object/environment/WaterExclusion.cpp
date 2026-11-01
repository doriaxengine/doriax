// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "WaterExclusion.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

WaterExclusion::WaterExclusion(Scene* scene): Object(scene){
    addComponent<WaterExclusionComponent>();
}

WaterExclusion::WaterExclusion(Scene* scene, Entity entity): Object(scene, entity){
}

WaterExclusion::~WaterExclusion(){
}

void WaterExclusion::setShape(WaterExclusionShape shape){
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    exclusion.shape = shape;
}

WaterExclusionShape WaterExclusion::getShape() const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    return exclusion.shape;
}

void WaterExclusion::setCenter(Vector3 center){
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    exclusion.center = center;
}

void WaterExclusion::setCenter(const float x, const float y, const float z){
    setCenter(Vector3(x, y, z));
}

Vector3 WaterExclusion::getCenter() const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    return exclusion.center;
}

void WaterExclusion::setSize(Vector3 size){
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    exclusion.size = size;
}

void WaterExclusion::setSize(const float x, const float y, const float z){
    setSize(Vector3(x, y, z));
}

Vector3 WaterExclusion::getSize() const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    return exclusion.size;
}

void WaterExclusion::setWakeFoam(float wakeFoam){
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    exclusion.wakeFoam = wakeFoam;
}

float WaterExclusion::getWakeFoam() const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    return exclusion.wakeFoam;
}

void WaterExclusion::setWakeWaves(float wakeWaves){
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    exclusion.wakeWaves = wakeWaves;
}

float WaterExclusion::getWakeWaves() const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();

    return exclusion.wakeWaves;
}

bool WaterExclusion::contains(Vector3 point) const{
    WaterExclusionComponent& exclusion = getComponent<WaterExclusionComponent>();
    Transform& transform = getComponent<Transform>();

    return RenderSystem::isInsideWaterExclusion(exclusion, transform, point);
}
