//
// (c) 2026 Eduardo Doria.
//

#include "Fog.h"
#include "util/Color.h"

using namespace doriax;

Fog::Fog(Scene* scene): EntityHandle(scene){
    addComponent<FogComponent>();
}

Fog::Fog(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

Fog::~Fog(){
}

FogType Fog::getType() const{
    FogComponent& fog = getComponent<FogComponent>();
    return fog.type;
}

Vector3 Fog::getColor() const{
    FogComponent& fog = getComponent<FogComponent>();
    return Color::linearTosRGB(fog.color);
}

float Fog::getDensity() const{
    FogComponent& fog = getComponent<FogComponent>();
    return fog.density;
}

float Fog::getLinearStart() const{
    FogComponent& fog = getComponent<FogComponent>();
    return fog.linearStart;
}

float Fog::getLinearEnd() const{
    FogComponent& fog = getComponent<FogComponent>();
    return fog.linearEnd;
}

void Fog::setType(FogType type){
    FogComponent& fog = getComponent<FogComponent>();
    fog.type = type;
}

void Fog::setColor(Vector3 color){
    FogComponent& fog = getComponent<FogComponent>();
    fog.color = Color::sRGBToLinear(color);
}

void Fog::setColor(float red, float green, float blue){
    setColor(Vector3(red, green, blue));
}

void Fog::setDensity(float density){
    FogComponent& fog = getComponent<FogComponent>();
    fog.density = density;
}

void Fog::setLinearStart(float start){
    FogComponent& fog = getComponent<FogComponent>();
    fog.linearStart = start;
}

void Fog::setLinearEnd(float end){
    FogComponent& fog = getComponent<FogComponent>();
    fog.linearEnd = end;
}

void Fog::setLinearStartEnd(float start, float end){
    FogComponent& fog = getComponent<FogComponent>();
    fog.linearStart = start;
    fog.linearEnd = end;
}