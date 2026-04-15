#include "ObjectTransformCmd.h"

using namespace doriax;

editor::ObjectTransformCmd::ObjectTransformCmd(Project* project, size_t sceneId, Entity entity, Matrix4 localMatrix){
    this->project = project;
    this->sceneId = sceneId;
    
    localMatrix.decomposeStandard(props[entity].newPosition, props[entity].newScale, props[entity].newRotation);

    this->wasModified = project->getScene(sceneId)->isModified;
}

editor::ObjectTransformCmd::ObjectTransformCmd(Project* project, size_t sceneId, Entity entity, Vector3 position, Quaternion rotation, Vector3 scale){
    this->project = project;
    this->sceneId = sceneId;

    props[entity].newPosition = position;
    props[entity].newRotation = rotation;
    props[entity].newScale = scale;

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::ObjectTransformCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return false;
    }
    for (auto& [entity, property] : props){
        if (Transform* transform = sceneProject->scene->findComponent<Transform>(entity)){
            property.oldPosition = transform->position;
            property.oldRotation = transform->rotation;
            property.oldScale = transform->scale;

            transform->position = property.newPosition;
            transform->rotation = property.newRotation;
            transform->scale = property.newScale;

            transform->needUpdate = true;

            if (project->isEntityInBundle(sceneId, entity)){
                project->bundlePropertyChanged(sceneId, entity, ComponentType::Transform, {"position", "rotation", "scale"});
            }
        }
    }

    sceneProject->isModified = true;

    return true;
}

void editor::ObjectTransformCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return;
    }
    for (auto const& [entity, property] : props){
        if (Transform* transform = sceneProject->scene->findComponent<Transform>(entity)){
            transform->position = property.oldPosition;
            transform->rotation = property.oldRotation;
            transform->scale = property.oldScale;

            transform->needUpdate = true;

            if (project->isEntityInBundle(sceneId, entity)){
                project->bundlePropertyChanged(sceneId, entity, ComponentType::Transform, {"position", "rotation", "scale"});
            }
        }
    }

    sceneProject->isModified = wasModified;
}

bool editor::ObjectTransformCmd::mergeWith(editor::Command* otherCommand){
    ObjectTransformCmd* otherCmd = dynamic_cast<ObjectTransformCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (sceneId == otherCmd->sceneId){

            for (auto const& [otherEntity, otherProperty] : otherCmd->props){
                if (props.find(otherEntity) != props.end()) {
                    props[otherEntity].oldPosition = otherProperty.oldPosition;
                    props[otherEntity].oldRotation = otherProperty.oldRotation;
                    props[otherEntity].oldScale = otherProperty.oldScale;
                }else{
                    props[otherEntity] = otherProperty;
                }
            }

            wasModified = wasModified && otherCmd->wasModified;

            return true;
        }
    }

    return false;
}
