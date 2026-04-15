#include "AddEntityToBundleCmd.h"
#include "util/ProjectUtils.h"

using namespace doriax;

editor::AddEntityToBundleCmd::AddEntityToBundleCmd(Project* project, uint32_t sceneId, Entity entity, Entity parent){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->parent = parent;
    this->wasModified = project->getScene(sceneId)->isModified;

    this->originalParent = NULL_ENTITY;
    this->originalTransformIndex = 0;
    this->hasTransform = false;

    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        Transform* transform = sceneProject->scene->findComponent<Transform>(entity);
        if (transform) {
            this->hasTransform = true;
            this->originalParent = transform->parent;
            this->originalTransformIndex = ProjectUtils::getTransformIndex(sceneProject->scene, entity);
        }
    }
}

bool editor::AddEntityToBundleCmd::execute(){
    bool result = project->addEntityToBundle(sceneId, entity, parent, false);

    if (result){
        project->getScene(sceneId)->isModified = true;
    }

    return result;
}

void editor::AddEntityToBundleCmd::undo(){
    project->removeEntityFromBundle(sceneId, entity, false);

    if (hasTransform) {
        SceneProject* sceneProject = project->getScene(sceneId);
        if (sceneProject) {
            ProjectUtils::moveEntityOrderByTransform(sceneProject->scene, sceneProject->entities, entity, originalParent, originalTransformIndex, true);
        }
    }

    project->getScene(sceneId)->isModified = wasModified;
}

bool editor::AddEntityToBundleCmd::mergeWith(editor::Command* otherCommand){

    return false;
}
