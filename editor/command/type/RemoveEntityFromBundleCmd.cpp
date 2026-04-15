#include "RemoveEntityFromBundleCmd.h"

using namespace doriax;

editor::RemoveEntityFromBundleCmd::RemoveEntityFromBundleCmd(Project* project, uint32_t sceneId, Entity entity, Entity parent){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->parent = parent;

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::RemoveEntityFromBundleCmd::execute(){
    recovery = project->removeEntityFromBundle(sceneId, entity, false);

    bool result = !recovery.empty();
    if (result){
        project->getScene(sceneId)->isModified = true;
    }

    return result;
}

void editor::RemoveEntityFromBundleCmd::undo(){
    project->addEntityToBundle(sceneId, recovery, parent, false);

    project->getScene(sceneId)->isModified = wasModified;
}

bool editor::RemoveEntityFromBundleCmd::mergeWith(editor::Command* otherCommand){

    return false;
}
