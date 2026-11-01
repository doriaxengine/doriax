// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "DuplicateEntityCmd.h"

#include "Stream.h"
#include "Out.h"
#include "command/type/DeleteEntityCmd.h"
#include "util/CameraTextureLink.h"
#include "util/ProjectUtils.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace doriax;

editor::DuplicateEntityCmd::DuplicateEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities){
    this->project = project;
    this->sceneId = sceneId;
    this->sourceEntities = entities;
    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::DuplicateEntityCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (!sceneProject || sourceEntities.empty()){
        return false;
    }

    Scene* scene = sceneProject->scene;

    std::vector<Entity> topLevel = Project::getTopLevelEntities(scene, sourceEntities);
    if (topLevel.empty()){
        return false;
    }

    for (Entity entity : topLevel){
        if (ProjectUtils::getModelBranchOwner(scene, entity) != NULL_ENTITY){
            Out::warning("Cannot duplicate entity '%u'. Duplicate the whole model instead.", entity);
            return false;
        }
    }

    // Record parents of source entities
    std::vector<Entity> sourceParents;
    for (Entity entity : topLevel) {
        Transform* transform = scene->findComponent<Transform>(entity);
        sourceParents.push_back(transform ? transform->parent : NULL_ENTITY);
    }

    // Encode then decode to create duplicates.
    // Bundle members (non-root) are skipped by the encoder when project context is set,
    // so encode them without project context to treat as plain entities.
    // They become bundle local entities automatically at save time.
    auto encodeEntityLocal = [&](Entity e) {
        bool isBundleMember = !project->findEntityBundlePathFor(sceneId, e).empty()
                              && !scene->findComponent<BundleComponent>(e);
        return Stream::encodeEntity(e, scene, isBundleMember ? nullptr : project, isBundleMember ? nullptr : sceneProject);
    };

    // A model's animation and track entities have no Transform, so the encoder never reaches
    // them. Copied with the roots, the same set DeleteEntityCmd removes with them.
    std::vector<Entity> virtualChildren = ProjectUtils::getVirtualChildren(scene, topLevel);

    std::vector<Entity> encodedRoots = topLevel;
    encodedRoots.insert(encodedRoots.end(), virtualChildren.begin(), virtualChildren.end());

    YAML::Node encoded;
    if (encodedRoots.size() == 1) {
        encoded = encodeEntityLocal(encodedRoots[0]);
    } else {
        encoded["type"] = "EntityBundle";
        YAML::Node membersNode(YAML::NodeType::Sequence);
        for (Entity entity : encodedRoots) {
            membersNode.push_back(encodeEntityLocal(entity));
        }
        encoded["members"] = membersNode;
    }
    // not an older file, so the decode keeps hidden children hidden
    encoded["visibilityInherited"] = true;

    // Decode keeping the source entity IDs: every source entity is still alive, so recreateEntity
    // fails and a fresh ID is allocated, recording old->new in entityRemap. Stripping the IDs
    // instead (the previous approach) also produced fresh entities but discarded that mapping,
    // leaving internal entity references (e.g. a model's meshNodesMapping/skeleton/bones, joint
    // bodies, script targets) pointing at the source entities. For multi-node models that meant
    // the copy's ModelComponent kept mapping to the source's child meshes, so on reload it
    // reused/reloaded those instead of populating its own children and the duplicate rendered
    // empty (and saving the broken state could hang).
    std::unordered_map<Entity, Entity> entityRemap;
    createdEntities = Stream::decodeEntitySelection(encoded, scene, &sceneProject->entities, project, sceneProject, NULL_ENTITY, true, &entityRemap);
    if (createdEntities.empty()){
        return false;
    }

    // Repoint internal references from the source entities to their duplicates. clearUnmapped is
    // false so references outside the duplicated set keep pointing at their original target.
    Project::remapEntityProperties(scene, createdEntities, entityRemap, false);

    // bind framebuffers of duplicated camera-linked textures
    CameraTextureLink::resolve(scene);

    // The remap pairs each source root with its copy: a parentless scan cannot, now that
    // transform-less virtual children are copied too.
    std::vector<Entity> createdTopLevel;
    for (Entity source : topLevel) {
        auto it = entityRemap.find(source);
        if (it != entityRemap.end())
            createdTopLevel.push_back(it->second);
    }

    lastSelected = project->getSelectedEntities(sceneId);
    project->clearSelectedEntities(sceneId);

    std::unordered_set<std::string> existingNames;
    for (Entity e : sceneProject->entities) {
        existingNames.insert(scene->getEntityName(e));
    }

    // Re-parent, reorder, rename, and select top-level duplicates
    for (size_t i = 0; i < topLevel.size() && i < createdTopLevel.size(); i++) {
        Entity duplicated = createdTopLevel[i];

        if (sourceParents[i] != NULL_ENTITY)
            scene->addEntityChild(sourceParents[i], duplicated, false);

        // Insert duplicate right after source in entities list
        auto& entities = sceneProject->entities;
        auto dupIt = std::find(entities.begin(), entities.end(), duplicated);
        if (dupIt != entities.end()) {
            entities.erase(dupIt);
            auto srcIt = std::find(entities.begin(), entities.end(), topLevel[i]);
            if (srcIt != entities.end())
                entities.insert(srcIt + 1, duplicated);
        }

        std::string newName = ProjectUtils::makeUniqueEntityName(scene->getEntityName(topLevel[i]), existingNames);
        existingNames.insert(newName);
        scene->setEntityName(duplicated, newName);
        project->addSelectedEntity(sceneId, duplicated);
    }

    // Update all created entities (exclude scene-wide flags which reload ALL meshes and recapture ALL probes)
    for (Entity entity : createdEntities) {
        Catalog::updateEntity(scene, entity, ~UpdateFlags_SceneWide);
    }

    sceneProject->isModified = true;

    editor::Out::info("Duplicated %zu %s", topLevel.size(), topLevel.size() == 1 ? "entity" : "entities");

    return true;
}

void editor::DuplicateEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (sceneProject){
        // Delete created entities in reverse order
        std::vector<Entity> entitiesToDestroy(createdEntities.rbegin(), createdEntities.rend());
        DeleteEntityCmd::destroyEntities(sceneProject->scene, entitiesToDestroy,
            sceneProject->entities, project, sceneId);

        if (!lastSelected.empty()){
            project->clearSelectedEntities(sceneId);
            for (Entity entity : lastSelected){
                project->addSelectedEntity(sceneId, entity);
            }
        }

        sceneProject->isModified = wasModified;
    }
}

bool editor::DuplicateEntityCmd::mergeWith(editor::Command* otherCommand){
    return false;
}

std::vector<Entity> editor::DuplicateEntityCmd::getCreatedEntities() const{
    return createdEntities;
}
