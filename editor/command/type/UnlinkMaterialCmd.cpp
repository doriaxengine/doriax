#include "UnlinkMaterialCmd.h"

using namespace doriax;

editor::UnlinkMaterialCmd::UnlinkMaterialCmd(Project* project, uint32_t sceneId, ComponentType componentType,
                                              std::string propertyName, unsigned int submeshIndex,
                                              const std::vector<Entity>& entityList,
                                              std::function<void()> onValueChanged){
    this->project = project;
    this->sceneId = sceneId;
    this->componentType = componentType;
    this->propertyName = propertyName;
    this->submeshIndex = submeshIndex;
    this->onValueChanged = onValueChanged;
    this->wasModified = project->getScene(sceneId)->isModified;

    for (const Entity& entity : entityList) {
        EntityData data;
        // Save current material value
        PropertyData prop = Catalog::getProperty(project->getScene(sceneId)->scene, entity, componentType, propertyName);
        if (prop.ref) {
            data.oldMaterial = *static_cast<Material*>(prop.ref);
        }
        // Save linked file path
        data.linkedFilePath = project->getMaterialFilePath(sceneId, entity, submeshIndex);
        entities[entity] = data;
    }
}

bool editor::UnlinkMaterialCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    for (auto& [entity, data] : entities) {
        // Unlink the material file
        project->unlinkMaterialFile(sceneId, entity, submeshIndex);

        // Clear material.name
        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, componentType, propertyName);
        if (prop.ref) {
            Material* matRef = static_cast<Material*>(prop.ref);
            matRef->name = "";
            Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

            if (project->isEntityInBundle(sceneId, entity)){
                project->bundlePropertyChanged(sceneId, entity, componentType, {propertyName});
            }
        }
    }

    sceneProject->isModified = true;

    if (onValueChanged) {
        onValueChanged();
    }

    return true;
}

void editor::UnlinkMaterialCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return;
    }

    for (auto& [entity, data] : entities) {
        // Restore the full material value
        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, componentType, propertyName);
        if (prop.ref) {
            Material* matRef = static_cast<Material*>(prop.ref);
            *matRef = data.oldMaterial;
            Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

            if (project->isEntityInBundle(sceneId, entity)){
                project->bundlePropertyChanged(sceneId, entity, componentType, {propertyName});
            }
        }

        // Re-link the material file if it was linked
        if (!data.linkedFilePath.empty()) {
            project->linkMaterialFile(sceneId, entity, submeshIndex, data.linkedFilePath);
        }
    }

    sceneProject->isModified = wasModified;

    if (onValueChanged) {
        onValueChanged();
    }
}

bool editor::UnlinkMaterialCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
