#include "RemoveChildSceneCmd.h"

using namespace doriax;

editor::RemoveChildSceneCmd::RemoveChildSceneCmd(Project* project, uint32_t sceneId, uint32_t childSceneId){
    this->project = project;
    this->sceneId = sceneId;
    this->childSceneId = childSceneId;
    this->index = -1;

    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject){
        this->wasModified = sceneProject->isModified;
        auto& childScenes = sceneProject->childScenes;
        auto it = std::find(childScenes.begin(), childScenes.end(), childSceneId);
        if (it != childScenes.end()){
            this->index = std::distance(childScenes.begin(), it);
        }
    }
}

bool editor::RemoveChildSceneCmd::execute() {
    if (index == -1) return false;
    project->removeChildScene(sceneId, childSceneId);
    return true;
}

void editor::RemoveChildSceneCmd::undo() {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject && index != -1){
        auto& childScenes = sceneProject->childScenes;
        if (index <= childScenes.size()){
            childScenes.insert(childScenes.begin() + index, childSceneId);
            sceneProject->isModified = wasModified;
        }
    }
}

bool editor::RemoveChildSceneCmd::mergeWith(Command* otherCommand){
    return false;
}
