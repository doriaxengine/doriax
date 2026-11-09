// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Water.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Water::Water(Scene* scene): Object(scene){
    addComponent<WaterComponent>();
}

Water::Water(Scene* scene, Entity entity): Object(scene, entity){
}

Water::~Water(){
}

bool Water::load(){
    WaterComponent& water = getComponent<WaterComponent>();

    auto renderSystem = scene->getSystem<RenderSystem>();
    return renderSystem->loadWater(entity, water, renderSystem->getScenePipelines());
}

void Water::setSize(Vector2 size){
    WaterComponent& water = getComponent<WaterComponent>();

    if (water.size != size){
        water.size = size;
        water.needReload = true;
    }
}

void Water::setSize(const float width, const float depth){
    setSize(Vector2(width, depth));
}

Vector2 Water::getSize() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.size;
}

void Water::setSubdivisions(unsigned int subdivisions){
    WaterComponent& water = getComponent<WaterComponent>();

    if (water.subdivisions != subdivisions){
        water.subdivisions = subdivisions;
        water.needReload = true;
    }
}

unsigned int Water::getSubdivisions() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.subdivisions;
}

void Water::setShallowColor(Vector3 color){
    WaterComponent& water = getComponent<WaterComponent>();

    water.shallowColor = Color::sRGBToLinear(color);
}

void Water::setShallowColor(const float r, const float g, const float b){
    setShallowColor(Vector3(r, g, b));
}

Vector3 Water::getShallowColor() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return Color::linearTosRGB(water.shallowColor);
}

void Water::setDeepColor(Vector3 color){
    WaterComponent& water = getComponent<WaterComponent>();

    water.deepColor = Color::sRGBToLinear(color);
}

void Water::setDeepColor(const float r, const float g, const float b){
    setDeepColor(Vector3(r, g, b));
}

Vector3 Water::getDeepColor() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return Color::linearTosRGB(water.deepColor);
}

void Water::setDepthFade(float depthFade){
    WaterComponent& water = getComponent<WaterComponent>();

    water.depthFade = depthFade;
}

float Water::getDepthFade() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.depthFade;
}

void Water::setWaveHeight(float waveHeight){
    WaterComponent& water = getComponent<WaterComponent>();

    water.waveHeight = waveHeight;
}

float Water::getWaveHeight() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.waveHeight;
}

void Water::setWaveLength(float waveLength){
    WaterComponent& water = getComponent<WaterComponent>();

    water.waveLength = waveLength;
}

float Water::getWaveLength() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.waveLength;
}

void Water::setWaveSpeed(float waveSpeed){
    WaterComponent& water = getComponent<WaterComponent>();

    water.waveSpeed = waveSpeed;
}

float Water::getWaveSpeed() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.waveSpeed;
}

void Water::setWaveDirection(float waveDirection){
    WaterComponent& water = getComponent<WaterComponent>();

    water.waveDirection = waveDirection;
}

float Water::getWaveDirection() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.waveDirection;
}

void Water::setWaveSteepness(float waveSteepness){
    WaterComponent& water = getComponent<WaterComponent>();

    water.waveSteepness = waveSteepness;
}

float Water::getWaveSteepness() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.waveSteepness;
}

void Water::setNormalTexture(const std::string& path){
    WaterComponent& water = getComponent<WaterComponent>();

    if (path.empty()){
        water.normalTexture = Texture();
    }else{
        water.normalTexture.setPath(path);
    }
    water.needUpdateTexture = true;
}

void Water::setNormalScale(float normalScale){
    WaterComponent& water = getComponent<WaterComponent>();

    water.normalScale = normalScale;
}

float Water::getNormalScale() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.normalScale;
}

void Water::setNormalStrength(float normalStrength){
    WaterComponent& water = getComponent<WaterComponent>();

    water.normalStrength = normalStrength;
}

float Water::getNormalStrength() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.normalStrength;
}

void Water::setRippleSpeed(float rippleSpeed){
    WaterComponent& water = getComponent<WaterComponent>();

    water.rippleSpeed = rippleSpeed;
}

float Water::getRippleSpeed() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.rippleSpeed;
}

void Water::setReflectivity(float reflectivity){
    WaterComponent& water = getComponent<WaterComponent>();

    water.reflectivity = reflectivity;
}

float Water::getReflectivity() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.reflectivity;
}

void Water::setSpecularIntensity(float specularIntensity){
    WaterComponent& water = getComponent<WaterComponent>();

    water.specularIntensity = specularIntensity;
}

float Water::getSpecularIntensity() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.specularIntensity;
}

void Water::setRoughness(float roughness){
    WaterComponent& water = getComponent<WaterComponent>();

    water.roughness = roughness;
}

float Water::getRoughness() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.roughness;
}

void Water::setFoamColor(Vector3 color){
    WaterComponent& water = getComponent<WaterComponent>();

    water.foamColor = Color::sRGBToLinear(color);
}

void Water::setFoamColor(const float r, const float g, const float b){
    setFoamColor(Vector3(r, g, b));
}

Vector3 Water::getFoamColor() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return Color::linearTosRGB(water.foamColor);
}

void Water::setShoreFoam(float shoreFoam){
    WaterComponent& water = getComponent<WaterComponent>();

    water.shoreFoam = shoreFoam;
}

float Water::getShoreFoam() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.shoreFoam;
}

void Water::setCrestFoam(float crestFoam){
    WaterComponent& water = getComponent<WaterComponent>();

    water.crestFoam = crestFoam;
}

float Water::getCrestFoam() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.crestFoam;
}

void Water::setReceiveShadows(bool receiveShadows){
    WaterComponent& water = getComponent<WaterComponent>();

    water.receiveShadows = receiveShadows;
}

bool Water::isReceiveShadows() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.receiveShadows;
}

void Water::setDepthEffects(bool depthEffects){
    WaterComponent& water = getComponent<WaterComponent>();

    water.depthEffects = depthEffects;
}

bool Water::isDepthEffects() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.depthEffects;
}

void Water::setPlanarReflection(bool planarReflection){
    WaterComponent& water = getComponent<WaterComponent>();

    water.planarReflection = planarReflection;
}

bool Water::isPlanarReflection() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.planarReflection;
}

void Water::setReflectionDistortion(float reflectionDistortion){
    WaterComponent& water = getComponent<WaterComponent>();

    water.reflectionDistortion = reflectionDistortion;
}

float Water::getReflectionDistortion() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.reflectionDistortion;
}

void Water::setRefraction(bool refraction){
    WaterComponent& water = getComponent<WaterComponent>();

    water.refraction = refraction;
}

bool Water::isRefraction() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.refraction;
}

void Water::setRefractionDistortion(float refractionDistortion){
    WaterComponent& water = getComponent<WaterComponent>();

    water.refractionDistortion = refractionDistortion;
}

float Water::getRefractionDistortion() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.refractionDistortion;
}

void Water::setCaustics(float caustics){
    WaterComponent& water = getComponent<WaterComponent>();

    water.caustics = caustics;
}

float Water::getCaustics() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.caustics;
}

void Water::setCausticsScale(float causticsScale){
    WaterComponent& water = getComponent<WaterComponent>();

    water.causticsScale = causticsScale;
}

float Water::getCausticsScale() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.causticsScale;
}

void Water::setUnderwater(bool underwater){
    WaterComponent& water = getComponent<WaterComponent>();

    water.underwater = underwater;
}

bool Water::isUnderwater() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.underwater;
}

void Water::setBuoyancy(bool buoyancy){
    WaterComponent& water = getComponent<WaterComponent>();

    water.buoyancy = buoyancy;
}

bool Water::isBuoyancy() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.buoyancy;
}

void Water::setBuoyancyDepth(float buoyancyDepth){
    WaterComponent& water = getComponent<WaterComponent>();

    water.buoyancyDepth = buoyancyDepth;
}

float Water::getBuoyancyDepth() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.buoyancyDepth;
}

void Water::setWakeArea(float wakeArea){
    WaterComponent& water = getComponent<WaterComponent>();

    water.wakeArea = wakeArea;
}

float Water::getWakeArea() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.wakeArea;
}

void Water::setWakeFade(float wakeFade){
    WaterComponent& water = getComponent<WaterComponent>();

    water.wakeFade = wakeFade;
}

float Water::getWakeFade() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.wakeFade;
}

void Water::setCustomShader(const std::string& path){
    WaterComponent& water = getComponent<WaterComponent>();

    if (water.customShader != path){
        water.customShader = path;

        water.needReload = true;
    }
}

std::string Water::getCustomShader() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.customShader;
}

void Water::setCustomUnderwaterShader(const std::string& path){
    WaterComponent& water = getComponent<WaterComponent>();

    if (water.customUnderwaterShader != path){
        water.customUnderwaterShader = path;

        water.needReload = true;
    }
}

std::string Water::getCustomUnderwaterShader() const{
    WaterComponent& water = getComponent<WaterComponent>();

    return water.customUnderwaterShader;
}

void Water::setShaderUniform(const std::string& name, const Vector4& value){
    WaterComponent& water = getComponent<WaterComponent>();

    ShaderUniforms::set(water.shaderUniforms, name, value);
    water.needUpdateShaderUniforms = true;
}

void Water::setShaderUniform(const std::string& name, const Vector3& value){
    setShaderUniform(name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Water::setShaderUniform(const std::string& name, const Vector2& value){
    setShaderUniform(name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Water::setShaderUniform(const std::string& name, float value){
    setShaderUniform(name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Water::getShaderUniform(const std::string& name) const{
    WaterComponent& water = getComponent<WaterComponent>();

    return ShaderUniforms::get(water.shaderUniforms, name);
}

bool Water::removeShaderUniform(const std::string& name){
    WaterComponent& water = getComponent<WaterComponent>();

    if (!ShaderUniforms::remove(water.shaderUniforms, name))
        return false;

    water.needUpdateShaderUniforms = true;
    return true;
}

float Water::getHeight(float x, float z) const{
    WaterComponent& water = getComponent<WaterComponent>();

    return getWorldPosition().y + RenderSystem::getWaterSurfaceOffset(water, x, z).y;
}

Vector3 Water::getNormal(float x, float z) const{
    WaterComponent& water = getComponent<WaterComponent>();

    Vector3 normal(0.0f, 1.0f, 0.0f);
    RenderSystem::getWaterSurfaceOffset(water, x, z, &normal);

    return normal;
}
