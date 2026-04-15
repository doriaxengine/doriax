#include "SetMainCameraCmd.h"

using namespace doriax;

editor::SetMainCameraCmd::SetMainCameraCmd(Project* project, uint32_t sceneId, Entity newMainCamera){
    this->project = project;
    this->sceneId = sceneId;
    this->newMainCamera = newMainCamera;
    
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject){
        this->oldMainCamera = sceneProject->mainCamera;
        this->wasModified = sceneProject->isModified;
    }else{
        this->oldMainCamera = NULL_ENTITY;
        this->wasModified = false;
    }
}

bool editor::SetMainCameraCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    sceneProject->mainCamera = newMainCamera;
    sceneProject->isModified = true;

    return true;
}

void editor::SetMainCameraCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return;
    }

    sceneProject->mainCamera = oldMainCamera;
    sceneProject->isModified = wasModified;
}

bool editor::SetMainCameraCmd::mergeWith(editor::Command* otherCommand){

    return false;
}