// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

    std::string reason;
    if (!ProjectUtils::canMoveLockedEntityOrder(sceneProject->scene, source, target, type, &reason)){
        editor::Out::warning("Cannot move entity '%u': %s", source, reason.c_str());
        return false;
    }

    // Entities without a Transform are listed apart, as Structure shows them
    bool sourceHasTransform = sceneProject->scene->findComponent<Transform>(source) != nullptr;
    bool targetHasTransform = sceneProject->scene->findComponent<Transform>(target) != nullptr;
    if (sourceHasTransform != targetHasTransform){
        editor::Out::warning("Cannot move entity '%u': only one of the two entities has a Transform", source);
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
                bool isSourceRoot = sourceBundle && (sourceBundle->getRootEntity(sceneId, source) == source);

                if (parentBundlePath != sourceBundlePath && targetBundlePath != sourceBundlePath && !isSourceRoot){
                    Out::error("Cannot move bundle entity %u outside entity bundle", source);
                    return false;
                }
            }
        }

        if (targetBundlePath == sourceBundlePath){
            EntityBundle* bundle = project->getEntityBundle(sourceBundlePath);
            bool isSourceRoot = bundle && (bundle->getRootEntity(sceneId, source) == source);
            bool sameInstance = bundle && (bundle->getInstanceId(sceneId, source) == bundle->getInstanceId(sceneId, target));

            // an instance root has no registry entity, reordering it only changes this scene
            if (!isSourceRoot || type == InsertionType::INTO){
                // another copy of the same bundle holds the same registry entities
                if (!sameInstance){
                    Out::error("Cannot move bundle entity %u into another instance of its bundle", source);
                    return false;
                }
                bundleMoveRecovery = project->moveEntityFromBundle(sceneId, source, target, type, false);
            }
        }
    }
    ProjectUtils::moveEntityOrderByTarget(sceneProject->scene, sceneProject->entities, source, target, type, oldParent, oldIndex, hasTransform);

    // a bundle registry has no current model matrices to keep the world transform with,
    // so the bundle takes the local transform the entity got here
    if (bundleMoveRecovery.size() > 0 && hasTransform){
        project->bundlePropertyChanged(sceneId, source, ComponentType::Transform, {"position", "rotation", "scale"});
    }

    sceneProject->isModified = true;

    return true;
}

void editor::MoveEntityOrderCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (bundleMoveRecovery.size() > 0){
        project->undoMoveEntityInBundle(sceneId, source, target, bundleMoveRecovery, false);
    }
    ProjectUtils::moveEntityOrderByIndex(sceneProject->scene, sceneProject->entities, source, oldParent, oldIndex, hasTransform);

    if (bundleMoveRecovery.size() > 0 && hasTransform){
        project->bundlePropertyChanged(sceneId, source, ComponentType::Transform, {"position", "rotation", "scale"});
    }

    sceneProject->isModified = wasModified;
}

bool editor::MoveEntityOrderCmd::mergeWith(Command* otherCommand){
    return false;
}