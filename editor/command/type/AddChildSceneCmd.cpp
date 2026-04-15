#include "AddChildSceneCmd.h"

using namespace doriax;

editor::AddChildSceneCmd::AddChildSceneCmd(Project* project, uint32_t sceneId, uint32_t childSceneId){
    this->project = project;
    this->sceneId = sceneId;
    this->childSceneId = childSceneId;

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::AddChildSceneCmd::execute() {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        auto& childScenes = sceneProject->childScenes;
        if (std::find(childScenes.begin(), childScenes.end(), childSceneId) != childScenes.end()) {
            return false;
        }
    }

    project->addChildScene(sceneId, childSceneId);
    return true;
}

void editor::AddChildSceneCmd::undo() {
    project->removeChildScene(sceneId, childSceneId);
    project->getScene(sceneId)->isModified = wasModified;
}

bool editor::AddChildSceneCmd::mergeWith(Command* otherCommand){
    return false;
}
