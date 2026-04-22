#include "SceneRender.h"

#include "resources/icons/camera-icon_png.h"

#include "Project.h"
#include "command/CommandHandle.h"
#include "command/type/ObjectTransformCmd.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "util/GraphicUtils.h"

#include <cmath>
#include <cfloat>

using namespace doriax;

editor::SceneRender::SceneRender(Scene* scene, bool use2DGizmos, bool enableViewGizmo, float gizmoScale, float selectionOffset): toolslayer(use2DGizmos), uilayer(enableViewGizmo){
    ScopedDefaultEntityPool sys(*scene, EntityPool::System);

    this->mouseClicked = false;
    this->lastCommand = nullptr;
    this->useGlobalTransform = true;
    this->isPlaying = false;

    this->gizmoScale = gizmoScale;
    this->selectionOffset = selectionOffset;

    this->scene = scene;
    this->camera = new Camera(scene);

    this->multipleEntitiesSelected = false;
    // displaySettings initialised with struct defaults

    this->zoom = 1.0f;

    selLines = new Lines(scene);
    selLines->addLine(Vector3::ZERO, Vector3::ZERO, Vector4::ZERO);
    selLines->setVisible(false);

    scene->setCamera(camera);

    cursorSelected = CursorSelected::POINTER;
}

editor::SceneRender::~SceneRender(){
    framebuffer.destroy();

    delete camera;
}

AABB editor::SceneRender::getAABB(Entity entity, bool local){
    Signature signature = scene->getSignature(entity);
    if (signature.test(scene->getComponentId<MeshComponent>())){
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        if (local){
            return mesh.aabb;
        }else{
            return mesh.worldAABB;
        }
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        if (signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                AABB aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
                if (local){
                    return aabb;
                }else{
                    if (signature.test(scene->getComponentId<Transform>())){
                        Transform& transform = scene->getComponent<Transform>(entity);
                        return transform.modelMatrix * aabb;
                    }
                    return aabb;
                }
            }
        }

        UIComponent& ui = scene->getComponent<UIComponent>(entity);
        if (local){
            return ui.aabb;
        }else{
            return ui.worldAABB;
        }
    }else if (signature.test(scene->getComponentId<CameraComponent>()) && signature.test(scene->getComponentId<Transform>())){
        CameraComponent& sceneCamera = scene->getComponent<CameraComponent>(camera->getEntity());

        if (sceneCamera.type == CameraType::CAMERA_ORTHO) {
            float halfSize = 16.0f * zoom;
            AABB aabb(-halfSize, -halfSize, -1, halfSize, halfSize, 1);

            if (local){
                return aabb;
            }else{
                Transform& transform = scene->getComponent<Transform>(entity);
                return transform.modelMatrix * aabb;
            }
        }
    }

    return AABB();
}

AABB editor::SceneRender::getFamilyAABB(Entity entity, float offset){
    auto transforms = scene->getComponentArray<Transform>();
    size_t index = transforms->getIndex(entity);

    AABB aabb;
    std::vector<Entity> parentList;
    for (int i = index; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);

        // Finding childs
        if (i > index){
            if (std::find(parentList.begin(), parentList.end(), transform.parent) == parentList.end()){
                break;
            }
        }

        entity = transforms->getEntity(i);
        parentList.push_back(entity);

        AABB entityAABB = getAABB(entity, false);

        if (!entityAABB.isNull() && !entityAABB.isInfinite()){
            Vector3 min = entityAABB.getMinimum() - Vector3(offset);
            Vector3 max = entityAABB.getMaximum() + Vector3(offset);
            entityAABB.setExtents(min, max);

            aabb.merge(entityAABB);
        }
    }

    return aabb;
}

AABB editor::SceneRender::getEntitiesAABB(const std::vector<Entity>& entities){
    AABB aabb;
    for (Entity entity : entities) {
        AABB entityAABB = getFamilyAABB(entity, 0.0f);
        if (!entityAABB.isNull() && !entityAABB.isInfinite()) {
            aabb.merge(entityAABB);
        }
    }
    return aabb;
}

OBB editor::SceneRender::getOBB(Entity entity, bool local){
    Signature signature = scene->getSignature(entity);

    Matrix4 modelMatrix;
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        modelMatrix = transform.modelMatrix;

        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            if (local){
                return mesh.aabb.getOBB();
            }else{
                return modelMatrix * mesh.aabb.getOBB();
            }
        }else if (signature.test(scene->getComponentId<UIComponent>())){
            if (signature.test(scene->getComponentId<UILayoutComponent>())){
                UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                if (layout.width > 0 && layout.height > 0){
                    Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                    AABB aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
                    if (local){
                        return aabb.getOBB();
                    }else{
                        return modelMatrix * aabb.getOBB();
                    }
                }
            }

            UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (local){
                return ui.aabb.getOBB();
            }else{
                return modelMatrix * ui.aabb.getOBB();
            }
        }else if (signature.test(scene->getComponentId<CameraComponent>())){
            CameraComponent& sceneCamera = scene->getComponent<CameraComponent>(camera->getEntity());

            if (sceneCamera.type == CameraType::CAMERA_ORTHO) {
                float halfSize = 16.0f * zoom;
                AABB aabb(-halfSize, -halfSize, -1, halfSize, halfSize, 1);
                if (local){
                    return aabb.getOBB();
                }else{
                    return modelMatrix * aabb.getOBB();
                }
            }
        }

        if (local){
            return OBB::ZERO;
        }else{
            return modelMatrix * OBB::ZERO;
        }
    }

    return OBB(); // null OBB
}

OBB editor::SceneRender::getFamilyOBB(Entity entity, float offset){
    auto transforms = scene->getComponentArray<Transform>();
    size_t index = transforms->getIndex(entity);

    OBB obb;
    std::vector<Entity> parentList;
    for (int i = index; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);

        // Finding childs
        if (i > index){
            if (std::find(parentList.begin(), parentList.end(), transform.parent) == parentList.end()){
                break;
            }
        }

        entity = transforms->getEntity(i);
        parentList.push_back(entity);

        OBB entityOBB = getOBB(entity, false);

        if (!entityOBB.isNull()){
            entityOBB.setHalfExtents(entityOBB.getHalfExtents() + Vector3(offset));

            obb.enclose(entityOBB);
        }
    }

    return obb;
}

void editor::SceneRender::hideAllGizmos(){
    selLines->setVisible(false);
    toolslayer.setGizmoVisible(false);
    uilayer.setViewGizmoImageVisible(false);
    uilayer.setSelectionBoxVisible(false);
}

void editor::SceneRender::setPlayMode(bool isPlaying){
    this->isPlaying = isPlaying;
    if (isPlaying){
        hideAllGizmos();
    }else{
        scene->setCamera(camera);
    }
}

void editor::SceneRender::activate(){
    Engine::setFramebuffer(&framebuffer);
    Engine::setScene(scene);

    Engine::removeAllSceneLayers(false);

    for (Scene* childScene : childSceneLayers) {
        Engine::addSceneLayer(childScene);
    }

    Engine::addSceneLayer(toolslayer.getScene());
    Engine::addSceneLayer(uilayer.getScene());
}

void editor::SceneRender::setChildSceneLayers(const std::vector<Scene*>& layers){
    childSceneLayers = layers;
}

void editor::SceneRender::updateSize(int width, int height){

}

void editor::SceneRender::updateRenderSystem(){
    // Meshes and UIs are created in update, without this can affect worldAABB
    scene->getSystem<MeshSystem>()->update(0);
    scene->getSystem<UISystem>()->update(0);
    // to avoid gizmos delays
    scene->getSystem<RenderSystem>()->update(0);
}

void editor::SceneRender::update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings){
    displaySettings = settings;
    if (isPlaying){
        return;
    }

    Entity cameraEntity = camera->getEntity();
    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(cameraEntity);
    Transform& cameratransform = scene->getComponent<Transform>(cameraEntity);

    // TODO: avoid get gizmo position and rotation every frame
    Vector3 gizmoPosition;
    Quaternion gizmoRotation;

    bool sameRotation = true;
    Quaternion firstRotation;
    bool firstEntity = true;

    size_t numTEntities = 0;
    std::vector<OBB> selBB;
    OBB totalSelBB;

    multipleEntitiesSelected = selEntities.size() > 1;

    bool showAnchorGizmo = false;
    Vector4 anchorData = Vector4::ZERO; // x: left, y: top, z: right, w: bottom
    Rect anchorArea = Rect(0.0f, 0.0f, 0.0f, 0.0f);

    for (Entity& entity: selEntities){
        if (Transform* transform = scene->findComponent<Transform>(entity)){
            numTEntities++;

            if (selEntities.size() == 1){
                Entity entity = selEntities[0];
                if (UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity)){
                    if (layout->usingAnchors){
                        showAnchorGizmo = true;
                    }
                    anchorData.x = layout->anchorPointLeft;
                    anchorData.y = layout->anchorPointTop;
                    anchorData.z = layout->anchorPointRight;
                    anchorData.w = layout->anchorPointBottom;

                    anchorArea = scene->getSystem<UISystem>()->getAnchorReferenceRect(*layout, *transform, true);
                }
            }

            gizmoPosition += transform->worldPosition;

            if (firstEntity) {
                firstRotation = transform->worldRotation;
                firstEntity = false;
            } else if (transform->worldRotation != firstRotation) {
                sameRotation = false;
            }

            if ((!useGlobalTransform && selEntities.size() == 1) || toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
                gizmoRotation = transform->worldRotation;
            }

            selBB.push_back(getFamilyOBB(entity, selectionOffset));

            totalSelBB.enclose(getOBB(entity, false));
        }

    }

    toolslayer.setShowAnchorGizmo(showAnchorGizmo);

    totalSelBB.setHalfExtents(totalSelBB.getHalfExtents() + Vector3(selectionOffset));

    bool selectionVisibility = false;
    if (numTEntities > 0){
        gizmoPosition /= numTEntities;

        selectionVisibility = true;

        float scale = gizmoScale * zoom;
        if (cameracomp.type == CameraType::CAMERA_PERSPECTIVE){
            float dist = (gizmoPosition - camera->getWorldPosition()).length();
            scale = std::tan(cameracomp.yfov) * dist * (gizmoScale / (float)framebuffer.getHeight());
            if (!std::isfinite(scale) || scale <= 0.0f) {
                scale = 1.0f;
            }
        }

        toolslayer.updateGizmo(camera, gizmoPosition, gizmoRotation, scale, totalSelBB, mouseRay, mouseClicked, anchorData, anchorArea);

        // Override gizmo for selected tile within tilemap
        if (selectedTileIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTileEntity
            && toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
            OBB tileOBB = getTileOBB(selectedTileEntity, selectedTileIndex);
            if (!tileOBB.isNull()){
                Vector3 tileCenter = tileOBB.getCenter();
                toolslayer.updateGizmo(camera, tileCenter, gizmoRotation, scale, tileOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }
        }

        // Override gizmo for selected instance within instanced mesh
        if (selectedInstanceIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedInstanceEntity){
            InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(selectedInstanceEntity);
            Transform* instTransform = scene->findComponent<Transform>(selectedInstanceEntity);
            if (instmesh && instTransform && selectedInstanceIndex < (int)instmesh->instances.size()){
                OBB instOBB = getInstanceOBB(selectedInstanceEntity, selectedInstanceIndex);
                if (!instOBB.isNull()){
                    const InstanceData& inst = instmesh->instances[selectedInstanceIndex];
                    Quaternion instWorldRotation = getInstanceWorldRotation(*instTransform, *instmesh, inst);
                    Vector3 instCenter = instOBB.getCenter();
                    toolslayer.updateGizmo(camera, instCenter, instWorldRotation, scale, instOBB, mouseRay, mouseClicked, anchorData, anchorArea);
                    // Replace selection outline with the instance OBB
                    selBB.clear();
                    selBB.push_back(instOBB);
                }
            }
        }

        if (selBB.size() > 0) {
            updateSelLines(selBB);
        }
    }
    toolslayer.updateCamera(cameracomp, cameratransform);

    // Determine selLines visibility: hide for single entity with OBJECT2D gizmo or empty selBB
    bool showSelLines = selectionVisibility;
    if (!multipleEntitiesSelected && (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D || selBB.size() == 0)) {
        if (selectedTileIndex < 0 && selectedInstanceIndex < 0) {
            showSelLines = false;
        }
    }
    selLines->setVisible(showSelLines && !displaySettings.hideSelectionOutline);

    if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D && !sameRotation){
        toolslayer.setGizmoVisible(false);
    }else{
        toolslayer.setGizmoVisible(selectionVisibility);
    }

    // Hide resize rects for tilemaps and cameras (must be after setGizmoVisible which propagates visibility to children)
    if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D && selEntities.size() == 1){
        bool isTilemap = scene->getSignature(selEntities[0]).test(scene->getComponentId<TilemapComponent>());
        bool isCamera = scene->getSignature(selEntities[0]).test(scene->getComponentId<CameraComponent>());
        bool isInstancedMesh = scene->getSignature(selEntities[0]).test(scene->getComponentId<InstancedMeshComponent>());
        toolslayer.setShowObject2DRects((!isTilemap || selectedTileIndex >= 0) && !isCamera && !isInstancedMesh);
    }

    uilayer.setViewGizmoImageVisible(true);
}

void editor::SceneRender::mouseHoverEvent(float x, float y){
    mouseRay = camera->screenToRay(x, y);
}

void editor::SceneRender::mouseClickEvent(float x, float y, std::vector<Entity> selEntities){
    mouseClicked = true;

    Vector3 viewDir = camera->getWorldDirection();

    Vector3 gizmoPosition = toolslayer.getGizmoPosition();
    Quaternion gizmoRotation = toolslayer.getGizmoRotation();
    Matrix4 gizmoRMatrix = gizmoRotation.getRotationMatrix();
    Matrix4 gizmoMatrix = Matrix4::translateMatrix(gizmoPosition) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));

    float dotX = viewDir.dotProduct(gizmoRMatrix * Vector3(1,0,0));
    float dotY = viewDir.dotProduct(gizmoRMatrix * Vector3(0,1,0));
    float dotZ = viewDir.dotProduct(gizmoRMatrix * Vector3(0,0,1));

    if (toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE || toolslayer.getGizmoSelected() == GizmoSelected::SCALE){
        if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XYZ){
            cursorPlane = Plane((gizmoRMatrix * Vector3(dotX, dotY, dotZ).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::X){
            cursorPlane = Plane((gizmoRMatrix * Vector3(0, dotY, dotZ).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::Y){
            cursorPlane = Plane((gizmoRMatrix * Vector3(dotX, 0, dotZ).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::Z){
            cursorPlane = Plane((gizmoRMatrix * Vector3(dotX, dotY, 0).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XY){
            cursorPlane = Plane((gizmoRMatrix * Vector3(0, 0, dotZ).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XZ){
            cursorPlane = Plane((gizmoRMatrix * Vector3(0, dotY, 0).normalize()), gizmoPosition);
        }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::YZ){
            cursorPlane = Plane((gizmoRMatrix * Vector3(dotX, 0, 0).normalize()), gizmoPosition);
        }
    }

    if (toolslayer.getGizmoSelected() == GizmoSelected::ROTATE){
        cursorPlane = Plane((gizmoRMatrix * Vector3(dotX, dotY, dotZ).normalize()), gizmoPosition);

        if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::X){
            rotationAxis = gizmoRMatrix * Vector3(dotX, 0.0, 0.0).normalize();
        }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Y){
            rotationAxis = gizmoRMatrix * Vector3(0.0, dotY, 0.0).normalize();
        }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Z){
            rotationAxis = gizmoRMatrix * Vector3(0.0, 0.0, dotZ).normalize();
        }else{
            rotationAxis = cursorPlane.normal;
        }
    }

    if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
        cursorPlane = Plane(Vector3(0, 0, 1), gizmoPosition);
    }

    RayReturn rretrun = mouseRay.intersects(cursorPlane);
    objectMatrixOffset.clear();
    if (rretrun){
        gizmoStartPosition = gizmoPosition;
        cursorStartOffset = gizmoPosition - rretrun.point;
        rotationStartOffset = gizmoRotation;
        scaleStartOffset = Vector3(1,1,1);

        for (Entity& entity: selEntities){
            Signature signature = scene->getSignature(entity);
            if (signature.test(scene->getComponentId<Transform>())){
                Transform& transform = scene->getComponent<Transform>(entity);
                objectMatrixOffset[entity] = gizmoMatrix.inverse() * transform.modelMatrix;
            }
            if (signature.test(scene->getComponentId<SpriteComponent>())){
                SpriteComponent& sprite = scene->getComponent<SpriteComponent>(entity);
                objectSizeOffset[entity] = Vector2(sprite.width, sprite.height);
            }
            if (signature.test(scene->getComponentId<TilemapComponent>())){
                TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
                objectSizeOffset[entity] = Vector2(tilemap.width, tilemap.height);
            }
            if (signature.test(scene->getComponentId<UILayoutComponent>())){
                UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                objectSizeOffset[entity] = Vector2(layout.width, layout.height);
            }
        }

        // Store tile start state for tile sub-selection drag
        if (selectedTileIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTileEntity){
            TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(selectedTileEntity);
            if (tilemap && selectedTileIndex < (int)tilemap->numTiles){
                tileStartPosition = tilemap->tiles[selectedTileIndex].position;
                tileStartWidth = tilemap->tiles[selectedTileIndex].width;
                tileStartHeight = tilemap->tiles[selectedTileIndex].height;

                // Override gizmo start position to tile center in world space
                OBB tileOBB = getTileOBB(selectedTileEntity, selectedTileIndex);
                if (!tileOBB.isNull()){
                    gizmoStartPosition = tileOBB.getCenter();
                    cursorStartOffset = gizmoStartPosition - rretrun.point;
                }
            }
        }

        // Store instance start state for instance sub-selection drag
        if (selectedInstanceIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedInstanceEntity){
            InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(selectedInstanceEntity);
            Transform* instTransform = scene->findComponent<Transform>(selectedInstanceEntity);
            if (instmesh && instTransform && selectedInstanceIndex < (int)instmesh->instances.size()){
                const InstanceData& inst = instmesh->instances[selectedInstanceIndex];
                instanceStartPosition = inst.position;
                instanceStartRotation = inst.rotation;
                instanceStartScale = inst.scale;

                OBB instOBB = getInstanceOBB(selectedInstanceEntity, selectedInstanceIndex);
                if (!instOBB.isNull()){
                    // Gizmo is centered on the instance OBB center (world space); its
                    // orientation must match getInstanceOBB (see getInstanceWorldRotation).
                    gizmoStartPosition = instOBB.getCenter();
                    cursorStartOffset = gizmoStartPosition - rretrun.point;
                    rotationStartOffset = getInstanceWorldRotation(*instTransform, *instmesh, inst);

                    // Build the actual instance world matrix (same formula the renderer uses
                    // in non-billboard mode) so drag math operates on the instance directly.
                    Matrix4 instanceLocalMatrix = Matrix4::translateMatrix(inst.position)
                                                  * inst.rotation.getRotationMatrix()
                                                  * Matrix4::scaleMatrix(inst.scale);
                    Matrix4 instanceWorldMatrix = instTransform->modelMatrix * instanceLocalMatrix;

                    Matrix4 gizmoM = Matrix4::translateMatrix(gizmoStartPosition)
                                     * rotationStartOffset.getRotationMatrix()
                                     * Matrix4::scaleMatrix(Vector3(1, 1, 1));
                    objectMatrixOffset[selectedInstanceEntity] = gizmoM.inverse() * instanceWorldMatrix;
                }
            }
        }
    }

}

void editor::SceneRender::mouseReleaseEvent(float x, float y){
    uilayer.setSelectionBoxVisible(false);

    mouseClicked = false;

    toolslayer.mouseRelease();

    if (lastCommand){
        lastCommand->setNoMerge();
        lastCommand = nullptr;
    }
}

void editor::SceneRender::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection){
    if (!disableSelection && !isPlaying){
        uilayer.setSelectionBoxVisible(true);
        uilayer.updateRect(Vector2(origX, origY), Vector2(x, y) - Vector2(origX, origY));
    }

    Vector3 gizmoPosition = toolslayer.getGizmoPosition();
    Matrix4 gizmoRMatrix = toolslayer.getGizmoRotation().getRotationMatrix();

    for (Entity& entity: selEntities){
        Transform* transform = scene->findComponent<Transform>(entity);
        if (transform){
            RayReturn rretrun = mouseRay.intersects(cursorPlane);

            SceneProject* sceneProject = project->getScene(sceneId);

            bool isInstanceDrag = selectedInstanceIndex >= 0 && selEntities.size() == 1
                                  && entity == selectedInstanceEntity
                                  && scene->findComponent<InstancedMeshComponent>(entity) != nullptr;

            auto emitInstanceTransformCmd = [&](const Matrix4& worldMatrix){
                Matrix4 instLocalMatrix = transform->modelMatrix.inverse() * worldMatrix;
                Vector3 newPos, newScale;
                Quaternion newRot;
                instLocalMatrix.decomposeStandard(newPos, newScale, newRot);

                std::string prefix = "instances[" + std::to_string(selectedInstanceIndex) + "]";
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                GizmoSelected gz = toolslayer.getGizmoSelected();
                if (gz == GizmoSelected::TRANSLATE || gz == GizmoSelected::OBJECT2D){
                    multiCmd->addPropertyCmd<Vector3>(project, sceneProject->id, entity, ComponentType::InstancedMeshComponent, prefix + ".position", newPos);
                }else if (gz == GizmoSelected::ROTATE){
                    multiCmd->addPropertyCmd<Quaternion>(project, sceneProject->id, entity, ComponentType::InstancedMeshComponent, prefix + ".rotation", newRot);
                }else if (gz == GizmoSelected::SCALE){
                    multiCmd->addPropertyCmd<Vector3>(project, sceneProject->id, entity, ComponentType::InstancedMeshComponent, prefix + ".scale", newScale);
                }
                lastCommand = multiCmd;
            };

            if (rretrun){

                toolslayer.mouseDrag(rretrun.point);

                Transform* transformParent = scene->findComponent<Transform>(transform->parent);

                // Emit a transform command for the active drag target (instance or entity).
                // worldMatrix is the new object world matrix built by the current gizmo branch.
                auto applyObjectTransform = [&](const Matrix4& worldMatrix){
                    if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::NONE) return;
                    if (isInstanceDrag){
                        emitInstanceTransformCmd(worldMatrix);
                    }else{
                        Matrix4 m = worldMatrix;
                        if (transformParent){
                            m = transformParent->modelMatrix.inverse() * m;
                        }
                        lastCommand = new ObjectTransformCmd(project, sceneProject->id, entity, m);
                    }
                };

                if (toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE){
                    Vector3 deltaPos = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);

                    if (displaySettings.snapToGrid){
                        float spacing = displaySettings.gridSpacing3D;
                        if (spacing > 0.0f){
                            deltaPos.x = std::round(deltaPos.x / spacing) * spacing;
                            deltaPos.y = std::round(deltaPos.y / spacing) * spacing;
                            deltaPos.z = std::round(deltaPos.z / spacing) * spacing;
                        }
                    }

                    Vector3 newPos;
                    if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XYZ){
                        newPos = gizmoStartPosition + (gizmoRMatrix * deltaPos);
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::X){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, 0, 0));
                    }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Y){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, deltaPos.y, 0));
                    }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Z){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, 0, deltaPos.z));
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, deltaPos.y, 0));
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XZ){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, 0, deltaPos.z));
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::YZ){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, deltaPos.y, deltaPos.z));
                    }

                    Matrix4 gizmoMatrix = Matrix4::translateMatrix(newPos) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));
                    Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                    applyObjectTransform(objMatrix);
                }

                if (toolslayer.getGizmoSelected() == GizmoSelected::ROTATE){
                    Vector3 lastPoint = gizmoPosition - rretrun.point;

                    float dot = cursorStartOffset.dotProduct(lastPoint);
                    float slength = cursorStartOffset.length() * lastPoint.length();
                    float cosine = (slength != 0) ? dot / slength : 0;
                    cosine = std::fmax(-1.0, std::fmin(1.0, cosine));
                    float orig_angle = acos(cosine);

                    Vector3 cross = cursorStartOffset.crossProduct(lastPoint);
                    float sign = cross.dotProduct(cursorPlane.normal);

                    float angle = (sign < 0) ? -orig_angle : orig_angle;

                    Quaternion newRot = Quaternion(Angle::radToDefault(angle), rotationAxis) * rotationStartOffset;

                    Matrix4 gizmoMatrix = Matrix4::translateMatrix(gizmoPosition) * newRot.getRotationMatrix() * Matrix4::scaleMatrix(Vector3(1,1,1));
                    Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                    applyObjectTransform(objMatrix);
                }

                if (toolslayer.getGizmoSelected() == GizmoSelected::SCALE){
                    Vector3 lastPoint = gizmoPosition - rretrun.point;

                    Vector3 startRotPoint = gizmoRMatrix.inverse() * cursorStartOffset;
                    Vector3 lastRotPoint = gizmoRMatrix.inverse() * lastPoint;

                    float radioX = (startRotPoint.x != 0) ? (lastRotPoint.x / startRotPoint.x) : 1;
                    float radioY = (startRotPoint.y != 0) ? (lastRotPoint.y / startRotPoint.y) : 1;
                    float radioZ = (startRotPoint.z != 0) ? (lastRotPoint.z / startRotPoint.z) : 1;

                    Vector3 newScale;
                    if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XYZ){
                        newScale = Vector3((lastPoint.length() / cursorStartOffset.length()));
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::X){
                        newScale = Vector3(radioX, 1, 1);
                    }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Y){
                        newScale = Vector3(1, radioY, 1);
                    }else if(toolslayer.getGizmoSideSelected() == GizmoSideSelected::Z){
                        newScale = Vector3(1, 1, radioZ);
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XY){
                        newScale = Vector3(radioX, radioY, 1);
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XZ){
                        newScale = Vector3(radioX, 1, radioZ);
                    }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::YZ){
                        newScale = Vector3(1, radioY, radioZ);
                    }

                    newScale = newScale * scaleStartOffset;
                    newScale = Vector3(abs(newScale.x), abs(newScale.y), abs(newScale.z));

                    Matrix4 gizmoMatrix = Matrix4::translateMatrix(gizmoPosition) * gizmoRMatrix * Matrix4::scaleMatrix(newScale);
                    Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                    applyObjectTransform(objMatrix);
                }

                if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
                    // Handle tile sub-selection drag
                    if (selectedTileIndex >= 0 && entity == selectedTileEntity){
                        TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);
                        Transform* tileTransform = scene->findComponent<Transform>(entity);
                        if (tilemap && tileTransform && selectedTileIndex < (int)tilemap->numTiles){
                            Vector3 delta = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);
                            Vector3 sizeDelta = gizmoRMatrix.inverse() * -(gizmoStartPosition - rretrun.point - cursorStartOffset);

                            // Transform delta to tilemap local space
                            Vector3 oScale = Vector3(1.0f, 1.0f, 1.0f);
                            oScale.x = Vector3(tileTransform->modelMatrix[0][0], tileTransform->modelMatrix[0][1], tileTransform->modelMatrix[0][2]).length();
                            oScale.y = Vector3(tileTransform->modelMatrix[1][0], tileTransform->modelMatrix[1][1], tileTransform->modelMatrix[1][2]).length();

                            Vector2 localDelta(delta.x / oScale.x, delta.y / oScale.y);
                            Vector2 localSizeDelta(sizeDelta.x / oScale.x, sizeDelta.y / oScale.y);

                            Vector2 newTilePos = tileStartPosition;
                            float newTileW = tileStartWidth;
                            float newTileH = tileStartHeight;
                            const float tileMaxX = tileStartPosition.x + tileStartWidth;
                            const float tileMaxY = tileStartPosition.y + tileStartHeight;

                            Gizmo2DSideSelected side = toolslayer.getGizmo2DSideSelected();
                            if (side == Gizmo2DSideSelected::CENTER){
                                newTilePos = tileStartPosition + localDelta;
                            }else if (side == Gizmo2DSideSelected::NX){
                                newTilePos.x = tileStartPosition.x + localDelta.x;
                                newTileW = tileStartWidth - localSizeDelta.x;
                            }else if (side == Gizmo2DSideSelected::NY){
                                newTilePos.y = tileStartPosition.y + localDelta.y;
                                newTileH = tileStartHeight - localSizeDelta.y;
                            }else if (side == Gizmo2DSideSelected::PX){
                                newTileW = tileStartWidth + localSizeDelta.x;
                            }else if (side == Gizmo2DSideSelected::PY){
                                newTileH = tileStartHeight + localSizeDelta.y;
                            }else if (side == Gizmo2DSideSelected::NX_NY){
                                newTilePos.x = tileStartPosition.x + localDelta.x;
                                newTilePos.y = tileStartPosition.y + localDelta.y;
                                newTileW = tileStartWidth - localSizeDelta.x;
                                newTileH = tileStartHeight - localSizeDelta.y;
                            }else if (side == Gizmo2DSideSelected::NX_PY){
                                newTilePos.x = tileStartPosition.x + localDelta.x;
                                newTileW = tileStartWidth - localSizeDelta.x;
                                newTileH = tileStartHeight + localSizeDelta.y;
                            }else if (side == Gizmo2DSideSelected::PX_NY){
                                newTilePos.y = tileStartPosition.y + localDelta.y;
                                newTileW = tileStartWidth + localSizeDelta.x;
                                newTileH = tileStartHeight - localSizeDelta.y;
                            }else if (side == Gizmo2DSideSelected::PX_PY){
                                newTileW = tileStartWidth + localSizeDelta.x;
                                newTileH = tileStartHeight + localSizeDelta.y;
                            }

                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    // Snap in world space so tiles align to the global grid
                                    // regardless of the tilemap entity's own world position/scale.
                                    // modelMatrix is [col][row]; column 3 holds world translation.
                                    float wX = tileTransform->modelMatrix[3][0];
                                    float wY = tileTransform->modelMatrix[3][1];
                                    float sX = (oScale.x > 0.0f) ? oScale.x : 1.0f;
                                    float sY = (oScale.y > 0.0f) ? oScale.y : 1.0f;

                                    // Convert a local X/Y value to/from world space and snap.
                                    auto snapX = [&](float lx) {
                                        return (std::round((lx * sX + wX) / spacing) * spacing - wX) / sX;
                                    };
                                    auto snapY = [&](float ly) {
                                        return (std::round((ly * sY + wY) / spacing) * spacing - wY) / sY;
                                    };

                                    if (side == Gizmo2DSideSelected::CENTER ||
                                        side == Gizmo2DSideSelected::NX ||
                                        side == Gizmo2DSideSelected::NX_NY ||
                                        side == Gizmo2DSideSelected::NX_PY) {
                                        newTilePos.x = snapX(newTilePos.x);
                                    } else {
                                        float snappedFarX = snapX(newTilePos.x + newTileW);
                                        newTileW = snappedFarX - newTilePos.x;
                                    }
                                    if (side == Gizmo2DSideSelected::CENTER ||
                                        side == Gizmo2DSideSelected::NY ||
                                        side == Gizmo2DSideSelected::NX_NY ||
                                        side == Gizmo2DSideSelected::PX_NY) {
                                        newTilePos.y = snapY(newTilePos.y);
                                    } else {
                                        float snappedFarY = snapY(newTilePos.y + newTileH);
                                        newTileH = snappedFarY - newTilePos.y;
                                    }
                                }
                            }

                            if (side == Gizmo2DSideSelected::CENTER){
                                if (newTilePos.x < 0) newTilePos.x = 0;
                                if (newTilePos.y < 0) newTilePos.y = 0;
                            }

                            if (side == Gizmo2DSideSelected::NX || side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY){
                                newTilePos.x = std::max(0.0f, std::min(newTilePos.x, tileMaxX - 1.0f));
                                newTileW = tileMaxX - newTilePos.x;
                            }

                            if (side == Gizmo2DSideSelected::NY || side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::PX_NY){
                                newTilePos.y = std::max(0.0f, std::min(newTilePos.y, tileMaxY - 1.0f));
                                newTileH = tileMaxY - newTilePos.y;
                            }

                            if (newTileW < 1) newTileW = 1;
                            if (newTileH < 1) newTileH = 1;

                            if (side != Gizmo2DSideSelected::NONE){
                                std::string prefix = "tiles[" + std::to_string(selectedTileIndex) + "]";
                                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                                multiCmd->addPropertyCmd<Vector2>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    prefix + ".position", newTilePos);
                                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    prefix + ".width", newTileW);
                                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entity, ComponentType::TilemapComponent,
                                    prefix + ".height", newTileH);
                                lastCommand = multiCmd;
                            }
                        }
                    }else if (isInstanceDrag){
                        // Instance CENTER drag (position only) for OBJECT2D
                        if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){
                            Vector3 newPos = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);
                            newPos = gizmoStartPosition + (gizmoRMatrix * newPos);
                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    newPos.x = std::round(newPos.x / spacing) * spacing;
                                    newPos.y = std::round(newPos.y / spacing) * spacing;
                                }
                            }
                            Matrix4 gizmoMatrix = Matrix4::translateMatrix(newPos) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));
                            Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];
                            emitInstanceTransformCmd(objMatrix);
                        }
                    }else{
                    // Original entity-level OBJECT2D handling
                    bool isSprite = scene->getComponentArray<SpriteComponent>()->hasEntity(entity);
                    bool isTilemap = scene->getComponentArray<TilemapComponent>()->hasEntity(entity);
                    bool isLayout = scene->getComponentArray<UILayoutComponent>()->hasEntity(entity);
                    bool isText = scene->getComponentArray<TextComponent>()->hasEntity(entity);

                    Vector3 newPos = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);

                    Vector3 newSize = gizmoRMatrix.inverse() * -(gizmoStartPosition - rretrun.point - cursorStartOffset);

                    if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){
                        newPos = gizmoStartPosition + (gizmoRMatrix * newPos);
                        // Snap absolute world position to grid (must be done after newPos becomes world-space)
                        if (displaySettings.snapToGrid) {
                            float spacing = displaySettings.gridSpacing2D;
                            if (spacing > 0.0f) {
                                newPos.x = std::round(newPos.x / spacing) * spacing;
                                newPos.y = std::round(newPos.y / spacing) * spacing;
                            }
                        }
                        newSize = Vector3(0, 0, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::NX){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(newPos.x, 0, 0));
                        newSize = Vector3(-newSize.x, 0, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::NY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, newPos.y, 0));
                        newSize = Vector3(0, -newSize.y, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::PX){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, 0, 0));
                        newSize = Vector3(newSize.x, 0, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::PY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, 0, 0));
                        newSize = Vector3(0, newSize.y, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::NX_NY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(newPos.x, newPos.y, 0));
                        newSize = Vector3(-newSize.x, -newSize.y, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::NX_PY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(newPos.x, 0, 0));
                        newSize = Vector3(-newSize.x, newSize.y, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::PX_NY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, newPos.y, 0));
                        newSize = Vector3(newSize.x, -newSize.y, 0);
                    }else if (toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::PX_PY){
                        newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, 0, 0));
                        newSize = Vector3(newSize.x, newSize.y, 0);
                    }

                    Matrix4 gizmoMatrix = Matrix4::translateMatrix(newPos) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));
                    Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                    if (transformParent){
                        objMatrix = transformParent->modelMatrix.inverse() * objMatrix;
                    }

                    Vector3 oScale = Vector3(1.0f, 1.0f, 1.0f);
                    oScale.x = Vector3(objMatrix[0][0], objMatrix[0][1], objMatrix[0][2]).length();
                    oScale.y = Vector3(objMatrix[1][0], objMatrix[1][1], objMatrix[1][2]).length();
                    oScale.z = Vector3(objMatrix[2][0], objMatrix[2][1], objMatrix[2][2]).length();

                    Vector2 size = objectSizeOffset[entity] + Vector2(newSize.x / oScale.x, newSize.y / oScale.y);
                    Vector3 pos = Vector3(objMatrix[3][0], objMatrix[3][1], objMatrix[3][2]);

                    if (size.x < 0) size.x = 0;
                    if (size.y < 0) size.y = 0;

                    if (isText){
                        TextComponent& text = scene->getComponent<TextComponent>(entity);
                        if (text.pivotCentered){
                            float widthDelta = size.x - objectSizeOffset[entity].x;
                            Gizmo2DSideSelected side = toolslayer.getGizmo2DSideSelected();
                            if (side == Gizmo2DSideSelected::NX || side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY) {
                                pos.x += widthDelta * 0.5f;
                            }else if (side == Gizmo2DSideSelected::PX || side == Gizmo2DSideSelected::PX_NY || side == Gizmo2DSideSelected::PX_PY) {
                                pos.x += widthDelta * 0.5f;
                            }
                        }
                    }

                    if (toolslayer.getGizmo2DSideSelected() != Gizmo2DSideSelected::NONE){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        if (isLayout){
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::UILayoutComponent, "width", static_cast<unsigned int>(size.x));
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::UILayoutComponent, "height", static_cast<unsigned int>(size.y));
                            if (isText){
                                Gizmo2DSideSelected side = toolslayer.getGizmo2DSideSelected();
                                if (side == Gizmo2DSideSelected::NX || side == Gizmo2DSideSelected::PX || 
                                    side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY || 
                                    side == Gizmo2DSideSelected::PX_NY || side == Gizmo2DSideSelected::PX_PY) {
                                    multiCmd->addPropertyCmd<bool>(project, sceneProject->id, entity, ComponentType::TextComponent, "fixedWidth", true);
                                }
                                if (side == Gizmo2DSideSelected::NY || side == Gizmo2DSideSelected::PY || 
                                    side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY || 
                                    side == Gizmo2DSideSelected::PX_NY || side == Gizmo2DSideSelected::PX_PY) {
                                    multiCmd->addPropertyCmd<bool>(project, sceneProject->id, entity, ComponentType::TextComponent, "fixedHeight", true);
                                }
                            }
                        }else if (isSprite){
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::SpriteComponent, "width", static_cast<unsigned int>(size.x));
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::SpriteComponent, "height", static_cast<unsigned int>(size.y));
                        }else if (isTilemap){
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::TilemapComponent, "width", static_cast<unsigned int>(size.x));
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::TilemapComponent, "height", static_cast<unsigned int>(size.y));
                        }
                        multiCmd->addPropertyCmd<Vector3>(project, sceneProject->id, entity, ComponentType::Transform, "position", pos);
                        lastCommand = multiCmd;
                    }
                    } // end else (entity-level OBJECT2D)
                }

                if (lastCommand){
                    CommandHandle::get(sceneId)->addCommand(lastCommand);
                }
            }
        }
    }
}

bool editor::SceneRender::isAnyGizmoSideSelected() const{
    return (toolslayer.getGizmoSideSelected() != editor::GizmoSideSelected::NONE || toolslayer.getGizmo2DSideSelected() != Gizmo2DSideSelected::NONE);
}

TextureRender& editor::SceneRender::getTexture(){
    //return camera->getFramebuffer()->getRender().getColorTexture();
    return framebuffer.getRender().getColorTexture();
}

Camera* editor::SceneRender::getCamera(){
    return camera;
}

editor::ToolsLayer* editor::SceneRender::getToolsLayer(){
    return &toolslayer;
}

editor::UILayer* editor::SceneRender::getUILayer(){
    return &uilayer;
}

bool editor::SceneRender::isUseGlobalTransform() const{
    return useGlobalTransform;
}

void editor::SceneRender::setUseGlobalTransform(bool useGlobalTransform){
    this->useGlobalTransform = useGlobalTransform;
}

void editor::SceneRender::changeUseGlobalTransform(){
    this->useGlobalTransform = !this->useGlobalTransform;
}

void editor::SceneRender::enableCursorPointer(){
    cursorSelected = CursorSelected::POINTER;
}

void editor::SceneRender::enableCursorHand(){
    cursorSelected = CursorSelected::HAND;
}

editor::CursorSelected editor::SceneRender::getCursorSelected() const{
    return cursorSelected;
}

bool editor::SceneRender::isMultipleEntitesSelected() const{
    return multipleEntitiesSelected;
}

void editor::SceneRender::selectTile(Entity entity, int tileIndex){
    selectedTileEntity = entity;
    selectedTileIndex = tileIndex;
}

void editor::SceneRender::clearTileSelection(){
    selectedTileEntity = 0;
    selectedTileIndex = -1;
}

int editor::SceneRender::hitTestTile(Entity entity, float x, float y){
    if (!scene->getComponentArray<TilemapComponent>()->hasEntity(entity)) return -1;
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return -1;

    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    Ray ray = camera->screenToRay(x, y);
    Plane tilePlane(Vector3(0, 0, 1), transform.worldPosition);
    RayReturn rr = ray.intersects(tilePlane);
    if (!rr) return -1;

    // Transform hit point to tilemap local space
    Matrix4 invModel = transform.modelMatrix.inverse();
    Vector3 localHit = invModel * rr.point;

    // Test tiles in reverse order (last drawn = on top)
    for (int i = (int)tilemap.numTiles - 1; i >= 0; i--){
        const TileData& tile = tilemap.tiles[i];
        if (localHit.x >= tile.position.x && localHit.x <= tile.position.x + tile.width &&
            localHit.y >= tile.position.y && localHit.y <= tile.position.y + tile.height){
            return i;
        }
    }

    return -1;
}

OBB editor::SceneRender::getTileOBB(Entity entity, int tileIndex){
    if (!scene->getComponentArray<TilemapComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return OBB();

    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    if (tileIndex < 0 || tileIndex >= (int)tilemap.numTiles) return OBB();

    const TileData& tile = tilemap.tiles[tileIndex];
    AABB tileAABB(tile.position.x, tile.position.y, 0,
                  tile.position.x + tile.width, tile.position.y + tile.height, 0);

    return transform.modelMatrix * tileAABB.getOBB();
}

void editor::SceneRender::selectInstance(Entity entity, int instanceIndex){
    selectedInstanceEntity = entity;
    selectedInstanceIndex = instanceIndex;
}

void editor::SceneRender::clearInstanceSelection(){
    selectedInstanceEntity = 0;
    selectedInstanceIndex = -1;
}

Quaternion editor::SceneRender::getInstanceWorldRotation(const Transform& transform, const InstancedMeshComponent& instmesh, const InstanceData& inst) const{
    // Billboard mode overrides per-instance rotation at render time, so mirror that
    // here by not composing inst.rotation into the picking/gizmo orientation.
    if (instmesh.instancedBillboard){
        return transform.worldRotation;
    }
    return transform.worldRotation * inst.rotation;
}

OBB editor::SceneRender::getInstanceOBB(Entity entity, int instanceIndex){
    if (!scene->getComponentArray<InstancedMeshComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<MeshComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return OBB();

    InstancedMeshComponent& instmesh = scene->getComponent<InstancedMeshComponent>(entity);
    MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    if (instanceIndex < 0 || instanceIndex >= (int)instmesh.instances.size()) return OBB();

    const InstanceData& inst = instmesh.instances[instanceIndex];
    // For billboard meshes the per-instance rotation is ignored at render time, so
    // skip it here too to keep the OBB aligned with what is actually drawn.
    Matrix4 rotMatrix = instmesh.instancedBillboard
        ? Matrix4()
        : inst.rotation.getRotationMatrix();
    Matrix4 instanceMatrix = Matrix4::translateMatrix(inst.position)
                             * rotMatrix
                             * Matrix4::scaleMatrix(inst.scale);

    AABB localAABB = mesh.verticesAABB;
    if (localAABB.isNull() || (localAABB.getMinimum() == Vector3::ZERO && localAABB.getMaximum() == Vector3::ZERO)){
        // Fallback to a small unit cube around origin so the instance is still selectable
        localAABB = AABB(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f));
    }

    return (transform.modelMatrix * instanceMatrix) * localAABB.getOBB();
}

int editor::SceneRender::hitTestInstance(Entity entity, float x, float y){
    if (!scene->getComponentArray<InstancedMeshComponent>()->hasEntity(entity)) return -1;
    if (!scene->getComponentArray<MeshComponent>()->hasEntity(entity)) return -1;
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return -1;

    InstancedMeshComponent& instmesh = scene->getComponent<InstancedMeshComponent>(entity);

    Ray ray = camera->screenToRay(x, y);

    // Only instances within maxInstances are actually rendered, so restrict picking to them.
    int renderedCount = (int)std::min<size_t>(instmesh.instances.size(), instmesh.maxInstances);

    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (int i = 0; i < renderedCount; i++){
        if (!instmesh.instances[i].visible) continue;
        OBB instOBB = getInstanceOBB(entity, i);
        if (instOBB.isNull()) continue;
        RayReturn rr = ray.intersects(instOBB);
        if (rr && rr.distance < bestDistance){
            bestDistance = rr.distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

void editor::SceneRender::setupCameraIcon(CameraObjects& co){
    TextureData iconData;
    iconData.loadTextureFromMemory(camera_icon_png, camera_icon_png_len);
    co.icon->setTexture("editor:resources:camera_icon", iconData);
    co.icon->setSize(128, 128);
    co.icon->setReceiveLights(false);
    co.icon->setCastShadows(false);
    co.icon->setReceiveShadows(false);
    co.icon->setPivotPreset(PivotPreset::CENTER);
}

void editor::SceneRender::updateCameraFrustum(CameraObjects& co, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum){
    if (cameraComponent.type == CameraType::CAMERA_UI){
        co.lines->clearLines();
        co.type = CameraType::CAMERA_UI;
        return;
    }

    bool changed = false;
    if (co.type != cameraComponent.type) changed = true;
    if (co.isMainCamera != isMainCamera) changed = true;
    if (cameraComponent.type == CameraType::CAMERA_PERSPECTIVE){
        if (co.yfov != cameraComponent.yfov || co.aspect != cameraComponent.aspect ||
            co.nearClip != cameraComponent.nearClip || co.farClip != cameraComponent.farClip){
            changed = true;
        }
    }else if (cameraComponent.type == CameraType::CAMERA_ORTHO){
        if (co.leftClip != cameraComponent.leftClip || co.rightClip != cameraComponent.rightClip ||
            co.bottomClip != cameraComponent.bottomClip || co.topClip != cameraComponent.topClip ||
            co.nearClip != cameraComponent.nearClip || co.farClip != cameraComponent.farClip){
            changed = true;
        }
    }

    if (!changed) return;

    co.type = cameraComponent.type;
    co.isMainCamera = isMainCamera;
    co.yfov = cameraComponent.yfov;
    co.aspect = cameraComponent.aspect;
    co.nearClip = cameraComponent.nearClip;
    co.farClip = cameraComponent.farClip;
    co.leftClip = cameraComponent.leftClip;
    co.rightClip = cameraComponent.rightClip;
    co.bottomClip = cameraComponent.bottomClip;
    co.topClip = cameraComponent.topClip;

    drawCameraFrustumLines(co.lines, cameraComponent, isMainCamera, fixedSizeFrustum);
}

void editor::SceneRender::drawCameraFrustumLines(Lines* lines, const CameraComponent& cameraComponent, bool isMainCamera, bool fixedSizeFrustum){
    lines->clearLines();

    if (cameraComponent.type == CameraType::CAMERA_UI){
        return;
    }

    Vector4 color(0.8f, 0.8f, 0.8f, 1.0f);
    if (isMainCamera){
        color = Vector4(0.5f, 1.0f, 0.5f, 1.0f);
    }

    float nearClip = cameraComponent.nearClip;
    float farClip = cameraComponent.farClip;

    if (fixedSizeFrustum){
        farClip = nearClip + 2.0f;
    }

    if (nearClip > farClip) {
        std::swap(nearClip, farClip);
    }

    if (cameraComponent.type == CameraType::CAMERA_PERSPECTIVE){
        float tanHalfFov = std::tan(cameraComponent.yfov / 2.0f);
        float nearHeight = 2.0f * std::abs(nearClip) * tanHalfFov;
        float nearWidth = nearHeight * cameraComponent.aspect;
        float farHeight = 2.0f * std::abs(farClip) * tanHalfFov;
        float farWidth = farHeight * cameraComponent.aspect;

        float hNear = nearHeight / 2.0f;
        float wNear = nearWidth / 2.0f;
        float hFar = farHeight / 2.0f;
        float wFar = farWidth / 2.0f;

        Vector3 ntl(-wNear, hNear, -std::abs(nearClip));
        Vector3 ntr(wNear, hNear, -std::abs(nearClip));
        Vector3 nbl(-wNear, -hNear, -std::abs(nearClip));
        Vector3 nbr(wNear, -hNear, -std::abs(nearClip));

        Vector3 ftl(-wFar, hFar, -std::abs(farClip));
        Vector3 ftr(wFar, hFar, -std::abs(farClip));
        Vector3 fbl(-wFar, -hFar, -std::abs(farClip));
        Vector3 fbr(wFar, -hFar, -std::abs(farClip));

        lines->addLine(ntl, ntr, color);
        lines->addLine(ntr, nbr, color);
        lines->addLine(nbr, nbl, color);
        lines->addLine(nbl, ntl, color);

        lines->addLine(ftl, ftr, color);
        lines->addLine(ftr, fbr, color);
        lines->addLine(fbr, fbl, color);
        lines->addLine(fbl, ftl, color);

        lines->addLine(ntl, ftl, color);
        lines->addLine(ntr, ftr, color);
        lines->addLine(nbl, fbl, color);
        lines->addLine(nbr, fbr, color);

        lines->addLine(Vector3(0,0,0), ntl, color);
        lines->addLine(Vector3(0,0,0), ntr, color);
        lines->addLine(Vector3(0,0,0), nbl, color);
        lines->addLine(Vector3(0,0,0), nbr, color);

        // Up marker
        float markerSize = hFar * 0.5f;
        Vector3 up1(0.0f, hFar + markerSize, -std::abs(farClip));
        Vector3 up2(-markerSize / 2.0f, hFar, -std::abs(farClip));
        Vector3 up3(markerSize / 2.0f, hFar, -std::abs(farClip));
        lines->addLine(up1, up2, color);
        lines->addLine(up2, up3, color);
        lines->addLine(up3, up1, color);

    }else if (cameraComponent.type == CameraType::CAMERA_ORTHO){
        float l = cameraComponent.leftClip;
        float r = cameraComponent.rightClip;
        float b = cameraComponent.bottomClip;
        float t = cameraComponent.topClip;
        float n = -nearClip;
        float f = -farClip;

        Vector3 ntl(l, t, n);
        Vector3 ntr(r, t, n);
        Vector3 nbl(l, b, n);
        Vector3 nbr(r, b, n);

        Vector3 ftl(l, t, f);
        Vector3 ftr(r, t, f);
        Vector3 fbl(l, b, f);
        Vector3 fbr(r, b, f);

        lines->addLine(ntl, ntr, color);
        lines->addLine(ntr, nbr, color);
        lines->addLine(nbr, nbl, color);
        lines->addLine(nbl, ntl, color);

        lines->addLine(ftl, ftr, color);
        lines->addLine(ftr, fbr, color);
        lines->addLine(fbr, fbl, color);
        lines->addLine(fbl, ftl, color);

        lines->addLine(ntl, ftl, color);
        lines->addLine(ntr, ftr, color);
        lines->addLine(nbl, fbl, color);
        lines->addLine(nbr, fbr, color);

        // Up marker
        float markerSize = std::abs(t - b) * 0.05f;
        float cx = (l + r) / 2.0f;
        Vector3 up1(cx, t + markerSize, f);
        Vector3 up2(cx - markerSize / 2.0f, t, f);
        Vector3 up3(cx + markerSize / 2.0f, t, f);
        lines->addLine(up1, up2, color);
        lines->addLine(up2, up3, color);
        lines->addLine(up3, up1, color);
    }
}
