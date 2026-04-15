#include "AddComponentCmd.h"

#include "util/ProjectUtils.h"

using namespace doriax;

editor::AddComponentCmd::AddComponentCmd(Project* project, size_t sceneId, Entity entity, ComponentType componentType){
    this->project = project;
    this->sceneId = sceneId;
    this->entities.push_back(entity);
    this->componentType = componentType;

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::AddComponentCmd::execute() {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        Scene* scene = sceneProject->scene;

        for (Entity& entity : entities){
            ProjectUtils::addEntityComponent(scene, entity, componentType, sceneProject->entities);

            if (project->isEntityInBundle(sceneId, entity)){
                project->addComponentToBundle(sceneId, entity, componentType, false);
            }
        }

        sceneProject->isModified = true;
    }

    return true;
}

void editor::AddComponentCmd::undo() {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        Scene* scene = sceneProject->scene;

        for (Entity& entity : entities){
            ProjectUtils::removeEntityComponent(scene, entity, componentType, sceneProject->entities);

            if (project->isEntityInBundle(sceneId, entity)){
                project->removeComponentFromBundle(sceneId, entity, componentType, false, false);
            }
        }

        sceneProject->isModified = wasModified;
    }
}

bool editor::AddComponentCmd::mergeWith(Command* otherCommand){
    AddComponentCmd* otherCmd = dynamic_cast<AddComponentCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (sceneId == otherCmd->sceneId){

            for (Entity& otherEntity :  otherCmd->entities){
                entities.push_back(otherEntity);
            }

            wasModified = wasModified && otherCmd->wasModified;

            return true;
        }
    }

    return false;
}