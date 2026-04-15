#include "MoveEntityOrderCmd.h"

#include "Out.h"
#include "util/ProjectUtils.h"

using namespace doriax;        
        
editor::MoveEntityOrderCmd::MoveEntityOrderCmd(Project* project, uint32_t sceneId, Entity source, Entity target, InsertionType type){
    this->project = project;
    this->sceneId = sceneId;
    this->source = source;
    this->target = target;
    this->type = type;

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::MoveEntityOrderCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (!ProjectUtils::canMoveLockedEntityOrder(sceneProject->scene, source, target, type)){
        editor::Out::warning("Cannot move entity '%u'. Locked entities can only be reordered within the same parent or virtual parent.", source);
        return false;
    }

    if (project->isEntityInBundle(sceneId, source)){

        fs::path sourceBundlePath = project->findEntityBundlePathFor(sceneId, source);
        fs::path targetBundlePath = project->findEntityBundlePathFor(sceneId, target);

        if (type == InsertionType::INTO){
            if (!project->isEntityInBundle(sceneId, target)){
                Out::error("Cannot move bundle entity %u into target %u", source, target);
                return false;
            }
        }else{
            Transform* transformTarget = sceneProject->scene->findComponent<Transform>(target);
            if (transformTarget){
                fs::path parentBundlePath = project->findEntityBundlePathFor(sceneId, transformTarget->parent);

                EntityBundle* sourceBundle = project->getEntityBundle(sourceBundlePath);
                uint32_t sourceInstanceId = sourceBundle->getInstanceId(sceneId, source);

                bool isSourceRoot = sourceBundle && (sourceBundle->getRootEntity(sceneId, sourceInstanceId) == source);

                if (parentBundlePath != sourceBundlePath && targetBundlePath != sourceBundlePath && !isSourceRoot){
                    Out::error("Cannot move bundle entity %u outside entity bundle", source);
                    return false;
                }
            }
        }

        if (targetBundlePath == sourceBundlePath){
            bundleMoveRecovery = project->moveEntityFromBundle(sceneId, source, target, type, false);
        }
    }
    ProjectUtils::moveEntityOrderByTarget(sceneProject->scene, sceneProject->entities, source, target, type, oldParent, oldIndex, hasTransform);

    sceneProject->isModified = true;

    return true;
}

void editor::MoveEntityOrderCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (bundleMoveRecovery.size() > 0){
        project->undoMoveEntityInBundle(sceneId, source, target, bundleMoveRecovery, false);
    }
    ProjectUtils::moveEntityOrderByIndex(sceneProject->scene, sceneProject->entities, source, oldParent, oldIndex, hasTransform);

    sceneProject->isModified = wasModified;
}

bool editor::MoveEntityOrderCmd::mergeWith(Command* otherCommand){
    return false;
}