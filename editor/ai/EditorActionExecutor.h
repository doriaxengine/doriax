// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "AiTypes.h"

#include <atomic>

namespace doriax::editor {
class Project;
class ResourcesWindow;
}

namespace doriax::editor::ai {

class HttpClient;

class EditorActionExecutor {
public:
    EditorActionExecutor(Project* project, ResourcesWindow* resourcesWindow, const HttpClient* httpClient);

    ActionResult execute(const std::string& name,
                         const Json& arguments,
                         const std::atomic<bool>* cancel = nullptr);

    // What the model reads: the message, then any data as JSON
    static std::string resultText(const ActionResult& result);
    // Logs a failure at its severity; source is who ran it, "AI" or "MCP"
    static void logFailure(const ActionResult& result, const std::string& source);
    // Changes can't run while a save thread reads the scene
    static bool isBlockedBySave(Project* project, const std::string& name);

private:
    Project* project;
    ResourcesWindow* resourcesWindow;
    const HttpClient* httpClient;

    // Validates and runs one action; execute() wraps it to contain throws.
    ActionResult dispatch(const std::string& name,
                          const Json& arguments,
                          const std::atomic<bool>* cancel);

    ActionResult getProjectSummary();
    ActionResult searchResources(const Json& arguments);
    ActionResult listSceneEntities(const Json& arguments);
    ActionResult inspectEntity(const Json& arguments);
    ActionResult inspectComponent(const Json& arguments);
    ActionResult listComponentTypes();
    ActionResult searchEngineApi(const Json& arguments);
    ActionResult searchEngineSource(const Json& arguments);
    ActionResult readEngineSource(const Json& arguments);
    ActionResult readOutputLog(const Json& arguments);
    ActionResult createEntity(const Json& arguments);
    ActionResult setEntityTransform(const Json& arguments);
    ActionResult renameEntity(const Json& arguments);
    ActionResult deleteEntity(const Json& arguments);
    ActionResult duplicateEntity(const Json& arguments);
    ActionResult reparentEntity(const Json& arguments);
    ActionResult moveEntityOrder(const Json& arguments);
    ActionResult selectEntities(const Json& arguments);
    ActionResult addComponent(const Json& arguments);
    ActionResult removeComponent(const Json& arguments);
    ActionResult addBody3DShape(const Json& arguments);
    ActionResult addBody2DShape(const Json& arguments);
    ActionResult setComponentProperty(const Json& arguments);
    ActionResult setTextureSettings(const Json& arguments);
    ActionResult addProjectScene(const Json& arguments);
    ActionResult createScene(const Json& arguments);
    ActionResult renameScene(const Json& arguments);
    ActionResult saveScene(const Json& arguments);
    ActionResult saveAllScenes();
    ActionResult setStartScene(const Json& arguments);
    ActionResult setSceneProperty(const Json& arguments);
    ActionResult controlPlayMode(const Json& arguments);
    ActionResult addChildScene(const Json& arguments);
    ActionResult removeChildScene(const Json& arguments);
    ActionResult setChildSceneStartActive(const Json& arguments);
    ActionResult loadChildSceneInline(const Json& arguments);
    ActionResult createFolder(const Json& arguments);
    ActionResult renameResource(const Json& arguments);
    ActionResult deleteResource(const Json& arguments);
    ActionResult importResourceFile(const Json& arguments);
    ActionResult linkMaterial(const Json& arguments);
    ActionResult unlinkMaterial(const Json& arguments);
    ActionResult createMaterialFile(const Json& arguments);
    ActionResult setTerrainTextures(const Json& arguments);
    ActionResult setTerrainFoliageLayer(const Json& arguments);
    ActionResult setEditorCamera(const Json& arguments);
    ActionResult createTerrainBlendmap(const Json& arguments);
    ActionResult addTerrainCollision(const Json& arguments);
    ActionResult createScript(const Json& arguments);
    ActionResult attachScript(const Json& arguments);
    ActionResult updateScriptEntry(const Json& arguments);
    ActionResult removeScriptEntry(const Json& arguments);
    ActionResult updateScriptFile(const Json& arguments);
    ActionResult createSourceFile(const Json& arguments);
    ActionResult setProjectScriptDirs(const Json& arguments);
    ActionResult setProjectAssetDirs(const Json& arguments);
    ActionResult checkCppBuildTools();
    ActionResult setProjectCxxStandard(const Json& arguments);
    ActionResult updateProjectBuildFile(const Json& arguments);
    ActionResult createBundleFromEntity(const Json& arguments);
    ActionResult importBundleInstance(const Json& arguments);
    ActionResult addEntityToBundle(const Json& arguments);
    ActionResult removeEntityFromBundle(const Json& arguments);
    ActionResult makeBundleComponentUnique(const Json& arguments);
    ActionResult revertBundleComponent(const Json& arguments);
    ActionResult setStandaloneBundle(const Json& arguments);
    ActionResult exportProject(const Json& arguments, const std::atomic<bool>* cancel);
    ActionResult setExportShaders(const Json& arguments);
    ActionResult generateShaders(const Json& arguments, const std::atomic<bool>* cancel);
    ActionResult forkShader(const Json& arguments);
    ActionResult writeShaderFile(const Json& arguments);
    ActionResult inspectShaderUniforms(const Json& arguments);
    ActionResult setShaderUniform(const Json& arguments);
    // Rejects a customShader value whose resolved .vert/.frag files do not exist.
    bool validateCustomShaderValue(const std::string& propertyName, const Json& args, std::string& error) const;
    ActionResult createTerrainHeightmap(const Json& arguments);
    ActionResult importProjectModel(const Json& arguments);
    ActionResult reloadProjectModel(const Json& arguments);
    ActionResult searchCuratedAssets(const Json& arguments, const std::atomic<bool>* cancel);
    ActionResult downloadCuratedAsset(const Json& arguments, const std::atomic<bool>* cancel);

    ActionResult readResourceFile(const Json& arguments);
    ActionResult setMainCamera(const Json& arguments);
    ActionResult openScene(const Json& arguments);
    ActionResult selectScene(const Json& arguments);
    ActionResult regenerateMeshGeometry(const Json& arguments);
    ActionResult deleteScene(const Json& arguments);
    ActionResult saveProject(const Json& arguments);
    ActionResult setProjectSettings(const Json& arguments);
    ActionResult createProject(const Json& arguments);
    ActionResult openProject(const Json& arguments);
    ActionResult copyResource(const Json& arguments);
    ActionResult updateMaterialFile(const Json& arguments);
    ActionResult setComponentProperties(const Json& arguments);
    ActionResult inspectScene(const Json& arguments);
    ActionResult addTilemapTile(const Json& arguments);
    ActionResult removeTilemapTile(const Json& arguments);
    ActionResult duplicateTilemapTile(const Json& arguments);
    ActionResult addAnimationAction(const Json& arguments);
    ActionResult removeAnimationAction(const Json& arguments);
    ActionResult setKeyframeTimes(const Json& arguments);
    ActionResult setKeyframeEasing(const Json& arguments);
    ActionResult setKeyframeTrack(const Json& arguments);
    ActionResult undoEditor(const Json& arguments);
    ActionResult redoEditor(const Json& arguments);
};

} // namespace doriax::editor::ai
