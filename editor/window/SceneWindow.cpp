#include "SceneWindow.h"

#include "Input.h"
#include "external/IconsFontAwesome6.h"
#include "Backend.h"
#include "util/Util.h"
#include "util/EntityPayload.h"
#include "command/CommandHandle.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/LinkMaterialCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "util/ProjectUtils.h"
#include "render/SceneRender2D.h"
#include "render/SceneRender3D.h"
#include "Stream.h"
#include "Out.h"
#include "App.h"

#include "math/Vector2.h"
#include "util/Angle.h"
#include "yaml-cpp/yaml.h"
#include <algorithm>
#include <cmath>

using namespace doriax;

namespace {

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

void editor::SceneWindow::clearSceneState(uint32_t sceneId) {
    draggingMouse.erase(sceneId);
    suppressLeftMouseUntilRelease.erase(sceneId);
    walkSpeed.erase(sceneId);
    width.erase(sceneId);
    height.erase(sceneId);
    hasNotification.erase(sceneId);
    lastPlayState.erase(sceneId);

    closeSceneQueue.erase(
        std::remove(closeSceneQueue.begin(), closeSceneQueue.end(), sceneId),
        closeSceneQueue.end());
}

void editor::SceneWindow::focusSceneWindow(const SceneProject& sceneProject) const {
    const std::string windowTitle = getWindowTitle(sceneProject);
    ImGui::SetWindowFocus(windowTitle.c_str());
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
    return icon + sceneProject.name + ((project->hasSceneUnsavedChanges(sceneProject.id)) ? " *" : "") + "###Scene" + std::to_string(sceneProject.id);
}

void editor::SceneWindow::handleResourceFileDragDrop(SceneProject* sceneProject) {
    static bool draggingResourceFile = false;
    static Image* tempImage = nullptr;
    static MeshComponent* selMesh = nullptr;
    static UIComponent* selUI = nullptr;
    static TextComponent* selText = nullptr;
    static std::vector<Material> originalMeshMaterials;
    static unsigned int originalMeshNumSubmeshes = 0;
    static Material cachedDragMaterial;
    static std::string cachedDragPath;
    static Texture originalTex;
    static std::string originalFont;
    static Entity lastSelEntity = NULL_ENTITY;

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* peekPayload = ImGui::GetDragDropPayload();
        if (peekPayload && peekPayload->IsDataType("resource_files")) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(peekPayload);
            if (receivedStrings.size() > 0) {
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();
                bool isFont = Util::isFontFile(droppedRelativePath);
                bool isImage = Util::isImageFile(droppedRelativePath);
                bool isMaterial = Util::isMaterialFile(droppedRelativePath);

                draggingResourceFile = true;
                ImVec2 windowPos = ImGui::GetWindowPos();
                ImGuiIO& io = ImGui::GetIO();
                ImVec2 mousePos = io.MousePos;
                float x = mousePos.x - windowPos.x;
                float y = mousePos.y - windowPos.y;
                Entity selEntity = project->findObjectByRay(sceneProject->id, x, y);

                if (selEntity == NULL_ENTITY || lastSelEntity != selEntity) {
                    if (selMesh) {
                        for (unsigned int s = 0; s < originalMeshNumSubmeshes && s < selMesh->numSubmeshes; s++) {
                            if (selMesh->submeshes[s].material != originalMeshMaterials[s]) {
                                selMesh->submeshes[s].material = originalMeshMaterials[s];
                                selMesh->submeshes[s].needUpdateTexture = true;
                            }
                        }
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

                if (selEntity != NULL_ENTITY) {
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
                                    if (text->font != droppedRelativePath) {
                                        text->font = droppedRelativePath;
                                        text->needReloadAtlas = true;
                                        text->needUpdateText = true;
                                    }
                                    if (payload->IsDelivery()) {
                                        std::string propName = "font";
                                        text->font = originalFont;
                                        text->needReloadAtlas = true;
                                        text->needUpdateText = true;

                                        PropertyCmd<std::string>* cmd = new PropertyCmd<std::string>(project, sceneProject->id, selEntity, ComponentType::TextComponent, propName, droppedRelativePath);
                                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);

                                        selText = nullptr;

                                        focusSceneWindow(*sceneProject);
                                    }
                                }
                            } else if (isImage) {
                                if (MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(selEntity)) {
                                    if (!selMesh) {
                                        selMesh = mesh;
                                        originalMeshMaterials.clear();
                                        originalMeshNumSubmeshes = mesh->numSubmeshes;
                                        for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                            originalMeshMaterials.push_back(mesh->submeshes[s].material);
                                        }
                                        originalTex = mesh->submeshes[0].material.baseColorTexture;
                                    }
                                    Texture newTex(droppedRelativePath);
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

                                        PropertyCmd<Texture>* cmd = new PropertyCmd<Texture>(project, sceneProject->id, selEntity, ComponentType::MeshComponent, propName, newTex);
                                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);

                                        selMesh = nullptr;
                                        originalMeshMaterials.clear();

                                        focusSceneWindow(*sceneProject);
                                    }
                                } else if (UIComponent* ui = sceneProject->scene->findComponent<UIComponent>(selEntity)) {
                                    if (!selUI) {
                                        selUI = ui;
                                        originalTex = ui->texture;
                                    }
                                    Texture newTex(droppedRelativePath);
                                    if (ui->texture != newTex) {
                                        ui->texture = newTex;
                                        ui->needUpdateTexture = true;
                                    }
                                    if (payload->IsDelivery()) {
                                        std::string propName = "texture";
                                        selUI->texture = originalTex;

                                        PropertyCmd<Texture>* cmd = new PropertyCmd<Texture>(project, sceneProject->id, selEntity, ComponentType::UIComponent, propName, newTex);
                                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(cmd);

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
                                            YAML::Node materialNode = YAML::LoadFile(materialPath.string());
                                            cachedDragMaterial = Stream::decodeMaterial(materialNode);
                                            cachedDragMaterial.name = fs::path(droppedRelativePath).lexically_normal().generic_string();
                                            cachedDragPath = droppedRelativePath;
                                        }

                                        if (!selMesh) {
                                            selMesh = mesh;
                                            originalMeshMaterials.clear();
                                            originalMeshNumSubmeshes = mesh->numSubmeshes;
                                            for (unsigned int s = 0; s < mesh->numSubmeshes; s++) {
                                                originalMeshMaterials.push_back(mesh->submeshes[s].material);
                                            }
                                        }

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
                                tempImage->setTexture(droppedRelativePath);
                                tempImage->setAlpha(0.5f);
                            }
                            Ray ray = sceneProject->sceneRender->getCamera()->screenToRay(x, y);
                            RayReturn rreturn = ray.intersects(Plane(Vector3(0, 0, 1), Vector3(0, 0, 0)));
                            if (rreturn) {
                                tempImage->setPosition(rreturn.point);
                            }
                            if (payload->IsDelivery()) {
                                CreateEntityCmd* cmd = nullptr;

                                if (sceneProject->sceneType == SceneType::SCENE_2D) {
                                    cmd = new CreateEntityCmd(project, sceneProject->id, "Sprite", EntityCreationType::SPRITE);

                                    cmd->addProperty<Vector3>(ComponentType::Transform, "position", rreturn.point);
                                    cmd->addProperty<Texture>(ComponentType::MeshComponent, "submeshes[0].material.baseColorTexture", Texture(droppedRelativePath));
                                    cmd->addProperty<unsigned int>(ComponentType::SpriteComponent, "width", tempImage->getWidth());
                                    cmd->addProperty<unsigned int>(ComponentType::SpriteComponent, "height", tempImage->getHeight());
                                } else {
                                    cmd = new CreateEntityCmd(project, sceneProject->id, "Image", EntityCreationType::IMAGE);

                                    cmd->addProperty<Vector3>(ComponentType::Transform, "position", rreturn.point);
                                    cmd->addProperty<Texture>(ComponentType::UIComponent, "texture", Texture(droppedRelativePath));
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

                            int freeSlot = (int)tilemap->numTiles;
                            if (freeSlot < (int)tilemap->tiles.size()) {
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

void editor::SceneWindow::sceneEventHandler(SceneProject* sceneProject) {
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

    // When scene is playing, forward mouse events to Engine
    if (sceneProject->playState == ScenePlayState::PLAYING && isMouseInWindow) {
        float x = mousePos.x - windowPos.x;
        float y = mousePos.y - windowPos.y;

        int mods = 0;
        if (io.KeyShift) mods |= S_MODIFIER_SHIFT;
        if (io.KeyCtrl) mods |= S_MODIFIER_CONTROL;
        if (io.KeyAlt) mods |= S_MODIFIER_ALT;
        if (io.KeySuper) mods |= S_MODIFIER_SUPER;

        Engine::systemMouseMove(x, y, mods);

        if (mouseWheel != 0) {
            Engine::systemMouseScroll(0, mouseWheel, mods);
        }

        for (int i = 0; i < 5; i++) {
            if (ImGui::IsMouseClicked(i)) {
                Engine::systemMouseDown(i, x, y, mods);
            }
            if (ImGui::IsMouseReleased(i)) {
                Engine::systemMouseUp(i, x, y, mods);
            }
        }
        return;
    }

    size_t sceneId = sceneProject->id;

    bool altHeld = ImGui::IsKeyDown(ImGuiKey_ModAlt);
    bool suppressLeftMouse = suppressLeftMouseUntilRelease[sceneId];

    bool disableSelection = 
        altHeld ||
        sceneProject->sceneRender->getCursorSelected() == CursorSelected::HAND || 
        sceneProject->sceneRender->isAnyGizmoSideSelected() ||
        sceneProject->sceneType != SceneType::SCENE_3D && ImGui::IsKeyDown(ImGuiKey_Space);

    if (isMouseInWindow){

        float x = mousePos.x - windowPos.x;
        float y = mousePos.y - windowPos.y;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !altHeld && !suppressLeftMouse){
            // Selecting and dragging an unselected object at same time (just for 2D object mode)
            GizmoSelected gizmoSelected = sceneProject->sceneRender->getToolsLayer()->getGizmoSelected();
            bool gizmoSideActive = sceneProject->sceneRender->isAnyGizmoSideSelected();
            if (!disableSelection && gizmoSelected == GizmoSelected::OBJECT2D) {
                Entity hitEntity = project->findObjectByRay(sceneId, x, y);
                if (hitEntity != NULL_ENTITY) {
                    bool alreadySelected = project->isSelectedEntity(sceneId, hitEntity);
                    if (!alreadySelected || (io.KeyShift && !gizmoSideActive)) {
                        sceneProject->sceneRender->clearTileSelection();
                        sceneProject->sceneRender->clearInstanceSelection();
                        bool changed = project->selectObjectByRay(sceneId, x, y, io.KeyShift);
                        if (changed) {
                            sceneProject->sceneRender->update(project->getSelectedEntities(sceneId), project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
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
                            // Re-update so gizmo wraps the tile
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedTileIndex() >= 0 && gizmo2DSide == Gizmo2DSideSelected::NONE) {
                            sceneProject->sceneRender->clearTileSelection();
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        }
                    }
                }
            }

            // Instance sub-selection within a selected InstancedMeshComponent entity
            {
                std::vector<Entity> selEntities = project->getSelectedEntities(sceneId);
                if (selEntities.size() == 1 && !io.KeyShift) {
                    Entity selEntity = selEntities[0];
                    bool gizmoSideActiveLocal = sceneProject->sceneRender->isAnyGizmoSideSelected();
                    if (!gizmoSideActiveLocal) {
                        int instHit = sceneProject->sceneRender->hitTestInstance(selEntity, x, y);
                        if (instHit >= 0) {
                            sceneProject->sceneRender->selectInstance(selEntity, instHit);
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        } else if (sceneProject->sceneRender->getSelectedInstanceIndex() >= 0) {
                            sceneProject->sceneRender->clearInstanceSelection();
                            sceneProject->sceneRender->update(selEntities, project->getEntities(sceneId), sceneProject->mainCamera, sceneProject->displaySettings);
                            sceneProject->sceneRender->mouseHoverEvent(x, y);
                        }
                    }
                }
            }
            // Shift+click on gizmo: duplicate before starting drag
            if (io.KeyShift && sceneProject->sceneRender->isAnyGizmoSideSelected()){
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

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !altHeld && !suppressLeftMouse){
            if (!mouseLeftDown){
                mouseLeftStartPos = Vector2(x, y);
                mouseLeftDown = true;
            }
            mouseLeftDragPos = Vector2(x, y);
            if (mouseLeftStartPos.distance(mouseLeftDragPos) > 5){
                mouseLeftDraggedInside = true;
            }
            if (mouseLeftDraggedInside){
                sceneProject->sceneRender->mouseDragEvent(x, y, mouseLeftStartPos.x, mouseLeftStartPos.y, project, sceneId, project->getSelectedEntities(sceneId), disableSelection);
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !altHeld && !suppressLeftMouse){
            if (!mouseLeftDraggedInside && mouseLeftDown && !disableSelection){
                // Remember sub-selection anchors BEFORE re-selecting, so we can tell
                // whether the release click actually changed the host entity.
                int prevTileIndex = sceneProject->sceneRender->getSelectedTileIndex();
                Entity prevTileEntity = sceneProject->sceneRender->getSelectedTileEntity();
                int prevInstanceIndex = sceneProject->sceneRender->getSelectedInstanceIndex();
                Entity prevInstanceEntity = sceneProject->sceneRender->getSelectedInstanceEntity();

                project->selectObjectByRay(sceneId, x, y, io.KeyShift);

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
            }
            mouseLeftDown = false;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
        float x = mousePos.x - windowPos.x;
        float y = mousePos.y - windowPos.y;

        if (suppressLeftMouse){
            suppressLeftMouseUntilRelease[sceneId] = false;
            mouseLeftDown = false;
            mouseLeftDraggedInside = false;
        }else{

            if (mouseLeftDraggedInside && !disableSelection){
                Vector2 clickStartPos = Vector2((2 * mouseLeftStartPos.x / width[sceneId]) - 1, -((2 * mouseLeftStartPos.y / height[sceneId]) - 1));
                Vector2 clickEndPos = Vector2((2 * mouseLeftDragPos.x / width[sceneId]) - 1, -((2 * mouseLeftDragPos.y / height[sceneId]) - 1));
                sceneProject->sceneRender->clearTileSelection();
                sceneProject->sceneRender->clearInstanceSelection();
                project->selectObjectsByRect(sceneId, clickStartPos, clickEndPos);
            }

            sceneProject->sceneRender->mouseReleaseEvent(x, y);

            mouseLeftDraggedInside = false;
        }
    }

    if (isMouseInWindow && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            (altHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !suppressLeftMouse))) {
        draggingMouse[sceneId] = true;

        ImGui::SetWindowFocus();
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) || ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
            (altHeld && ImGui::IsMouseReleased(ImGuiMouseButton_Left))){
        draggingMouse[sceneId] = false;
        Backend::enableMouseCursor();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    Camera* camera = sceneProject->sceneRender->getCamera();

    bool walkingMode = false;

    if (sceneProject->sceneType == SceneType::SCENE_3D){

        float distanceFromTarget = camera->getDistanceFromTarget();

        // Scale factor: movements should be proportional to distance
        // Base movement rate is maintained at around distanceFromTarget = 10
        float distanceScaleFactor = std::max(0.1f, distanceFromTarget / 10.0f);

        // Check for mouse clicks
        if (draggingMouse[sceneId] && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                walkingMode = true;

                camera->rotateView(-0.1 * mouseDelta.x);
                camera->elevateView(-0.1 * mouseDelta.y);

                // Restore up vector after rotation (fixes Y-snap leaving Z as up)
                Vector3 dir = camera->getWorldDirection().normalize();
                if (std::abs(dir.y) < 0.99f) {
                    camera->setUp(0, 1, 0);
                }

                float minSpeed = 0.5;
                float maxSpeed = 1000;
                float speedOffset = 10.0;

                walkSpeed[sceneId] += mouseWheel;
                if (walkSpeed[sceneId] <= -speedOffset) {
                    walkSpeed[sceneId] = -speedOffset + minSpeed;
                }
                if (walkSpeed[sceneId] > maxSpeed) {
                    walkSpeed[sceneId] = maxSpeed;
                }

                // Apply distanceScaleFactor to walking speed
                float finalSpeed = 0.02 * (speedOffset + walkSpeed[sceneId]) * distanceScaleFactor;

                // Shift multiplier for faster movement
                if (ImGui::IsKeyDown(ImGuiKey_ModShift)) {
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
                if (ImGui::IsKeyDown(ImGuiKey_ModShift)) {
                    camera->slide(-0.01 * mouseDelta.x * distanceScaleFactor);
                    camera->slideUp(0.01 * mouseDelta.y * distanceScaleFactor);
                } else if (ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
                    float ctrlZoomAmount = -0.1f * mouseDelta.y * distanceScaleFactor;
                    if (ctrlZoomAmount > distanceFromTarget - 0.1f) {
                        ctrlZoomAmount = distanceFromTarget - 0.1f;
                    }
                    camera->zoom(ctrlZoomAmount);
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

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !altHeld && sceneProject->sceneRender->getCursorSelected() == CursorSelected::HAND) {
            camera->slide(-0.01 * mouseDelta.x * distanceScaleFactor);
            camera->slideUp(0.01 * mouseDelta.y * distanceScaleFactor);
        }

        // The zoom speed itself can remain constant
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (isMouseInWindow && mouseWheel != 0.0f) {
                float zoomAmount = 2.0 * mouseWheel * distanceScaleFactor;
                if (zoomAmount > distanceFromTarget - 0.1f) {
                     zoomAmount = distanceFromTarget - 0.1f;
                }
                camera->zoom(zoomAmount);
            }
        }

    }else{

        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || 
                (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_Space)) ||
                (ImGui::IsMouseDown(ImGuiMouseButton_Left) && sceneProject->sceneRender->getCursorSelected() == CursorSelected::HAND)){

            SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
            float currentZoom = sceneRender2D->getZoom();

            float slideX = -currentZoom * mouseDelta.x;
            float slideY = -currentZoom * mouseDelta.y;

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

                if (sceneProject->sceneType == SceneType::SCENE_2D){
                    mouseY = height[sceneProject->id] - mouseY;
                }

                SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
                sceneRender2D->zoomAtPosition(width[sceneProject->id], height[sceneProject->id], Vector2(mouseX, mouseY), zoomFactor);
            }
        }

    }

    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()){
        if (project->getSelectedSceneId() == sceneId){
            if (!walkingMode){

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
                    if (!ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
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
    AABB aabb = sceneProject->sceneRender->getEntitiesAABB(entities);
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

    Vector3 direction = camera->getWorldDirection();
    direction.normalize();
    Vector3 newPos = center - direction * distance;

    camera->setPosition(newPos.x, newPos.y, newPos.z);
    camera->setTarget(center.x, center.y, center.z);
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

    // The gizmo Image is 100x100 anchored at TOP_RIGHT of the canvas
    float gizmoLeft = canvasWidth - VIEWPORT_GIZMO_SIZE;
    float gizmoTop = 0.0f;

    // Check if click is within the gizmo area
    if (canvasX < gizmoLeft || canvasX > canvasWidth || canvasY < gizmoTop || canvasY > VIEWPORT_GIZMO_SIZE) {
        return false;
    }

    // Normalize to [-1, 1] within the gizmo area
    float normalizedX = ((canvasX - gizmoLeft) / VIEWPORT_GIZMO_SIZE) * 2.0f - 1.0f;
    float normalizedY = -(((canvasY - gizmoTop) / VIEWPORT_GIZMO_SIZE) * 2.0f - 1.0f); // flip Y

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

void editor::SceneWindow::show() {
    for (uint32_t sceneId: closeSceneQueue){
        project->closeScene(sceneId);
    }
    closeSceneQueue.clear();

    windowFocused = false;

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

        // Detect play state transition to trigger notification
        ScenePlayState prevState = lastPlayState[sceneProject.id];
        if (sceneProject.playState == ScenePlayState::PLAYING && prevState != ScenePlayState::PLAYING) {
            if (!sceneProject.isVisible) {
                hasNotification[sceneProject.id] = true;
            }
        }
        if (sceneProject.playState == ScenePlayState::STOPPED && prevState != ScenePlayState::STOPPED) {
            hasNotification[sceneProject.id] = false;
        }
        lastPlayState[sceneProject.id] = sceneProject.playState;

        if (hasNotification[sceneProject.id]) {
            App::pushTabNotificationStyle();
        }
        ImGuiWindowFlags windowFlags = hasNotification[sceneProject.id] ? ImGuiWindowFlags_UnsavedDocument : 0;
        ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin(getWindowTitle(sceneProject).c_str(), canClose ? &isOpen : nullptr, windowFlags)) {
            if (hasNotification[sceneProject.id]) App::popTabNotificationStyle();
            sceneProject.isVisible = true;
            hasNotification[sceneProject.id] = false;

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                windowFocused = true;
                project->setSelectedSceneId(sceneProject.id);
            }

            bool isPlaying = (sceneProject.playState == ScenePlayState::PLAYING);
            bool isPaused = (sceneProject.playState == ScenePlayState::PAUSED);
            bool isStopped = (sceneProject.playState == ScenePlayState::STOPPED);
            bool isCancelling = (sceneProject.playState == ScenePlayState::CANCELLING);

            // Play button - disabled when already playing
            ImGui::BeginDisabled(isPlaying || isCancelling || (isStopped && project->isAnyScenePlaying()));
            if (ImGui::Button(ICON_FA_PLAY " Play")) {
                if (!isPaused) {
                    project->start(sceneProject.id);
                }else{
                    project->resume(sceneProject.id);
                }
            }
            ImGui::EndDisabled();

            // Pause/Resume button - disabled when stopped
            ImGui::BeginDisabled(isStopped || isPaused || isCancelling);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_PAUSE " Pause")) {
                project->pause(sceneProject.id);
            }
            ImGui::EndDisabled();

            // Stop button - disabled when stopped
            ImGui::BeginDisabled(isStopped || isCancelling);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " Stop")) {
                project->stop(sceneProject.id);
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0, 10);
            ImGui::Dummy(ImVec2(1, 20));
            ImGui::SameLine(0, 10);

            CursorSelected cursorSelected = sceneProject.sceneRender->getCursorSelected();

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

            ImGui::SameLine(0, 10);
            ImGui::Dummy(ImVec2(1, 20));

            GizmoSelected gizmoSelected = sceneProject.sceneRender->getToolsLayer()->getGizmoSelected();
            bool multipleEntitiesSelected = sceneProject.sceneRender->isMultipleEntitesSelected();

            if (sceneProject.sceneType != SceneType::SCENE_UI){
                ImGui::SameLine(0, 10);

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

                ImGui::SameLine(0, 10);
                ImGui::Dummy(ImVec2(1, 20));
                ImGui::SameLine(0, 10);

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

                ImGui::SameLine(0, 10);
                ImGui::Dummy(ImVec2(1, 20));
            }

            {
                std::string sceneSettingsPopupId = "SceneSettingsPopup" + std::to_string(sceneProject.id);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_GEAR)) {
                    ImGui::OpenPopup(sceneSettingsPopupId.c_str());
                }
                ImGui::SetItemTooltip("Scene display settings");

                if (ImGui::BeginPopup(sceneSettingsPopupId.c_str())) {
                    if (ImGui::BeginTable("scene_settings_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 175.0f);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                        auto drawSettingRow = [](const char* name, bool& value, bool disabled = false) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            if (disabled) ImGui::BeginDisabled();
                            ImGui::Text("%s", name);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Checkbox((std::string("##") + name).c_str(), &value);
                            if (disabled) ImGui::EndDisabled();
                        };

                        bool notStopped = (sceneProject.playState != ScenePlayState::STOPPED);

                        drawSettingRow(ICON_FA_LINK " Show all joints", sceneProject.displaySettings.showAllJoints, notStopped);
                        drawSettingRow(ICON_FA_BONE " Show all bones", sceneProject.displaySettings.showAllBones, notStopped);
                        drawSettingRow(ICON_FA_CUBES " Hide all bodies", sceneProject.displaySettings.hideAllBodies, notStopped);

                        drawSettingRow(ICON_FA_CAMERA " Hide camera view", sceneProject.displaySettings.hideCameraView);
                        drawSettingRow(ICON_FA_LIGHTBULB " Hide light icons", sceneProject.displaySettings.hideLightIcons, sceneProject.sceneType != SceneType::SCENE_3D);

                        drawSettingRow(ICON_FA_SQUARE " Hide container guides", sceneProject.displaySettings.hideContainerGuides, sceneProject.sceneType == SceneType::SCENE_3D);

                        if (sceneProject.sceneType == SceneType::SCENE_3D) {
                            drawSettingRow(ICON_FA_TABLE_CELLS " Show grid", sceneProject.displaySettings.showGrid3D);
                        } else {
                            drawSettingRow(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Show origin axis", sceneProject.displaySettings.showOrigin);
                        }

                        drawSettingRow(ICON_FA_OBJECT_GROUP " Hide selection outline", sceneProject.displaySettings.hideSelectionOutline);
                        drawSettingRow(ICON_FA_MAGNET " Snap to grid", sceneProject.displaySettings.snapToGrid);

                        if (sceneProject.sceneType == SceneType::SCENE_3D) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", ICON_FA_TABLE_CELLS " Grid spacing");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-1);
                            ImGui::DragFloat("##GridSpacing3D", &sceneProject.displaySettings.gridSpacing3D, 0.1f, 0.1f, 1000.0f, "%.1f");
                        } else {
                            drawSettingRow(ICON_FA_TABLE_CELLS " Show grid", sceneProject.displaySettings.showGrid2D);
                            if (sceneProject.displaySettings.showGrid2D) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s", ICON_FA_TABLE_CELLS " Grid spacing");
                                ImGui::TableSetColumnIndex(1);
                                ImGui::SetNextItemWidth(-1);
                                ImGui::DragFloat("##GridSpacing2D", &sceneProject.displaySettings.gridSpacing2D, 1.0f, 1.0f, 10000.0f, "%.0f");
                            }
                        }

                        ImGui::EndTable();
                    }

                    ImGui::EndPopup();
                }

                ImGui::SameLine(0, 10);
                ImGui::Dummy(ImVec2(1, 20));
            }

            ImGui::BeginChild(("Canvas" + std::to_string(sceneProject.id)).c_str());
            {
                int widthNew = ImGui::GetContentRegionAvail().x;
                int heightNew = ImGui::GetContentRegionAvail().y;

                if (widthNew != width[sceneProject.id] || heightNew != height[sceneProject.id]) {
                    width[sceneProject.id] = ImGui::GetContentRegionAvail().x;
                    height[sceneProject.id] = ImGui::GetContentRegionAvail().y;

                    sceneProject.needUpdateRender = true;
                }

                ImGui::Image((ImTextureID)(intptr_t)sceneProject.sceneRender->getTexture().getGLHandler(), ImGui::GetContentRegionAvail(), ImVec2(0, 1), ImVec2(1, 0));

                if (sceneProject.playState == ScenePlayState::STOPPED){
                    if (ImGui::IsWindowHovered()) {
                        CursorSelected cursorSelected = sceneProject.sceneRender->getCursorSelected();
                        if (cursorSelected == CursorSelected::HAND) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                        } else {
                            Gizmo2DSideSelected side = sceneProject.sceneRender->getToolsLayer()->getGizmo2DSideSelected();
                            if (side == Gizmo2DSideSelected::NX || side == Gizmo2DSideSelected::PX) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                            } else if (side == Gizmo2DSideSelected::NY || side == Gizmo2DSideSelected::PY) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                            } else if (side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::PX_PY) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                            } else if (side == Gizmo2DSideSelected::NX_PY || side == Gizmo2DSideSelected::PX_NY) {
                                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                            }
                        }
                    }

                    handleResourceFileDragDrop(&sceneProject);
                    handleTileRectDragDrop(&sceneProject);
                }

                // Viewport gizmo click-to-snap (3D scenes only)
                if (sceneProject.sceneType == SceneType::SCENE_3D && sceneProject.playState == ScenePlayState::STOPPED) {
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        ImVec2 windowPos = ImGui::GetWindowPos();
                        ImGuiIO& io = ImGui::GetIO();
                        float canvasX = io.MousePos.x - windowPos.x;
                        float canvasY = io.MousePos.y - windowPos.y;
                        suppressLeftMouseUntilRelease[sceneProject.id] = handleViewportGizmoClick(&sceneProject, canvasX, canvasY, width[sceneProject.id], height[sceneProject.id]);
                    }
                }

                sceneEventHandler(&sceneProject);
            }
            ImGui::EndChild();
        }else{
            if (hasNotification[sceneProject.id]) App::popTabNotificationStyle();
            sceneProject.isVisible = false;
        }
        ImGui::End();

        // Handle window closing
        if (!isOpen) {
            handleCloseScene(sceneProject.id);
        }
    }
}

int editor::SceneWindow::getWidth(uint32_t sceneId) const{
    if (width.count(sceneId)){
        return width.at(sceneId);
    }

    return 0;
}

int editor::SceneWindow::getHeight(uint32_t sceneId) const{
    if (height.count(sceneId)){
        return height.at(sceneId);
    }

    return 0;
}
