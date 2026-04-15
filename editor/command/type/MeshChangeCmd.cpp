#include "MeshChangeCmd.h"

#include "Stream.h"

using namespace doriax;

editor::MeshChangeCmd::MeshChangeCmd(Project* project, uint32_t sceneId, Entity entity, MeshComponent mesh){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->newMesh = Stream::encodeMeshComponent(mesh);

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::MeshChangeCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    oldMesh = Stream::encodeMeshComponent(sceneProject->scene->getComponent<MeshComponent>(entity));
    sceneProject->scene->getComponent<MeshComponent>(entity) = Stream::decodeMeshComponent(newMesh);

    sceneProject->isModified = true;

    if (project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::MeshComponent, {});
    }

    return true;
}

void editor::MeshChangeCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    sceneProject->scene->getComponent<MeshComponent>(entity) = Stream::decodeMeshComponent(oldMesh);

    sceneProject->isModified = wasModified;

    if (project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::MeshComponent, {});
    }
}

bool editor::MeshChangeCmd::mergeWith(editor::Command* otherCommand){
    MeshChangeCmd* otherCmd = dynamic_cast<MeshChangeCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (sceneId == otherCmd->sceneId && entity == otherCmd->entity){
            this->oldMesh = otherCmd->oldMesh;

            this->wasModified = this->wasModified && otherCmd->wasModified;
            return true;
        }
    }
    return false;
}