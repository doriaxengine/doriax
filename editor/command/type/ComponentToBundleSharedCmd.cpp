#include "ComponentToBundleSharedCmd.h"

#include "Stream.h"

using namespace doriax;

editor::ComponentToBundleSharedCmd::ComponentToBundleSharedCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType componentType){
    this->project = project;
    this->sceneId = sceneId;
    this->componentType = componentType;

    ComponentToBundleSharedData entityData;
    entityData.entity = entity;
    entities.push_back(entityData);

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::ComponentToBundleSharedCmd::execute() {
    SceneProject* sceneProject = project->getScene(sceneId);

    for (ComponentToBundleSharedData& entityData : entities){

        fs::path filepath = project->findEntityBundlePathFor(sceneId, entityData.entity);
        if (sceneProject && !filepath.empty()) {
            EntityBundle* bundle = project->getEntityBundle(filepath);

            Signature signature = Catalog::componentTypeToSignature(sceneProject->scene, componentType);
            entityData.recovery = Stream::encodeComponents(entityData.entity, sceneProject->scene, signature);

            if (!bundle->hasComponentOverride(sceneId, entityData.entity, componentType)){
                return false;
            }

            // Clear the override and copy values from the bundle registry
            bundle->clearComponentOverride(sceneProject->id, entityData.entity, componentType);

            Entity registryEntity = bundle->getRegistryEntity(sceneId, entityData.entity);
            Catalog::copyComponent(bundle->registry.get(), registryEntity, sceneProject->scene, entityData.entity, componentType);
        }

    }

    sceneProject->isModified = true;

    return true;
}

void editor::ComponentToBundleSharedCmd::undo() {
    SceneProject* sceneProject = project->getScene(sceneId);

    for (ComponentToBundleSharedData& entityData : entities){

        fs::path filepath = project->findEntityBundlePathFor(sceneId, entityData.entity);
        if (sceneProject && !filepath.empty()) {
            EntityBundle* bundle = project->getEntityBundle(filepath);

            bundle->setComponentOverride(sceneProject->id, entityData.entity, componentType);

            Entity parent = NULL_ENTITY;
            if (componentType == ComponentType::Transform){
                parent = sceneProject->scene->getComponent<Transform>(entityData.entity).parent;
            }

            Stream::decodeComponents(entityData.entity, parent, sceneProject->scene, entityData.recovery);
        }

    }

    sceneProject->isModified = wasModified;
}

bool editor::ComponentToBundleSharedCmd::mergeWith(Command* otherCommand){
    ComponentToBundleSharedCmd* otherCmd = dynamic_cast<ComponentToBundleSharedCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (sceneId == otherCmd->sceneId){

            for (ComponentToBundleSharedData& otherEntityData :  otherCmd->entities){
                entities.push_back(otherEntityData);
            }

            wasModified = wasModified && otherCmd->wasModified;

            return true;
        }
    }

    return false;
}
