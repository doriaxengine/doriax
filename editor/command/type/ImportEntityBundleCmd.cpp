#include "ImportEntityBundleCmd.h"

#include "editor/Out.h"
#include "Stream.h"

using namespace doriax;

editor::ImportEntityBundleCmd::ImportEntityBundleCmd(Project* project, uint32_t sceneId, const fs::path& filepath, Entity parent, bool needSaveScene){
    this->project = project;
    this->sceneId = sceneId;
    this->filepath = filepath;
    this->parent = parent;
    this->needSaveScene = needSaveScene;
    this->rootEntity = NULL_ENTITY;
    this->wasModified = project->getScene(sceneId)->isModified;
    this->addedToParentBundle = false;
}

bool editor::ImportEntityBundleCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (!sceneProject){
        return false;
    }

    lastSelected = project->getSelectedEntities(sceneId);

    Scene* scene = sceneProject->scene;

    // Create root entity with BundleComponent (Preserve ID for Undo/Redo)
    if (rootEntity == NULL_ENTITY){
        rootEntity = scene->createUserEntity();
    }else{
        if (!scene->recreateEntity(rootEntity)){
            rootEntity = scene->createUserEntity();
        }
    }

    BundleComponent bundleComp;
    bundleComp.name = filepath.stem().string();
    bundleComp.path = filepath.string();
    scene->addComponent<BundleComponent>(rootEntity, bundleComp);

    // Check if bundle has top-level entities with transforms to decide if root needs one
    EntityBundle* bundle = project->getEntityBundle(filepath);
    bool hasTopLevelTransform = false;

    if (!bundle || bundle->registryEntities.empty()) {
        // Bundle not loaded yet — load from disk to check
        try {
            std::filesystem::path fullBundlePath = project->getProjectPath() / filepath;
            YAML::Node node = YAML::LoadFile(fullBundlePath.string());
            // Temporarily decode to check for transforms
            EntityRegistry tempRegistry;
            std::vector<Entity> tempEntities;
            Stream::decodeEntitySelection(node, &tempRegistry, &tempEntities);
            std::vector<Entity> topLevel = Project::getTopLevelEntities(&tempRegistry, tempEntities);
            for (Entity e : topLevel) {
                if (tempRegistry.getSignature(e).test(tempRegistry.getComponentId<Transform>())) {
                    hasTopLevelTransform = true;
                    break;
                }
            }
        } catch (const std::exception& e) {
            Out::error("Failed to check bundle file: %s", e.what());
            scene->destroyEntity(rootEntity);
            rootEntity = NULL_ENTITY;
            return false;
        }
    } else {
        std::vector<Entity> topLevel = Project::getTopLevelEntities(bundle->registry.get(), bundle->registryEntities);
        for (Entity e : topLevel) {
            if (bundle->registry->getSignature(e).test(bundle->registry->getComponentId<Transform>())) {
                hasTopLevelTransform = true;
                break;
            }
        }
    }

    if (hasTopLevelTransform) {
        scene->addComponent<Transform>(rootEntity, {});
    }

    // Set name
    std::string rootName = filepath.stem().string();
    if (rootName.empty()) {
        rootName = "Bundle";
    }
    scene->setEntityName(rootEntity, rootName);
    sceneProject->entities.push_back(rootEntity);

    // Reparent under drop target if provided
    if (parent != NULL_ENTITY && hasTopLevelTransform) {
        if (scene->findComponent<Transform>(parent)) {
            scene->addEntityChild(parent, rootEntity, true);
        }
    }

    // Import bundle contents
    importedEntities = project->importEntityBundle(sceneProject, &sceneProject->entities, filepath, rootEntity, needSaveScene);

    if (importedEntities.empty()){
        // Clean up root if import failed
        scene->destroyEntity(rootEntity);
        auto it = std::find(sceneProject->entities.begin(), sceneProject->entities.end(), rootEntity);
        if (it != sceneProject->entities.end()) {
            sceneProject->entities.erase(it);
        }
        rootEntity = NULL_ENTITY;
        return false;
    }

    // If parent is part of a bundle, add the new root as a nested bundle member
    addedToParentBundle = false;
    if (parent != NULL_ENTITY && project->isEntityInBundle(sceneId, parent)) {
        addedToParentBundle = project->addEntityToBundle(sceneId, rootEntity, parent, false);
    }

    // Select the root entity
    project->setSelectedEntity(sceneId, rootEntity);

    ImGui::SetWindowFocus(("###Scene" + std::to_string(sceneId)).c_str());

    editor::Out::info("Imported entity bundle from '%s' to scene '%s'", filepath.string().c_str(), sceneProject->name.c_str());

    return true;
}

void editor::ImportEntityBundleCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return;
    }

    // Remove from parent bundle first if it was added
    if (addedToParentBundle) {
        project->removeEntityFromBundle(sceneId, rootEntity, false);
        addedToParentBundle = false;
    }

    // Unimport the entity bundle (removes instance + destroys entities)
    project->unimportEntityBundle(sceneId, filepath, rootEntity, importedEntities);

    // Restore previous selection
    if (!lastSelected.empty()) {
        project->replaceSelectedEntities(sceneId, lastSelected);
    }

    // Restore modified state
    sceneProject->isModified = wasModified;
}

bool editor::ImportEntityBundleCmd::mergeWith(editor::Command* otherCommand){
    return false;
}

std::vector<Entity> editor::ImportEntityBundleCmd::getImportedEntities() const{
    return importedEntities;
}
