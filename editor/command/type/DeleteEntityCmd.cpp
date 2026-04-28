#include "DeleteEntityCmd.h"

#include "Stream.h"
#include "Out.h"
#include "util/ProjectUtils.h"
#include "command/type/MoveEntityOrderCmd.h"

using namespace doriax;

editor::DeleteEntityCmd::DeleteEntityCmd(Project* project, uint32_t sceneId, Entity entity, bool allowLockedRoots)
    : DeleteEntityCmd(project, sceneId, std::vector<Entity>{entity}, allowLockedRoots){

}

editor::DeleteEntityCmd::DeleteEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities, bool allowLockedRoots){
    this->project = project;
    this->sceneId = sceneId;
    this->requestedEntities = entities;
    this->allowLockedRoots = allowLockedRoots;

    for (const Entity& entity : entities){
        DeleteEntityData entityData;
        entityData.entity = entity;

        SceneProject* sceneProject = project->getScene(sceneId);
        Scene* scene = sceneProject->scene;

        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<Transform>())) {
            entityData.hasTransform = true;
            Transform& transform = scene->getComponent<Transform>(entity);
            auto transforms = scene->getComponentArray<Transform>();
            entityData.entityIndex = transforms->getIndex(entity);
            entityData.parent = transform.parent;
        }else{
            entityData.hasTransform = false;
            auto it = std::find(sceneProject->entities.begin(), sceneProject->entities.end(), entity);
            if (it != sceneProject->entities.end()) {
                entityData.entityIndex = std::distance(sceneProject->entities.begin(), it);
            }
        }

        this->entities.push_back(entityData);
    }

    this->wasModified = project->getScene(sceneId)->isModified;
}

void editor::DeleteEntityCmd::destroyEntity(EntityRegistry* registry, Entity entity, std::vector<Entity>& entities, Project* project, uint32_t sceneId){
    int structuralFlags = UpdateFlags_None;
    if (registry->isEntityCreated(entity)){ // locked child are deleted by systems when their parent is deleted
        if (registry->findComponent<LightComponent>(entity)){
            structuralFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::LightComponent);
        }
        if (registry->findComponent<FogComponent>(entity)){
            structuralFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::FogComponent);
        }
        registry->destroyEntity(entity);
    }
    if (structuralFlags != UpdateFlags_None){
        Catalog::updateEntity(registry, NULL_ENTITY, structuralFlags);
    }

    auto ite = std::find(entities.begin(), entities.end(), entity);
    if (ite != entities.end()) {
        entities.erase(ite);
    }

    if (project){
        if (project->isSelectedEntity(sceneId, entity)){
            project->clearSelectedEntities(sceneId);
        }

        SceneProject* sceneProject = project->getScene(sceneId);
        if (sceneProject && sceneProject->mainCamera == entity){
            sceneProject->mainCamera = NULL_ENTITY;
        }
    }
}

bool editor::DeleteEntityCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    std::vector<Entity> entitiesToDelete;
    auto isRequestedEntity = [&](Entity entity) {
        return std::find(requestedEntities.begin(), requestedEntities.end(), entity) != requestedEntities.end();
    };

    auto it = entities.begin();
    while (it != entities.end()) {
        Entity lockedParent = ProjectUtils::getLockedEntityParent(sceneProject->scene, it->entity);
        bool canDelete = true;
        if (lockedParent != NULL_ENTITY){
            canDelete = (allowLockedRoots && isRequestedEntity(it->entity))
                || isRequestedEntity(lockedParent)
                || std::find(entitiesToDelete.begin(), entitiesToDelete.end(), lockedParent) != entitiesToDelete.end();
        }

        if (!canDelete){
             Out::warning("Cannot delete entity '%u'. It is a locked child of another component.", it->entity);
             it = entities.erase(it);
        } else {
             entitiesToDelete.push_back(it->entity);
             ++it;
        }
    }

    if (entities.empty()){
        return false;
    }

    // Also delete virtual children of this entity
    std::vector<Entity> virtualChildren = ProjectUtils::getVirtualChildren(sceneProject->scene, entitiesToDelete);
    for (Entity vChild : virtualChildren) {
        auto existing = std::find_if(this->entities.begin(), this->entities.end(),
            [vChild](const DeleteEntityData& data) {
                return data.entity == vChild;
            });
        if (existing != this->entities.end()) {
            continue;
        }

        // Skip bundle members whose root is already being deleted.
        // They will be destroyed/restored by unimportEntityBundle/importEntityBundle.
        fs::path bundlePath = project->findEntityBundlePathFor(sceneId, vChild);
        if (!bundlePath.empty()) {
            EntityBundle* bundle = project->getEntityBundle(bundlePath);
            if (bundle) {
                Entity rootEntity = bundle->getRootEntity(sceneId, vChild);
                if (std::find(entitiesToDelete.begin(), entitiesToDelete.end(), rootEntity) != entitiesToDelete.end()) {
                    continue;
                }
            }
        }

        DeleteEntityData vData;
        vData.entity = vChild;
        vData.hasTransform = false;
        auto it = std::find(sceneProject->entities.begin(), sceneProject->entities.end(), vChild);
        if (it != sceneProject->entities.end()) {
            vData.entityIndex = std::distance(sceneProject->entities.begin(), it);
        }
        this->entities.push_back(vData);
    }

    lastSelected = project->getSelectedEntities(sceneId);

    for (auto it = entities.rbegin(); it != entities.rend(); ++it){
        DeleteEntityData& entityData = *it;
        // Check if entity is part of a bundle BEFORE encoding (non-root members produce empty nodes)
        fs::path bundlePath = project->findEntityBundlePathFor(sceneId, entityData.entity);
        EntityBundle* bundle = project->getEntityBundle(bundlePath);

        if (bundle) {
            entityData.instanceId = bundle->getInstanceId(sceneId, entityData.entity);

            if (entityData.entity == bundle->getRootEntity(sceneId, entityData.entity)) {
                entityData.isBundleRoot = true;
                entityData.data = Stream::encodeEntity(entityData.entity, sceneProject->scene, project, sceneProject);
                const auto* inst = bundle->getInstance(sceneId, entityData.entity);
                std::vector<Entity> memberEntities;
                if (inst) {
                    for (const auto& member : inst->members) {
                        memberEntities.push_back(member.localEntity);
                    }
                }
                project->unimportEntityBundle(sceneId, bundlePath, entityData.entity, memberEntities);
            } else {
                if (!entityData.hasTransform) {
                    entityData.parent = bundle->getRootEntity(sceneId, entityData.entity);
                }
                entityData.recoveryBundleData = project->removeEntityFromBundle(sceneId, entityData.entity, true);
            }
        } else {
            entityData.data = Stream::encodeEntity(entityData.entity, sceneProject->scene, project, sceneProject);

            std::vector<Entity> allEntities;
            ProjectUtils::collectEntities(entityData.data, allEntities);

            for (const Entity& entity : allEntities) {
                destroyEntity(sceneProject->scene, entity, sceneProject->entities, project, sceneId);
            }
        }

        sceneProject->isModified = true;
    }

    return true;
}

void editor::DeleteEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    for (DeleteEntityData& entityData : entities){
        if (entityData.recoveryBundleData.size() == 0){

            std::vector<Entity> allEntities = Stream::decodeEntity(entityData.data, sceneProject->scene, &sceneProject->entities, project, sceneProject);
            entityData.entity = allEntities[0];

            ProjectUtils::moveEntityOrderByIndex(sceneProject->scene, sceneProject->entities, entityData.entity, entityData.parent, entityData.entityIndex, entityData.hasTransform);

        }else{

            project->addEntityToBundle(sceneId, entityData.recoveryBundleData, entityData.parent, true);
        }
    }

    if (lastSelected.size() > 0){
        project->replaceSelectedEntities(sceneId, lastSelected);
    }

    sceneProject->isModified = wasModified;
}

bool editor::DeleteEntityCmd::mergeWith(editor::Command* otherCommand){
    DeleteEntityCmd* otherCmd = dynamic_cast<DeleteEntityCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (sceneId == otherCmd->sceneId){

            lastSelected = otherCmd->lastSelected;

            for (DeleteEntityData& otherEntityData :  otherCmd->entities){
                entities.push_back(otherEntityData);
            }

            wasModified = wasModified && otherCmd->wasModified;

            return true;
        }
    }

    return false;
}