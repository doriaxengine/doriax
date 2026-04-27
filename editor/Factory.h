#pragma once

#include "Scene.h"
#include "Catalog.h"
#include <string>
#include <sstream>
#include <filesystem>
#include <array>
#include <set>
#include <map>
#include <unordered_map>
#include "util/EntityBundle.h"

namespace fs = std::filesystem;

namespace doriax::editor{

    class Project;

    struct BundleOverrideInfo {
        Entity sceneEntity;       // entity in the scene with override data
        std::vector<ComponentType> overriddenComponents;
    };

    struct BundleInstanceInfo {
        std::filesystem::path bundlePath;
        Entity rootEntity;
        std::set<Entity> memberEntities;
        std::vector<BundleOverrideInfo> overrides;
    };

    class Factory{
    private:
        static bool writeHeaderIfChanged(const std::filesystem::path& path, const std::string& varName, const unsigned char* data, size_t len);
        static bool ensureDefaultSkyFiles(const fs::path& baseDir);
        static void writeSkyIncludes(std::ostringstream& out, bool usesDefaultSky, const fs::path& generatedPath, const std::string& ind = "");

        static std::string indentation(int spaces);
        static std::string formatVector2(const Vector2& v);
        static std::string formatVector3(const Vector3& v);
        static std::string formatVector4(const Vector4& v);
        static std::string formatRect(const Rect& r);
        static std::string formatPrimitiveType(PrimitiveType type);
        static std::string formatPivotPreset(PivotPreset preset);
        static std::string formatAnchorPreset(AnchorPreset preset);
        static std::string formatContainerType(ContainerType type);
        static std::string formatLightType(LightType type);
        static std::string formatBodyType(BodyType type);
        static std::string formatShape2DType(Shape2DType type);
        static std::string formatShape3DType(Shape3DType type);
        static std::string formatShape3DSource(Shape3DSource source);
        static std::string formatJoint2DType(Joint2DType type);
        static std::string formatJoint3DType(Joint3DType type);
        static std::string formatActionState(ActionState state);
        static std::string formatLightState(LightState state);
        static std::string formatAudioState(AudioState state);
        static std::string formatAudioAttenuation(AudioAttenuation attenuation);
        static std::string formatUIEventState(UIEventState state);
        static std::string formatCameraType(CameraType type);
        static std::string formatScriptType(ScriptType type);
        static std::string formatQuaternion(const Quaternion& q);
        static std::string formatBool(bool value);
        static std::string formatFloat(float value);
        static std::string formatDouble(double value);
        static std::string formatInt(int value);
        static std::string formatUInt(unsigned int value);
        static std::string formatEntity(Entity entity, const std::unordered_map<Entity, std::string>* entityVarNames);
        static std::string formatString(const std::string& value);
        static std::string formatAttributeType(AttributeType type);
        static std::string formatAttributeDataType(AttributeDataType type);
        static std::string formatTextureFilter(TextureFilter filter);
        static std::string formatTextureWrap(TextureWrap wrap);
        static std::string formatScriptPropertyType(ScriptPropertyType type);
        static std::string formatScriptPropertyValue(const EntityRegistry* scene, const ScriptPropertyValue& value);

        static std::string formatTexture(int indentSpaces, const Texture& texture, const std::string& variableName, const fs::path& projectPath);

        static std::string formatPropertyValue(const PropertyData& property, const std::string& propertyName);

        static void addComponentCode(std::ostringstream& code, const std::string& ind, const std::string& sceneName, const std::string& entityName, Entity entity, const std::string& componentType, const std::string& varName, bool assignExisting = false);

        static std::vector<Entity> getBundleMemberEntities(EntityRegistry* registry, const std::vector<Entity>& registryEntities);

    public:
        Factory();

        static std::string toIdentifier(const std::string& name);

        static std::string bundleToFunctionName(const std::filesystem::path& bundlePath);
        static std::string bundleToFileName(const std::filesystem::path& bundlePath);

        static std::string createComponent(int indentSpaces, EntityRegistry* scene, Entity entity, ComponentType componentType, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createAllComponents(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createScene(int indentSpaces, Scene* scene, std::string name, std::vector<Entity> entities, Entity camera, const fs::path& projectPath, const fs::path& generatedPath, const std::vector<BundleInstanceInfo>& bundleInstances = {});

        static std::string createBundle(const std::filesystem::path& bundlePath, EntityRegistry* registry, const std::vector<Entity>& registryEntities, const fs::path& projectPath, const fs::path& generatedPath);
        static std::string createBundleHeader(const std::filesystem::path& bundlePath);

        static std::string setComponent(EntityRegistry* scene, Entity entity, ComponentType componentType, const fs::path& projectPath);
        static std::string setAllComponents(EntityRegistry* scene, Entity entity, const fs::path& projectPath);

        static std::string createTransform(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool skipParent = false, bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createMeshComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createUIComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createButtonComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createUILayoutComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createUIContainerComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createTextComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createImageComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createSpriteComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createTilemapComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createTerrainComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createLightComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createCameraComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createAudioComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createScriptComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createSkyComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createBody2DComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createBody3DComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createJoint2DComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createJoint3DComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createTimedActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createPositionActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createRotationActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createScaleActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createColorActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createAlphaActionComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createSpriteAnimationComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createAnimationComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createModelComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createBoneComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createKeyframeTracksComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createTranslateTracksComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createRotateTracksComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createScaleTracksComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createMorphTracksComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createParticlesComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createPointsComponent(int indentSpaces, EntityRegistry* scene, Entity entity, const fs::path& projectPath, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
        static std::string createInstancedMeshComponent(int indentSpaces, EntityRegistry* scene, Entity entity, std::string sceneName = "", std::string entityName = "", bool assignExisting = false, const std::unordered_map<Entity, std::string>* entityVarNames = nullptr);
    };

}