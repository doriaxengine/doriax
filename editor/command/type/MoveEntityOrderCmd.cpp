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

    if (source == target || (sourceHasTransform && sceneProject->scene->isParentOf(source, target))){
        editor::Out::warning("Cannot move entity '%u' to itself or its own child", source);
        return false;
    }

    if (!project->canMoveBundleEntity(sceneId, source, target, type, &reason)){
        editor::Out::warning("Cannot move entity '%u': %s", source, reason.c_str());
        return false;
    }

    if (!ProjectUtils::moveEntityOrderByTarget(sceneProject->scene, sceneProject->entities, source, target, type, oldParent, oldIndex, hasTransform)){
        return false;
    }
    // only members reach the registry, the move of an instance root stays in this scene
    if (project->isEntityInBundle(sceneId, source)){
        bundleMoveRecovery = project->moveEntityFromBundle(sceneId, source, target, type, false);
    }

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
        project->undoMoveEntityInBundle(sceneId, source, bundleMoveRecovery, false);
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