// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneTypeCmd.h"

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

    project->setSceneType(sceneId, newType);

    sceneProject->isModified = true;

    return true;
}

void editor::SceneTypeCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return;
    }

    project->setSceneType(sceneId, oldType);

    sceneProject->isModified = wasModified;
}

bool editor::SceneTypeCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
