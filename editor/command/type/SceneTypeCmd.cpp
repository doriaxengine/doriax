// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneTypeCmd.h"
#include "Out.h"

using namespace doriax;

editor::SceneTypeCmd::SceneTypeCmd(Project* project, uint32_t sceneId, SceneType type){
    this->project = project;
    this->sceneId = sceneId;
    this->newType = type;

    SceneProject* sceneProject = project->getScene(sceneId);
    this->oldType = sceneProject->sceneType;
    this->wasModified = sceneProject->isModified;
}

bool editor::SceneTypeCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    oldCamera = editorCamera();
    project->setSceneType(sceneId, newType, newCamera);

    sceneProject->isModified = true;

    return true;
}

void editor::SceneTypeCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return;
    }

    newCamera = editorCamera();
    project->setSceneType(sceneId, oldType, oldCamera);

    sceneProject->isModified = wasModified;
}

bool editor::SceneTypeCmd::canChangeType() const{
    if (sceneId == project->getSelectedSceneId() && !project->isAnyScenePlaying()){
        return true;
    }

    Out::warning("A scene type changes only on the selected scene, with Play stopped");
    return false;
}

YAML::Node editor::SceneTypeCmd::editorCamera() const{
    SceneProject* sceneProject = project->getScene(sceneId);
    project->storeEditorCameraState(sceneProject);

    return YAML::Clone(sceneProject->editorCameraState);
}

bool editor::SceneTypeCmd::canUndo() const{
    return canChangeType();
}

bool editor::SceneTypeCmd::canRedo() const{
    return canChangeType();
}

bool editor::SceneTypeCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
