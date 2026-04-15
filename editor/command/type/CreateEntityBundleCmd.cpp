#include "CreateEntityBundleCmd.h"
#include "editor/Out.h"
#include "Stream.h"

using namespace doriax;

editor::CreateEntityBundleCmd::CreateEntityBundleCmd(Project* project, uint32_t sceneId, const fs::path& filepath, const YAML::Node& bundleNode){
    this->project = project;
    this->sceneId = sceneId;
    this->filepath = filepath;
    this->wasModified = project->getScene(sceneId)->isModified;
    this->wasSuccessful = false;
    this->bundleNode = bundleNode;
}

bool editor::CreateEntityBundleCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (!sceneProject){
        editor::Out::error("Invalid scene ID: %u", sceneId);
        return false;
    }

    if (!filepath.is_relative()) {
        editor::Out::error("EntityBundle filepath must be relative: %s", filepath.string().c_str());
        return false;
    }

    // Check if a shared group already exists at this path
    if (project->getEntityBundle(filepath)) {
        editor::Out::error("EntityBundle already exists at %s", filepath.string().c_str());
        return false;
    }

    // Store current selection for undo
    lastSelected = project->getSelectedEntities(sceneId);


    // Mark the entity as shared (this will also save it to disk)
    wasSuccessful = project->createEntityBundle(sceneId, filepath, bundleNode);

    if (wasSuccessful) {
        editor::Out::info("Created EntityBundle at '%s'", filepath.string().c_str());
        return true;
    }

    return false;
}

void editor::CreateEntityBundleCmd::undo(){
    if (!wasSuccessful) {
        return;
    }

    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) {
        return;
    }

    // Delete the shared entity file from disk if it exists
    try {
        fs::path fullPath = project->getProjectPath() / filepath;
        if (fs::exists(fullPath)) {
            fs::remove(fullPath);
            editor::Out::info("Removed shared entity file: %s", fullPath.string().c_str());
        }
    } catch (const std::exception& e) {
        editor::Out::warning("Failed to remove shared entity file: %s", e.what());
    }

    // Remove the bundle and its scene root entities
    project->removeEntityBundle(filepath);

    // Restore previous selection
    if (!lastSelected.empty()) {
        project->replaceSelectedEntities(sceneId, lastSelected);
    }

    // Restore modified state
    sceneProject->isModified = wasModified;

    //editor::Out::info("Unmarked entity '%s' from shared group at '%s'", sceneProject->scene->getEntityName(entity).c_str(), filepath.string().c_str());
}

bool editor::CreateEntityBundleCmd::mergeWith(editor::Command* otherCommand){
    // Create EntityBundle commands typically don't merge with each other
    return false;
}