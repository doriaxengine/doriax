// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneWindow.h"
#include "util/BidiText.h"

#include "Engine.h"
#include "Input.h"
#include "external/IconsFontAwesome6.h"
#include "Backend.h"
#include "util/Util.h"
#include "util/EntityPayload.h"
#include "util/TextEncoding.h"
#include "command/CommandHandle.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/LinkMaterialCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/ModelLoadCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "util/ProjectUtils.h"
#include "render/SceneRender2D.h"
#include "render/SceneRender3D.h"
#include "subsystem/MeshSystem.h"
#include "Stream.h"
#include "Out.h"
#include "App.h"
#include "EditorHost.h"
#include "Theme.h"
#include "Widgets.h"

#include "math/Vector2.h"
#include "util/Angle.h"
#include <algorithm>
#include <cmath>

using namespace doriax;

namespace {

// with repeat, linear filtering bleeds the opposite edge into the border
Texture edgeClampedTexture(const std::string& path) {
    Texture texture(path);
    texture.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    texture.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    return texture;
}

constexpr float VIEWPORT_GIZMO_SIZE = 100.0f;

}

editor::SceneWindow::SceneWindow(Project* project) {
    this->project = project;
    this->mouseLeftDraggedInside = false;
    this->windowFocused = false;
}

void editor::SceneWindow::handleCloseScene(uint32_t sceneId) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        project->checkUnsavedAndExecute(sceneId, [this, sceneId]() {
            closeSceneInternal(sceneId);
        });
    }
}

void editor::SceneWindow::closeSceneInternal(uint32_t sceneId) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (sceneProject) {
        // If we're closing the currently selected scene
        if (project->getSelectedSceneId() == sceneId) {
            // Find another scene to select
            for (const auto& otherScene : project->getScenes()) {
                if (otherScene.id != sceneId && otherScene.opened) {
                    project->setSelectedSceneId(otherScene.id);
                    ImGui::SetWindowFocus(("###Scene" + std::to_string(otherScene.id)).c_str());
                    break;
                }
            }
        }
        closeSceneQueue.push_back(sceneId);
    }
}

bool editor::SceneWindow::isFocused() const {
    return windowFocused;
}

void editor::SceneWindow::resetProjectState() {
    releasePlayKeys(0);
    releasePlayMouseButtons(0);
    for (const auto& entry : lookActive) {
        // Only one look can be active, and endLook only rewrites keys that exist
        if (entry.second) { endLook(entry.first); break; }
    }
    windowFocused = false;
    mouseLeftDown = false;
    mouseLeftDraggedInside = false;
    draggingMouse.clear();
    suppressLeftMouseUntilRelease.clear();
    subSelectionClickConsumesRelease.clear();
    resyncLookDelta.clear();
    lookActive.clear();
    lookReturnPos.clear();
    focusCanvasOnNextFrame.clear();
    fullscreenSceneId = NULL_PROJECT_SCENE;
    width.clear();
    height.clear();
    framebufferScale.clear();
    canvasDpiScale.clear();
    hasNotification.clear();
    structureSelectionParents.clear();
    closeSceneQueue.clear();
}

void editor::SceneWindow::clearSceneState(uint32_t sceneId) {
    if (sceneId == playKeysSceneId) {
        releasePlayKeys(0);
    }
    if (sceneId == playMouseSceneId) {
        releasePlayMouseButtons(0);
    }
    if (lookActive[sceneId]) {
        endLook(sceneId);
    }
    draggingMouse.erase(sceneId);
    suppressLeftMouseUntilRelease.erase(sceneId);
    subSelectionClickConsumesRelease.erase(sceneId);
    resyncLookDelta.erase(sceneId);
    lookActive.erase(sceneId);
    lookReturnPos.erase(sceneId);
    focusCanvasOnNextFrame.erase(sceneId);
    if (fullscreenSceneId == sceneId) {
        fullscreenSceneId = NULL_PROJECT_SCENE;
    }
    width.erase(sceneId);
    height.erase(sceneId);
    framebufferScale.erase(sceneId);
    canvasDpiScale.erase(sceneId);
    hasNotification.erase(sceneId);
    structureSelectionParents.erase(sceneId);

    closeSceneQueue.erase(
        std::remove(closeSceneQueue.begin(), closeSceneQueue.end(), sceneId),
        closeSceneQueue.end());
}

void editor::SceneWindow::beginStructureVisibilityUpdate(uint32_t sceneId) {
    structureSelectionParents.erase(sceneId);
}

void editor::SceneWindow::setStructureSelectionParent(uint32_t sceneId, Entity entity, Entity selectionParent) {
    if (entity == NULL_ENTITY || selectionParent == NULL_ENTITY || entity == selectionParent) {
        return;
    }
    structureSelectionParents[sceneId][entity] = selectionParent;
}

Entity editor::SceneWindow::resolveStructureSelection(uint32_t viewportSceneId, uint32_t sceneId, Entity entity) const {
    auto sceneIt = structureSelectionParents.find(sceneId);
    if (sceneIt == structureSelectionParents.end()) {
        return entity;
    }

    const SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return entity;
    }

    // The 2D gizmo is drawn from the entity's own bounds, so a group parent without any
    // would be an invisible selection. The other gizmos outline the whole family.
    const SceneProject* viewportScene = project->getScene(viewportSceneId);
    bool outlinesFamily = viewportScene && viewportScene->sceneRender
        && viewportScene->sceneRender->getToolsLayer()->getGizmoSelected() != GizmoSelected::OBJECT2D;

    // Keep the outermost collapsed ancestor that can be selected, else the entity itself
    Entity resolved = entity;
    auto entityIt = sceneIt->second.find(entity);
    while (entityIt != sceneIt->second.end()) {
        Entity ancestor = entityIt->second;
        if (!sceneProject->scene->isEntityCreated(ancestor)) {
            break;
        }
        if (outlinesFamily || project->isEntityPickable(sceneId, ancestor)) {
            resolved = ancestor;
        }
        entityIt = sceneIt->second.find(ancestor);
    }

    return resolved;
}

Entity editor::SceneWindow::findSelectableObjectByRay(uint32_t sceneId, float x, float y, uint32_t* outSceneId) {
    uint32_t hitSceneId = sceneId;
    Entity hitEntity = project->findObjectByRay(sceneId, x, y, outSceneId ? &hitSceneId : nullptr);
    if (outSceneId) {
        *outSceneId = hitSceneId;
    }
    return resolveStructureSelection(sceneId, hitSceneId, hitEntity);
}

bool editor::SceneWindow::selectObjectByRay(uint32_t sceneId, float x, float y, bool shiftPressed) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->sceneRender) {
        return false;
    }

    project->setSelectedSceneForProperties(sceneId);

    uint32_t hitSceneId = sceneId;
    Entity hitEntity = findSelectableObjectByRay(sceneId, x, y, &hitSceneId);

    if (!sceneProject->sceneRender->isAnyGizmoSideSelected()) {
        if (hitEntity != NULL_ENTITY) {
            if (hitSceneId != sceneId) {
                project->setSelectedSceneForProperties(hitSceneId);
            }
            if (!shiftPressed) {
                project->clearAllSelections(sceneId);
            }
            project->addSelectedEntity(hitSceneId, hitEntity);
            if (Structure* structure = Backend::getApp().getStructureWindow()) {
                structure->revealEntity(hitSceneId, hitEntity);
            }
            return true;
        }

        project->clearAllSelections(sceneId);
    }

    return false;
}

void editor::SceneWindow::requestPlayFocus(uint32_t sceneId) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->opened) {
        return;
    }

    project->setSelectedSceneId(sceneId);
    focusCanvasOnNextFrame[sceneId] = true;
    focusSceneWindow(*sceneProject);
}

void editor::SceneWindow::toggleFullscreen(uint32_t sceneId) {
    if (fullscreenSceneId != NULL_PROJECT_SCENE) {
        exitFullscreen();
        return;
    }

    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->opened ||
        (sceneProject->playState != ScenePlayState::PLAYING && sceneProject->playState != ScenePlayState::PAUSED)) {
        return;
    }

    fullscreenSceneId = sceneId;
    fullscreenStartTime = ImGui::GetTime();
    focusCanvasOnNextFrame[sceneId] = true;
}

void editor::SceneWindow::exitFullscreen() {
    const uint32_t sceneId = fullscreenSceneId;
    fullscreenSceneId = NULL_PROJECT_SCENE;
    requestPlayFocus(sceneId);
}

bool editor::SceneWindow::isFullscreen() const {
    return fullscreenSceneId != NULL_PROJECT_SCENE;
}

bool editor::SceneWindow::viewThroughCamera(uint32_t sceneId, Entity cameraEntity) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene || !sceneProject->sceneRender) {
        return false;
    }
    if (sceneProject->playState != ScenePlayState::STOPPED) {
        return false;
    }
    if (!sceneProject->sceneRender->setPreviewCamera(cameraEntity)) {
        return false;
    }

    project->setSelectedSceneId(sceneId);
    sceneProject->needUpdateRender = true;
    focusSceneWindow(*sceneProject);
    return true;
}

void editor::SceneWindow::stopViewingCamera(uint32_t sceneId) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->sceneRender) {
        return;
    }

    sceneProject->sceneRender->clearPreviewCamera();
    sceneProject->needUpdateRender = true;
}

void editor::SceneWindow::focusSceneWindow(const SceneProject& sceneProject) const {
    ImGui::SetWindowFocus(("###Scene" + std::to_string(sceneProject.id)).c_str());
}

void editor::SceneWindow::requestOpenParentScene(uint32_t childSceneId, uint32_t parentSceneId) {
    // Defer until the current viewport image has been submitted.
    editor::getEditorHost().enqueueMainThreadTask([this, childSceneId, parentSceneId]() {
        if (project->isAnyScenePlaying()) {
            Out::warning("Cannot open a new scene while a scene is playing.");
            return;
        }
        if (!project->getScene(childSceneId) || !project->getScene(parentSceneId)) {
            return;
        }

        project->checkUnsavedAndExecute(childSceneId, [this, childSceneId, parentSceneId]() {
            if (project->isAnyScenePlaying()) {
                Out::warning("Cannot open a new scene while a scene is playing.");
                return;
            }

            SceneProject* parentScene = project->getScene(parentSceneId);
            if (!parentScene) {
                return;
            }

            const fs::path parentPath = parentScene->filepath;
            project->closeScene(childSceneId, true);

            if (parentPath.empty()) {
                project->setSelectedSceneId(parentSceneId);
            } else {
                project->openScene(parentPath, false);
            }

            parentScene = project->getScene(parentSceneId);
            if (parentScene && parentScene->opened) {
                focusSceneWindow(*parentScene);
            }
        });
    });
}

void editor::SceneWindow::drawParentSceneButton(const SceneProject& sceneProject) {
    std::vector<const SceneProject*> parentScenes;
    for (const SceneProject& possibleParent : project->getScenes()) {
        if (project->hasChildScene(possibleParent.id, sceneProject.id)) {
            parentScenes.push_back(&possibleParent);
        }
    }

    if (parentScenes.empty()) {
        return;
    }

    const std::string popupId = "ParentScenePopup##" + std::to_string(sceneProject.id);
    const std::string buttonId = std::string(ICON_FA_ARROW_TURN_UP) + " Parent##ParentScene" + std::to_string(sceneProject.id);

    ImGui::SameLine(0, Theme::dpi(10.0f));
    ImGui::BeginDisabled(project->isAnyScenePlaying());
    if (ImGui::Button(buttonId.c_str())) {
        if (parentScenes.size() == 1) {
            requestOpenParentScene(sceneProject.id, parentScenes.front()->id);
        } else {
            ImGui::OpenPopup(popupId.c_str());
        }
    }

    if (parentScenes.size() == 1) {
        const std::string parentName = BidiText::toVisual(parentScenes.front()->name);
        ImGui::SetItemTooltip("Return to parent scene '%s'", parentName.c_str());
    } else {
        ImGui::SetItemTooltip("Choose a parent scene");
    }

    if (ImGui::BeginPopup(popupId.c_str())) {
        for (const SceneProject* parentScene : parentScenes) {
            const std::string label = BidiText::toVisual(parentScene->name)
                + "##ParentScene" + std::to_string(parentScene->id);
            if (ImGui::MenuItem(label.c_str())) {
                requestOpenParentScene(sceneProject.id, parentScene->id);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
}

std::string editor::SceneWindow::getWindowTitle(const SceneProject& sceneProject) const {
    std::string icon;
    if (sceneProject.sceneType == SceneType::SCENE_3D){
        icon = ICON_FA_CUBES + std::string("  ");
    }else if (sceneProject.sceneType == SceneType::SCENE_2D){
        icon = ICON_FA_CUBES_STACKED + std::string("  ");
    }else if (sceneProject.sceneType == SceneType::SCENE_UI){
        icon = ICON_FA_WINDOW_RESTORE + std::string("  ");
    }
    // everything before "###" is drawn, the id after it keeps docking state stable
    return icon + BidiText::toVisual(sceneProject.name) + ((project->hasSceneUnsavedChanges(sceneProject.id)) ? " *" : "") + "###Scene" + std::to_string(sceneProject.id);
}

Vector3 editor::SceneWindow::getModelDropPosition(SceneProject* sceneProject, float x, float y, Entity hitEntity) {
    Camera* camera = sceneProject->sceneRender->getCamera();
    Ray ray = camera->screenToRay(x, y);

    if (hitEntity != NULL_ENTITY) {
        AABB entityAABB = sceneProject->sceneRender->getEntitiesAABB({hitEntity});
        RayReturn entityHit = ray.intersects(entityAABB);
        if (entityHit) {
            // If the hit entity is terrain, use its precise surface height instead of the AABB top
            Vector3 terrainPosition;
            TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(hitEntity);
            Transform* transform = sceneProject->scene->findComponent<Transform>(hitEntity);
            if (terrain && transform && sceneProject->scene->getSystem<MeshSystem>()->raycastTerrainSurface(ray, *terrain, *transform, terrainPosition)) {
                return terrainPosition;
            }
            return entityHit.point;
        }
    }

    RayReturn hit = ray.intersects(Plane(Vector3(0, 1, 0), Vector3(0, 0, 0)));
    if (hit) {
        return hit.point;
    }

    return camera->getTarget();
}

void editor::SceneWindow::handleResourceFileDragDrop(SceneProject* sceneProject) {
    static bool draggingResourceFile = false;
    static Image* tempImage = nullptr;
    static MeshComponent* selMesh = nullptr;
    static uint32_t selMeshSceneId = NULL_PROJECT_SCENE;
    static UIComponent* selUI = nullptr;
    static TextComponent* selText = nullptr;
    static std::vector<Material> originalMeshMaterials;
    static unsigned int originalMeshNumSubmeshes = 0;
    static Material cachedDragMaterial;
    static std::string cachedDragPath;
    static Texture originalTex;
    static FontArray originalFont;
    static Entity lastSelEntity = NULL_ENTITY;

    // fetched by id each frame: the pointer goes stale, and the scene can close during the drag
    SceneProject* selMeshScene = nullptr;
    if (selMesh) {
        selMeshScene = project->getScene(selMeshSceneId);
        selMesh = selMeshScene ? selMeshScene->scene->findComponent<MeshComponent>(lastSelEntity) : nullptr;
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* peekPayload = ImGui::GetDragDropPayload();
        if (peekPayload && peekPayload->IsDataType("resource_files")) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(peekPayload);
            if (receivedStrings.size() > 0) {
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();
                // Fonts, images and models are stored relative to the assets root, material
                // files stay project-relative. Outside the root a drop previews but is refused.
                const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                const std::string droppedAssetPath = insideAssets
                    ? project->normalizeToAssetsRelative(receivedStrings[0]).generic_string()
                    : std::filesystem::path(receivedStrings[0]).generic_string();
                bool isFont = Util::isFontFile(droppedRelativePath);
                bool isImage = Util::isImageFile(droppedRelativePath);
                bool isMaterial = Util::isMaterialFile(droppedRelativePath);
                bool isModel = Util::isModelFile(droppedRelativePath);

                draggingResourceFile = true;
                ImVec2 windowPos = ImGui::GetWindowPos();
                ImGuiIO& io = ImGui::GetIO();
                ImVec2 mousePos = io.MousePos;
                float x = mousePos.x - windowPos.x;
                float y = mousePos.y - windowPos.y;
                toEngineCanvas(sceneProject->id, x, y);
                Entity selEntity = project->findObjectByRay(sceneProject->id, x, y);

                if (selEntity == NULL_ENTITY || lastSelEntity != selEntity) {
                    if (selMesh) {
                        for (unsigned int s = 0; s < originalMeshNumSubmeshes && s < selMesh->numSubmeshes; s++) {
                            if (selMesh->submeshes[s].material != originalMeshMaterials[s]) {
                                selMesh->submeshes[s].material = originalMeshMaterials[s];
                                selMesh->submeshes[s].needUpdateTexture = true;
                            }
                        }
                        Catalog::updateSpriteBatch(selMeshScene->scene, lastSelEntity);
                        selMesh = nullptr;
                        originalMeshMaterials.clear();
                    }
                    if (selUI) {
                        if (selUI->texture != originalTex) {
                            selUI->texture = originalTex;
                            selUI->needUpdateTexture = true;
                        }
                        selUI = nullptr;
                    }
                    if (selText) {
                        if (selText->font != originalFont) {
                            selText->font = originalFont;
                            selText->needReloadAtlas = true;
                            selText->needUpdateText = true;
                        }
                        selText = nullptr;
                    }
                    cachedDragPath.clear();
                }

                if (isModel && sceneProject->sceneType == SceneType::SCENE_3D) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                        if (payload->IsDelivery() && !insideAssets) {
                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                            focusSceneWindow(*sceneProject);
                        } else if (payload->IsDelivery()) {
                            std::string modelEntityName = std::filesystem::path(droppedAssetPath).stem().string();
                            if (modelEntityName.empty()) {
                                modelEntityName = "Model";
                            }

                            Vector3 dropPosition = getModelDropPosition(sceneProject, x, y, selEntity);
                            CommandHandle::get(sceneProject->id)->addCommandNoMerge(
                                new ModelLoadCmd(project, sceneProject->id, modelEntityName, dropPosition, droppedAssetPath));

                            focusSceneWindow(*sceneProject);
                        }
                    }
                } else if (selEntity != NULL_ENTITY) {
                    bool accept = false;
                    if (isFont && sceneProject->scene->findComponent<TextComponent>(selEntity)) {
                        accept = true;
                    }
                    if (isImage && (sceneProject->scene->findComponent<MeshComponent>(selEntity) || sceneProject->scene->findComponent<UIComponent>(selEntity))) {
                        accept = true;
                    }
                    if (isMaterial && sceneProject->scene->findComponent<MeshComponent>(selEntity)) {
                        accept = true;
                    }

                    if (accept) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                            lastSelEntity = selEntity;

                            if (tempImage != nullptr) {
                                delete tempImage;
                                tempImage = nullptr;
                            }

                            if (TextComponent* text = sceneProject->scene->findComponent<TextComponent>(selEntity)) {
                                if (isFont) {
                                    if (!selText) {
                                        selText = text;
                                        originalFont = text->font;
                                    }
                                    // the drop replaces the main font, the fallbacks stay
                                    if (text->font[0] != droppedAssetPath) {
                                        text->font[0] = droppedAssetPath;
                                        text->needReloadAtlas = true;
                                        text->needUpdateText = true;
                                    }
                                    if (payload->IsDelivery()) {
                                        std::string propName = "font";
                                        text->font = originalFont;
                                        text->needReloadAtlas = true;
                                        text->needUpdateText = true;

                                        if (insideAssets) {
                                            FontArray newFonts = originalFont;
                                            newFonts[0] = droppedAssetPath;
                                            PropertyCmd<FontArray>* cmd = new PropertyCmd<FontArray>(project, sceneProject->id, selEntity, ComponentType::TextComponent, propName, newFonts);
                                            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);
                                        } else {
                                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                                        }

                                        selText = nullptr;

                                        focusSceneWindow(*sceneProject);
                                    }
                                }
                            } else if (isImage) {
                                if (MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(selEntity)) {
                                    if (!selMesh) {
                                        selMesh = mesh;
                                        selMeshSceneId = sceneProject->id;
                                        originalMeshMaterials.clear();
                                        originalMeshNumSubmeshes = mesh->numSubmeshes;
                                        for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                            originalMeshMaterials.push_back(mesh->submeshes[s].material);
                                        }
                                        originalTex = mesh->submeshes[0].material.baseColorTexture;
                                    }
                                    Catalog::updateSpriteBatch(sceneProject->scene, selEntity);
                                    const bool isSprite = sceneProject->scene->findComponent<SpriteComponent>(selEntity) != nullptr;
                                    Texture newTex = isSprite ? edgeClampedTexture(droppedAssetPath) : Texture(droppedAssetPath);
                                    if (mesh->submeshes[0].material.baseColorTexture != newTex) {
                                        mesh->submeshes[0].material.baseColorTexture = newTex;
                                        mesh->submeshes[0].needUpdateTexture = true;
                                    }
                                    if (payload->IsDelivery()) {
                                        std::string propName = "submeshes[0].material.baseColorTexture";
                                        for (unsigned int s = 0; s < originalMeshNumSubmeshes && s < selMesh->numSubmeshes; s++) {
                                            selMesh->submeshes[s].material = originalMeshMaterials[s];
                                            selMesh->submeshes[s].needUpdateTexture = true;
                                        }

                                        if (insideAssets) {
                                            PropertyCmd<Texture>* cmd = new PropertyCmd<Texture>(project, sceneProject->id, selEntity, ComponentType::MeshComponent, propName, newTex);
                                            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);
                                        } else {
                                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                                        }

                                        selMesh = nullptr;
                                        originalMeshMaterials.clear();

                                        focusSceneWindow(*sceneProject);
                                    }
                                } else if (UIComponent* ui = sceneProject->scene->findComponent<UIComponent>(selEntity)) {
                                    if (!selUI) {
                                        selUI = ui;
                                        originalTex = ui->texture;
                                    }
                                    Texture newTex = edgeClampedTexture(droppedAssetPath);
                                    if (ui->texture != newTex) {
                                        ui->texture = newTex;
                                        ui->needUpdateTexture = true;
                                    }
                                    if (payload->IsDelivery()) {
                                        std::string propName = "texture";
                                        selUI->texture = originalTex;

                                        if (insideAssets) {
                                            PropertyCmd<Texture>* cmd = new PropertyCmd<Texture>(project, sceneProject->id, selEntity, ComponentType::UIComponent, propName, newTex);
                                            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);
                                        } else {
                                            selUI->needUpdateTexture = true;
                                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                                        }

                                        selUI = nullptr;

                                        focusSceneWindow(*sceneProject);
                                    }
                                }
                            } else if (isMaterial) {
                                if (MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(selEntity)) {
                                    try {
                                        // Cache the decoded material — only reload when the path changes
                                        if (cachedDragPath != droppedRelativePath) {
                                            fs::path materialPath = project->getProjectPath() / droppedRelativePath;
                                            YAML::Node materialNode = TextEncoding::loadYamlFile(materialPath);
                                            cachedDragMaterial = Stream::decodeMaterial(materialNode);
                                            cachedDragMaterial.name = fs::path(droppedRelativePath).lexically_normal().generic_string();
                                            cachedDragPath = droppedRelativePath;
                                        }

                                        if (!selMesh) {
                                            selMesh = mesh;
                                            selMeshSceneId = sceneProject->id;
                                            originalMeshMaterials.clear();
                                            originalMeshNumSubmeshes = mesh->numSubmeshes;
                                            for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                                originalMeshMaterials.push_back(mesh->submeshes[s].material);
                                            }
                                        }
                                        Catalog::updateSpriteBatch(sceneProject->scene, selEntity);

                                        // Preview: apply to all submeshes
                                        for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                            if (mesh->submeshes[s].material != cachedDragMaterial) {
                                                mesh->submeshes[s].material = cachedDragMaterial;
                                                mesh->submeshes[s].needUpdateTexture = true;
                                            }
                                        }

                                        if (payload->IsDelivery()) {
                                            // Restore originals before issuing commands
                                            for (unsigned int s = 0; s < originalMeshNumSubmeshes && s < selMesh->numSubmeshes; s++) {
                                                selMesh->submeshes[s].material = originalMeshMaterials[s];
                                                selMesh->submeshes[s].needUpdateTexture = true;
                                            }

                                            // Create a multi-command for all submeshes (link + property)
                                            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                                            for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                                std::string propName = "submeshes[" + std::to_string(s) + "].material";
                                                multiCmd->addCommand(std::make_unique<LinkMaterialCmd>(
                                                    project, sceneProject->id, selEntity, ComponentType::MeshComponent, propName, s, cachedDragMaterial));
                                            }
                                            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(multiCmd);

                                            selMesh = nullptr;
                                            originalMeshMaterials.clear();
                                            cachedDragPath.clear();

                                            focusSceneWindow(*sceneProject);
                                        }
                                    } catch (const std::exception& e) {
                                        Out::error("Error loading material file '%s': %s", droppedRelativePath.c_str(), e.what());
                                    }
                                }
                            }
                        }
                    }
                } else if (sceneProject->sceneType != SceneType::SCENE_3D) {
                    if (isImage) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {

                            if (!tempImage) {
                                tempImage = new Image(sceneProject->scene);
                                tempImage->setTexture(droppedAssetPath);
                                tempImage->setAlpha(0.5f);
                            }
                            Ray ray = sceneProject->sceneRender->getCamera()->screenToRay(x, y);
                            RayReturn rreturn = ray.intersects(Plane(Vector3(0, 0, 1), Vector3(0, 0, 0)));
                            if (rreturn) {
                                tempImage->setPosition(rreturn.point);
                            }
                            if (payload->IsDelivery() && !insideAssets) {
                                Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);

                                delete tempImage;
                                tempImage = nullptr;

                                focusSceneWindow(*sceneProject);
                            } else if (payload->IsDelivery()) {
                                CreateEntityCmd* cmd = nullptr;

                                if (sceneProject->sceneType == SceneType::SCENE_2D) {
                                    cmd = new CreateEntityCmd(project, sceneProject->id, "Sprite", EntityCreationType::SPRITE);

                                    cmd->addProperty<Vector3>(ComponentType::Transform, "position", rreturn.point);
                                    cmd->addProperty<Texture>(ComponentType::MeshComponent, "submeshes[0].material.baseColorTexture", edgeClampedTexture(droppedAssetPath));
                                    cmd->addProperty<unsigned int>(ComponentType::SpriteComponent, "width", tempImage->getWidth());
                                    cmd->addProperty<unsigned int>(ComponentType::SpriteComponent, "height", tempImage->getHeight());
                                } else {
                                    cmd = new CreateEntityCmd(project, sceneProject->id, "Image", EntityCreationType::IMAGE);

                                    cmd->addProperty<Vector3>(ComponentType::Transform, "position", rreturn.point);
                                    cmd->addProperty<Texture>(ComponentType::UIComponent, "texture", edgeClampedTexture(droppedAssetPath));
                                    cmd->addProperty<unsigned int>(ComponentType::UILayoutComponent, "width", tempImage->getWidth());
                                    cmd->addProperty<unsigned int>(ComponentType::UILayoutComponent, "height", tempImage->getHeight());
                                }

                                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);

                                delete tempImage;
                                tempImage = nullptr;

                                focusSceneWindow(*sceneProject);
                            }

                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    } else {
        if (draggingResourceFile) {
            // If user released mouse (ended drag) and we have a temp texture applied, revert it
            if (selMesh) {
                for (unsigned int s = 0; s < originalMeshNumSubmeshes && s < selMesh->numSubmeshes; s++) {
                    if (selMesh->submeshes[s].material != originalMeshMaterials[s]) {
                        selMesh->submeshes[s].material = originalMeshMaterials[s];
                        selMesh->submeshes[s].needUpdateTexture = true;
                    }
                }
                Catalog::updateSpriteBatch(selMeshScene->scene, lastSelEntity);
                selMesh = nullptr;
                originalMeshMaterials.clear();
            }
            if (selUI) {
                if (selUI->texture != originalTex) {
                    selUI->texture = originalTex;
                    selUI->needUpdateTexture = true;
                }
                selUI = nullptr;
            }
            if (selText) {
                if (selText->font != originalFont) {
                    selText->font = originalFont;
                    selText->needReloadAtlas = true;
                    selText->needUpdateText = true;
                }
                selText = nullptr;
            }
            draggingResourceFile = false; // Reset flag
            lastSelEntity = NULL_ENTITY;
            cachedDragPath.clear();
        }
        if (tempImage != nullptr) {
            delete tempImage;
            tempImage = nullptr;
        }
    }
}

void editor::SceneWindow::handleTileRectDragDrop(SceneProject* sceneProject) {
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* peekPayload = ImGui::GetDragDropPayload();
        if (peekPayload && peekPayload->IsDataType("tile_rect")) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("tile_rect")) {
                const TileRectPayload* tileRectPayload = static_cast<const TileRectPayload*>(payload->Data);

                if (tileRectPayload->sceneId == sceneProject->id) {
                    Entity entity = tileRectPayload->entity;
                    int rectIndex = tileRectPayload->rectIndex;

                    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
                    Transform* transform = sceneProject->scene->findComponent<Transform>(entity);

                    if (tilemap && transform && rectIndex >= 0 && rectIndex < (int)tilemap->numTilesRect) {
                        // Compute drop position in tilemap local space
                        ImVec2 windowPos = ImGui::GetWindowPos();
                        ImGuiIO& io = ImGui::GetIO();
                        float x = io.MousePos.x - windowPos.x;
                        float y = io.MousePos.y - windowPos.y;
                        toEngineCanvas(sceneProject->id, x, y);

                        Ray ray = sceneProject->sceneRender->getCamera()->screenToRay(x, y);
                        Plane tilePlane(Vector3(0, 0, 1), transform->worldPosition);
                        RayReturn rr = ray.intersects(tilePlane);

                        if (rr) {
                            Matrix4 invModel = transform->modelMatrix.inverse();
                            Vector3 localHit = invModel * rr.point;

                            const TileRectData& rectData = tilemap->tilesRect[rectIndex];
                            float tileW = rectData.rect.getWidth();
                            float tileH = rectData.rect.getHeight();

                            // Center tile on drop point and clamp to non-negative
                            float posX = std::max(0.0f, localHit.x - tileW * 0.5f);
                            float posY = std::max(0.0f, localHit.y - tileH * 0.5f);

                            // Snap to tile size so dropped tiles pack edge-to-edge
                            if (sceneProject->displaySettings.snapTile && tileW > 0.0f && tileH > 0.0f) {
                                posX = std::max(0.0f, std::round(posX / tileW) * tileW);
                                posY = std::max(0.0f, std::round(posY / tileH) * tileH);
                            }

                            int freeSlot = (int)tilemap->numTiles;
                            if (tilemap->tiles.validIndex(freeSlot)) {
                                std::string tilePrefix = "tiles[" + std::to_string(freeSlot) + "]";
                                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".name", rectData.name);
                                multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".rectId", rectIndex);
                                multiCmd->addPropertyCmd<Vector2>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".position", Vector2(posX, posY));
                                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".width", tileW);
                                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".height", tileH);
                                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    "numTiles", (unsigned int)(freeSlot + 1));
                                multiCmd->setNoMerge();
                                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                                focusSceneWindow(*sceneProject);
                            }
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void editor::SceneWindow::forwardPlayKeyboardInput(ImGuiIO& io, int mods){
    for (int i = 0; i < io.InputQueueCharacters.Size; i++){
        const ImWchar codepoint = io.InputQueueCharacters[i];
        // Other control characters would show up as boxes
        if ((codepoint >= 32 && codepoint != 0x7f) || codepoint == '\t' || codepoint == '\b' ||
            codepoint == '\r' || codepoint == '\n' || codepoint == 0x1b){
            Engine::systemCharInput(static_cast<wchar_t>(codepoint));
        }
    }
    io.InputQueueCharacters.resize(0);

    auto forwardKeyPress = [&](ImGuiKey imguiKey, int engineKey, wchar_t charInput = 0, bool allowRepeat = true){
#ifndef __APPLE__
        // Only imgui_impl_osx leaves control characters out of the queue
        charInput = 0;
#endif
        if (ImGui::IsKeyPressed(imguiKey, false)){
            if (charInput != 0){
                Engine::systemCharInput(charInput);
            }
            Engine::systemKeyDown(engineKey, false, mods);
            playPressedKeys.insert(engineKey);
        }else if (allowRepeat && ImGui::IsKeyPressed(imguiKey, true)){
            if (charInput != 0){
                Engine::systemCharInput(charInput);
            }
            Engine::systemKeyDown(engineKey, true, mods);
            playPressedKeys.insert(engineKey);
        }
    };

    auto forwardKeyRelease = [&](ImGuiKey imguiKey, int engineKey){
        if (ImGui::IsKeyReleased(imguiKey)){
            Engine::systemKeyUp(engineKey, false, mods);
            playPressedKeys.erase(engineKey);
        }
    };

    auto forwardKey = [&](ImGuiKey imguiKey, int engineKey, wchar_t charInput = 0, bool allowRepeat = true){
        forwardKeyPress(imguiKey, engineKey, charInput, allowRepeat);
        forwardKeyRelease(imguiKey, engineKey);
    };

    auto forwardKeyRange = [&](ImGuiKey imguiFirst, int engineFirst, int count){
        for (int i = 0; i < count; i++){
            forwardKey(static_cast<ImGuiKey>(static_cast<int>(imguiFirst) + i), engineFirst + i);
        }
    };

    forwardKey(ImGuiKey_Tab, D_KEY_TAB, L'\t', false);
    forwardKey(ImGuiKey_Backspace, D_KEY_BACKSPACE, L'\b');
    forwardKey(ImGuiKey_Enter, D_KEY_ENTER, L'\r', false);
    forwardKey(ImGuiKey_KeypadEnter, D_KEY_KP_ENTER, L'\r', false);
    forwardKey(ImGuiKey_Escape, D_KEY_ESCAPE, L'\x1b', false);
    forwardKey(ImGuiKey_Space, D_KEY_SPACE);
    forwardKey(ImGuiKey_LeftArrow, D_KEY_LEFT);
    forwardKey(ImGuiKey_RightArrow, D_KEY_RIGHT);
    forwardKey(ImGuiKey_UpArrow, D_KEY_UP);
    forwardKey(ImGuiKey_DownArrow, D_KEY_DOWN);
    forwardKey(ImGuiKey_Insert, D_KEY_INSERT, 0, false);
    forwardKey(ImGuiKey_Home, D_KEY_HOME, 0, false);
    forwardKey(ImGuiKey_End, D_KEY_END, 0, false);
    forwardKey(ImGuiKey_PageUp, D_KEY_PAGE_UP);
    forwardKey(ImGuiKey_PageDown, D_KEY_PAGE_DOWN);
    forwardKey(ImGuiKey_Delete, D_KEY_DELETE);

    forwardKey(ImGuiKey_Apostrophe, D_KEY_APOSTROPHE);
    forwardKey(ImGuiKey_Comma, D_KEY_COMMA);
    forwardKey(ImGuiKey_Minus, D_KEY_MINUS);
    forwardKey(ImGuiKey_Period, D_KEY_PERIOD);
    forwardKey(ImGuiKey_Slash, D_KEY_SLASH);
    forwardKey(ImGuiKey_Semicolon, D_KEY_SEMICOLON);
    forwardKey(ImGuiKey_Equal, D_KEY_EQUAL);
    forwardKey(ImGuiKey_LeftBracket, D_KEY_LEFT_BRACKET);
    forwardKey(ImGuiKey_Backslash, D_KEY_BACKSLASH);
    forwardKey(ImGuiKey_RightBracket, D_KEY_RIGHT_BRACKET);
    forwardKey(ImGuiKey_GraveAccent, D_KEY_GRAVE_ACCENT);

    forwardKey(ImGuiKey_CapsLock, D_KEY_CAPS_LOCK, 0, false);
    forwardKey(ImGuiKey_ScrollLock, D_KEY_SCROLL_LOCK, 0, false);
    forwardKey(ImGuiKey_NumLock, D_KEY_NUM_LOCK, 0, false);
    forwardKey(ImGuiKey_PrintScreen, D_KEY_PRINT_SCREEN, 0, false);
    forwardKey(ImGuiKey_Pause, D_KEY_PAUSE, 0, false);

    forwardKey(ImGuiKey_LeftShift, D_KEY_LEFT_SHIFT, 0, false);
    forwardKey(ImGuiKey_LeftCtrl, D_KEY_LEFT_CONTROL, 0, false);
    forwardKey(ImGuiKey_LeftAlt, D_KEY_LEFT_ALT, 0, false);
    forwardKey(ImGuiKey_LeftSuper, D_KEY_LEFT_SUPER, 0, false);
    forwardKey(ImGuiKey_RightShift, D_KEY_RIGHT_SHIFT, 0, false);
    forwardKey(ImGuiKey_RightCtrl, D_KEY_RIGHT_CONTROL, 0, false);
    forwardKey(ImGuiKey_RightAlt, D_KEY_RIGHT_ALT, 0, false);
    forwardKey(ImGuiKey_RightSuper, D_KEY_RIGHT_SUPER, 0, false);
    forwardKey(ImGuiKey_Menu, D_KEY_MENU, 0, false);

    forwardKeyRange(ImGuiKey_0, D_KEY_0, 10);
    forwardKeyRange(ImGuiKey_A, D_KEY_A, 26);
    // F8 and F11 stay with the editor (a reattach would forward the F8 press)
    forwardKeyRange(ImGuiKey_F1, D_KEY_F1, 7);
    forwardKeyRange(ImGuiKey_F9, D_KEY_F9, 2);
    forwardKeyRange(ImGuiKey_F12, D_KEY_F12, 13);
    forwardKeyRange(ImGuiKey_Keypad0, D_KEY_KP_0, 10);

    forwardKey(ImGuiKey_KeypadDecimal, D_KEY_KP_DECIMAL);
    forwardKey(ImGuiKey_KeypadDivide, D_KEY_KP_DIVIDE);
    forwardKey(ImGuiKey_KeypadMultiply, D_KEY_KP_MULTIPLY);
    forwardKey(ImGuiKey_KeypadSubtract, D_KEY_KP_SUBTRACT);
    forwardKey(ImGuiKey_KeypadAdd, D_KEY_KP_ADD);
    forwardKey(ImGuiKey_KeypadEqual, D_KEY_KP_EQUAL);
}

void editor::SceneWindow::releasePlayKeys(int mods){
    for (int key : playPressedKeys){
        Engine::systemKeyUp(key, false, mods);
    }
    playPressedKeys.clear();
}

// Gives the cursor back and lets ImGui see the mouse again. NoMouse is global, so
// this has to run even when the viewport that armed it is no longer drawn.
void editor::SceneWindow::endLook(uint32_t sceneId){
    draggingMouse[sceneId] = false;
    lookActive[sceneId] = false;
    resyncLookDelta[sceneId] = false;
    Backend::enableMouseCursor();
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
}

void editor::SceneWindow::releasePlayMouseButtons(int mods){
    // The engine drops events outside the view rect, so keep the synthetic release inside it
    const Rect view = Engine::getViewRect();
    const float x = std::clamp(playMousePos.x, view.getX(), view.getX() + std::max(0.0f, view.getWidth()));
    const float y = std::clamp(playMousePos.y, view.getY(), view.getY() + std::max(0.0f, view.getHeight()));
    for (int button : playPressedMouseButtons){
        if (project->isSimulateTouch()) {
            Engine::systemTouchEnd(0, x, y);
        } else {
            Engine::systemMouseUp(button, x, y, mods);
        }
    }
    playPressedMouseButtons.clear();
}

void editor::SceneWindow::sceneEventHandler(SceneProject* sceneProject) {
    if (sceneProject->playState == ScenePlayState::SAVING || sceneProject->playState == ScenePlayState::LOADING || sceneProject->playState == ScenePlayState::CANCELLING) {
        return;
    }

    // Get the current window's position and size
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImGuiIO& io = ImGui::GetIO();
    float mouseWheel = io.MouseWheel;
    ImVec2 mousePos = io.MousePos;
    ImVec2 mouseDelta = io.MouseDelta;

    // Check if the mouse is within the window bounds
    bool isMouseInWindow = ImGui::IsWindowHovered() && (mousePos.x >= windowPos.x && mousePos.x <= windowPos.x + windowSize.x &&
                            mousePos.y >= windowPos.y && mousePos.y <= windowPos.y + windowSize.y);

    // Keep the viewport redrawing while it is used (hover highlight, camera drag/fly,
    // gizmo drag, shortcuts), not just hovered. draggingMouse tracks a drag so it survives
    // the cursor leaving the window; edits flag the scene through the command system.
    {
        bool viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        bool anyMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                            ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                            ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        bool hoverInput = false;
        if (isMouseInWindow) {
            bool mouseMoved = mouseDelta.x != 0.0f || mouseDelta.y != 0.0f;
            hoverInput = anyMouseDown || mouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
                         (mouseMoved && sceneProject->sceneRender->hasHoverFeedback());
            for (int key = ImGuiKey_Keyboard_BEGIN; !hoverInput && key < ImGuiKey_Keyboard_END; key++) {
                hoverInput = ImGui::IsKeyPressed(static_cast<ImGuiKey>(key));
            }
        }
        if (hoverInput || draggingMouse[sceneProject->id] || (viewportFocused && anyMouseDown)) {
            sceneProject->needUpdateRender = true;
        }
    }

    int mods = 0;
    if (io.KeyShift) mods |= D_MODIFIER_SHIFT;
    if (io.KeyCtrl) mods |= D_MODIFIER_CONTROL;
    if (io.KeyAlt) mods |= D_MODIFIER_ALT;
    if (io.KeySuper) mods |= D_MODIFIER_SUPER;

    const bool cameraDetached = sceneProject->sceneRender->isCameraDetached();
    const bool gameView = !cameraDetached &&
        (sceneProject->playState == ScenePlayState::PLAYING || sceneProject->playState == ScenePlayState::PAUSED);

    // End an editor camera look left running by re-attaching
    if (gameView && (draggingMouse[sceneProject->id] || lookActive[sceneProject->id])) {
        endLook(sceneProject->id);
    }

    // When scene is playing, forward mouse and keyboard events to Engine
    if (sceneProject->playState == ScenePlayState::PLAYING && !cameraDetached) {
        // A captured cursor reports a virtual position that leaves the viewport,
        // so the mouse follows window focus like the keyboard below.
        const bool capturedMouse = Engine::getMouseMode() == MouseMode::CAPTURED;
        bool forwardMouse = capturedMouse
            ? ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
            : isMouseInWindow;

        // A drag that leaves the viewport keeps being forwarded until its button comes up
        if (playMouseSceneId == sceneProject->id) {
            for (int button : playPressedMouseButtons) {
                if (ImGui::IsMouseDown(button)) {
                    forwardMouse = true;
                    break;
                }
            }
        }

        if (forwardMouse) {
            float x = mousePos.x - windowPos.x;
            float y = mousePos.y - windowPos.y;
            toEngineCanvas(sceneProject->id, x, y);

            // Engine::transformCoordPos maps the view rect onto the canvas and drops what
            // falls outside, so a button released out there is still down and needs the
            // synthetic up below. LETTERBOX insets the rect, so test it and not the widget.
            const Rect view = Engine::getViewRect();
            const bool accepted = capturedMouse || Engine::isAllowEventsOutCanvas() ||
                (x >= view.getX() && x <= view.getX() + view.getWidth() &&
                 y >= view.getY() && y <= view.getY() + view.getHeight());

            if (accepted) {
                playMouseSceneId = sceneProject->id;
                playMousePos = ImVec2(x, y);
            }

            if (project->isSimulateTouch()) {
                // the left button is one finger, which never hovers
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    Engine::systemTouchStart(0, x, y);
                    if (accepted) playPressedMouseButtons.insert(ImGuiMouseButton_Left);
                } else if (playPressedMouseButtons.count(ImGuiMouseButton_Left) && (mouseDelta.x != 0 || mouseDelta.y != 0)) {
                    Engine::systemTouchMove(0, x, y);
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && playPressedMouseButtons.count(ImGuiMouseButton_Left)) {
                    Engine::systemTouchEnd(0, x, y);
                    if (accepted) playPressedMouseButtons.erase(ImGuiMouseButton_Left);
                }
            } else {
                Engine::systemMouseMove(x, y, mods);

                if (mouseWheel != 0) {
                    Engine::systemMouseScroll(0, mouseWheel, mods);
                }

                for (int i = 0; i < 5; i++) {
                    if (ImGui::IsMouseClicked(i)) {
                        Engine::systemMouseDown(i, x, y, mods);
                        if (accepted) playPressedMouseButtons.insert(i);
                    }
                    if (ImGui::IsMouseReleased(i)) {
                        Engine::systemMouseUp(i, x, y, mods);
                        if (accepted) playPressedMouseButtons.erase(i);
                    }
                }
            }
        } else if (playMouseSceneId == sceneProject->id) {
            releasePlayMouseButtons(mods);
        }

        // Keyboard follows window focus, not mouse hover, so held keys keep
        // working while the mouse drifts out of the window. On focus loss the
        // key-up events would never arrive, so release held keys explicitly.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            playKeysSceneId = sceneProject->id;
            // ImGui navigation would take Escape to leave this window, and the game loses the keyboard
            ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID);
            forwardPlayKeyboardInput(io, mods);
        } else if (playKeysSceneId == sceneProject->id) {
            releasePlayKeys(mods);
        }

        // Keeps the editor camera off while the game owns the mouse
        if (forwardMouse || isMouseInWindow) {
            return;
        }
    } else {
        // Play stopped, paused or detached with input still held
        if (playKeysSceneId == sceneProject->id) releasePlayKeys(mods);
        if (playMouseSceneId == sceneProject->id) releasePlayMouseButtons(mods);
    }

    // The editor camera takes no input while the game camera is drawn
    if (gameView) {
        return;
    }

    if (sceneProject->playState == ScenePlayState::STOPPED && sceneProject->sceneRender->isPreviewCameraActive()) {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            stopViewingCamera(sceneProject->id);
        }
        return;
    }

    size_t sceneId = sceneProject->id;

    bool altHeld = ImGui::IsKeyDown(ImGuiMod_Alt);
    bool suppressLeftMouse = suppressLeftMouseUntilRelease[sceneId];
    bool handPanEnabled = sceneProject->sceneRender->getCursorSelected() == CursorSelected::HAND;
    GizmoSelected gizmoSelected = sceneProject->sceneRender->getToolsLayer()->getGizmoSelected();
    Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
    bool gizmoSideActive = !handPanEnabled && sceneProject->sceneRender->isAnyGizmoSideSelected();
    // Alt + click on a gizmo handle duplicates the target and drags the copy.
    // For the 2D gizmo, only the center move area duplicates; resize handles
    // keep editing the current selection. Alt + click on empty space keeps its
    // camera-orbit behavior, so the guard is gated on a hovered/active gizmo side.
    bool altGizmoDrag = altHeld && gizmoSideActive;
    bool altGizmoDuplicate = altGizmoDrag &&
        (gizmoSelected != GizmoSelected::OBJECT2D || gizmo2DSide == Gizmo2DSideSelected::CENTER);

    bool disableSelection = 
        altHeld ||
        handPanEnabled || 
        sceneProject->sceneRender->isTerrainEditing() ||
        gizmoSideActive ||
        sceneProject->sceneType != SceneType::SCENE_3D && ImGui::IsKeyDown(ImGuiKey_Space);

    if (isMouseInWindow){

        float logicalX = mousePos.x - windowPos.x;
        float logicalY = mousePos.y - windowPos.y;
        float x = logicalX;
        float y = logicalY;
        toEditorCanvas(sceneProject, x, y);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (!altHeld || altGizmoDrag) && !suppressLeftMouse && !handPanEnabled){
            subSelectionClickConsumesRelease[sceneId] = false;

            // Selecting and dragging an unselected object at same time (just for 2D object mode)
            if (!disableSelection && gizmoSelected == GizmoSelected::OBJECT2D) {
                Entity hitEntity = findSelectableObjectByRay(sceneId, x, y);
                if (hitEntity != NULL_ENTITY) {
                    bool alreadySelected = project->isSelectedEntity(sceneId, hitEntity);
                    if (!alreadySelected || (io.KeyShift && !gizmoSideActive)) {
                        sceneProject->sceneRender->clearTileSelection();
                        sceneProject->sceneRender->clearInstanceSelection();
                        sceneProject->sceneRender->clearOccluderPointSelection();
                        sceneProject->sceneRender->clearLinePointSelection();
                        sceneProject->sceneRender->clearPolygonPointSelection();
                        sceneProject->sceneRender->clearTrackPointSelection();
                        bool changed = selectObjectByRay(sceneId, x, y, io.KeyShift);
                        if (changed) {
                            sceneProject->sceneRender->update(project->getSelectedEntities(sceneId), project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        }
                    }
                }
            }

            // Occluder polygon point sub-selection within a selected occluder entity
            // (mirrors the tile sub-selection flow below)
            if (gizmoSelected == GizmoSelected::OBJECT2D) {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                    if (gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER) {
                        int pointHit = sceneProject->sceneRender->hitTestOccluderPoint(selEntity, x, y);
                        if (pointHit >= 0) {
                            sceneProject->sceneRender->selectOccluderPoint(selEntity, pointHit);
                            subSelectionClickConsumesRelease[sceneId] = true;
                            // Re-update so the gizmo cross jumps to the point
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedOccluderPointIndex() >= 0 && gizmo2DSide == Gizmo2DSideSelected::NONE) {
                            // Only clear eagerly when clicking the entity body itself; clicking
                            // empty space clears point + entity together on click-up.
                            Entity bodyHit = findSelectableObjectByRay(sceneId, x, y);
                            if (bodyHit == selEntity) {
                                sceneProject->sceneRender->clearOccluderPointSelection();
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            }
                        }
                    }
                }
            }

            // Line endpoint sub-selection within a selected lines entity (2D gizmo
            // or 3D translate; the side guard mirrors the instance block below)
            if (gizmoSelected == GizmoSelected::OBJECT2D || gizmoSelected == GizmoSelected::TRANSLATE) {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                    bool blockPointHit;
                    if (gizmoSelected == GizmoSelected::OBJECT2D) {
                        blockPointHit = !(gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER);
                    } else {
                        blockPointHit = sceneProject->sceneRender->isAnyGizmoSideSelected();
                    }
                    if (!blockPointHit) {
                        int pointHit = sceneProject->sceneRender->hitTestLinePoint(selEntity, x, y);
                        if (pointHit >= 0) {
                            sceneProject->sceneRender->selectLinePoint(selEntity, pointHit);
                            subSelectionClickConsumesRelease[sceneId] = true;
                            // Re-update so the gizmo jumps to the endpoint
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedLinePointIndex() >= 0 && !sceneProject->sceneRender->isAnyGizmoSideSelected()) {
                            // Only clear eagerly when clicking the entity body itself; clicking
                            // empty space clears point + entity together on click-up.
                            Entity bodyHit = findSelectableObjectByRay(sceneId, x, y);
                            if (bodyHit == selEntity) {
                                sceneProject->sceneRender->clearLinePointSelection();
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            }
                        }
                    }
                }
            }

            // Polygon / MeshPolygon vertex sub-selection within a selected polygon entity
            // (2D gizmo or 3D translate; same side guards as the line block above)
            if (gizmoSelected == GizmoSelected::OBJECT2D || gizmoSelected == GizmoSelected::TRANSLATE) {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    bool isMeshPolygon = sceneProject->scene->findComponent<MeshPolygonComponent>(selEntity) != nullptr;
                    bool isPolygon = sceneProject->scene->findComponent<PolygonComponent>(selEntity) != nullptr;
                    if (isMeshPolygon || isPolygon) {
                        Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                        bool blockPointHit;
                        if (gizmoSelected == GizmoSelected::OBJECT2D) {
                            blockPointHit = !(gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER);
                        } else {
                            blockPointHit = sceneProject->sceneRender->isAnyGizmoSideSelected();
                        }
                        if (!blockPointHit) {
                            int pointHit = sceneProject->sceneRender->hitTestPolygonPoint(selEntity, isMeshPolygon, x, y);
                            if (pointHit >= 0) {
                                sceneProject->sceneRender->selectPolygonPoint(selEntity, isMeshPolygon, pointHit);
                                subSelectionClickConsumesRelease[sceneId] = true;
                                // Re-update so the gizmo jumps to the vertex
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            } else if (sceneProject->sceneRender->getSelectedPolygonPointIndex() >= 0 && !sceneProject->sceneRender->isAnyGizmoSideSelected()) {
                                Entity bodyHit = findSelectableObjectByRay(sceneId, x, y);
                                if (bodyHit == selEntity) {
                                    sceneProject->sceneRender->clearPolygonPointSelection();
                                    sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                    sceneProject->sceneRender->mouseHoverEvent(x, y);
                                }
                            }
                        }
                    }
                }
            }

            // TranslateTracks keyframe point sub-selection within a selected tracks entity
            // (2D gizmo or 3D translate; same side guards as the line block above)
            if (gizmoSelected == GizmoSelected::OBJECT2D || gizmoSelected == GizmoSelected::TRANSLATE) {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    if (sceneProject->scene->findComponent<TranslateTracksComponent>(selEntity)) {
                        Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                        bool blockPointHit;
                        if (gizmoSelected == GizmoSelected::OBJECT2D) {
                            blockPointHit = !(gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER);
                        } else {
                            blockPointHit = sceneProject->sceneRender->isAnyGizmoSideSelected();
                        }
                        if (!blockPointHit) {
                            int pointHit = sceneProject->sceneRender->hitTestTrackPoint(selEntity, x, y);
                            if (pointHit >= 0) {
                                sceneProject->sceneRender->selectTrackPoint(selEntity, pointHit);
                                subSelectionClickConsumesRelease[sceneId] = true;
                                // Re-update so the gizmo jumps to the point
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            }
                        }
                    }
                }
            }

            // Tile sub-selection within a selected tilemap (outside disableSelection guard
            // so clicking a tile works even when the tilemap gizmo body is hovered)
            if (gizmoSelected == GizmoSelected::OBJECT2D) {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                    if (gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER) {
                        int tileHit = sceneProject->sceneRender->hitTestTile(selEntity, x, y);
                        if (tileHit >= 0) {
                            sceneProject->sceneRender->selectTile(selEntity, tileHit);
                            subSelectionClickConsumesRelease[sceneId] = true;
                            // Re-update so gizmo wraps the tile
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedTileIndex() >= 0 && gizmo2DSide == Gizmo2DSideSelected::NONE) {
                            // Only clear eagerly when clicking the entity body itself. When
                            // clicking empty space the click-up handler clears both the tile
                            // and the entity together, avoiding a two-step gizmo jump.
                            Entity bodyHit = findSelectableObjectByRay(sceneId, x, y);
                            if (bodyHit == selEntity) {
                                sceneProject->sceneRender->clearTileSelection();
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            }
                        }
                    }
                }
            }

            // Instance sub-selection within a selected InstancedMeshComponent entity
            {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    bool blockInstanceHit;
                    if (gizmoSelected == GizmoSelected::OBJECT2D) {
                        // With the 2D gizmo, CENTER is set when hovering over the entity body —
                        // the same region where instances live. Allow hit-testing in that case,
                        // mirroring the logic used for tile sub-selection.
                        Gizmo2DSideSelected gizmo2DSide = sceneProject->sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                        blockInstanceHit = !(gizmo2DSide == Gizmo2DSideSelected::NONE || gizmo2DSide == Gizmo2DSideSelected::CENTER);
                    } else {
                        blockInstanceHit = sceneProject->sceneRender->isAnyGizmoSideSelected();
                    }
                    if (!blockInstanceHit) {
                        int instHit = sceneProject->sceneRender->hitTestInstance(selEntity, x, y);
                        if (instHit >= 0) {
                            sceneProject->sceneRender->selectInstance(selEntity, instHit);
                            subSelectionClickConsumesRelease[sceneId] = true;
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedInstanceIndex() >= 0) {
                            // Only clear eagerly when clicking the entity body itself, so the
                            // gizmo correctly snaps to the entity level. When clicking empty
                            // space the click-up selectObjectByRay handler will clear both the
                            // instance and the entity at once, avoiding a two-step visual where
                            // the gizmo briefly jumps to entity position before disappearing.
                            Entity bodyHit = findSelectableObjectByRay(sceneId, x, y);
                            if (bodyHit == selEntity) {
                                sceneProject->sceneRender->clearInstanceSelection();
                                sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                                sceneProject->sceneRender->mouseHoverEvent(x, y);
                            }
                        }
                    }
                }
            }
            // Alt+click on gizmo: duplicate before starting drag (Shift is reserved
            // for aspect-ratio lock on 2D corner handles, see mouseDragEvent).
            if (altGizmoDuplicate){
                int tileIdx = sceneProject->sceneRender->getSelectedTileIndex();
                Entity tileEntity = sceneProject->sceneRender->getSelectedTileEntity();
                if (tileIdx >= 0) {
                    // Duplicate tile
                    Command* dupCmd = ProjectUtils::buildDuplicateTileCmd(project, sceneId, tileEntity, (unsigned int)tileIdx);
                    if (dupCmd) {
                        CommandHandle::get(sceneId)->addCommand(dupCmd);
                        TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(tileEntity);
                        if (tilemap) {
                            sceneProject->sceneRender->selectTile(tileEntity, (int)tilemap->numTiles - 1);
                        }
                        sceneProject->sceneRender->update(project->getSelectedEntities(sceneId), project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                        sceneProject->sceneRender->mouseHoverEvent(x, y);
                    }
                } else if (sceneProject->sceneRender->getSelectedInstanceIndex() >= 0) {
                    int instIdx = sceneProject->sceneRender->getSelectedInstanceIndex();
                    Entity instEntity = sceneProject->sceneRender->getSelectedInstanceEntity();
                    Command* dupCmd = ProjectUtils::buildDuplicateInstanceCmd(project, sceneId, instEntity, (unsigned int)instIdx);
                    if (dupCmd) {
                        CommandHandle::get(sceneId)->addCommand(dupCmd);
                        InstancedMeshComponent* instmesh = sceneProject->scene->findComponent<InstancedMeshComponent>(instEntity);
                        if (instmesh) {
                            sceneProject->sceneRender->clearTileSelection();
                            sceneProject->sceneRender->selectInstance(instEntity, (int)instmesh->instances.size() - 1);
                        }
                        sceneProject->sceneRender->update(project->getSelectedEntities(sceneId), project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                        sceneProject->sceneRender->mouseHoverEvent(x, y);
                    }
                } else {
                    // Duplicate entities
                    std::vector<Entity> selectedEntities = project->getSelectedEntities(sceneId);
                    if (!selectedEntities.empty()){
                        auto* dupCmd = new DuplicateEntityCmd(project, sceneId, selectedEntities);
                        CommandHandle::get(sceneId)->addCommandNoMerge(dupCmd);
                        sceneProject->sceneRender->update(project->getSelectedEntities(sceneId), project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                        sceneProject->sceneRender->mouseHoverEvent(x, y);
                    }
                }
            }
            sceneProject->sceneRender->mouseClickEvent(x, y, project->getSelectedEntities(sceneId));
        }

        if (!(ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right))){
            sceneProject->sceneRender->mouseHoverEvent(x, y);
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (!altHeld || altGizmoDrag) && !suppressLeftMouse && !handPanEnabled){
            if (!mouseLeftDown){
                mouseLeftStartPos = Vector2(logicalX, logicalY);
                mouseLeftDown = true;
            }
            mouseLeftDragPos = Vector2(logicalX, logicalY);
            // Terrain brush strokes skip the drag threshold: holding the button
            // keeps applying flow and tiny precise drags still paint.
            if (mouseLeftStartPos.distance(mouseLeftDragPos) > 5 || sceneProject->sceneRender->isTerrainEditing()){
                mouseLeftDraggedInside = true;
            }
            if (mouseLeftDraggedInside){
                float origX = mouseLeftStartPos.x;
                float origY = mouseLeftStartPos.y;
                toEditorCanvas(sceneProject, origX, origY);
                sceneProject->sceneRender->mouseDragEvent(x, y, origX, origY, project, sceneId, project->getSelectedEntities(sceneId), disableSelection, io.KeyCtrl, io.KeyShift);
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && (!altHeld || altGizmoDrag) && !suppressLeftMouse){
            bool consumeSelectionRelease = subSelectionClickConsumesRelease[sceneId];
            if (!mouseLeftDraggedInside && mouseLeftDown && !disableSelection && !consumeSelectionRelease){
                // Remember sub-selection anchors BEFORE re-selecting, so we can tell
                // whether the release click actually changed the host entity.
                int prevTileIndex = sceneProject->sceneRender->getSelectedTileIndex();
                Entity prevTileEntity = sceneProject->sceneRender->getSelectedTileEntity();
                int prevInstanceIndex = sceneProject->sceneRender->getSelectedInstanceIndex();
                Entity prevInstanceEntity = sceneProject->sceneRender->getSelectedInstanceEntity();
                int prevOccluderPointIndex = sceneProject->sceneRender->getSelectedOccluderPointIndex();
                Entity prevOccluderPointEntity = sceneProject->sceneRender->getSelectedOccluderPointEntity();
                int prevLinePointIndex = sceneProject->sceneRender->getSelectedLinePointIndex();
                Entity prevLinePointEntity = sceneProject->sceneRender->getSelectedLinePointEntity();
                int prevPolygonPointIndex = sceneProject->sceneRender->getSelectedPolygonPointIndex();
                Entity prevPolygonPointEntity = sceneProject->sceneRender->getSelectedPolygonPointEntity();
                int prevTrackPointIndex = sceneProject->sceneRender->getSelectedTrackPointIndex();
                Entity prevTrackPointEntity = sceneProject->sceneRender->getSelectedTrackPointEntity();

                selectObjectByRay(sceneId, x, y, io.KeyShift);

                // Sub-selection is only meaningful while its host entity is the sole
                // selected entity. Clear it whenever that invariant no longer holds
                // (different entity picked, nothing picked, or multi-select).
                std::vector<Entity> selAfter = project->getSelectedEntities(sceneId);
                bool soleHost = selAfter.size() == 1;
                if (prevTileIndex >= 0 && !(soleHost && selAfter[0] == prevTileEntity)){
                    sceneProject->sceneRender->clearTileSelection();
                }
                if (prevInstanceIndex >= 0 && !(soleHost && selAfter[0] == prevInstanceEntity)){
                    sceneProject->sceneRender->clearInstanceSelection();
                }
                if (prevOccluderPointIndex >= 0 && !(soleHost && selAfter[0] == prevOccluderPointEntity)){
                    sceneProject->sceneRender->clearOccluderPointSelection();
                }
                if (prevLinePointIndex >= 0 && !(soleHost && selAfter[0] == prevLinePointEntity)){
                    sceneProject->sceneRender->clearLinePointSelection();
                }
                if (prevPolygonPointIndex >= 0 && !(soleHost && selAfter[0] == prevPolygonPointEntity)){
                    sceneProject->sceneRender->clearPolygonPointSelection();
                }
                if (prevTrackPointIndex >= 0 && !(soleHost && selAfter[0] == prevTrackPointEntity)){
                    sceneProject->sceneRender->clearTrackPointSelection();
                }
            }
            subSelectionClickConsumesRelease[sceneId] = false;
            mouseLeftDown = false;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
        float x = mousePos.x - windowPos.x;
        float y = mousePos.y - windowPos.y;
        toEditorCanvas(sceneProject, x, y);

        if (suppressLeftMouse){
            suppressLeftMouseUntilRelease[sceneId] = false;
            subSelectionClickConsumesRelease[sceneId] = false;
            mouseLeftDown = false;
            mouseLeftDraggedInside = false;
        }else{

            if (mouseLeftDraggedInside && !disableSelection){
                float startX = mouseLeftStartPos.x;
                float startY = mouseLeftStartPos.y;
                float endX = mouseLeftDragPos.x;
                float endY = mouseLeftDragPos.y;
                toEditorCanvas(sceneProject, startX, startY);
                toEditorCanvas(sceneProject, endX, endY);
                ImVec2 canvasSize = getEditorCanvasSize(sceneProject);
                Vector2 clickStartPos = Vector2((2 * startX / canvasSize.x) - 1, -((2 * startY / canvasSize.y) - 1));
                Vector2 clickEndPos = Vector2((2 * endX / canvasSize.x) - 1, -((2 * endY / canvasSize.y) - 1));
                sceneProject->sceneRender->clearTileSelection();
                sceneProject->sceneRender->clearInstanceSelection();
                sceneProject->sceneRender->clearOccluderPointSelection();
                sceneProject->sceneRender->clearLinePointSelection();
                sceneProject->sceneRender->clearPolygonPointSelection();
                sceneProject->sceneRender->clearTrackPointSelection();
                project->selectObjectsByRect(sceneId, clickStartPos, clickEndPos);
            }

            sceneProject->sceneRender->mouseReleaseEvent(x, y);
            subSelectionClickConsumesRelease[sceneId] = false;

            mouseLeftDraggedInside = false;
        }
    }

    if (isMouseInWindow && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            (altHeld && !gizmoSideActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !suppressLeftMouse))) {
        draggingMouse[sceneId] = true;

        // X11 still delivers motion queued before the pointer was warped to the centre,
        // which converts to one huge delta, so the first motion of a look is discarded.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            resyncLookDelta[sceneId] = true;
            // Remember where the look began so we can restore it on release (see below)
            lookActive[sceneId] = true;
            lookReturnPos[sceneId] = mousePos;
        }

        ImGui::SetWindowFocus();
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) || ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
            (altHeld && ImGui::IsMouseReleased(ImGuiMouseButton_Left))){
        draggingMouse[sceneId] = false;
        Backend::enableMouseCursor();
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

        // The captured cursor left io.MousePos far outside the window, which would fail the
        // isMouseInWindow test, so snap it back before the backend restore lands next frame.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && lookActive[sceneId]) {
            io.AddMousePosEvent(lookReturnPos[sceneId].x, lookReturnPos[sceneId].y);
            lookActive[sceneId] = false;
        }
    }

    // Focus loss clears the buttons without reporting a release, and the Alt+LMB term
    // needs Alt still held, so end stale drags from the button state.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            (draggingMouse[sceneId] || lookActive[sceneId] || (io.ConfigFlags & ImGuiConfigFlags_NoMouse))) {
        endLook(sceneId);
    }

    Camera* camera = sceneProject->sceneRender->getCamera();

    bool walkingMode = false;

    if (sceneProject->sceneType == SceneType::SCENE_3D){

        SceneRender3D* sceneRender3D = static_cast<SceneRender3D*>(sceneProject->sceneRender);

        float distanceFromTarget = camera->getDistanceFromTarget();

        // Scale factor: movements should be proportional to distance
        // Base movement rate is maintained at around distanceFromTarget = 10
        float distanceScaleFactor = std::max(0.1f, distanceFromTarget / 10.0f);

        // Check for mouse clicks
        if (draggingMouse[sceneId] && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                walkingMode = true;

                float lookDeltaX = mouseDelta.x;
                float lookDeltaY = mouseDelta.y;

                // Drop the first motion of the look, it may predate the warp (see above)
                if (resyncLookDelta[sceneId]) {
                    if (lookDeltaX != 0.0f || lookDeltaY != 0.0f) {
                        lookDeltaX = 0.0f;
                        lookDeltaY = 0.0f;
                        resyncLookDelta[sceneId] = false;
                    }
                }

                camera->rotateView(-0.1 * lookDeltaX);
                camera->elevateView(-0.1 * lookDeltaY);

                // Restore up vector after rotation (fixes Y-snap leaving Z as up)
                Vector3 dir = camera->getWorldDirection().normalize();
                if (std::abs(dir.y) < 0.99f) {
                    camera->setUp(0, 1, 0);
                }

                if (mouseWheel != 0.0f) {
                    sceneRender3D->setWalkSpeedOffset(sceneRender3D->getWalkSpeedOffset() + mouseWheel);
                }
                float walkSpeed = DEFAULT_EDITOR_WALK_SPEED + sceneRender3D->getWalkSpeedOffset();

                sceneRender3D->getUILayer()->showSpeedGauge(
                    walkSpeed / DEFAULT_EDITOR_WALK_SPEED,
                    MIN_EDITOR_WALK_SPEED / DEFAULT_EDITOR_WALK_SPEED,
                    MAX_EDITOR_WALK_SPEED / DEFAULT_EDITOR_WALK_SPEED);

                // Speed is per second, with the delta clamped so a stall cannot jump
                float finalSpeed = 1.2f * walkSpeed * distanceScaleFactor *
                                   std::min(io.DeltaTime, 0.1f);

                // Shift multiplier for faster movement
                if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                    finalSpeed *= 3.0f;
                }

                if (ImGui::IsKeyDown(ImGuiKey_W)) {
                    camera->slideForward(finalSpeed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_S)) {
                    camera->slideForward(-finalSpeed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_A)) {
                    camera->slide(-finalSpeed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_D)) {
                    camera->slide(finalSpeed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_E)) {
                    camera->slideUp(finalSpeed);
                }
                if (ImGui::IsKeyDown(ImGuiKey_Q)) {
                    camera->slideUp(-finalSpeed);
                }

                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
                Backend::disableMouseCursor();
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                    camera->slide(-0.01 * mouseDelta.x * distanceScaleFactor);
                    camera->slideUp(0.01 * mouseDelta.y * distanceScaleFactor);
                } else if (ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                    sceneRender3D->zoomCamera(-0.1f * mouseDelta.y * distanceScaleFactor);
                } else {
                    camera->rotatePosition(-0.1 * mouseDelta.x);
                    camera->elevatePosition(0.1 * mouseDelta.y);

                    // Restore up vector after orbit (fixes Y-snap leaving Z as up)
                    Vector3 dir = camera->getWorldDirection().normalize();
                    if (std::abs(dir.y) < 0.99f && camera->getUp() != Vector3(0, 1, 0)) {
                        camera->setUp(0, 1, 0);
                    }
                }
            }
        }

        // Alt+LMB orbit
        if (draggingMouse[sceneId] && altHeld && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            camera->rotatePosition(-0.1 * mouseDelta.x);
            camera->elevatePosition(0.1 * mouseDelta.y);

            // Restore up vector after orbit (fixes Y-snap leaving Z as up)
            Vector3 dir = camera->getWorldDirection().normalize();
            if (std::abs(dir.y) < 0.99f && camera->getUp() != Vector3(0, 1, 0)) {
                camera->setUp(0, 1, 0);
            }
        }

        if (isMouseInWindow && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !altHeld && handPanEnabled) {
            camera->slide(-0.01 * mouseDelta.x * distanceScaleFactor);
            camera->slideUp(0.01 * mouseDelta.y * distanceScaleFactor);
        }

        if (!walkingMode) {
            sceneRender3D->getUILayer()->hideSpeedGauge();
        }

        // The zoom speed itself can remain constant
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (isMouseInWindow && mouseWheel != 0.0f) {
                sceneRender3D->zoomCamera(2.0f * mouseWheel * distanceScaleFactor);
            }
        }

    }else{

        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || 
            (draggingMouse[sceneId] && altHeld && ImGui::IsMouseDown(ImGuiMouseButton_Left)) ||
            (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_Space)) ||
            (isMouseInWindow && ImGui::IsMouseDown(ImGuiMouseButton_Left) && handPanEnabled)){

            SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
            float currentZoom = sceneRender2D->getZoom();
            const ImVec2 scale = canvasEngineScale(sceneId);

            float slideX = -currentZoom * mouseDelta.x * scale.x;
            float slideY = -currentZoom * mouseDelta.y * scale.y;

            if (sceneProject->sceneType == SceneType::SCENE_2D){
                slideY = -slideY;
            }

            camera->slide(slideX);
            camera->slideUp(slideY);
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)){
            if (isMouseInWindow && mouseWheel != 0.0f){
                float zoomFactor = 1.0f - (0.1f * mouseWheel);

                float mouseX = mousePos.x - windowPos.x;
                float mouseY = mousePos.y - windowPos.y;
                toEngineCanvas(sceneProject->id, mouseX, mouseY);

                if (sceneProject->sceneType == SceneType::SCENE_2D){
                    mouseY = getHeight(sceneProject->id) - mouseY;
                }

                SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
                sceneRender2D->zoomAtPosition(getWidth(sceneProject->id), getHeight(sceneProject->id), Vector2(mouseX, mouseY), zoomFactor);
            }
        }

    }

    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()){
        if (project->getSelectedSceneId() == sceneId){
            if (!walkingMode){

                // Brush shortcuts while terrain editing: [ / ] resize the brush,
                // Shift+[ / Shift+] adjust its strength.
                if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
                    if (terrainEditWindow->isEditingScene(sceneProject->scene)){
                        const bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
                        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)){
                            if (shiftHeld){
                                terrainEditWindow->adjustBrushStrength(1.0f / 1.15f);
                            }else{
                                terrainEditWindow->adjustBrushSize(1.0f / 1.15f);
                            }
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)){
                            if (shiftHeld){
                                terrainEditWindow->adjustBrushStrength(1.15f);
                            }else{
                                terrainEditWindow->adjustBrushSize(1.15f);
                            }
                        }
                    }
                }

                if (sceneProject->sceneType != SceneType::SCENE_UI){
                    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                        sceneProject->sceneRender->getToolsLayer()->enableTranslateGizmo();
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                        sceneProject->sceneRender->getToolsLayer()->enableRotateGizmo();
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                        sceneProject->sceneRender->getToolsLayer()->enableScaleGizmo();
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_T)){
                        sceneProject->sceneRender->changeUseGlobalTransform();
                    }
                }

                // F key: Focus camera on selected entities
                if (sceneProject->sceneType == SceneType::SCENE_3D && ImGui::IsKeyPressed(ImGuiKey_F)) {
                    std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                    focusOnEntities(sceneProject, selEntities);
                }

                // Home key: Frame all objects in the scene
                if (sceneProject->sceneType == SceneType::SCENE_3D && ImGui::IsKeyPressed(ImGuiKey_Home)) {
                    focusOnEntities(sceneProject, sceneProject->entities);
                }

                // Numpad preset views (3D only)
                if (sceneProject->sceneType == SceneType::SCENE_3D) {
                    Camera* cam = sceneProject->sceneRender->getCamera();
                    if (!ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1)) {
                            // Numpad 1: Front view (-Z looking toward +Z)
                            snapCameraToDirection(cam, Vector3(0, 0, 1));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3)) {
                            // Numpad 3: Right view (+X looking toward -X)
                            snapCameraToDirection(cam, Vector3(-1, 0, 0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7)) {
                            // Numpad 7: Top view (+Y looking down)
                            snapCameraToDirection(cam, Vector3(0, -1, 0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad9)) {
                            // Numpad 9: Bottom view (-Y looking up)
                            snapCameraToDirection(cam, Vector3(0, 1, 0));
                        }
                    } else {
                        // Ctrl+Numpad for opposite directions
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1)) {
                            // Ctrl+Numpad 1: Back view (+Z looking toward -Z)
                            snapCameraToDirection(cam, Vector3(0, 0, -1));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3)) {
                            // Ctrl+Numpad 3: Left view (-X looking toward +X)
                            snapCameraToDirection(cam, Vector3(1, 0, 0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7)) {
                            // Ctrl+Numpad 7: Bottom view (-Y looking up)
                            snapCameraToDirection(cam, Vector3(0, 1, 0));
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_Keypad9)) {
                            // Ctrl+Numpad 9: Top view (+Y looking down)
                            snapCameraToDirection(cam, Vector3(0, -1, 0));
                        }
                    }
                }
            }
        }
    }

}

void editor::SceneWindow::focusOnEntities(SceneProject* sceneProject, const std::vector<Entity>& entities) {
    if (entities.empty() || !sceneProject || !sceneProject->sceneRender) return;
    if (sceneProject->sceneType != SceneType::SCENE_3D) return;

    Camera* camera = sceneProject->sceneRender->getCamera();

    AABB aabb;
    for (Entity entity : entities) {
        AABB entityAABB = sceneProject->sceneRender->getEntitiesAABB({entity});
        if (!entityAABB.isNull() && !entityAABB.isInfinite()) {
            aabb.merge(entityAABB);
        } else if (Transform* transform = sceneProject->scene->findComponent<Transform>(entity)) {
            // entities without bounds, like lights, still have a world origin to focus on
            aabb.merge(transform->worldPosition);
        }
    }
    if (aabb.isNull() || aabb.isInfinite()) return;

    Vector3 center = aabb.getCenter();
    Vector3 size = aabb.getSize();
    float radius = size.length() * 0.5f;
    radius = std::max(radius, 0.5f);

    float yfovRad = Angle::defaultToRad(camera->getYFov());
    float aspect = camera->getAspect();
    if (aspect <= 0.0f && height[sceneProject->id] > 0) {
        aspect = (float)width[sceneProject->id] / (float)height[sceneProject->id];
    }
    aspect = std::max(aspect, 0.0001f);

    float halfYFov = yfovRad * 0.5f;
    float halfXFov = std::atan(std::tan(halfYFov) * aspect);
    float limitingHalfFov = std::min(halfYFov, halfXFov);
    float paddedRadius = radius * 1.1f;
    float distance = paddedRadius / std::sin(limitingHalfFov);
    distance = std::max(distance, paddedRadius * 2.0f);
    distance = std::clamp(distance, MIN_EDITOR_CAMERA_DISTANCE, MAX_EDITOR_CAMERA_DISTANCE);

    Vector3 direction = camera->getWorldDirection();
    direction.normalize();
    Vector3 newPos = center - direction * distance;

    camera->setPosition(newPos.x, newPos.y, newPos.z);
    camera->setTarget(center.x, center.y, center.z);
    sceneProject->needUpdateRender = true;
}

void editor::SceneWindow::snapCameraToDirection(Camera* camera, const Vector3& direction) {
    Vector3 target = camera->getWorldTarget();
    float distance = camera->getDistanceFromTarget();

    Vector3 newPos = target - direction * distance;
    camera->setPosition(newPos.x, newPos.y, newPos.z);

    // When looking along Y axis, up vector (0,1,0) is parallel to direction — use Z as up instead
    if (std::abs(direction.y) > 0.9f) {
        camera->setUp(0, 0, direction.y > 0 ? -1.0f : 1.0f);
    } else {
        camera->setUp(0, 1, 0);
    }

    camera->setTarget(target.x, target.y, target.z);
}

bool editor::SceneWindow::handleViewportGizmoClick(SceneProject* sceneProject, float canvasX, float canvasY, int canvasWidth, int canvasHeight) {
    if (sceneProject->sceneType != SceneType::SCENE_3D) return false;

    SceneRender3D* sceneRender3D = static_cast<SceneRender3D*>(sceneProject->sceneRender);
    ViewportGizmo* viewGizmo = sceneRender3D->getViewportGizmo();

    const float gizmoSize = VIEWPORT_GIZMO_SIZE * getOverlayScale(sceneProject->id);

    // The gizmo Image is anchored at TOP_RIGHT of the canvas
    float gizmoLeft = canvasWidth - gizmoSize;
    float gizmoTop = 0.0f;

    // Check if click is within the gizmo area
    if (canvasX < gizmoLeft || canvasX > canvasWidth || canvasY < gizmoTop || canvasY > gizmoSize) {
        return false;
    }

    // Normalize to [-1, 1] within the gizmo area
    float normalizedX = ((canvasX - gizmoLeft) / gizmoSize) * 2.0f - 1.0f;
    float normalizedY = -(((canvasY - gizmoTop) / gizmoSize) * 2.0f - 1.0f); // flip Y

    ViewportGizmoAxis axis = viewGizmo->hitTest(normalizedX, normalizedY);
    if (axis == ViewportGizmoAxis::NONE) {
        return false;
    }

    Camera* camera = sceneProject->sceneRender->getCamera();

    switch (axis) {
        case ViewportGizmoAxis::POSITIVE_X:  snapCameraToDirection(camera, Vector3(-1, 0, 0)); break; // Look from +X toward origin
        case ViewportGizmoAxis::NEGATIVE_X:  snapCameraToDirection(camera, Vector3( 1, 0, 0)); break;
        case ViewportGizmoAxis::POSITIVE_Y:  snapCameraToDirection(camera, Vector3( 0,-1, 0)); break; // Look from +Y (top)
        case ViewportGizmoAxis::NEGATIVE_Y:  snapCameraToDirection(camera, Vector3( 0, 1, 0)); break;
        case ViewportGizmoAxis::POSITIVE_Z:  snapCameraToDirection(camera, Vector3( 0, 0,-1)); break;
        case ViewportGizmoAxis::NEGATIVE_Z:  snapCameraToDirection(camera, Vector3( 0, 0, 1)); break;
        default: break;
    }

    return true;
}

void editor::SceneWindow::showCanvas(SceneProject& sceneProject, bool& gameCursorInSceneRect) {
    const bool isPlaying = (sceneProject.playState == ScenePlayState::PLAYING);
    const bool isStopped = (sceneProject.playState == ScenePlayState::STOPPED);
    const bool isCameraDetached = sceneProject.sceneRender->isCameraDetached();
    const bool isCameraPreview = isStopped && sceneProject.sceneRender->isPreviewCameraActive();
    const Entity previewCameraEntity = isCameraPreview ? sceneProject.sceneRender->getPreviewCameraEntity() : NULL_ENTITY;

    ImGui::BeginChild(("Canvas" + std::to_string(sceneProject.id)).c_str());
    {
        if (focusCanvasOnNextFrame[sceneProject.id]) {
            ImGui::SetWindowFocus();
            focusCanvasOnNextFrame[sceneProject.id] = false;
            windowFocused = true;
            project->setSelectedSceneId(sceneProject.id);
        }

        int widthNew = ImGui::GetContentRegionAvail().x;
        int heightNew = ImGui::GetContentRegionAvail().y;

        ImGuiViewport* canvasViewport = ImGui::GetWindowViewport();
        ImVec2 scale = (canvasViewport && canvasViewport->FramebufferScale.x > 0.0f && canvasViewport->FramebufferScale.y > 0.0f)
            ? canvasViewport->FramebufferScale
            : ImGui::GetIO().DisplayFramebufferScale;
        if (scale.x <= 0.0f) scale.x = 1.0f;
        if (scale.y <= 0.0f) scale.y = 1.0f;

        float dpiScale = (canvasViewport && canvasViewport->DpiScale > 0.0f)
            ? canvasViewport->DpiScale
            : 1.0f;
        if (dpiScale < 0.5f || dpiScale > 8.0f) {
            dpiScale = 1.0f;
        }

        const ImVec2 prevScale = framebufferScale[sceneProject.id];
        const float prevDpi = canvasDpiScale[sceneProject.id];
        if (widthNew != width[sceneProject.id] || heightNew != height[sceneProject.id]
            || prevScale.x != scale.x || prevScale.y != scale.y
            || prevDpi != dpiScale) {
            width[sceneProject.id] = widthNew;
            height[sceneProject.id] = heightNew;
            framebufferScale[sceneProject.id] = scale;
            canvasDpiScale[sceneProject.id] = dpiScale;

            sceneProject.needUpdateRender = true;
        }

        ImTextureID previewTex = Backend::getImGuiTexture(&sceneProject.sceneRender->getTexture());
        ImVec2 canvasAvail = ImGui::GetContentRegionAvail();

        if (isCameraPreview) {
            // Fixed-aspect cameras (autoResize == false) render distorted into the
            // full viewport; display them at their true aspect (letterbox/pillarbox)
            // so the preview matches the framing they'll actually render. autoResize
            // cameras already fill the viewport correctly, so they stay full-size.
            ImVec2 imageSize = canvasAvail;
            if (CameraComponent* previewCam = sceneProject.scene->findComponent<CameraComponent>(previewCameraEntity)) {
                if (!previewCam->autoResize && canvasAvail.x > 0 && canvasAvail.y > 0) {
                    float camAspect;
                    if (previewCam->type == CameraType::CAMERA_PERSPECTIVE) {
                        camAspect = previewCam->aspect;
                    } else {
                        float clipW = previewCam->rightClip - previewCam->leftClip;
                        float clipH = previewCam->topClip - previewCam->bottomClip;
                        camAspect = (clipH != 0.0f) ? std::fabs(clipW / clipH) : 0.0f;
                    }
                    if (camAspect > 0.0f) {
                        float viewAspect = canvasAvail.x / canvasAvail.y;
                        if (camAspect > viewAspect) {
                            imageSize = ImVec2(canvasAvail.x, canvasAvail.x / camAspect);
                        } else {
                            imageSize = ImVec2(canvasAvail.y * camAspect, canvasAvail.y);
                        }
                    }
                }
            }

            ImVec2 canvasMin = ImGui::GetCursorScreenPos();
            ImVec2 imageMin(canvasMin.x + (canvasAvail.x - imageSize.x) * 0.5f,
                            canvasMin.y + (canvasAvail.y - imageSize.y) * 0.5f);
            ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasMin, ImVec2(canvasMin.x + canvasAvail.x, canvasMin.y + canvasAvail.y), IM_COL32(0, 0, 0, 255));

            ImGui::SetCursorScreenPos(imageMin);
            Widgets::image(previewTex, imageSize);

            // Yellow frame as a "viewing through camera" cue (inset 1px so it's not clipped at the edge).
            // Matches the previewed camera's highlight color in the Structure tree.
            const float frameInset = Theme::dpi(1.0f);
            drawList->AddRect(ImVec2(imageMin.x + frameInset, imageMin.y + frameInset), ImVec2(imageMax.x - frameInset, imageMax.y - frameInset), IM_COL32(255, 219, 51, 235), 0.0f, 0, Theme::dpi(2.0f));
        } else {
            // Black until the first render creates the framebuffer.
            Widgets::image(previewTex, canvasAvail, IM_COL32(0, 0, 0, 255));
        }

        if (sceneProject.playState == ScenePlayState::STOPPED && !isCameraPreview){
            if (ImGui::IsWindowHovered()) {
                CursorSelected cursorSelected = sceneProject.sceneRender->getCursorSelected();
                if (cursorSelected == CursorSelected::HAND) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                } else {
                    Gizmo2DSideSelected side = sceneProject.sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                    bool uiScene = sceneProject.sceneType == SceneType::SCENE_UI;
                    if (side == Gizmo2DSideSelected::NX || side == Gizmo2DSideSelected::PX) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    } else if (side == Gizmo2DSideSelected::NY || side == Gizmo2DSideSelected::PY) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    } else if (side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::PX_PY) {
                        ImGui::SetMouseCursor(uiScene ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
                    } else if (side == Gizmo2DSideSelected::NX_PY || side == Gizmo2DSideSelected::PX_NY) {
                        ImGui::SetMouseCursor(uiScene ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeNWSE);
                    }
                }
            }

            handleResourceFileDragDrop(&sceneProject);
            handleTileRectDragDrop(&sceneProject);
        }

        // Viewport gizmo click-to-snap (3D scenes only)
        if (sceneProject.sceneType == SceneType::SCENE_3D && (isStopped || isCameraDetached) && !isCameraPreview) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 windowPos = ImGui::GetWindowPos();
                ImGuiIO& io = ImGui::GetIO();
                float canvasX = io.MousePos.x - windowPos.x;
                float canvasY = io.MousePos.y - windowPos.y;
                toEditorCanvas(&sceneProject, canvasX, canvasY);
                ImVec2 canvasSize = getEditorCanvasSize(&sceneProject);
                suppressLeftMouseUntilRelease[sceneProject.id] = handleViewportGizmoClick(&sceneProject, canvasX, canvasY, (int)canvasSize.x, (int)canvasSize.y);
            }
        }

        if (isPlaying && !isCameraDetached && ImGui::IsWindowHovered()) {
            gameCursorInSceneRect = true;
        }

        sceneEventHandler(&sceneProject);
    }
    ImGui::EndChild();
}

void editor::SceneWindow::showFullscreenCanvas(SceneProject& sceneProject, bool& gameCursorInSceneRect) {
    // Its docked tab can be hidden behind another one
    sceneProject.isVisible = true;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##SceneFullscreen", nullptr, flags);
    ImGui::PopStyleVar(3);

    // Over the docked panels, under popups (alerts, crash report)
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        windowFocused = true;
        project->setSelectedSceneId(sceneProject.id);
    }

    showCanvas(sceneProject, gameCursorInSceneRect);

    // Escape belongs to the game
    if (ImGui::GetTime() - fullscreenStartTime < 3.0) {
        const char* hint = "Press F11 to exit full screen";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        const ImVec2 padding(Theme::dpi(14.0f), Theme::dpi(8.0f));
        const ImVec2 hintMin(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f - padding.x, viewport->Pos.y + Theme::dpi(24.0f));
        const ImVec2 hintMax(hintMin.x + textSize.x + padding.x * 2.0f, hintMin.y + textSize.y + padding.y * 2.0f);

        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        drawList->AddRectFilled(hintMin, hintMax, IM_COL32(0, 0, 0, 190), Theme::dpi(6.0f));
        drawList->AddText(ImVec2(hintMin.x + padding.x, hintMin.y + padding.y), IM_COL32(255, 255, 255, 255), hint);
    }

    ImGui::End();
}

void editor::SceneWindow::show() {
    for (uint32_t sceneId: closeSceneQueue){
        project->closeScene(sceneId);
    }
    closeSceneQueue.clear();

    windowFocused = false;
    bool gameCursorInSceneRect = false;

    int openedScenesCount = 0;
    for (const auto& s : project->getScenes()) {
        if (s.opened) openedScenesCount++;
    }

    // Iterate through all scenes in the project
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.opened) continue;

        // Disable close button if this is the only open scene
        bool canClose = openedScenesCount > 1;
        bool isOpen = true;

        const bool isRuntimeActive = sceneProject.playState == ScenePlayState::PLAYING
            || sceneProject.playState == ScenePlayState::PAUSED;

        if (sceneProject.id == fullscreenSceneId && !isRuntimeActive) {
            exitFullscreen();
        }

        if (hasNotification[sceneProject.id]) {
            App::pushTabNotificationStyle();
        }
        ImGuiWindowFlags windowFlags = hasNotification[sceneProject.id] ? ImGuiWindowFlags_UnsavedDocument : 0;
        ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ImVec2(FLT_MAX, FLT_MAX));
        const bool sceneWindowVisible = ImGui::Begin(getWindowTitle(sceneProject).c_str(), canClose ? &isOpen : nullptr, windowFlags);
        if (hasNotification[sceneProject.id]) App::popTabNotificationStyle();

        if (!isRuntimeActive) {
            hasNotification[sceneProject.id] = false;
        } else if (sceneWindowVisible) {
            hasNotification[sceneProject.id] = false;
        } else {
            hasNotification[sceneProject.id] = true;
        }

        if (sceneWindowVisible) {
            sceneProject.isVisible = true;

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                windowFocused = true;
                project->setSelectedSceneId(sceneProject.id);
            }

            bool isPlaying = (sceneProject.playState == ScenePlayState::PLAYING);
            bool isPaused = (sceneProject.playState == ScenePlayState::PAUSED);
            bool isStopped = (sceneProject.playState == ScenePlayState::STOPPED);
            bool isSaving = (sceneProject.playState == ScenePlayState::SAVING);
            bool isLoading = (sceneProject.playState == ScenePlayState::LOADING);
            bool isCancelling = (sceneProject.playState == ScenePlayState::CANCELLING);
            bool isCameraDetached = sceneProject.sceneRender->isCameraDetached();
            bool isCameraPreview = isStopped && sceneProject.sceneRender->isPreviewCameraActive();
            Entity previewCameraEntity = isCameraPreview ? sceneProject.sceneRender->getPreviewCameraEntity() : NULL_ENTITY;
            if (isCameraPreview) {
                sceneProject.needUpdateRender = true;
            }

            // Full-height toolbar vertically centered on a text-height row, overlapping the
            // spacing above/below (same layout pattern as the code editor status row)
            float toolbarRowY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(toolbarRowY - (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);

            // Play button - disabled when already playing
            ImGui::BeginDisabled(isPlaying || isSaving || isLoading || isCancelling || (isStopped && project->isAnyScenePlaying()));
            if (ImGui::Button(ICON_FA_PLAY " Play")) {
                if (!isPaused) {
                    project->start(sceneProject.id);
                }else{
                    project->resume(sceneProject.id);
                }
            }
            ImGui::EndDisabled();

            // Pause/Resume button - disabled when stopped
            ImGui::BeginDisabled(isStopped || isPaused || isSaving || isLoading || isCancelling);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_PAUSE " Pause")) {
                project->pause(sceneProject.id);
            }
            ImGui::EndDisabled();

            // Stop button - disabled when stopped
            ImGui::BeginDisabled(isStopped || isSaving || isCancelling);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " Stop")) {
                project->stop(sceneProject.id);
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0, Theme::dpi(10.0f));
            ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));

            // Detach camera toggle - only while the scene runs
            ImGui::BeginDisabled(!isPlaying && !isPaused);
            ImGui::SameLine();
            if (isCameraDetached) ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ButtonActivated);
            if (ImGui::Button(ICON_FA_VIDEO)) {
                project->toggleCameraDetached(sceneProject.id);
            }
            if (isCameraDetached) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Detach camera (F8)");
            ImGui::EndDisabled();

            // Full screen - only while the scene runs
            ImGui::BeginDisabled(!isPlaying && !isPaused);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_MAXIMIZE)) {
                toggleFullscreen(sceneProject.id);
            }
            ImGui::SetItemTooltip("Full screen (F11)");
            ImGui::EndDisabled();

            // Touch simulation toggle - disabled while any scene plays
            bool simulateTouch = project->isSimulateTouch();
            ImGui::BeginDisabled(project->isAnyScenePlaying());
            ImGui::SameLine();
            if (simulateTouch) ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ButtonActivated);
            if (ImGui::Button(ICON_FA_MOBILE_SCREEN)) {
                project->setSimulateTouch(!simulateTouch);
                project->saveWorkspaceFile();
            }
            if (simulateTouch) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Play as a touch device (the mouse acts as one finger)");
            ImGui::EndDisabled();

            ImGui::SameLine(0, Theme::dpi(10.0f));
            ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
            ImGui::SameLine(0, Theme::dpi(10.0f));

            CursorSelected cursorSelected = sceneProject.sceneRender->getCursorSelected();

            // While viewing through a camera the editor manipulation tools are inert
            // (input is suppressed in sceneEventHandler and gizmos are hidden), so
            // disable the whole select/gizmo/transform group. Closed before the Gear
            // button, which stays usable. Nests with each button's own BeginDisabled.
            ImGui::BeginDisabled(isCameraPreview);

            ImGui::BeginDisabled(cursorSelected == CursorSelected::POINTER);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_POINTER)) {
                sceneProject.sceneRender->enableCursorPointer();
            }
            ImGui::SetItemTooltip("Select mode");
            ImGui::EndDisabled();

            ImGui::BeginDisabled(cursorSelected == CursorSelected::HAND);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_HAND)) {
                sceneProject.sceneRender->enableCursorHand();
            }
            ImGui::SetItemTooltip("Pan view");
            ImGui::EndDisabled();

            ImGui::SameLine(0, Theme::dpi(10.0f));
            ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));

            GizmoSelected gizmoSelected = sceneProject.sceneRender->getToolsLayer()->getGizmoSelected();
            bool multipleEntitiesSelected = sceneProject.sceneRender->isMultipleEntitesSelected();

            if (sceneProject.sceneType != SceneType::SCENE_UI){
                ImGui::SameLine(0, Theme::dpi(10.0f));

                if (sceneProject.sceneType != SceneType::SCENE_3D){
                    ImGui::BeginDisabled(gizmoSelected == GizmoSelected::OBJECT2D);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_EXPAND)) {
                        sceneProject.sceneRender->getToolsLayer()->enableObject2DGizmo();
                    }
                    ImGui::SetItemTooltip("2D Gizmo (Q)");
                    ImGui::EndDisabled();
                }

                ImGui::BeginDisabled(gizmoSelected == GizmoSelected::TRANSLATE);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT)) {
                    sceneProject.sceneRender->getToolsLayer()->enableTranslateGizmo();
                }
                ImGui::SetItemTooltip("Translate (W)");
                ImGui::EndDisabled();

                ImGui::BeginDisabled(gizmoSelected == GizmoSelected::ROTATE);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_ROTATE)) {
                    sceneProject.sceneRender->getToolsLayer()->enableRotateGizmo();
                }
                ImGui::SetItemTooltip("Rotate (E)");
                ImGui::EndDisabled();

                ImGui::BeginDisabled(gizmoSelected == GizmoSelected::SCALE);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER)) {
                    sceneProject.sceneRender->getToolsLayer()->enableScaleGizmo();
                }
                ImGui::SetItemTooltip("Scale (R)");
                ImGui::EndDisabled();

                ImGui::SameLine(0, Theme::dpi(10.0f));
                ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
                ImGui::SameLine(0, Theme::dpi(10.0f));

                bool useGlobalTransform = sceneProject.sceneRender->isUseGlobalTransform();

                ImGui::BeginDisabled(useGlobalTransform || gizmoSelected == GizmoSelected::OBJECT2D || multipleEntitiesSelected);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_GLOBE)) {
                    sceneProject.sceneRender->setUseGlobalTransform(true);
                }
                ImGui::SetItemTooltip("World transform (T)");
                ImGui::EndDisabled();

                ImGui::BeginDisabled(!useGlobalTransform || gizmoSelected == GizmoSelected::OBJECT2D || multipleEntitiesSelected);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_LOCATION_DOT)) {
                    sceneProject.sceneRender->setUseGlobalTransform(false);
                }
                ImGui::SetItemTooltip("Local transform (T)");
                ImGui::EndDisabled();

                ImGui::SameLine(0, Theme::dpi(10.0f));
                ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
            }

            // Close the select/gizmo/transform disabled group opened before the cursor buttons.
            ImGui::EndDisabled();

            {
                std::string sceneSettingsPopupId = "SceneSettingsPopup" + std::to_string(sceneProject.id);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_GEAR)) {
                    ImGui::OpenPopup(sceneSettingsPopupId.c_str());
                }
                ImGui::SetItemTooltip("Scene display settings");

                if (ImGui::BeginPopup(sceneSettingsPopupId.c_str())) {
                    // same table id in every section keeps the columns aligned
                    auto beginSection = [](const char* title) {
                        ImGui::SeparatorText(title);
                        if (!ImGui::BeginTable("scene_settings_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                            return false;
                        }
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, Theme::dpi(175.0f));
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, Theme::dpi(80.0f));
                        return true;
                    };

                    auto drawSettingRow = [&](const char* name, bool& value, bool disabled = false) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (disabled) ImGui::BeginDisabled();
                        ImGui::Text("%s", name);
                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::Checkbox((std::string("##") + name).c_str(), &value)) {
                            sceneProject.needUpdateRender = true;
                            project->saveWorkspaceFile();
                        }
                        if (disabled) ImGui::EndDisabled();
                    };

                    bool notStopped = (sceneProject.playState != ScenePlayState::STOPPED);

                    if (beginSection("Overlays")) {
                        drawSettingRow(ICON_FA_LINK " Show all joints", sceneProject.displaySettings.showAllJoints, notStopped);
                        drawSettingRow(ICON_FA_BONE " Show all bones", sceneProject.displaySettings.showAllBones, notStopped);
                        drawSettingRow(ICON_FA_CUBES " Show all bodies", sceneProject.displaySettings.showAllBodies, notStopped);

                        drawSettingRow(ICON_FA_CAMERA " Hide camera view", sceneProject.displaySettings.hideCameraView);
                        drawSettingRow(ICON_FA_EXPAND " Show full camera frustum", sceneProject.displaySettings.showFullFrustum);
                        drawSettingRow(ICON_FA_LIGHTBULB " Hide light icons", sceneProject.displaySettings.hideLightIcons, sceneProject.sceneType != SceneType::SCENE_3D);
                        drawSettingRow(ICON_FA_VOLUME_HIGH " Hide sound icons", sceneProject.displaySettings.hideSoundIcons);

                        drawSettingRow(ICON_FA_SQUARE " Hide container guides", sceneProject.displaySettings.hideContainerGuides, sceneProject.sceneType == SceneType::SCENE_3D);

                        if (sceneProject.sceneType != SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Show origin axis", sceneProject.displaySettings.showOrigin);
                        }

                        drawSettingRow(ICON_FA_OBJECT_GROUP " Hide selection outline", sceneProject.displaySettings.hideSelectionOutline);

                        ImGui::EndTable();
                    }

                    if (beginSection("Rendering")) {
                        if (sceneProject.sceneType == SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_CLONE " Disable face culling", sceneProject.displaySettings.disableFaceCulling, notStopped);
                        }
                        drawSettingRow(ICON_FA_SMOG " Disable fog", sceneProject.displaySettings.disableFog, notStopped);
                        if (sceneProject.sceneType == SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_WATER " Disable underwater", sceneProject.displaySettings.disableUnderwater, notStopped);
                        }

                        ImGui::EndTable();
                    }

                    if (beginSection("Grid & snapping")) {
                        if (sceneProject.sceneType == SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_TABLE_CELLS " Show grid", sceneProject.displaySettings.showGrid3D);

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", ICON_FA_TABLE_CELLS " Grid spacing");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-1);
                            if (ImGui::DragFloat("##GridSpacing3D", &sceneProject.displaySettings.gridSpacing3D, 0.1f, 0.1f, 1000.0f, "%.1f")) {
                                sceneProject.needUpdateRender = true;
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                project->saveWorkspaceFile();
                            }
                        } else {
                            drawSettingRow(ICON_FA_TABLE_CELLS " Show grid", sceneProject.displaySettings.showGrid2D);
                            if (sceneProject.displaySettings.showGrid2D) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s", ICON_FA_TABLE_CELLS " Grid spacing");
                                ImGui::TableSetColumnIndex(1);
                                ImGui::SetNextItemWidth(-1);
                                if (ImGui::DragFloat("##GridSpacing2D", &sceneProject.displaySettings.gridSpacing2D, 1.0f, 1.0f, 10000.0f, "%.0f")) {
                                    sceneProject.needUpdateRender = true;
                                }
                                if (ImGui::IsItemDeactivatedAfterEdit()) {
                                    project->saveWorkspaceFile();
                                }
                            }
                        }

                        drawSettingRow(ICON_FA_MAGNET " Snap to grid", sceneProject.displaySettings.snapToGrid);
                        if (sceneProject.sceneType != SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_BORDER_ALL " Snap tile", sceneProject.displaySettings.snapTile);
                        }

                        drawSettingRow(ICON_FA_ROTATE " Snap rotation", sceneProject.displaySettings.snapRotation);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", ICON_FA_ROTATE " Rotation step");
                        ImGui::TableSetColumnIndex(1);
                        if (!sceneProject.displaySettings.snapRotation) ImGui::BeginDisabled();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##RotationSnapDegrees", &sceneProject.displaySettings.rotationSnapDegrees, 1.0f, 0.1f, 180.0f, "%.1f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            project->saveWorkspaceFile();
                        }
                        if (!sceneProject.displaySettings.snapRotation) ImGui::EndDisabled();

                        ImGui::EndTable();
                    }

                    if (sceneProject.sceneType == SceneType::SCENE_3D && sceneProject.sceneRender && beginSection("Editor camera")) {
                        Camera* editorCamera = sceneProject.sceneRender->getCamera();

                        auto drawFloatSettingRow = [&](const char* name, const char* id, float value, float defaultValue, float dragSpeed, float minValue, float maxValue, const char* format, auto validate, auto apply) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", name);

                            bool defChanged = std::fabs(value - defaultValue) > 1e-4f;
                            if (defChanged) {
                                ImGui::SameLine();
                                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                                if (ImGui::Button((std::string(ICON_FA_ROTATE_LEFT) + "##" + id).c_str())) {
                                    apply(defaultValue);
                                    sceneProject.needUpdateRender = true;
                                    project->saveWorkspaceFile();
                                }
                                ImGui::PopStyleColor(2);
                                ImGui::PopStyleVar(3);
                            }

                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-1);
                            float editedValue = value;
                            if (ImGui::DragFloat((std::string("##") + id).c_str(), &editedValue, dragSpeed, minValue, maxValue, format)) {
                                apply(editedValue);
                                validate();
                                sceneProject.needUpdateRender = true;
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                project->saveWorkspaceFile();
                            }
                        };

                        auto validateNearClip = [&]() {
                            if (editorCamera->getNearClip() >= editorCamera->getFarClip()) {
                                editorCamera->setNearClip(editorCamera->getFarClip() - 0.01f);
                            }
                        };
                        auto validateFarClip = [&]() {
                            if (editorCamera->getFarClip() <= editorCamera->getNearClip()) {
                                editorCamera->setFarClip(editorCamera->getNearClip() + 1.0f);
                            }
                        };

                        drawFloatSettingRow(
                            ICON_FA_CAMERA " Editor camera near", "EditorCameraNear",
                            editorCamera->getNearClip(), DEFAULT_EDITOR_CAMERA_NEAR,
                            0.01f, 0.01f, 100.0f, "%.2f", validateNearClip,
                            [&](float v) { editorCamera->setNearClip(v); });

                        drawFloatSettingRow(
                            ICON_FA_CAMERA " Editor camera far", "EditorCameraFar",
                            editorCamera->getFarClip(), DEFAULT_EDITOR_CAMERA_FAR,
                            1.0f, 1.0f, 100000.0f, "%.0f", validateFarClip,
                            [&](float v) { editorCamera->setFarClip(v); });

                        ImGui::EndTable();
                    }

                    ImGui::EndPopup();
                }

                ImGui::SameLine(0, Theme::dpi(10.0f));
                ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
            }

            if (isCameraPreview) {
                ImGui::SameLine(0, Theme::dpi(10.0f));
                ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
                ImGui::SameLine(0, Theme::dpi(10.0f));

                std::string cameraName = sceneProject.scene->getEntityName(previewCameraEntity);
                if (cameraName.empty()) {
                    cameraName = "Camera";
                }

                ImGui::Text("%s %s", ICON_FA_VIDEO, cameraName.c_str());
                ImGui::SetItemTooltip("Viewing through this camera");
                ImGui::SameLine();
                std::string stopPreviewButtonId = std::string(ICON_FA_XMARK) + "##StopCameraPreview" + std::to_string(sceneProject.id);
                if (ImGui::Button(stopPreviewButtonId.c_str())) {
                    stopViewingCamera(sceneProject.id);
                    isCameraPreview = false;
                }
                ImGui::SetItemTooltip("Exit camera preview (Esc)");
            }

            drawParentSceneButton(sceneProject);

            if (sceneProject.id != fullscreenSceneId) {
                ImGui::SetCursorPosY(toolbarRowY + ImGui::GetTextLineHeightWithSpacing());
                showCanvas(sceneProject, gameCursorInSceneRect);
            }
        } else {
            sceneProject.isVisible = false;
        }
        ImGui::End();

        if (sceneProject.id == fullscreenSceneId) {
            showFullscreenCanvas(sceneProject, gameCursorInSceneRect);
        }

        // Handle window closing
        if (!isOpen) {
            handleCloseScene(sceneProject.id);
        }
    }

    Backend::setGameCursorInSceneRect(gameCursorInSceneRect);
}

ImVec2 editor::SceneWindow::canvasFramebufferScale(uint32_t sceneId) const {
    auto it = framebufferScale.find(sceneId);
    return (it != framebufferScale.end()) ? it->second : ImVec2(1.0f, 1.0f);
}

float editor::SceneWindow::canvasDpi(uint32_t sceneId) const {
    auto it = canvasDpiScale.find(sceneId);
    return (it != canvasDpiScale.end() && it->second > 0.0f) ? it->second : 1.0f;
}

ImVec2 editor::SceneWindow::canvasEngineScale(uint32_t sceneId) const {
    return Backend::sceneRenderScale(canvasFramebufferScale(sceneId), canvasDpi(sceneId));
}

void editor::SceneWindow::toEngineCanvas(uint32_t sceneId, float& x, float& y) const {
    ImVec2 scale = canvasEngineScale(sceneId);
    x *= scale.x;
    y *= scale.y;
}

// Detached, the engine keeps the game's canvas mapped onto the view rect, as for game input
void editor::SceneWindow::toEditorCanvas(const SceneProject* sceneProject, float& x, float& y) const {
    toEngineCanvas(sceneProject->id, x, y);
    if (sceneProject->sceneRender->isCameraDetached()) {
        const Rect view = Engine::getViewRect();
        x = (x - view.getX()) * Engine::getCanvasWidth() / view.getWidth();
        y = (y - view.getY()) * Engine::getCanvasHeight() / view.getHeight();
    }
}

ImVec2 editor::SceneWindow::getEditorCanvasSize(const SceneProject* sceneProject) const {
    if (sceneProject->sceneRender->isCameraDetached()) {
        return ImVec2(Engine::getCanvasWidth(), Engine::getCanvasHeight());
    }
    return ImVec2(getWidth(sceneProject->id), getHeight(sceneProject->id));
}

int editor::SceneWindow::getWidth(uint32_t sceneId) const{
    if (!width.count(sceneId) || width.at(sceneId) <= 0){
        return 0;
    }
    return std::max(1, (int)std::lround((float)width.at(sceneId) * canvasEngineScale(sceneId).x));
}

int editor::SceneWindow::getHeight(uint32_t sceneId) const{
    if (!height.count(sceneId) || height.at(sceneId) <= 0){
        return 0;
    }
    return std::max(1, (int)std::lround((float)height.at(sceneId) * canvasEngineScale(sceneId).y));
}

float editor::SceneWindow::getOverlayScale(uint32_t sceneId) const {
    return canvasDpi(sceneId) * canvasEngineScale(sceneId).x;
}
