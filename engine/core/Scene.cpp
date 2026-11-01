// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Scene.h"

#include "object/Camera.h"
#include "Engine.h"

#include "object/Camera.h"

#include "subsystem/RenderSystem.h"
#include "subsystem/MeshSystem.h"
#include "subsystem/UISystem.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/AudioSystem.h"
#include "subsystem/PhysicsSystem.h"
#include "manager/BundleManager.h"
#include "object/EntityHandle.h"
#include "util/Color.h"

using namespace doriax;

Scene::Scene() : EntityRegistry() {
    init();
}

Scene::Scene(EntityPool defaultPool) : EntityRegistry(defaultPool) {
    init();
}

void Scene::init(){
    registerSystem<ActionSystem>();
    registerSystem<MeshSystem>();
    registerSystem<UISystem>();
    registerSystem<RenderSystem>();
    registerSystem<PhysicsSystem>();
    registerSystem<AudioSystem>();

    camera = NULL_ENTITY;
    defaultCamera = NULL_ENTITY;
    viewCamera = NULL_ENTITY;

    // All persistent scene configuration (background, SSAO/SSR, gravity, default shaders, ...)
    // defaults through the SceneSettings member; see SceneSettings.h.
    uiEventState = UIEventState::NOT_SET;
}

Scene::~Scene(){
    // sounds of a deleted scene would play on; not in destroy(), which also runs on view recreation
    getSystem<AudioSystem>()->stopSceneSounds();
    // a later scene at this address would find its bundle instances
    BundleManager::forgetInstances(this);
    // and a Lua wrapper collected after this would destroy its entity in it
    EntityHandle::forgetScene(this);
    destroy();
}

template<typename Component, typename Callback>
static void removeComponentSubscriptionsByTag(Scene* scene, const std::string& substring, Callback callback) {
    auto components = scene->getComponentArray<Component>();

    for (size_t i = 0; i < components->size(); i++) {
        callback(components->getComponentFromIndex(i), substring);
    }
}

void Scene::removeSubscriptionsByTag(const std::string& substring) {
    removeComponentSubscriptionsByTag<ActionComponent>(this, substring, [](ActionComponent& action, const std::string& tag) {
        action.onStart.removeByTagSubstring(tag);
        action.onPause.removeByTagSubstring(tag);
        action.onStop.removeByTagSubstring(tag);
        action.onStep.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<SoundComponent>(this, substring, [](SoundComponent& sound, const std::string& tag) {
        sound.onStart.removeByTagSubstring(tag);
        sound.onPause.removeByTagSubstring(tag);
        sound.onStop.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<UIComponent>(this, substring, [](UIComponent& ui, const std::string& tag) {
        ui.onGetFocus.removeByTagSubstring(tag);
        ui.onLostFocus.removeByTagSubstring(tag);
        ui.onPointerEnter.removeByTagSubstring(tag);
        ui.onPointerLeave.removeByTagSubstring(tag);
        ui.onPointerMove.removeByTagSubstring(tag);
        ui.onPointerDown.removeByTagSubstring(tag);
        ui.onPointerUp.removeByTagSubstring(tag);
        ui.onClick.removeByTagSubstring(tag);
        ui.onDoubleClick.removeByTagSubstring(tag);
        ui.onDragStart.removeByTagSubstring(tag);
        ui.onDrag.removeByTagSubstring(tag);
        ui.onDragEnd.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<ButtonComponent>(this, substring, [](ButtonComponent& button, const std::string& tag) {
        button.onPress.removeByTagSubstring(tag);
        button.onRelease.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<PanelComponent>(this, substring, [](PanelComponent& panel, const std::string& tag) {
        panel.onMove.removeByTagSubstring(tag);
        panel.onResize.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<ScrollbarComponent>(this, substring, [](ScrollbarComponent& scrollbar, const std::string& tag) {
        scrollbar.onChange.removeByTagSubstring(tag);
    });

    removeComponentSubscriptionsByTag<TextEditComponent>(this, substring, [](TextEditComponent& textEdit, const std::string& tag) {
        textEdit.onChange.removeByTagSubstring(tag);
        textEdit.onSubmit.removeByTagSubstring(tag);
    });

    getSystem<PhysicsSystem>()->removeSubscriptionsByTag(substring);
}

void Scene::setCamera(Camera* camera){
    setCamera(camera->getEntity());
}

void Scene::setCamera(Entity camera){
    if (CameraComponent* cameracomp = findComponent<CameraComponent>(camera)){
        if (camera != this->camera){
            this->camera = camera;
            if (defaultCamera != NULL_ENTITY){
                destroyEntity(defaultCamera);
                defaultCamera = NULL_ENTITY;
            }
            cameracomp->needUpdate = true;
            updateCameraSize();
        }
    }else{
        Log::error("Invalid camera entity: need CameraComponent");
    }
}

Entity Scene::getCamera() const{
    return camera;
}

void Scene::setViewCamera(Entity camera){
    if (camera != NULL_ENTITY && !findComponent<CameraComponent>(camera)){
        Log::error("Invalid view camera entity: need CameraComponent");
        return;
    }
    if (camera != viewCamera){
        viewCamera = camera;
        if (CameraComponent* cameracomp = findComponent<CameraComponent>(getRenderCamera())){
            cameracomp->needUpdate = true;
        }
        updateCameraSize();
    }
}

Entity Scene::getRenderCamera() const{
    return (viewCamera != NULL_ENTITY) ? viewCamera : camera;
}

Entity Scene::createDefaultCamera(){
    defaultCamera = createSystemEntity();
    addComponent<CameraComponent>(defaultCamera);
    addComponent<Transform>(defaultCamera);

    CameraComponent& camera = getComponent<CameraComponent>(defaultCamera);
    camera.type = CameraType::CAMERA_UI;
    camera.transparentSort = false;
    camera.depthTest = false;

    Transform& cameratransform = getComponent<Transform>(defaultCamera);
    cameratransform.position = Vector3(0.0, 0.0, 1.0);

    return defaultCamera;
}

void Scene::setBackgroundColor(Vector4 color){
    settings.backgroundColor = color;
}

void Scene::setBackgroundColor(float red, float green, float blue){
    setBackgroundColor(Vector4(red, green, blue, 1.0));
}

void Scene::setBackgroundColor(float red, float green, float blue, float alpha){
    setBackgroundColor(Vector4(red, green, blue, alpha));
}

Vector4 Scene::getBackgroundColor() const{
    return settings.backgroundColor;
}

void Scene::setShadowQuality(ShadowQuality quality){
    // uniform-driven (no shader variant), so no mesh reload is needed
    settings.shadowQuality = quality;
}

ShadowQuality Scene::getShadowQuality() const{
    return settings.shadowQuality;
}

void Scene::setMeshLodEnabled(bool enabled){
    settings.meshLodEnabled = enabled;
}

bool Scene::isMeshLodEnabled() const{
    return settings.meshLodEnabled;
}

void Scene::setMeshLodThreshold(float pixels){
    settings.meshLodThreshold = std::max(pixels, 0.0f);
}

float Scene::getMeshLodThreshold() const{
    return settings.meshLodThreshold;
}

void Scene::setDepthPrepassEnabled(bool enabled){
    if (settings.depthPrepassEnabled != enabled){
        settings.depthPrepassEnabled = enabled;
        // the prepass pipelines are built with the mesh
        getSystem<RenderSystem>()->needReloadMeshes();
    }
}

bool Scene::isDepthPrepassEnabled() const{
    return settings.depthPrepassEnabled;
}

void Scene::setSSAOEnabled(bool ssaoEnabled){
    if (settings.ssaoEnabled != ssaoEnabled){
        settings.ssaoEnabled = ssaoEnabled;
        // toggling SSAO changes the mesh shader variant (USE_SSAO), so recompile
        getSystem<RenderSystem>()->needReloadMeshes();
    }
}

bool Scene::isSSAOEnabled() const{
    return settings.ssaoEnabled;
}

void Scene::setSSAORadius(float radius){
    settings.ssaoRadius = radius;
}

float Scene::getSSAORadius() const{
    return settings.ssaoRadius;
}

void Scene::setSSAOIntensity(float intensity){
    settings.ssaoIntensity = intensity;
}

float Scene::getSSAOIntensity() const{
    return settings.ssaoIntensity;
}

void Scene::setSSAOBias(float bias){
    settings.ssaoBias = bias;
}

float Scene::getSSAOBias() const{
    return settings.ssaoBias;
}

void Scene::setSSAODebug(bool debug){
    settings.ssaoDebug = debug;
}

bool Scene::isSSAODebug() const{
    return settings.ssaoDebug;
}

void Scene::setSSREnabled(bool ssrEnabled){
    if (settings.ssrEnabled != ssrEnabled){
        settings.ssrEnabled = ssrEnabled;
        // SSR requires the depth pre-pass, so meshes must (re)build their depth shader
        getSystem<RenderSystem>()->needReloadMeshes();
    }
}

bool Scene::isSSREnabled() const{
    return settings.ssrEnabled;
}

void Scene::setSSRMaxDistance(float maxDistance){
    settings.ssrMaxDistance = maxDistance;
}

float Scene::getSSRMaxDistance() const{
    return settings.ssrMaxDistance;
}

void Scene::setSSRThickness(float thickness){
    settings.ssrThickness = thickness;
}

float Scene::getSSRThickness() const{
    return settings.ssrThickness;
}

void Scene::setSSRMaxSteps(int maxSteps){
    settings.ssrMaxSteps = maxSteps;
}

int Scene::getSSRMaxSteps() const{
    return settings.ssrMaxSteps;
}

void Scene::setSSRIntensity(float intensity){
    settings.ssrIntensity = intensity;
}

float Scene::getSSRIntensity() const{
    return settings.ssrIntensity;
}

void Scene::setSSRBlur(float blur){
    settings.ssrBlur = blur;
}

float Scene::getSSRBlur() const{
    return settings.ssrBlur;
}

void Scene::setSSRDebugMode(int mode){
    settings.ssrDebugMode = mode;
}

int Scene::getSSRDebugMode() const{
    return settings.ssrDebugMode;
}

void Scene::setFixedResolutionEnabled(bool fixedResolutionEnabled){
    if (settings.fixedResolutionEnabled != fixedResolutionEnabled){
        settings.fixedResolutionEnabled = fixedResolutionEnabled;
        // the offscreen target renders with PIP_RTT; (re)bake it on all
        // renderables so a runtime toggle works in exported builds too
        getSystem<RenderSystem>()->needReloadMeshes();
        getSystem<RenderSystem>()->needReloadUIs();
        getSystem<RenderSystem>()->needReloadSky();
        getSystem<RenderSystem>()->needReloadPoints();
        getSystem<RenderSystem>()->needReloadLines();
    }
}

bool Scene::isFixedResolutionEnabled() const{
    return settings.fixedResolutionEnabled;
}

void Scene::setFixedResolutionWidth(unsigned int width){
    settings.fixedResolutionWidth = width;
}

unsigned int Scene::getFixedResolutionWidth() const{
    return settings.fixedResolutionWidth;
}

void Scene::setFixedResolutionHeight(unsigned int height){
    settings.fixedResolutionHeight = height;
}

unsigned int Scene::getFixedResolutionHeight() const{
    return settings.fixedResolutionHeight;
}

void Scene::setFixedResolutionSize(unsigned int width, unsigned int height){
    settings.fixedResolutionWidth = width;
    settings.fixedResolutionHeight = height;
}

void Scene::setFixedResolutionFilter(TextureFilter filter){
    settings.fixedResolutionFilter = filter;
}

TextureFilter Scene::getFixedResolutionFilter() const{
    return settings.fixedResolutionFilter;
}

void Scene::setLightState(LightState state){
    if (settings.lightState != state){
        settings.lightState = state;
        getSystem<RenderSystem>()->needReloadMeshes();
    }
}

LightState Scene::getLightState() const{
    return settings.lightState;
}

void Scene::setGlobalIllumination(float intensity, Vector3 color){
    settings.globalIllumIntensity = intensity;
    settings.globalIllumColor = Color::sRGBToLinear(color);
}

void Scene::setGlobalIllumination(float intensity){
    settings.globalIllumIntensity = intensity;
}

void Scene::setGlobalIllumination(Vector3 color){
    settings.globalIllumColor = Color::sRGBToLinear(color);
}

float Scene::getGlobalIlluminationIntensity() const{
    return settings.globalIllumIntensity;
}

Vector3 Scene::getGlobalIlluminationColor() const{
    return Color::linearTosRGB(settings.globalIllumColor);
}

Vector3 Scene::getGlobalIlluminationColorLinear() const{
    return settings.globalIllumColor;
}

void Scene::setAmbientLight2D(float intensity, Vector3 color){
    settings.ambientLight2DIntensity = intensity;
    settings.ambientLight2DColor = Color::sRGBToLinear(color);
}

void Scene::setAmbientLight2D(float intensity){
    settings.ambientLight2DIntensity = intensity;
}

void Scene::setAmbientLight2D(Vector3 color){
    settings.ambientLight2DColor = Color::sRGBToLinear(color);
}

float Scene::getAmbientLight2DIntensity() const{
    return settings.ambientLight2DIntensity;
}

Vector3 Scene::getAmbientLight2DColor() const{
    return Color::linearTosRGB(settings.ambientLight2DColor);
}

Vector3 Scene::getAmbientLight2DColorLinear() const{
    return settings.ambientLight2DColor;
}

void Scene::setShadow2DQuality(ShadowQuality quality){
    settings.shadow2DQuality = quality;
}

ShadowQuality Scene::getShadow2DQuality() const{
    return settings.shadow2DQuality;
}

Vector2 Scene::getGravity2D() const{
    // getSystem is non-const but this lookup does not mutate the scene
    return const_cast<Scene*>(this)->getSystem<PhysicsSystem>()->getGravity2D();
}

void Scene::setGravity2D(Vector2 gravity){
    getSystem<PhysicsSystem>()->setGravity2D(gravity);
}

void Scene::setGravity2D(float x, float y){
    getSystem<PhysicsSystem>()->setGravity2D(x, y);
}

Vector3 Scene::getGravity3D() const{
    return const_cast<Scene*>(this)->getSystem<PhysicsSystem>()->getGravity3D();
}

void Scene::setGravity3D(Vector3 gravity){
    getSystem<PhysicsSystem>()->setGravity3D(gravity);
}

void Scene::setGravity3D(float x, float y, float z){
    getSystem<PhysicsSystem>()->setGravity3D(x, y, z);
}

void Scene::setDefaultMeshShader(const std::string& path){
    if (settings.defaultMeshShader != path){
        settings.defaultMeshShader = path;
        getSystem<RenderSystem>()->needReloadMeshes();
    }
}

const std::string& Scene::getDefaultMeshShader() const{
    return settings.defaultMeshShader;
}

void Scene::setDefaultUIShader(const std::string& path){
    if (settings.defaultUIShader != path){
        settings.defaultUIShader = path;
        getSystem<RenderSystem>()->needReloadUIs();
    }
}

const std::string& Scene::getDefaultUIShader() const{
    return settings.defaultUIShader;
}

void Scene::setDefaultSkyShader(const std::string& path){
    if (settings.defaultSkyShader != path){
        settings.defaultSkyShader = path;
        getSystem<RenderSystem>()->needReloadSky();
    }
}

const std::string& Scene::getDefaultSkyShader() const{
    return settings.defaultSkyShader;
}

void Scene::setDefaultPointsShader(const std::string& path){
    if (settings.defaultPointsShader != path){
        settings.defaultPointsShader = path;
        getSystem<RenderSystem>()->needReloadPoints();
    }
}

const std::string& Scene::getDefaultPointsShader() const{
    return settings.defaultPointsShader;
}

void Scene::setDefaultLinesShader(const std::string& path){
    if (settings.defaultLinesShader != path){
        settings.defaultLinesShader = path;
        getSystem<RenderSystem>()->needReloadLines();
    }
}

const std::string& Scene::getDefaultLinesShader() const{
    return settings.defaultLinesShader;
}

void Scene::setDefaultWaterShader(const std::string& path){
    if (settings.defaultWaterShader != path){
        settings.defaultWaterShader = path;
        getSystem<RenderSystem>()->needReloadWater();
    }
}

const std::string& Scene::getDefaultWaterShader() const{
    return settings.defaultWaterShader;
}

void Scene::setDefaultCustomShader(ShaderType type, const std::string& path){
    switch (type){
        case ShaderType::MESH:   setDefaultMeshShader(path);   break;
        case ShaderType::UI:     setDefaultUIShader(path);     break;
        case ShaderType::SKYBOX: setDefaultSkyShader(path);    break;
        case ShaderType::POINTS: setDefaultPointsShader(path); break;
        case ShaderType::LINES:  setDefaultLinesShader(path);  break;
        case ShaderType::WATER:  setDefaultWaterShader(path);  break;
        default: break; // internal pass types cannot have scene defaults
    }
}

const std::string& Scene::getDefaultCustomShader(ShaderType type) const{
    static const std::string empty;
    switch (type){
        case ShaderType::MESH:   return settings.defaultMeshShader;
        case ShaderType::UI:     return settings.defaultUIShader;
        case ShaderType::SKYBOX: return settings.defaultSkyShader;
        case ShaderType::POINTS: return settings.defaultPointsShader;
        case ShaderType::LINES:  return settings.defaultLinesShader;
        case ShaderType::WATER:  return settings.defaultWaterShader;
        default: return empty;
    }
}

// null for the types without a scene default
static ShaderUniformValues* defaultShaderUniformsField(SceneSettings& settings, ShaderType type){
    switch (type){
        case ShaderType::MESH:   return &settings.defaultMeshShaderUniforms;
        case ShaderType::UI:     return &settings.defaultUIShaderUniforms;
        case ShaderType::SKYBOX: return &settings.defaultSkyShaderUniforms;
        case ShaderType::POINTS: return &settings.defaultPointsShaderUniforms;
        case ShaderType::LINES:  return &settings.defaultLinesShaderUniforms;
        case ShaderType::WATER:  return &settings.defaultWaterShaderUniforms;
        default: return nullptr;
    }
}

void Scene::setDefaultShaderUniforms(ShaderType type, const ShaderUniformValues& values){
    ShaderUniformValues* field = defaultShaderUniformsField(settings, type);
    if (!field)
        return;

    *field = values;
    getSystem<RenderSystem>()->needUpdateDefaultShaderUniforms(type);
}

const ShaderUniformValues& Scene::getDefaultShaderUniforms(ShaderType type) const{
    static const ShaderUniformValues empty;
    switch (type){
        case ShaderType::MESH:   return settings.defaultMeshShaderUniforms;
        case ShaderType::UI:     return settings.defaultUIShaderUniforms;
        case ShaderType::SKYBOX: return settings.defaultSkyShaderUniforms;
        case ShaderType::POINTS: return settings.defaultPointsShaderUniforms;
        case ShaderType::LINES:  return settings.defaultLinesShaderUniforms;
        case ShaderType::WATER:  return settings.defaultWaterShaderUniforms;
        default: return empty;
    }
}

void Scene::setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector4& value){
    ShaderUniformValues* field = defaultShaderUniformsField(settings, type);
    if (field && ShaderUniforms::set(*field, name, value))
        getSystem<RenderSystem>()->needUpdateDefaultShaderUniforms(type);
}

void Scene::setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector3& value){
    setDefaultShaderUniform(type, name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Scene::setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector2& value){
    setDefaultShaderUniform(type, name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Scene::setDefaultShaderUniform(ShaderType type, const std::string& name, float value){
    setDefaultShaderUniform(type, name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Scene::getDefaultShaderUniform(ShaderType type, const std::string& name) const{
    return ShaderUniforms::get(getDefaultShaderUniforms(type), name);
}

bool Scene::removeDefaultShaderUniform(ShaderType type, const std::string& name){
    ShaderUniformValues* field = defaultShaderUniformsField(settings, type);
    if (!field || !ShaderUniforms::remove(*field, name))
        return false;

    getSystem<RenderSystem>()->needUpdateDefaultShaderUniforms(type);
    return true;
}

// null for the types without a scene default
static std::vector<std::pair<std::string, Texture>>* defaultShaderTexturesField(SceneSettings& settings, ShaderType type){
    switch (type){
        case ShaderType::MESH:   return &settings.defaultMeshShaderCustomTextures;
        case ShaderType::UI:     return &settings.defaultUIShaderCustomTextures;
        case ShaderType::SKYBOX: return &settings.defaultSkyShaderCustomTextures;
        case ShaderType::POINTS: return &settings.defaultPointsShaderCustomTextures;
        case ShaderType::LINES:  return &settings.defaultLinesShaderCustomTextures;
        case ShaderType::WATER:  return &settings.defaultWaterShaderCustomTextures;
        default: return nullptr;
    }
}

void Scene::setDefaultShaderTextures(ShaderType type, const std::vector<std::pair<std::string, Texture>>& textures){
    std::vector<std::pair<std::string, Texture>>* field = defaultShaderTexturesField(settings, type);
    if (!field)
        return;

    *field = textures;
    getSystem<RenderSystem>()->needUpdateDefaultShaderCustomTextures(type);
}

const std::vector<std::pair<std::string, Texture>>& Scene::getDefaultShaderTextures(ShaderType type) const{
    static const std::vector<std::pair<std::string, Texture>> empty;
    switch (type){
        case ShaderType::MESH:   return settings.defaultMeshShaderCustomTextures;
        case ShaderType::UI:     return settings.defaultUIShaderCustomTextures;
        case ShaderType::SKYBOX: return settings.defaultSkyShaderCustomTextures;
        case ShaderType::POINTS: return settings.defaultPointsShaderCustomTextures;
        case ShaderType::LINES:  return settings.defaultLinesShaderCustomTextures;
        case ShaderType::WATER:  return settings.defaultWaterShaderCustomTextures;
        default: return empty;
    }
}

void Scene::setDefaultShaderTexture(ShaderType type, const std::string& name, const Texture& texture){
    std::vector<std::pair<std::string, Texture>>* field = defaultShaderTexturesField(settings, type);
    if (!field)
        return;

    auto it = std::find_if(field->begin(), field->end(),
        [&](const auto& e){ return e.first == name; });
    if (it != field->end()){
        if (it->second == texture)
            return;
        it->second = texture;
    }else{
        field->push_back({name, texture});
    }

    getSystem<RenderSystem>()->needUpdateDefaultShaderCustomTextures(type);
}

Texture Scene::getDefaultShaderTexture(ShaderType type, const std::string& name) const{
    const auto& textures = getDefaultShaderTextures(type);
    auto it = std::find_if(textures.begin(), textures.end(),
        [&](const auto& e){ return e.first == name; });
    return it != textures.end() ? it->second : Texture();
}

bool Scene::removeDefaultShaderTexture(ShaderType type, const std::string& name){
    std::vector<std::pair<std::string, Texture>>* field = defaultShaderTexturesField(settings, type);
    if (!field)
        return false;

    auto it = std::find_if(field->begin(), field->end(),
        [&](const auto& e){ return e.first == name; });
    if (it == field->end())
        return false;

    field->erase(it);
    getSystem<RenderSystem>()->needUpdateDefaultShaderCustomTextures(type);
    return true;
}

const std::vector<std::pair<std::string, Texture>>* Scene::getInheritedShaderTextures(ShaderType type, const std::string& customShader) const{
    if (!customShader.empty() || getDefaultCustomShader(type).empty())
        return nullptr;

    return &getDefaultShaderTextures(type);
}

const ShaderUniformValues* Scene::getInheritedShaderUniforms(ShaderType type, const std::string& customShader) const{
    if (!customShader.empty() || getDefaultCustomShader(type).empty())
        return nullptr;

    return &getDefaultShaderUniforms(type);
}

const std::vector<PostProcessPass>& Scene::getPostProcessPasses() const{
    return settings.postProcess;
}

void Scene::setPostProcessPasses(const std::vector<PostProcessPass>& passes){
    // only uniform values changed: keep the compiled chain
    bool sameChain = passes.size() == settings.postProcess.size();
    for (size_t i = 0; sameChain && i < passes.size(); i++){
        sameChain = passes[i].shader == settings.postProcess[i].shader &&
                    passes[i].enabled == settings.postProcess[i].enabled &&
                    passes[i].customTextures == settings.postProcess[i].customTextures;
    }

    settings.postProcess = passes;

    if (sameChain){
        getSystem<RenderSystem>()->needUpdatePostProcessUniforms();
    }else{
        getSystem<RenderSystem>()->needReloadPostProcess();
    }
}

void Scene::setPostProcessUniform(unsigned int index, const std::string& name, const Vector4& value){
    if (index >= settings.postProcess.size()){
        Log::error("Post-process pass %u does not exist, cannot set uniform '%s'", index, name.c_str());
        return;
    }

    if (ShaderUniforms::set(settings.postProcess[index].uniforms, name, value))
        getSystem<RenderSystem>()->needUpdatePostProcessUniforms();
}

void Scene::setPostProcessUniform(unsigned int index, const std::string& name, const Vector3& value){
    setPostProcessUniform(index, name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Scene::setPostProcessUniform(unsigned int index, const std::string& name, const Vector2& value){
    setPostProcessUniform(index, name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Scene::setPostProcessUniform(unsigned int index, const std::string& name, float value){
    setPostProcessUniform(index, name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Scene::getPostProcessUniform(unsigned int index, const std::string& name) const{
    if (index >= settings.postProcess.size())
        return Vector4::ZERO;

    return ShaderUniforms::get(settings.postProcess[index].uniforms, name);
}

bool Scene::removePostProcessUniform(unsigned int index, const std::string& name){
    if (index >= settings.postProcess.size())
        return false;

    if (!ShaderUniforms::remove(settings.postProcess[index].uniforms, name))
        return false;

    getSystem<RenderSystem>()->needUpdatePostProcessUniforms();
    return true;
}

void Scene::setPostProcessPassEnabled(unsigned int index, bool enabled){
    if (index >= settings.postProcess.size()){
        Log::error("Post-process pass %u does not exist, cannot enable it", index);
        return;
    }
    if (settings.postProcess[index].enabled == enabled)
        return;

    settings.postProcess[index].enabled = enabled;
    getSystem<RenderSystem>()->needReloadPostProcess();
}

bool Scene::isPostProcessPassEnabled(unsigned int index) const{
    if (index >= settings.postProcess.size())
        return false;

    return settings.postProcess[index].enabled;
}

bool Scene::canReceiveUIEvents(){
    switch (this->uiEventState) {
        case UIEventState::ENABLED:
            return true;
        case UIEventState::DISABLED:
            return false;
        case UIEventState::NOT_SET:
        default:
            Signature cameraSignature = getSignature(getCamera());
            if (cameraSignature.test(getComponentId<CameraComponent>())){
                return getComponent<CameraComponent>(getCamera()).type == CameraType::CAMERA_UI;
            }

            return false;
    }
}

UIEventState Scene::getEnableUIEvents() const{
    return this->uiEventState;
}

void Scene::setEnableUIEvents(UIEventState enableUIEvents){
    this->uiEventState = enableUIEvents;
}

void Scene::enableUIEvents(){
    this->uiEventState = UIEventState::ENABLED;
}

bool Scene::isEnableUIEvents() const{
    return this->uiEventState != UIEventState::DISABLED;
}

void Scene::setEnableUIEvents(bool enableUIEvents){
    this->uiEventState = enableUIEvents ? UIEventState::ENABLED : UIEventState::DISABLED;
}

void Scene::load(){
    if (camera == NULL_ENTITY){
        camera = createDefaultCamera();
    }

    for (auto const &pair: systems) {
        if (Engine::isViewLoaded()){
            pair.second->load();
        }
    }
}

void Scene::destroy(){
    for (auto const& pair : systems){
        pair.second->destroy();
    }
}

void Scene::draw(){
    for (auto const& pair : systems){
        pair.second->draw();
    }
}


void Scene::update(double dt){
    for (auto const& pair : systems){
        pair.second->update(dt);
    }
}

void Scene::fixedUpdate(double dt){
    for (auto const& pair : systems){
        pair.second->fixedUpdate(dt);
    }
}

void Scene::updateCameraSize(){
    getSystem<RenderSystem>()->updateCameraSize(getCamera());
    if (viewCamera != NULL_ENTITY && viewCamera != camera){
        getSystem<RenderSystem>()->updateCameraSize(viewCamera);
    }
}
