#include "LinkMaterialCmd.h"

using namespace doriax;

editor::LinkMaterialCmd::LinkMaterialCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType componentType,
                                          std::string propertyName, unsigned int submeshIndex,
                                          Material newMaterial, std::function<void()> onValueChanged){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->componentType = componentType;
    this->propertyName = propertyName;
    this->submeshIndex = submeshIndex;
    this->newMaterial = newMaterial;
    this->onValueChanged = onValueChanged;
    this->wasModified = project->getScene(sceneId)->isModified;

    // Save current material value
    PropertyData prop = Catalog::getProperty(project->getScene(sceneId)->scene, entity, componentType, propertyName);
    if (prop.ref) {
        oldMaterial = *static_cast<Material*>(prop.ref);
    }

    // Save current linked file path (if any)
    oldLinkedFilePath = project->getMaterialFilePath(sceneId, entity, submeshIndex);
}

bool editor::LinkMaterialCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, componentType, propertyName);
    if (prop.ref) {
        Material* matRef = static_cast<Material*>(prop.ref);
        *matRef = newMaterial;
        Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

        if (project->isEntityInBundle(sceneId, entity)){
            project->bundlePropertyChanged(sceneId, entity, componentType, {propertyName});
        }
    }

    // Link the new material file
    if (!newMaterial.name.empty()) {
        project->linkMaterialFile(sceneId, entity, submeshIndex, newMaterial.name);
    }

    sceneProject->isModified = true;

    if (onValueChanged) {
        onValueChanged();
    }

    return true;
}

void editor::LinkMaterialCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return;
    }

    // Unlink the current material file
    project->unlinkMaterialFile(sceneId, entity, submeshIndex);

    // Restore old material value
    PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, componentType, propertyName);
    if (prop.ref) {
        Material* matRef = static_cast<Material*>(prop.ref);
        *matRef = oldMaterial;
        Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

        if (project->isEntityInBundle(sceneId, entity)){
            project->bundlePropertyChanged(sceneId, entity, componentType, {propertyName});
        }
    }

    // Re-link old file if there was one
    if (!oldLinkedFilePath.empty()) {
        project->linkMaterialFile(sceneId, entity, submeshIndex, oldLinkedFilePath);
    }

    sceneProject->isModified = wasModified;

    if (onValueChanged) {
        onValueChanged();
    }
}

bool editor::LinkMaterialCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
