#include "DuplicateEntityCmd.h"

#include "Stream.h"
#include "Out.h"
#include "command/type/DeleteEntityCmd.h"

using namespace doriax;

editor::DuplicateEntityCmd::DuplicateEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities){
    this->project = project;
    this->sceneId = sceneId;
    this->sourceEntities = entities;
    this->wasModified = project->getScene(sceneId)->isModified;
}

void editor::DuplicateEntityCmd::stripEntityIds(YAML::Node node){
    if (!node || !node.IsMap())
        return;

    if (node["members"] && node["members"].IsSequence()) {
        for (auto member : node["members"]) {
            stripEntityIds(member);
        }
        return;
    }

    node.remove("entity");

    if (node["children"] && node["children"].IsSequence()) {
        for (auto child : node["children"]) {
            stripEntityIds(child);
        }
    }

    // bundleOverrides: keep registryEntity (references bundle registry, not scene entities)
    // bundleLocalEntities: strip scene entity IDs from local entities
    if (node["bundleLocalEntities"] && node["bundleLocalEntities"].IsSequence()) {
        for (auto entry : node["bundleLocalEntities"]) {
            stripEntityIds(entry);
        }
    }
}

std::string editor::DuplicateEntityCmd::makeUniqueCopyName(const std::string& name, std::unordered_set<std::string>& existingNames){
    // Remove existing " (copy)" or " (copy N)" suffix
    std::string cleanBase = name;
    size_t pos = cleanBase.rfind(" (copy)");
    if (pos != std::string::npos && pos == cleanBase.size() - 7) {
        cleanBase = cleanBase.substr(0, pos);
    } else {
        pos = cleanBase.rfind(" (copy ");
        if (pos != std::string::npos) {
            size_t endParen = cleanBase.rfind(')');
            if (endParen == cleanBase.size() - 1 && endParen > pos)
                cleanBase = cleanBase.substr(0, pos);
        }
    }

    std::string newName = cleanBase + " (copy)";
    unsigned int count = 2;
    while (existingNames.count(newName)) {
        newName = cleanBase + " (copy " + std::to_string(count++) + ")";
    }

    existingNames.insert(newName);
    return newName;
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

    // Record parents of source entities
    std::vector<Entity> sourceParents;
    for (Entity entity : topLevel) {
        Transform* transform = scene->findComponent<Transform>(entity);
        sourceParents.push_back(transform ? transform->parent : NULL_ENTITY);
    }

    // Encode, strip IDs, decode to create duplicates
    // Bundle members (non-root) are skipped by the encoder when project context is set,
    // so encode them without project context to treat as plain entities.
    // They become bundle local entities automatically at save time.
    auto encodeEntityLocal = [&](Entity e) {
        bool isBundleMember = !project->findEntityBundlePathFor(sceneId, e).empty()
                              && !scene->findComponent<BundleComponent>(e);
        return Stream::encodeEntity(e, scene, isBundleMember ? nullptr : project, isBundleMember ? nullptr : sceneProject);
    };

    YAML::Node encoded;
    if (topLevel.size() == 1) {
        encoded = encodeEntityLocal(topLevel[0]);
    } else {
        encoded["type"] = "EntityBundle";
        YAML::Node membersNode(YAML::NodeType::Sequence);
        for (Entity entity : topLevel) {
            membersNode.push_back(encodeEntityLocal(entity));
        }
        encoded["members"] = membersNode;
    }
    stripEntityIds(encoded);

    createdEntities = Stream::decodeEntitySelection(encoded, scene, &sceneProject->entities, project, sceneProject, NULL_ENTITY, true);
    if (createdEntities.empty()){
        return false;
    }

    // Identify top-level duplicates (decoded with no parent)
    std::vector<Entity> createdTopLevel;
    for (Entity e : createdEntities) {
        Transform* t = scene->findComponent<Transform>(e);
        if (!t || t->parent == NULL_ENTITY)
            createdTopLevel.push_back(e);
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

        std::string newName = makeUniqueCopyName(scene->getEntityName(topLevel[i]), existingNames);
        scene->setEntityName(duplicated, newName);
        project->addSelectedEntity(sceneId, duplicated);
    }

    // Update all created entities (exclude Scene_Mesh_Reload which reloads ALL meshes)
    for (Entity entity : createdEntities) {
        Catalog::updateEntity(scene, entity, ~UpdateFlags_Scene_Mesh_Reload);
    }

    sceneProject->isModified = true;

    editor::Out::info("Duplicated %zu entity(ies)", topLevel.size());

    return true;
}

void editor::DuplicateEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (sceneProject){
        // Delete created entities in reverse order
        for (auto it = createdEntities.rbegin(); it != createdEntities.rend(); ++it){
            DeleteEntityCmd::destroyEntity(sceneProject->scene, *it, sceneProject->entities, project, sceneId);
        }

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
