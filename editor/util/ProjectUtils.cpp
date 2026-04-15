#include "ProjectUtils.h"

#include "Out.h"
#include "Catalog.h"
#include "Stream.h"

#include "component/ActionComponent.h"
#include "component/AnimationComponent.h"
#include "component/BoneComponent.h"
#include "component/ButtonComponent.h"
#include "component/ModelComponent.h"
#include "component/Transform.h"

#include "component/TilemapComponent.h"
#include "command/type/MultiPropertyCmd.h"

#include <algorithm>
#include <cctype>

#include "EntityHandle.h"
#include "Object.h"
#include "Mesh.h"
#include "Shape.h"
#include "Model.h"
#include "Points.h"
#include "Sprite.h"
#include "Terrain.h"
#include "Light.h"
#include "Camera.h"

#include "resources/sky/Daylight_Box_Back_png.h"
#include "resources/sky/Daylight_Box_Bottom_png.h"
#include "resources/sky/Daylight_Box_Front_png.h"
#include "resources/sky/Daylight_Box_Left_png.h"
#include "resources/sky/Daylight_Box_Right_png.h"
#include "resources/sky/Daylight_Box_Top_png.h"

using namespace doriax;

void editor::ProjectUtils::setDefaultSkyTexture(Texture& outTexture) {
    TextureData skyBack;
    TextureData skyBottom;
    TextureData skyFront;
    TextureData skyLeft;
    TextureData skyRight;
    TextureData skyTop;

    skyBack.loadTextureFromMemory(Daylight_Box_Back_png, Daylight_Box_Back_png_len);
    skyBottom.loadTextureFromMemory(Daylight_Box_Bottom_png, Daylight_Box_Bottom_png_len);
    skyFront.loadTextureFromMemory(Daylight_Box_Front_png, Daylight_Box_Front_png_len);
    skyLeft.loadTextureFromMemory(Daylight_Box_Left_png, Daylight_Box_Left_png_len);
    skyRight.loadTextureFromMemory(Daylight_Box_Right_png, Daylight_Box_Right_png_len);
    skyTop.loadTextureFromMemory(Daylight_Box_Top_png, Daylight_Box_Top_png_len);

    outTexture.setId("editor:resources:default_sky");
    outTexture.setCubeDatas("editor:resources:default_sky", skyFront, skyBack, skyLeft, skyRight, skyTop, skyBottom);
}

void editor::ProjectUtils::collectModelEntities(Scene* scene, const ModelComponent& model, std::vector<Entity>& out){
    for (const auto& bone : model.bonesIdMapping){
        out.push_back(bone.second);
    }
    for (const auto& anim : model.animations){
        out.push_back(anim);
        AnimationComponent* animComp = scene->findComponent<AnimationComponent>(anim);
        if (animComp){
            for (const auto& frame : animComp->actions){
                if (frame.action != NULL_ENTITY){
                    out.push_back(frame.action);
                }
            }
        }
    }
}

Entity editor::ProjectUtils::getLockedEntityParent(Scene* scene, Entity entity){
    if (entity == NULL_ENTITY)
        return NULL_ENTITY;

    Signature signature = scene->getSignature(entity);

    auto models = scene->getComponentArray<ModelComponent>();
    for (size_t i = 0; i < models->size(); ++i) {
        Entity modelEntity = models->getEntity(i);
        ModelComponent& model = models->getComponentFromIndex(i);

        if (model.skeleton == entity) {
            return modelEntity;
        }

        for (const auto& anim : model.animations){
            if (anim == entity) {
                return modelEntity;
            }

            AnimationComponent* animComp = scene->findComponent<AnimationComponent>(anim);
            if (animComp){
                for (const auto& frame : animComp->actions){
                    if (frame.action == entity){
                        return anim;
                    }
                }
            }
        }

        for (const auto& bone : model.bonesIdMapping) {
            if (bone.second == entity) {
                Transform* transform = scene->findComponent<Transform>(entity);
                if (transform && transform->parent != NULL_ENTITY) {
                    return transform->parent;
                }
                return modelEntity;
            }
        }
    }

    if (!signature.test(scene->getComponentId<Transform>())){
        return NULL_ENTITY;
    }

    Transform& transform = scene->getComponent<Transform>(entity);
    if (transform.parent == NULL_ENTITY){
        return NULL_ENTITY;
    }

    Entity parent = transform.parent;
    Signature parentSignature = scene->getSignature(parent);

    if (parentSignature.test(scene->getComponentId<ButtonComponent>())){
        ButtonComponent& button = scene->getComponent<ButtonComponent>(parent);
        if (button.label == entity){
            return parent;
        }
    }

    return NULL_ENTITY;
}

bool editor::ProjectUtils::isEntityLocked(Scene* scene, Entity entity){
    return getLockedEntityParent(scene, entity) != NULL_ENTITY;
}

Entity editor::ProjectUtils::getEffectiveParent(Scene* scene, Entity entity) {
    Entity virtualParent = getVirtualParent(scene, entity);
    if (virtualParent != NULL_ENTITY) {
        return virtualParent;
    }

    Transform* transform = scene->findComponent<Transform>(entity);
    if (transform) {
        return transform->parent;
    }

    return getLockedEntityParent(scene, entity);
}

bool editor::ProjectUtils::canMoveLockedEntityOrder(Scene* scene, Entity source, Entity target, InsertionType type) {
    if (!isEntityLocked(scene, source)) {
        return true;
    }

    if (type == InsertionType::INTO) {
        return false;
    }

    return getEffectiveParent(scene, source) == getEffectiveParent(scene, target);
}

size_t editor::ProjectUtils::getTransformIndex(EntityRegistry* registry, Entity entity){
    Signature signature = registry->getSignature(entity);
    if (signature.test(registry->getComponentId<Transform>())) {
        Transform& transform = registry->getComponent<Transform>(entity);
        auto transforms = registry->getComponentArray<Transform>();
        return transforms->getIndex(entity);
    }

    return 0;
}

void editor::ProjectUtils::sortEntitiesByTransformOrder(EntityRegistry* registry, std::vector<Entity>& entities) {
    auto transforms = registry->getComponentArray<Transform>();
    std::unordered_map<Entity, size_t> transformOrder;
    for (size_t i = 0; i < transforms->size(); ++i) {
        Entity ent = transforms->getEntity(i);
        transformOrder[ent] = i;
    }
    std::sort(entities.begin(), entities.end(),
        [&transformOrder](const Entity& a, const Entity& b) {
            return transformOrder[a] < transformOrder[b];
        }
    );
}

bool editor::ProjectUtils::moveEntityOrderByTarget(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity target, InsertionType type, Entity& oldParent, size_t& oldIndex, bool& hasTransform) {
    Transform* transformSource = registry->findComponent<Transform>(source);
    Transform* transformTarget = registry->findComponent<Transform>(target);

    if (transformSource && transformTarget){
        hasTransform = true;

        if (registry->isParentOf(source, target)){
            Out::error("Cannot move an entity to its own child");
            return false;
        }

        auto transforms = registry->getComponentArray<Transform>();

        size_t sourceTransformIndex = transforms->getIndex(source);
        size_t targetTransformIndex = transforms->getIndex(target);

        Entity newParent = NULL_ENTITY;
        if (type == InsertionType::INTO){
            newParent = target;
        }else{
            newParent = transformTarget->parent;
        }

        oldParent = transformSource->parent;
        oldIndex = sourceTransformIndex;

        if (type == InsertionType::AFTER || type == InsertionType::INTO){
            targetTransformIndex++;
        }

        moveEntityOrderByTransform(registry, entities, source, newParent, targetTransformIndex);

    }else{

        hasTransform = false;

        auto itSource = std::find(entities.begin(), entities.end(), source);
        auto itTarget = std::find(entities.begin(), entities.end(), target);

        if (itSource == entities.end() || itTarget == entities.end()) {
            Out::error("Source or Target entity not found in entities vector");
            return false;
        }

        oldIndex = std::distance(entities.begin(), itSource);

        Entity tempSource = *itSource;
        size_t sourceIndex = std::distance(entities.begin(), itSource);
        size_t targetIndex = std::distance(entities.begin(), itTarget);

        entities.erase(itSource);

        if (type == InsertionType::BEFORE) {
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex, tempSource);
        } else if (type == InsertionType::AFTER) {
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex + 1, tempSource);
        } else if (type == InsertionType::INTO) {
            // "IN" is ambiguous without hierarchy, treat as AFTER
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex + 1, tempSource);
        }

    }

    return true;
}

void editor::ProjectUtils::moveEntityOrderByIndex(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t index, bool hasTransform){
    if (hasTransform){

        auto transforms = registry->getComponentArray<Transform>();
        size_t sourceTransformIndex = transforms->getIndex(source);

        size_t sizeOfSourceBranch = registry->findBranchLastIndex(source) - sourceTransformIndex + 1;
        if (sourceTransformIndex < index){
            index += sizeOfSourceBranch;
        }

        moveEntityOrderByTransform(registry, entities, source, parent, index);

    }else{

        auto itSource = std::find(entities.begin(), entities.end(), source);
        if (itSource == entities.end()) {
            Out::error("Source entity not found in entities vector for undo");
            return;
        }

        Entity tempSource = *itSource;
        entities.erase(itSource);

        // Clamp index for safety
        if (index > entities.size()) {
            entities.push_back(tempSource);
        } else {
            entities.insert(entities.begin() + index, tempSource);
        }

    }
}

void editor::ProjectUtils::moveEntityOrderByTransform(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t transformIndex, bool enableMove){
    registry->addEntityChild(parent, source, true);

    if (enableMove){
        registry->moveChildToIndex(source, transformIndex);
    }

    ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
}

void editor::ProjectUtils::addEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, YAML::Node componentNode){
    switch (componentType) {
        case ComponentType::Transform:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Transform>(entity, {});
            }else{
                registry->addComponent<Transform>(entity, Stream::decodeTransform(componentNode));
            }
            ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
            break;
        case ComponentType::MeshComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MeshComponent>(entity, {});
            }else{
                registry->addComponent<MeshComponent>(entity, Stream::decodeMeshComponent(componentNode));
            }
            break;
        case ComponentType::UIComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UIComponent>(entity, {});
            }else{
                registry->addComponent<UIComponent>(entity, Stream::decodeUIComponent(componentNode));
            }
            break;
        case ComponentType::UILayoutComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UILayoutComponent>(entity, {});
            }else{
                registry->addComponent<UILayoutComponent>(entity, Stream::decodeUILayoutComponent(componentNode));
            }
            break;
        case ComponentType::ActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ActionComponent>(entity, {});
            }else{
                registry->addComponent<ActionComponent>(entity, Stream::decodeActionComponent(componentNode));
            }
            break;
        case ComponentType::AlphaActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<AlphaActionComponent>(entity, {});
            }else{
                registry->addComponent<AlphaActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::AnimationComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<AnimationComponent>(entity, {});
            }else{
                registry->addComponent<AnimationComponent>(entity, Stream::decodeAnimationComponent(componentNode));
            }
            break;
        case ComponentType::AudioComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<AudioComponent>(entity, {});
            }else{
                registry->addComponent<AudioComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::Body2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Body2DComponent>(entity, {});
            }else{
                registry->addComponent<Body2DComponent>(entity, Stream::decodeBody2DComponent(componentNode));
            }
            break;
        case ComponentType::Body3DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Body3DComponent>(entity, {});
            }else{
                registry->addComponent<Body3DComponent>(entity, Stream::decodeBody3DComponent(componentNode));
            }
            break;
        case ComponentType::BoneComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<BoneComponent>(entity, {});
            }else{
                registry->addComponent<BoneComponent>(entity, Stream::decodeBoneComponent(componentNode));
            }
            break;
        case ComponentType::ButtonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ButtonComponent>(entity, {});
            }else{
                registry->addComponent<ButtonComponent>(entity, Stream::decodeButtonComponent(componentNode));
            }
            break;
        case ComponentType::BundleComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<BundleComponent>(entity, {});
            }else{
                registry->addComponent<BundleComponent>(entity, Stream::decodeBundleComponent(componentNode));
            }
            break;
        case ComponentType::CameraComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<CameraComponent>(entity, {});
            }else{
                registry->addComponent<CameraComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ColorActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ColorActionComponent>(entity, {});
            }else{
                registry->addComponent<ColorActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::FogComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<FogComponent>(entity, {});
            }else{
                registry->addComponent<FogComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ImageComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ImageComponent>(entity, {});
            }else{
                registry->addComponent<ImageComponent>(entity, Stream::decodeImageComponent(componentNode));
            }
            break;
        case ComponentType::InstancedMeshComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<InstancedMeshComponent>(entity, {});
            }else{
                registry->addComponent<InstancedMeshComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::Joint2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Joint2DComponent>(entity, {});
            }else{
                registry->addComponent<Joint2DComponent>(entity, Stream::decodeJoint2DComponent(componentNode));
            }
            break;
        case ComponentType::Joint3DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Joint3DComponent>(entity, {});
            }else{
                registry->addComponent<Joint3DComponent>(entity, Stream::decodeJoint3DComponent(componentNode));
            }
            break;
        case ComponentType::KeyframeTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<KeyframeTracksComponent>(entity, {});
            }else{
                registry->addComponent<KeyframeTracksComponent>(entity, Stream::decodeKeyframeTracksComponent(componentNode));
            }
            break;
        case ComponentType::LightComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<LightComponent>(entity, {});
            }else{
                registry->addComponent<LightComponent>(entity, Stream::decodeLightComponent(componentNode));
            }
            break;
        case ComponentType::LinesComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<LinesComponent>(entity, {});
            }else{
                registry->addComponent<LinesComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::MeshPolygonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MeshPolygonComponent>(entity, {});
            }else{
                registry->addComponent<MeshPolygonComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ModelComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ModelComponent>(entity, {});
            }else{
                registry->addComponent<ModelComponent>(entity, Stream::decodeModelComponent(componentNode));
            }
            break;
        case ComponentType::MorphTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MorphTracksComponent>(entity, {});
            }else{
                registry->addComponent<MorphTracksComponent>(entity, Stream::decodeMorphTracksComponent(componentNode));
            }
            break;
        case ComponentType::PanelComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PanelComponent>(entity, {});
            }else{
                registry->addComponent<PanelComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ParticlesComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ParticlesComponent>(entity, {});
            }else{
                registry->addComponent<ParticlesComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::PointsComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PointsComponent>(entity, {});
            }else{
                registry->addComponent<PointsComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::PolygonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PolygonComponent>(entity, {});
            }else{
                registry->addComponent<PolygonComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::PositionActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PositionActionComponent>(entity, {});
            }else{
                registry->addComponent<PositionActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::RotateTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<RotateTracksComponent>(entity, {});
            }else{
                registry->addComponent<RotateTracksComponent>(entity, Stream::decodeRotateTracksComponent(componentNode));
            }
            break;
        case ComponentType::RotationActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<RotationActionComponent>(entity, {});
            }else{
                registry->addComponent<RotationActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ScaleActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScaleActionComponent>(entity, {});
            }else{
                registry->addComponent<ScaleActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::ScaleTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScaleTracksComponent>(entity, {});
            }else{
                registry->addComponent<ScaleTracksComponent>(entity, Stream::decodeScaleTracksComponent(componentNode));
            }
            break;
        case ComponentType::ScriptComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScriptComponent>(entity, {});
            }else{
                registry->addComponent<ScriptComponent>(entity, Stream::decodeScriptComponent(componentNode));
            }
            break;
        case ComponentType::ScrollbarComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScrollbarComponent>(entity, {});
            }else{
                registry->addComponent<ScrollbarComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::SkyComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SkyComponent>(entity, {});
            }else{
                registry->addComponent<SkyComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::SpriteAnimationComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SpriteAnimationComponent>(entity, {});
            }else{
                registry->addComponent<SpriteAnimationComponent>(entity, Stream::decodeSpriteAnimationComponent(componentNode));
            }
            break;
        case ComponentType::SpriteComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SpriteComponent>(entity, {});
            }else{
                registry->addComponent<SpriteComponent>(entity, Stream::decodeSpriteComponent(componentNode));
            }
            break;
        case ComponentType::TerrainComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TerrainComponent>(entity, {});
            }else{
                registry->addComponent<TerrainComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::TextComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TextComponent>(entity, {});
            }else{
                registry->addComponent<TextComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::TextEditComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TextEditComponent>(entity, {});
            }else{
                registry->addComponent<TextEditComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::TilemapComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TilemapComponent>(entity, {});
            }else{
                registry->addComponent<TilemapComponent>(entity, Stream::decodeTilemapComponent(componentNode));
            }
            break;
        case ComponentType::TimedActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TimedActionComponent>(entity, {});
            }else{
                registry->addComponent<TimedActionComponent>(entity, {});
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            break;
        case ComponentType::TranslateTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TranslateTracksComponent>(entity, {});
            }else{
                registry->addComponent<TranslateTracksComponent>(entity, Stream::decodeTranslateTracksComponent(componentNode));
            }
            break;
        case ComponentType::UIContainerComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UIContainerComponent>(entity, {});
            }else{
                registry->addComponent<UIContainerComponent>(entity, Stream::decodeUIContainerComponent(componentNode));
            }
            break;
        default:
            break;
    }
}

Entity editor::ProjectUtils::getVirtualParent(Scene* scene, Entity entity) {
    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<ActionComponent>())) return NULL_ENTITY;
    if (signature.test(scene->getComponentId<Transform>())) return NULL_ENTITY;

    ActionComponent& action = scene->getComponent<ActionComponent>(entity);
    if (action.target != NULL_ENTITY && action.target != entity) {
        return action.target;
    }

    return NULL_ENTITY;
}

std::vector<Entity> editor::ProjectUtils::getVirtualChildren(Scene* scene, const std::vector<Entity>& parentEntities) {
    std::vector<Entity> result;
    auto actionArr = scene->getComponentArray<ActionComponent>();
    if (!actionArr) {
        return result;
    }

    // Expand parent entities to include all Transform descendants
    std::vector<Entity> allEntities = parentEntities;
    auto transforms = scene->getComponentArray<Transform>();
    if (transforms) {
        for (const Entity& parent : parentEntities) {
            if (!scene->findComponent<Transform>(parent)) {
                continue;
            }
            size_t parentIdx = transforms->getIndex(parent);
            size_t branchEnd = scene->findBranchLastIndex(parent);
            for (size_t i = parentIdx + 1; i <= branchEnd; ++i) {
                Entity descendant = transforms->getEntity(i);
                if (std::find(allEntities.begin(), allEntities.end(), descendant) == allEntities.end()) {
                    allEntities.push_back(descendant);
                }
            }
        }
    }

    bool foundNew;
    do {
        foundNew = false;
        for (size_t i = 0; i < actionArr->size(); ++i) {
            Entity entity = actionArr->getEntity(i);
            Entity vParent = getVirtualParent(scene, entity);
            if (vParent != NULL_ENTITY
                && std::find(allEntities.begin(), allEntities.end(), vParent) != allEntities.end()
                && std::find(allEntities.begin(), allEntities.end(), entity) == allEntities.end()) {
                allEntities.push_back(entity);
                result.push_back(entity);
                foundNew = true;
            }
        }
    } while (foundNew);

    return result;
}

YAML::Node editor::ProjectUtils::removeEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, bool encodeComponent){
    YAML::Node oldComponent;

    switch (componentType) {
        case ComponentType::Transform:
            if (encodeComponent){
                oldComponent = Stream::encodeTransform(registry->getComponent<Transform>(entity));
            }
            registry->removeComponent<Transform>(entity);
            ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
            break;
        case ComponentType::MeshComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMeshComponent(registry->getComponent<MeshComponent>(entity));
            }
            registry->removeComponent<MeshComponent>(entity);
            break;
        case ComponentType::UIComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUIComponent(registry->getComponent<UIComponent>(entity));
            }
            registry->removeComponent<UIComponent>(entity);
            break;
        case ComponentType::UILayoutComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUILayoutComponent(registry->getComponent<UILayoutComponent>(entity));
            }
            registry->removeComponent<UILayoutComponent>(entity);
            break;
        case ComponentType::ActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeActionComponent(registry->getComponent<ActionComponent>(entity));
            }
            registry->removeComponent<ActionComponent>(entity);
            break;
        case ComponentType::AlphaActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<AlphaActionComponent>(entity);
            break;
        case ComponentType::AnimationComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeAnimationComponent(registry->getComponent<AnimationComponent>(entity));
            }
            registry->removeComponent<AnimationComponent>(entity);
            break;
        case ComponentType::AudioComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<AudioComponent>(entity);
            break;
        case ComponentType::Body2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBody2DComponent(registry->getComponent<Body2DComponent>(entity));
            }
            registry->removeComponent<Body2DComponent>(entity);
            break;
        case ComponentType::Body3DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBody3DComponent(registry->getComponent<Body3DComponent>(entity));
            }
            registry->removeComponent<Body3DComponent>(entity);
            break;
        case ComponentType::BoneComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBoneComponent(registry->getComponent<BoneComponent>(entity));
            }
            registry->removeComponent<BoneComponent>(entity);
            break;
        case ComponentType::ButtonComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeButtonComponent(registry->getComponent<ButtonComponent>(entity));
            }
            registry->removeComponent<ButtonComponent>(entity);
            break;
        case ComponentType::BundleComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBundleComponent(registry->getComponent<BundleComponent>(entity));
            }
            registry->removeComponent<BundleComponent>(entity);
            break;
        case ComponentType::CameraComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeCameraComponent(registry->getComponent<CameraComponent>(entity));
            }
            registry->removeComponent<CameraComponent>(entity);
            break;
        case ComponentType::ColorActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<ColorActionComponent>(entity);
            break;
        case ComponentType::FogComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<FogComponent>(entity);
            break;
        case ComponentType::ImageComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeImageComponent(registry->getComponent<ImageComponent>(entity));
            }
            registry->removeComponent<ImageComponent>(entity);
            break;
        case ComponentType::InstancedMeshComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<InstancedMeshComponent>(entity);
            break;
        case ComponentType::Joint2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeJoint2DComponent(registry->getComponent<Joint2DComponent>(entity));
            }
            registry->removeComponent<Joint2DComponent>(entity);
            break;
        case ComponentType::Joint3DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeJoint3DComponent(registry->getComponent<Joint3DComponent>(entity));
            }
            registry->removeComponent<Joint3DComponent>(entity);
            break;
        case ComponentType::KeyframeTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeKeyframeTracksComponent(registry->getComponent<KeyframeTracksComponent>(entity));
            }
            registry->removeComponent<KeyframeTracksComponent>(entity);
            break;
        case ComponentType::LightComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeLightComponent(registry->getComponent<LightComponent>(entity));
            }
            registry->removeComponent<LightComponent>(entity);
            break;
        case ComponentType::LinesComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<LinesComponent>(entity);
            break;
        case ComponentType::MeshPolygonComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<MeshPolygonComponent>(entity);
            break;
        case ComponentType::ModelComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeModelComponent(registry->getComponent<ModelComponent>(entity));
            }
            registry->removeComponent<ModelComponent>(entity);
            break;
        case ComponentType::MorphTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMorphTracksComponent(registry->getComponent<MorphTracksComponent>(entity));
            }
            registry->removeComponent<MorphTracksComponent>(entity);
            break;
        case ComponentType::PanelComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<PanelComponent>(entity);
            break;
        case ComponentType::ParticlesComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<ParticlesComponent>(entity);
            break;
        case ComponentType::PointsComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<PointsComponent>(entity);
            break;
        case ComponentType::PolygonComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<PolygonComponent>(entity);
            break;
        case ComponentType::PositionActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<PositionActionComponent>(entity);
            break;
        case ComponentType::RotateTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeRotateTracksComponent(registry->getComponent<RotateTracksComponent>(entity));
            }
            registry->removeComponent<RotateTracksComponent>(entity);
            break;
        case ComponentType::RotationActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<RotationActionComponent>(entity);
            break;
        case ComponentType::ScaleActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<ScaleActionComponent>(entity);
            break;
        case ComponentType::ScaleTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScaleTracksComponent(registry->getComponent<ScaleTracksComponent>(entity));
            }
            registry->removeComponent<ScaleTracksComponent>(entity);
            break;
        case ComponentType::ScriptComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScriptComponent(registry->getComponent<ScriptComponent>(entity));
            }
            registry->removeComponent<ScriptComponent>(entity);
            break;
        case ComponentType::ScrollbarComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<ScrollbarComponent>(entity);
            break;
        case ComponentType::SkyComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<SkyComponent>(entity);
            break;
        case ComponentType::SpriteAnimationComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSpriteAnimationComponent(registry->getComponent<SpriteAnimationComponent>(entity));
            }
            registry->removeComponent<SpriteAnimationComponent>(entity);
            break;
        case ComponentType::SpriteComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSpriteComponent(registry->getComponent<SpriteComponent>(entity));
            }
            registry->removeComponent<SpriteComponent>(entity);
            break;
        case ComponentType::TerrainComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<TerrainComponent>(entity);
            break;
        case ComponentType::TextComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<TextComponent>(entity);
            break;
        case ComponentType::TextEditComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<TextEditComponent>(entity);
            break;
        case ComponentType::TilemapComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTilemapComponent(registry->getComponent<TilemapComponent>(entity));
            }
            registry->removeComponent<TilemapComponent>(entity);
            break;
        case ComponentType::TimedActionComponent:
            if (encodeComponent){
                Out::error("Missing component serialization of %s", Catalog::getComponentName(componentType).c_str());
            }
            registry->removeComponent<TimedActionComponent>(entity);
            break;
        case ComponentType::TranslateTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTranslateTracksComponent(registry->getComponent<TranslateTracksComponent>(entity));
            }
            registry->removeComponent<TranslateTracksComponent>(entity);
            break;
        case ComponentType::UIContainerComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUIContainerComponent(registry->getComponent<UIContainerComponent>(entity));
            }
            registry->removeComponent<UIContainerComponent>(entity);
            break;
        default:
            break;
    }

    return oldComponent;
}

ScriptPropertyValue editor::ProjectUtils::luaValueToScriptPropertyValue(lua_State* L, int idx, ScriptPropertyType type) {
    switch (type) {
    case ScriptPropertyType::Bool:
        return ScriptPropertyValue(lua_toboolean(L, idx) != 0);

    case ScriptPropertyType::Int:
        return ScriptPropertyValue((int)luaL_optinteger(L, idx, 0));

    case ScriptPropertyType::Float:
        return ScriptPropertyValue((float)luaL_optnumber(L, idx, 0.0));

    case ScriptPropertyType::String:
        if (lua_isstring(L, idx))
            return ScriptPropertyValue(std::string(lua_tostring(L, idx)));
        return ScriptPropertyValue(std::string());

    case ScriptPropertyType::Vector2: {
        Vector2 v(0, 0);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            v.x = (float)luaL_optnumber(L, -2, 0.0);
            v.y = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 2);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::Vector3:
    case ScriptPropertyType::Color3: {
        Vector3 v(0, 0, 0);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            lua_rawgeti(L, idx, 3);
            v.x = (float)luaL_optnumber(L, -3, 0.0);
            v.y = (float)luaL_optnumber(L, -2, 0.0);
            v.z = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 3);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::Vector4:
    case ScriptPropertyType::Color4: {
        Vector4 v(0, 0, 0, 1);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            lua_rawgeti(L, idx, 3);
            lua_rawgeti(L, idx, 4);
            v.x = (float)luaL_optnumber(L, -4, 0.0);
            v.y = (float)luaL_optnumber(L, -3, 0.0);
            v.z = (float)luaL_optnumber(L, -2, 0.0);
            v.w = (float)luaL_optnumber(L, -1, 1.0);
            lua_pop(L, 4);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::EntityReference: {
        // For now, leave as null entity. The editor will fill it later.
        return ScriptPropertyValue(EntityReference{NULL_ENTITY, 0});
    }
    }

    return ScriptPropertyValue{};
}

void editor::ProjectUtils::loadLuaScriptProperties(ScriptEntry& entry, const std::string& luaPath) {
    lua_State* L = LuaBinding::getLuaState();
    if (!L) return;

    // Load script file as a chunk (returns the script table on the stack)
    if (luaL_dofile(L, luaPath.c_str()) != LUA_OK) {
        Out::error("Failed to load Lua script \"%s\": %s", luaPath.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1); // pop error message
        return;
    }

    // Stack: script_table
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "properties");  // Stack: script_table, properties_table

    if (lua_istable(L, -1)) {
        entry.properties.clear();

        lua_pushnil(L);  // Stack: script_table, properties_table, nil
        while (lua_next(L, -2) != 0) {  // Stack: script_table, properties_table, key, property_table
            if (lua_istable(L, -1)) {
                ScriptProperty prop;

                lua_getfield(L, -1, "name");
                if (lua_isstring(L, -1)) prop.name = lua_tostring(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "displayName");
                prop.displayName = lua_isstring(L, -1) ? lua_tostring(L, -1) : prop.name;
                lua_pop(L, 1);

                lua_getfield(L, -1, "type");
                if (lua_isstring(L, -1)) {
                    std::string typeStr = lua_tostring(L, -1);
                    std::string typeLower = typeStr;
                    std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), 
                                [](unsigned char c){ return (char)std::tolower(c); });

                    if (typeLower == "bool" || typeLower == "boolean"){
                        prop.type = ScriptPropertyType::Bool;
                    }else if (typeLower == "int"  || typeLower == "integer"){
                        prop.type = ScriptPropertyType::Int;
                    }else if (typeLower == "float" || typeLower == "number"){
                        prop.type = ScriptPropertyType::Float;
                    }else if (typeLower == "string"){
                        prop.type = ScriptPropertyType::String;
                    }else if (typeLower == "vector2" || typeLower == "vec2"){
                        prop.type = ScriptPropertyType::Vector2;
                    }else if (typeLower == "vector3" || typeLower == "vec3"){
                        prop.type = ScriptPropertyType::Vector3;
                    }else if (typeLower == "vector4" || typeLower == "vec4"){
                        prop.type = ScriptPropertyType::Vector4;
                    }else if (typeLower == "color3"){
                        prop.type = ScriptPropertyType::Color3;
                    }else if (typeLower == "color4"){
                        prop.type = ScriptPropertyType::Color4;
                    }else if (typeLower == "entity" || typeLower == "entitypointer" || typeLower == "pointer"){
                        prop.type = ScriptPropertyType::EntityReference;
                    }else{
                        prop.type = ScriptPropertyType::EntityReference;
                        prop.ptrTypeName = typeStr;
                    }
                } else {
                    prop.type = ScriptPropertyType::Float;
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "default");
                prop.defaultValue = ProjectUtils::luaValueToScriptPropertyValue(L, -1, prop.type);
                prop.value = prop.defaultValue;
                lua_pop(L, 1);

                entry.properties.push_back(std::move(prop));
            }
            lua_pop(L, 1);  // pop value, keep key
        }
    }

    lua_pop(L, 2);  // pop properties_table and script_table
}

void editor::ProjectUtils::collectEntities(const YAML::Node& entityNode, std::vector<Entity>& allEntities) {
    if (!entityNode || !entityNode.IsMap())
        return;

    if (entityNode["members"] && entityNode["members"].IsSequence()) {
        for (const auto& member : entityNode["members"]) {
            collectEntities(member, allEntities);
        }
        return;
    }

    if (entityNode["entity"]) {
        allEntities.push_back(entityNode["entity"].as<Entity>());
    }

    // Recursively process children
    if (entityNode["children"] && entityNode["children"].IsSequence()) {
        for (const auto& child : entityNode["children"]) {
            collectEntities(child, allEntities);
        }
    }
}

editor::Command* editor::ProjectUtils::buildDuplicateTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap || tileIndex >= tilemap->numTiles) {
        return nullptr;
    }

    unsigned int newSlot = tilemap->numTiles;
    if (newSlot >= tilemap->tiles.size()) {
        return nullptr;
    }

    ComponentType cpType = ComponentType::TilemapComponent;
    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

    const TileData& src = tilemap->tiles[tileIndex];
    std::string dstPrefix = "tiles[" + std::to_string(newSlot) + "]";

    // Copy tile data with a small position offset
    multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, dstPrefix + ".name", src.name);
    multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, dstPrefix + ".rectId", src.rectId);
    multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, dstPrefix + ".position", src.position + Vector2(10.0f, 10.0f));
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".width", src.width);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".height", src.height);

    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, cpType, "numTiles", (unsigned int)(newSlot + 1));

    multiCmd->setNoMerge();
    return multiCmd;
}

editor::Command* editor::ProjectUtils::buildDeleteTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap || tileIndex >= tilemap->numTiles) {
        return nullptr;
    }

    ComponentType cpType = ComponentType::TilemapComponent;
    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

    // Shift tiles down
    for (unsigned int j = tileIndex; j < tilemap->numTiles - 1; j++) {
        std::string dstPrefix = "tiles[" + std::to_string(j) + "]";
        multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, dstPrefix + ".name", tilemap->tiles[j + 1].name);
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, dstPrefix + ".rectId", tilemap->tiles[j + 1].rectId);
        multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, dstPrefix + ".position", tilemap->tiles[j + 1].position);
        multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".width", tilemap->tiles[j + 1].width);
        multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".height", tilemap->tiles[j + 1].height);
    }

    // Clear last slot
    unsigned int lastIdx = tilemap->numTiles - 1;
    std::string lastPrefix = "tiles[" + std::to_string(lastIdx) + "]";
    multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, lastPrefix + ".name", std::string(""));
    multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, lastPrefix + ".rectId", 0);
    multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, lastPrefix + ".position", Vector2(0, 0));
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, lastPrefix + ".width", 0.0f);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, lastPrefix + ".height", 0.0f);

    // Decrement count
    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, cpType, "numTiles", (unsigned int)(tilemap->numTiles - 1));

    multiCmd->setNoMerge();
    return multiCmd;
}