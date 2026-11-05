// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneRender.h"

#include "resources/icons/camera-icon_png.h"
#include "resources/icons/audio-source-icon_png.h"

// defined by resources/icons/bulb-icon_png.h, included in SceneRender3D.cpp
// (the xxd headers define non-static arrays, so they link in one TU only)
extern unsigned char bulb_icon_png[];
extern unsigned int bulb_icon_png_len;

#include "Backend.h"
#include "Project.h"
#include "command/CommandHandle.h"
#include "command/type/ObjectTransformCmd.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "util/GraphicUtils.h"
#include "window/TerrainEditWindow.h"

#include <cmath>
#include <cfloat>

using namespace doriax;

// how PhysicsSystem places a Box2D shape: world scale, rotation and position, without hierarchy shear
static Matrix4 body2DMatrix(const Transform& transform){
    return Matrix4::translateMatrix(transform.worldPosition) * transform.worldRotation.getRotationMatrix()
        * Matrix4::scaleMatrix(Vector3(transform.worldScale.x, transform.worldScale.y, 1.0f));
}

editor::SceneRender::SceneRender(Scene* scene, bool use2DGizmos, bool enable3DOverlays, float gizmoScale, float selectionOffset): toolslayer(use2DGizmos), uilayer(enable3DOverlays){
    ScopedDefaultEntityPool sys(*scene, EntityPool::System);

    this->mouseClicked = false;
    this->lastCommand = nullptr;
    this->useGlobalTransform = true;
    this->isPlaying = false;
    this->cameraDetached = false;

    this->gizmoScale = gizmoScale;
    this->selectionOffset = selectionOffset;

    this->scene = scene;
    this->camera = new Camera(scene);
    this->previewCameraEntity = NULL_ENTITY;

    this->multipleEntitiesSelected = false;
    // displaySettings initialised with struct defaults

    this->zoom = 1.0f;

    selLines = new Lines(scene);
    selLines->addLine(Vector3::ZERO, Vector3::ZERO, Vector4::ZERO);
    selLines->setVisible(false);

    terrainBrushLines = new Lines(scene);
    terrainBrushLines->addLine(Vector3::ZERO, Vector3::ZERO, Vector4::ZERO);
    terrainBrushLines->setVisible(false);

    scene->setCamera(camera);

    cursorSelected = CursorSelected::POINTER;
}

editor::SceneRender::~SceneRender(){
    framebuffer.destroy();

    delete terrainBrushLines;
    delete camera;
}

AABB editor::SceneRender::getMeshLocalAABB(const MeshComponent& mesh){
    return mesh.skinnedAABB.isNull() ? mesh.aabb : mesh.skinnedAABB;
}

AABB editor::SceneRender::getAABB(Entity entity, bool local){
    Signature signature = scene->getSignature(entity);
    if (signature.test(scene->getComponentId<MeshComponent>())){
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        if (local){
            return getMeshLocalAABB(mesh);
        }else{
            return mesh.worldAABB;
        }
    }else if (signature.test(scene->getComponentId<WaterComponent>())){
        WaterComponent& water = scene->getComponent<WaterComponent>(entity);
        return local ? water.aabb : water.worldAABB;
    }else if (signature.test(scene->getComponentId<WaterExclusionComponent>())){
        WaterExclusionComponent& exclusion = scene->getComponent<WaterExclusionComponent>(entity);
        return local ? exclusion.aabb : exclusion.worldAABB;
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        if (!signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<UILayoutComponent>())){
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
    }else if (signature.test(scene->getComponentId<UILayoutComponent>())){
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
    }else if ((signature.test(scene->getComponentId<CameraComponent>()) ||
               signature.test(scene->getComponentId<LightComponent>()) ||
               signature.test(scene->getComponentId<Light2DComponent>()) ||
               signature.test(scene->getComponentId<SoundComponent>())) && signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        CameraComponent& sceneCamera = scene->getComponent<CameraComponent>(camera->getEntity());
        float halfSize = billboardScreenScale(transform.worldPosition, 16.0f);
        AABB aabb = (sceneCamera.type == CameraType::CAMERA_ORTHO)
            ? AABB(-halfSize, -halfSize, -1, halfSize, halfSize, 1)
            : AABB(-halfSize, -halfSize, -halfSize, halfSize, halfSize, halfSize);

        if (local){
            return aabb;
        }else{
            return transform.modelMatrix * aabb;
        }
    }else if (signature.test(scene->getComponentId<Occluder2DComponent>()) && signature.test(scene->getComponentId<Transform>())){
        Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
        Transform& transform = scene->getComponent<Transform>(entity);

        // hug the polygon points; the entity origin is only the fallback when
        // there is nothing else to bound
        AABB aabb(Vector3(0, 0, -1), Vector3(0, 0, 1));
        if (occluder.shape == Occluder2DShape::POLYGON && occluder.points.size() > 0){
            aabb.setNull();
            for (const Vector2& point : occluder.points){
                aabb.merge(Vector3(point.x, point.y, 0.0f));
            }
            Vector3 mn = aabb.getMinimum(); mn.z = -1.0f;
            Vector3 mx = aabb.getMaximum(); mx.z = 1.0f;
            aabb.setExtents(mn, mx);
        }

        if (local){
            return aabb;
        }else{
            return transform.modelMatrix * aabb;
        }
    }else if ((signature.test(scene->getComponentId<PointsComponent>()) || signature.test(scene->getComponentId<LinesComponent>())) && signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        AABB aabb(Vector3::ZERO, Vector3::ZERO);

        if (signature.test(scene->getComponentId<PointsComponent>())){
            PointsComponent& points = scene->getComponent<PointsComponent>(entity);
            for (const PointData& pt : points.points){
                if (pt.visible){
                    aabb.merge(pt.position);
                }
            }
        }

        if (signature.test(scene->getComponentId<LinesComponent>())){
            LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
            for (const LineData& line : lines.lines){
                aabb.merge(line.pointA);
                aabb.merge(line.pointB);
            }
        }

        if (local){
            return aabb;
        }else{
            return transform.modelMatrix * aabb;
        }
    }

    return AABB();
}

AABB editor::SceneRender::getFamilyAABB(Entity entity, float offset){
    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<Transform>())) {
        AABB entityAABB = getAABB(entity, false);
        if (!entityAABB.isNull() && !entityAABB.isInfinite()) {
            Vector3 min = entityAABB.getMinimum() - Vector3(offset);
            Vector3 max = entityAABB.getMaximum() + Vector3(offset);
            entityAABB.setExtents(min, max);
        }
        return entityAABB;
    }

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

// Transforms a local AABB into a world-space box using the FULL model matrix,
// preserving shear. A node's world matrix can be sheared (e.g. a non-uniform
// parent scale combined with the child's rotation), which a regular OBB cannot
// represent: Matrix4*OBB decomposes the matrix and discards the shear, so the
// resulting box is rotated/mis-sized relative to the rendered geometry.
//
// The engine OBB must stay orthonormal for SAT collision/enclose, so this is
// kept editor-side: for the selection outline and gizmo bounds we want a box
// that hugs the actual geometry, i.e. a parallelepiped. We map the half-extent
// edge vectors through the linear part and store them as (normalized) axes plus
// their lengths, so OBB::getCorner reproduces the transformed box exactly.
OBB editor::SceneRender::transformAABBPreservingShear(const Matrix4& modelMatrix, const AABB& localAABB){
    OBB local = localAABB.getOBB();
    if (local.isNull() || local.isInfinite()){
        return modelMatrix * local;
    }

    Vector3 he = local.getHalfExtents();
    Matrix3 linear = modelMatrix.linear();

    Vector3 edgeX = linear * Vector3(he.x, 0.0f, 0.0f);
    Vector3 edgeY = linear * Vector3(0.0f, he.y, 0.0f);
    Vector3 edgeZ = linear * Vector3(0.0f, 0.0f, he.z);

    // Half-extents are the transformed edge lengths; a degenerate (zero-extent)
    // axis must keep its 0 length so a flat box stays flat.
    Vector3 halfExtents(edgeX.length(), edgeY.length(), edgeZ.length());

    // A flat box leaves that axis direction undefined; substitute a valid
    // orthogonal direction so the gizmo orientation stays well-defined. The
    // zero half-extent above means it never affects the drawn corners.
    if (halfExtents.x < 1e-6f) edgeX = edgeY.crossProduct(edgeZ);
    if (halfExtents.y < 1e-6f) edgeY = edgeZ.crossProduct(edgeX);
    if (halfExtents.z < 1e-6f) edgeZ = edgeX.crossProduct(edgeY);

    OBB result(modelMatrix * local.getCenter(), halfExtents);
    result.setAxes(edgeX, edgeY, edgeZ);
    return result;
}

OBB editor::SceneRender::getOBB(Entity entity, bool local, bool visual){
    Signature signature = scene->getSignature(entity);

    Matrix4 modelMatrix;
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        modelMatrix = transform.modelMatrix;

        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            AABB meshAABB = getMeshLocalAABB(mesh);
            if (local){
                return meshAABB.getOBB();
            }else{
                return visual ? transformAABBPreservingShear(modelMatrix, meshAABB)
                              : modelMatrix * meshAABB.getOBB();
            }
        }else if (signature.test(scene->getComponentId<UIComponent>())){
            if (!signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<UILayoutComponent>())){
                UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                if (layout.width > 0 && layout.height > 0){
                    Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                    AABB aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
                    if (local){
                        return aabb.getOBB();
                    }else{
                        return visual ? transformAABBPreservingShear(modelMatrix, aabb)
                              : modelMatrix * aabb.getOBB();
                    }
                }
            }

            UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (local){
                return ui.aabb.getOBB();
            }else{
                return visual ? transformAABBPreservingShear(modelMatrix, ui.aabb)
                              : modelMatrix * ui.aabb.getOBB();
            }
        }else if (signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                AABB aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
                if (local){
                    return aabb.getOBB();
                }else{
                    return visual ? transformAABBPreservingShear(modelMatrix, aabb)
                              : modelMatrix * aabb.getOBB();
                }
            }
        }else if (signature.test(scene->getComponentId<CameraComponent>()) ||
                   signature.test(scene->getComponentId<LightComponent>()) ||
                   signature.test(scene->getComponentId<Light2DComponent>()) ||
                   signature.test(scene->getComponentId<SoundComponent>())){
            CameraComponent& sceneCamera = scene->getComponent<CameraComponent>(camera->getEntity());
            float halfSize = billboardScreenScale(transform.worldPosition, 16.0f);
            AABB aabb = (sceneCamera.type == CameraType::CAMERA_ORTHO)
                ? AABB(-halfSize, -halfSize, -1, halfSize, halfSize, 1)
                : AABB(-halfSize, -halfSize, -halfSize, halfSize, halfSize, halfSize);
            if (local){
                return aabb.getOBB();
            }else{
                return visual ? transformAABBPreservingShear(modelMatrix, aabb)
                              : modelMatrix * aabb.getOBB();
            }
        }else if (signature.test(scene->getComponentId<Occluder2DComponent>())){
            Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);

            // hug the polygon points; the entity origin is only the fallback when
            // there is nothing else to bound
            AABB aabb(Vector3(0, 0, -1), Vector3(0, 0, 1));
            if (occluder.shape == Occluder2DShape::POLYGON && occluder.points.size() > 0){
                aabb.setNull();
                for (const Vector2& point : occluder.points){
                    aabb.merge(Vector3(point.x, point.y, 0.0f));
                }
                Vector3 mn = aabb.getMinimum(); mn.z = -1.0f;
                Vector3 mx = aabb.getMaximum(); mx.z = 1.0f;
                aabb.setExtents(mn, mx);
            }

            if (local){
                return aabb.getOBB();
            }else{
                return visual ? transformAABBPreservingShear(modelMatrix, aabb)
                          : modelMatrix * aabb.getOBB();
            }
        }else if (signature.test(scene->getComponentId<PointsComponent>()) || signature.test(scene->getComponentId<LinesComponent>())){
            AABB aabb(Vector3::ZERO, Vector3::ZERO);

            if (signature.test(scene->getComponentId<PointsComponent>())){
                PointsComponent& points = scene->getComponent<PointsComponent>(entity);
                for (const PointData& pt : points.points){
                    if (pt.visible){
                        aabb.merge(pt.position);
                    }
                }
            }

            if (signature.test(scene->getComponentId<LinesComponent>())){
                LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
                for (const LineData& line : lines.lines){
                    aabb.merge(line.pointA);
                    aabb.merge(line.pointB);
                }
            }

            if (local){
                return aabb.getOBB();
            }else{
                return visual ? transformAABBPreservingShear(modelMatrix, aabb)
                              : modelMatrix * aabb.getOBB();
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

// Builds a single shear-preserving selection box enclosing the entity and every
// entity in its subtree. We cannot merge the members with OBB::enclose: the visual
// boxes have non-orthonormal axes (preserved shear), and enclose projects onto
// those axes assuming they are orthonormal, which inflates and skews the result.
// Instead we gather every member's world-space corners in the SELECTED entity's
// local frame, build one local AABB, then transform it once with the selected
// entity's matrix. The box is oriented to the selected node and tightly bounds the
// whole subtree (and reduces to the exact single-mesh box for a leaf). A singular
// selected transform falls back to an axis-aligned world-space box.
OBB editor::SceneRender::getFamilyVisualOBB(Entity entity, float offset){
    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<Transform>())) {
        OBB entityOBB = getOBB(entity, false, /*visual*/ true);
        if (!entityOBB.isNull()){
            entityOBB.setHalfExtents(entityOBB.getHalfExtents() + Vector3(offset));
        }
        return entityOBB;
    }

    const Matrix4& selectedMatrix = scene->getComponent<Transform>(entity).modelMatrix;
    const float selectedDeterminant = selectedMatrix.linear().determinant();
    const bool useSelectedFrame = selectedMatrix.isValid()
        && std::isfinite(selectedDeterminant)
        && selectedDeterminant != 0.0f;

    const Matrix4 worldToBounds = useSelectedFrame ? selectedMatrix.inverse() : Matrix4();

    auto transforms = scene->getComponentArray<Transform>();
    size_t index = transforms->getIndex(entity);

    AABB familyBounds;
    std::vector<Entity> parentList;
    for (int i = index; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);

        // Finding childs
        if (i > index){
            if (std::find(parentList.begin(), parentList.end(), transform.parent) == parentList.end()){
                break;
            }
        }

        Entity member = transforms->getEntity(i);
        parentList.push_back(member);

        OBB memberOBB = getOBB(member, false, /*visual*/ true);
        if (memberOBB.isNull()){
            continue;
        }

        const Vector3* corners = memberOBB.getCorners();
        for (int c = 0; c < 8; c++){
            const Vector3 point = worldToBounds * corners[c];
            if (point.isValid()){
                familyBounds.merge(point);
            }
        }
    }

    if (familyBounds.isNull()){
        return OBB();
    }

    OBB result = useSelectedFrame
        ? transformAABBPreservingShear(selectedMatrix, familyBounds)
        : familyBounds.getOBB();
    result.setHalfExtents(result.getHalfExtents() + Vector3(offset));
    return result;
}

void editor::SceneRender::hideAllGizmos(){
    selLines->setVisible(false);
    terrainBrushLines->setVisible(false);
    toolslayer.setGizmoVisible(false);
    uilayer.setViewGizmoImageVisible(false);
    uilayer.setSelectionBoxVisible(false);
    uilayer.setCameraGaugeVisible(false);
}

void editor::SceneRender::setPlayMode(bool isPlaying){
    this->isPlaying = isPlaying;
    if (isPlaying){
        // Play mode owns the scene camera (the game main camera), so drop any editor
        // camera preview. This keeps the invariant "playing => no preview" and avoids
        // dropping the user back into preview view when the scene is stopped.
        previewCameraEntity = NULL_ENTITY;
        hideAllGizmos();
    }else{
        setCameraDetached(false);
        syncSceneCamera();
    }
}

void editor::SceneRender::setCameraDetached(bool detached){
    if (cameraDetached == detached || (detached && !isPlaying)){
        return;
    }

    cameraDetached = detached;
    scene->setViewCamera(detached ? camera->getEntity() : NULL_ENTITY);

    // update() skips the overlays on the game camera
    if (!detached && isPlaying){
        hideAllGizmos();
    }
}

bool editor::SceneRender::isCameraDetached() const{
    return cameraDetached;
}

// a deleted runtime scene's address can be reused, so the copy is also known by name
static const char* RUNTIME_VIEW_CAMERA_NAME = "Editor View Camera";

static bool isRuntimeViewCamera(Scene* scene, Entity entity){
    return entity != NULL_ENTITY && scene->isEntityCreated(entity) &&
        scene->getEntityName(entity) == RUNTIME_VIEW_CAMERA_NAME && scene->findComponent<CameraComponent>(entity);
}

// A scene loaded by SceneManager during Play draws from a copy of the editor camera
void editor::SceneRender::syncRuntimeViewCamera(){
    Scene* shown = Engine::getMainScene();
    if (!isPlaying || !cameraDetached || shown == scene){
        releaseRuntimeViewCamera();
        return;
    }

    // another viewport has the engine
    const SceneProject* runtime = Backend::getApp().getProject()->getRuntimeScene(shown);
    if (!runtime){
        return;
    }

    const Entity source = camera->getEntity();
    const Transform& fromTransform = scene->getComponent<Transform>(source);
    const CameraComponent& from = scene->getComponent<CameraComponent>(source);

    if (shown != runtimeViewScene){
        releaseRuntimeViewCamera();
        runtimeViewScene = shown;
    }
    if (!isRuntimeViewCamera(shown, runtimeViewCamera)){
        runtimeViewCamera = shown->createSystemEntity();
        shown->setEntityName(runtimeViewCamera, RUNTIME_VIEW_CAMERA_NAME);
        shown->addComponent<Transform>(runtimeViewCamera, {});
        shown->addComponent<CameraComponent>(runtimeViewCamera, {});
        runtimeViewMirrorY = 2.0f * fromTransform.position.y + from.topClip - from.bottomClip;
    }

    // 2D views take the y direction of the scene shown (UI is y-down) and mirror the
    // editor camera, so they start on the same area and still pan with the cursor
    CameraType type = from.type;
    if (type != CameraType::CAMERA_PERSPECTIVE){
        type = (runtime->sceneType == SceneType::SCENE_UI) ? CameraType::CAMERA_UI : CameraType::CAMERA_ORTHO;
    }
    Vector3 position = fromTransform.position;
    Vector3 target = from.target;
    if (type != from.type){
        float height = from.topClip - from.bottomClip;
        position.y = runtimeViewMirrorY - position.y - height;
        target.y = runtimeViewMirrorY - target.y - height;
    }

    Transform& toTransform = shown->getComponent<Transform>(runtimeViewCamera);
    CameraComponent& to = shown->getComponent<CameraComponent>(runtimeViewCamera);

    // only on a change, a camera update rebuilds the instance views
    if (toTransform.position != position || toTransform.rotation != fromTransform.rotation){
        toTransform.position = position;
        toTransform.rotation = fromTransform.rotation;
        toTransform.needUpdate = true;
    }
    if (to.type != type || to.target != target || to.up != from.up || to.useTarget != from.useTarget ||
            to.yfov != from.yfov || to.aspect != from.aspect || to.nearClip != from.nearClip || to.farClip != from.farClip ||
            to.leftClip != from.leftClip || to.rightClip != from.rightClip ||
            to.bottomClip != from.bottomClip || to.topClip != from.topClip || to.autoResize != from.autoResize ||
            to.depthTest != from.depthTest || to.transparentSort != from.transparentSort){
        to.type = type;
        to.target = target;
        to.up = from.up;
        to.useTarget = from.useTarget;
        to.yfov = from.yfov;
        to.aspect = from.aspect;
        to.nearClip = from.nearClip;
        to.farClip = from.farClip;
        to.leftClip = from.leftClip;
        to.rightClip = from.rightClip;
        to.bottomClip = from.bottomClip;
        to.topClip = from.topClip;
        to.autoResize = from.autoResize;
        to.depthTest = from.depthTest;
        to.transparentSort = from.transparentSort;
        to.needUpdate = true;
    }

    if (shown->getRenderCamera() != runtimeViewCamera){
        shown->setViewCamera(runtimeViewCamera);
    }
    shown->getSystem<RenderSystem>()->setDisableFixedResolution(true);
}

void editor::SceneRender::releaseRuntimeViewCamera(){
    if (!runtimeViewScene){
        return;
    }
    // a retired or stopped runtime scene is already deleted
    if (Backend::getApp().getProject()->getRuntimeScene(runtimeViewScene)){
        if (isRuntimeViewCamera(runtimeViewScene, runtimeViewCamera)){
            if (runtimeViewScene->getRenderCamera() == runtimeViewCamera){
                runtimeViewScene->setViewCamera(NULL_ENTITY);
            }
            runtimeViewScene->destroyEntity(runtimeViewCamera);
        }
        runtimeViewScene->getSystem<RenderSystem>()->setDisableFixedResolution(false);
    }
    runtimeViewScene = nullptr;
    runtimeViewCamera = NULL_ENTITY;
}

void editor::SceneRender::activate(){
    if (!isPlaying){
        syncSceneCamera();
    }

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

void editor::SceneRender::setOverlayScale(float scale){
    if (scale <= 0.0f){
        scale = 1.0f;
    }
    if (overlayScale == scale){
        return;
    }
    overlayScale = scale;
    uilayer.setOverlayScale(scale);
}

float editor::SceneRender::billboardScreenScale(const Vector3& worldPos, float logicalScale) const {
    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(camera->getEntity());
    if (cameracomp.type == CameraType::CAMERA_PERSPECTIVE){
        float dist = (worldPos - camera->getWorldPosition()).length();
        float s = std::tan(cameracomp.yfov) * dist * (overlayPx(logicalScale) / (float)framebuffer.getHeight());
        if (!std::isfinite(s) || s <= 0.0f) {
            return 1.0f;
        }
        return s;
    }
    return overlayPx(logicalScale) * zoom;
}

void editor::SceneRender::updateRenderSystem(){
    if (!isPlaying){
        syncSceneCamera();
    }

    // Meshes and UIs are created in update, without this can affect worldAABB
    scene->getSystem<MeshSystem>()->update(0);
    scene->getSystem<UISystem>()->update(0);
    // to avoid gizmos delays
    scene->getSystem<RenderSystem>()->update(0);
}

void editor::SceneRender::update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings){
    displaySettings = settings;

    // Editor-only viewport debug override: forces meshes to render without face
    // culling while editing, but reverts to the normal per-object culling during
    // play so the running game looks as it actually will. The setter only reloads
    // meshes when the value changes, so this is a no-op every frame once settled.
    scene->getSystem<RenderSystem>()->setDisableFaceCulling(displaySettings.disableFaceCulling && !isPlaying);
    scene->getSystem<RenderSystem>()->setDisableFog(displaySettings.disableFog && !isPlaying);
    scene->getSystem<RenderSystem>()->setDisableUnderwater(displaySettings.disableUnderwater && !isPlaying);

    // Fixed game resolution only applies to the game view: editor camera viewports
    // stay at native resolution (gizmos and overlays would otherwise mismatch).
    scene->getSystem<RenderSystem>()->setDisableFixedResolution(!isGameView());

    syncRuntimeViewCamera();

    if (isGameView()){
        return;
    }

    // while detached the game keeps its main camera
    if (!isPlaying){
        syncSceneCamera();
    }
    if (isPreviewCameraActive()){
        hideAllGizmos();
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

            selBB.push_back(getFamilyVisualOBB(entity, selectionOffset));

            totalSelBB.enclose(getOBB(entity, false));
        }

    }

    toolslayer.setShowAnchorGizmo(showAnchorGizmo);

    auto syncObject2DGizmoMode = [&](){
        if (toolslayer.getGizmoSelected() != GizmoSelected::OBJECT2D){
            return;
        }

        bool showRects = true;
        bool showCross = false;

        if (selEntities.size() == 1){
            Signature signature = scene->getSignature(selEntities[0]);
            bool isCamera = signature.test(scene->getComponentId<CameraComponent>());
            bool isInstancedMesh = signature.test(scene->getComponentId<InstancedMeshComponent>());
            bool canResize2D = signature.test(scene->getComponentId<UILayoutComponent>())
                || signature.test(scene->getComponentId<SpriteComponent>())
                || (signature.test(scene->getComponentId<TilemapComponent>()) && selectedTileIndex >= 0);

            showCross = signature.test(scene->getComponentId<PointsComponent>()) || signature.test(scene->getComponentId<LinesComponent>());

            // a selected occluder / polygon / track point is a size-less target: show the move cross
            if (selectedOccluderPointIndex >= 0 && selEntities[0] == selectedOccluderPointEntity){
                showCross = true;
            }
            if (selectedPolygonPointIndex >= 0 && selEntities[0] == selectedPolygonPointEntity){
                showCross = true;
            }
            if (selectedTrackPointIndex >= 0 && selEntities[0] == selectedTrackPointEntity){
                showCross = true;
            }
            if (selectedBody2DPointIndex >= 0 && selEntities[0] == selectedBody2DPointEntity){
                showCross = true;
            }

            showRects = canResize2D && !isCamera && !isInstancedMesh && !showCross;
        }

        toolslayer.setShowObject2DRects(showRects);
        toolslayer.setShowObject2DCross(showCross);
    };

    syncObject2DGizmoMode();

    bool terrainEditing = isTerrainEditing();

    totalSelBB.setHalfExtents(totalSelBB.getHalfExtents() + Vector3(selectionOffset));

    // Gizmo screen-size is distance-based in perspective, so it must be computed
    // from the gizmo's actual world position. Sub-selections (instance, tile,
    // point) place the gizmo away from the entity origin, so recompute per
    // position instead of reusing the entity-derived scale (otherwise the gizmo
    // is sized for the entity's distance and looks wrong).
    auto computeGizmoScale = [&](const Vector3& worldPos) -> float {
        return billboardScreenScale(worldPos, gizmoScale);
    };

    bool selectionVisibility = false;
    if (numTEntities > 0){
        gizmoPosition /= numTEntities;

        selectionVisibility = true;

        float scale = computeGizmoScale(gizmoPosition);

        toolslayer.updateGizmo(camera, gizmoPosition, gizmoRotation, scale, totalSelBB, mouseRay, mouseClicked, anchorData, anchorArea);

        // Override gizmo for selected tile within tilemap
        if (selectedTileIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTileEntity
            && toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
            OBB tileOBB = getTileOBB(selectedTileEntity, selectedTileIndex, true);
            if (!tileOBB.isNull()){
                Vector3 tileCenter = tileOBB.getCenter();
                toolslayer.updateGizmo(camera, tileCenter, gizmoRotation, computeGizmoScale(tileCenter), tileOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }
        }

        // Override gizmo for selected instance within instanced mesh
        if (selectedInstanceIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedInstanceEntity){
            InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(selectedInstanceEntity);
            Transform* instTransform = scene->findComponent<Transform>(selectedInstanceEntity);
            if (instmesh && instTransform && selectedInstanceIndex < (int)instmesh->instances.size()){
                OBB instOBB = getInstanceOBB(selectedInstanceEntity, selectedInstanceIndex, true);
                if (!instOBB.isNull()){
                    const InstanceData& inst = instmesh->instances[selectedInstanceIndex];
                    Quaternion instWorldRotation = getInstanceWorldRotation(*instTransform, *instmesh, inst);
                    Vector3 instCenter = instOBB.getCenter();
                    toolslayer.updateGizmo(camera, instCenter, instWorldRotation, computeGizmoScale(instCenter), instOBB, mouseRay, mouseClicked, anchorData, anchorArea);
                    // Replace selection outline with the instance OBB
                    selBB.clear();
                    selBB.push_back(instOBB);
                }
            }
        }

        // Override gizmo for selected occluder polygon point
        if (selectedOccluderPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedOccluderPointEntity
            && toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
            OBB pointOBB = getOccluderPointOBB(selectedOccluderPointEntity, selectedOccluderPointIndex);
            if (!pointOBB.isNull()){
                Vector3 pointCenter = pointOBB.getCenter();
                toolslayer.updateGizmo(camera, pointCenter, gizmoRotation, computeGizmoScale(pointCenter), pointOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }else{
                // point removed or shape changed: drop the stale sub-selection
                clearOccluderPointSelection();
            }
        }

        // Override gizmo for selected line endpoint (2D gizmo or 3D translate)
        if (selectedLinePointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedLinePointEntity
            && (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D || toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE)){
            OBB pointOBB = getLinePointOBB(selectedLinePointEntity, selectedLinePointIndex);
            if (!pointOBB.isNull()){
                Vector3 pointCenter = pointOBB.getCenter();
                toolslayer.updateGizmo(camera, pointCenter, gizmoRotation, computeGizmoScale(pointCenter), pointOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }else{
                // line removed: drop the stale sub-selection
                clearLinePointSelection();
            }
        }

        // Override gizmo for selected polygon/mesh-polygon vertex (2D gizmo or 3D translate)
        if (selectedPolygonPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedPolygonPointEntity
            && (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D || toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE)){
            OBB pointOBB = getPolygonPointOBB(selectedPolygonPointEntity, selectedPolygonPointIsMesh, selectedPolygonPointIndex);
            if (!pointOBB.isNull()){
                Vector3 pointCenter = pointOBB.getCenter();
                toolslayer.updateGizmo(camera, pointCenter, gizmoRotation, computeGizmoScale(pointCenter), pointOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }else{
                // vertex removed: drop the stale sub-selection
                clearPolygonPointSelection();
            }
        }

        // Override gizmo for selected Body2D vertex
        if (selectedBody2DPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedBody2DPointEntity
            && toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
            OBB pointOBB = getBody2DPointOBB(selectedBody2DPointEntity, selectedBody2DPointShape, selectedBody2DPointIndex);
            if (!pointOBB.isNull()){
                Vector3 pointCenter = pointOBB.getCenter();
                toolslayer.updateGizmo(camera, pointCenter, gizmoRotation, computeGizmoScale(pointCenter), pointOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            }else{
                // vertex or shape removed: drop the stale sub-selection
                clearBody2DPointSelection();
            }
        }

        if (selBB.size() > 0) {
            updateSelLines(selBB);
        }
    }

    // Gizmo for a selected TranslateTracks keyframe point. The tracks entity has
    // no Transform (numTEntities is 0 for it), so this runs as its own branch.
    if (selectedTrackPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTrackPointEntity
        && (toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE || toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D)){
        OBB pointOBB = getTrackPointOBB(selectedTrackPointEntity, selectedTrackPointIndex);
        if (!pointOBB.isNull()){
            Vector3 pointCenter = pointOBB.getCenter();

            toolslayer.updateGizmo(camera, pointCenter, gizmoRotation, computeGizmoScale(pointCenter), pointOBB, mouseRay, mouseClicked, anchorData, anchorArea);
            selectionVisibility = true;
        }else{
            // point removed or values changed: drop the stale sub-selection
            clearTrackPointSelection();
        }
    }

    toolslayer.updateCamera(cameracomp, cameratransform);

    // Determine selLines visibility: hide for single entity with OBJECT2D gizmo (none while playing) or empty selBB
    bool object2DGizmo = toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D && !isPlaying;
    bool showSelLines = selectionVisibility;
    if (!multipleEntitiesSelected && (object2DGizmo || selBB.size() == 0)) {
        if (selectedTileIndex < 0 && selectedInstanceIndex < 0) {
            showSelLines = false;
        }
    }
    selLines->setVisible(showSelLines && !displaySettings.hideSelectionOutline);

    // no editing while playing, hiding also clears the hovered side
    if (terrainEditing || isPlaying){
        toolslayer.setGizmoVisible(false);
    }else if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D && !sameRotation){
        toolslayer.setGizmoVisible(false);
    }else{
        toolslayer.setGizmoVisible(selectionVisibility);
    }

    if (!terrainEditing && terrainBrushLines){
        terrainBrushLines->setVisible(false);
    }

    syncObject2DGizmoMode();

    uilayer.setViewGizmoImageVisible(true);
}

void editor::SceneRender::mouseHoverEvent(float x, float y){
    mouseRay = camera->screenToRay(x, y);
    updateTerrainBrushCursor();
}

void editor::SceneRender::mouseClickEvent(float x, float y, std::vector<Entity> selEntities){
    mouseClicked = true;

    if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
        if (terrainEditWindow->isEditingScene(scene)){
            terrainEditWindow->beginStroke(scene, mouseRay);
            updateTerrainBrushCursor();
            return;
        }
    }

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

        // Override gizmo start position to the selected occluder point (the drag
        // follows the cursor absolutely, so no start value is needed)
        if (selectedOccluderPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedOccluderPointEntity){
            OBB pointOBB = getOccluderPointOBB(selectedOccluderPointEntity, selectedOccluderPointIndex);
            if (!pointOBB.isNull()){
                gizmoStartPosition = pointOBB.getCenter();
                cursorStartOffset = gizmoStartPosition - rretrun.point;
            }
        }

        // Override gizmo start position to the selected line endpoint
        if (selectedLinePointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedLinePointEntity){
            OBB pointOBB = getLinePointOBB(selectedLinePointEntity, selectedLinePointIndex);
            if (!pointOBB.isNull()){
                gizmoStartPosition = pointOBB.getCenter();
                cursorStartOffset = gizmoStartPosition - rretrun.point;
            }
        }

        // Override gizmo start position to the selected polygon vertex
        if (selectedPolygonPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedPolygonPointEntity){
            OBB pointOBB = getPolygonPointOBB(selectedPolygonPointEntity, selectedPolygonPointIsMesh, selectedPolygonPointIndex);
            if (!pointOBB.isNull()){
                gizmoStartPosition = pointOBB.getCenter();
                cursorStartOffset = gizmoStartPosition - rretrun.point;
            }
        }

        // Override gizmo start position to the selected Body2D vertex (only the 2D gizmo moves it)
        if (selectedBody2DPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedBody2DPointEntity
            && toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D){
            OBB pointOBB = getBody2DPointOBB(selectedBody2DPointEntity, selectedBody2DPointShape, selectedBody2DPointIndex);
            if (!pointOBB.isNull()){
                gizmoStartPosition = pointOBB.getCenter();
                cursorStartOffset = gizmoStartPosition - rretrun.point;
            }
        }

        // Override gizmo start position to the selected track keyframe point
        if (selectedTrackPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTrackPointEntity){
            OBB pointOBB = getTrackPointOBB(selectedTrackPointEntity, selectedTrackPointIndex);
            if (!pointOBB.isNull()){
                gizmoStartPosition = pointOBB.getCenter();
                cursorStartOffset = gizmoStartPosition - rretrun.point;
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

    if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
        terrainEditWindow->endStroke();
    }

    mouseClicked = false;

    toolslayer.mouseRelease();

    if (lastCommand){
        lastCommand->setNoMerge();
        lastCommand = nullptr;
    }
}

Vector3 editor::SceneRender::getMatrixScale(const Matrix4& matrix){
    return Vector3(
        Vector3(matrix[0][0], matrix[0][1], matrix[0][2]).length(),
        Vector3(matrix[1][0], matrix[1][1], matrix[1][2]).length(),
        Vector3(matrix[2][0], matrix[2][1], matrix[2][2]).length());
}

bool editor::SceneRender::isCorner2DGizmoSide(Gizmo2DSideSelected side){
    return side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY ||
           side == Gizmo2DSideSelected::PX_NY || side == Gizmo2DSideSelected::PX_PY;
}

bool editor::SceneRender::gizmo2DSideUsesNegativeX(Gizmo2DSideSelected side){
    return side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::NX_PY;
}

bool editor::SceneRender::gizmo2DSideUsesNegativeY(Gizmo2DSideSelected side){
    return side == Gizmo2DSideSelected::NX_NY || side == Gizmo2DSideSelected::PX_NY;
}

Vector2 editor::SceneRender::lockObject2DAspectRatio(const Vector2& startSize, const Vector2& candidateSize){
    if (startSize.x <= 0.0f || startSize.y <= 0.0f){
        return candidateSize;
    }

    float scaleX = candidateSize.x / startSize.x;
    float scaleY = candidateSize.y / startSize.y;
    float chosenScale = (std::fabs(scaleX - 1.0f) >= std::fabs(scaleY - 1.0f)) ? scaleX : scaleY;

    if (!std::isfinite(chosenScale)){
        return candidateSize;
    }

    if (chosenScale < 0.0f){
        chosenScale = 0.0f;
    }

    return Vector2(startSize.x * chosenScale, startSize.y * chosenScale);
}

void editor::SceneRender::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio){
    mouseRay = camera->screenToRay(x, y);

    if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
        if (terrainEditWindow->isEditingScene(scene)){
            terrainEditWindow->paintStroke(scene, mouseRay);
            updateTerrainBrushCursor();
            return;
        }
    }

    if (!disableSelection && !isGameView()){
        uilayer.setSelectionBoxVisible(true);
        uilayer.updateRect(Vector2(origX, origY), Vector2(x, y) - Vector2(origX, origY));
    }

    Vector3 gizmoPosition = toolslayer.getGizmoPosition();
    Matrix4 gizmoRMatrix = toolslayer.getGizmoRotation().getRotationMatrix();

    // TranslateTracks keyframe point drag: the tracks entity has no Transform,
    // so it is handled outside the per-entity transform loop below.
    if (selectedTrackPointIndex >= 0 && selEntities.size() == 1 && selEntities[0] == selectedTrackPointEntity){
        Entity trackEntity = selectedTrackPointEntity;
        TranslateTracksComponent* tracks = scene->findComponent<TranslateTracksComponent>(trackEntity);
        RayReturn rretrun = mouseRay.intersects(cursorPlane);
        SceneProject* sceneProject = project->getScene(sceneId);

        if (tracks && rretrun && selectedTrackPointIndex < (int)tracks->values.size()){
            std::string pointProp = "values[" + std::to_string(selectedTrackPointIndex) + "]";

            if (toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE
                && toolslayer.getGizmoSideSelected() != GizmoSideSelected::NONE){
                toolslayer.mouseDrag(rretrun.point);

                Vector3 deltaPos = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);

                if (displaySettings.snapToGrid){
                    float spacing = displaySettings.gridSpacing3D;
                    if (spacing > 0.0f){
                        deltaPos.x = std::round(deltaPos.x / spacing) * spacing;
                        deltaPos.y = std::round(deltaPos.y / spacing) * spacing;
                        deltaPos.z = std::round(deltaPos.z / spacing) * spacing;
                    }
                }

                Vector3 newPos = gizmoStartPosition;
                if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XYZ){
                    newPos = gizmoStartPosition + (gizmoRMatrix * deltaPos);
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::X){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, 0, 0));
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::Y){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, deltaPos.y, 0));
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::Z){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, 0, deltaPos.z));
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XY){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, deltaPos.y, 0));
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::XZ){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(deltaPos.x, 0, deltaPos.z));
                }else if (toolslayer.getGizmoSideSelected() == GizmoSideSelected::YZ){
                    newPos = gizmoStartPosition + (gizmoRMatrix * Vector3(0, deltaPos.y, deltaPos.z));
                }

                Vector3 newLocalPos = getTrackPointsWorldMatrix(trackEntity).inverse() * newPos;
                lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, trackEntity,
                    ComponentType::TranslateTracksComponent, pointProp, newLocalPos);
                CommandHandle::get(sceneId)->addCommand(lastCommand);

            }else if (toolslayer.getGizmoSelected() == GizmoSelected::OBJECT2D
                && toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){
                toolslayer.mouseDrag(rretrun.point);

                // new world position of the point, following the cursor
                Vector3 newWorldPos = rretrun.point + cursorStartOffset;

                if (displaySettings.snapToGrid){
                    float spacing = displaySettings.gridSpacing2D;
                    if (spacing > 0.0f){
                        newWorldPos.x = std::round(newWorldPos.x / spacing) * spacing;
                        newWorldPos.y = std::round(newWorldPos.y / spacing) * spacing;
                    }
                }

                // the cursor plane is at the gizmo's Z: keep the point's own local Z
                Vector3 oldLocal = tracks->values[selectedTrackPointIndex];
                Vector3 newLocalPos = getTrackPointsWorldMatrix(trackEntity).inverse() * newWorldPos;
                newLocalPos.z = oldLocal.z;

                lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, trackEntity,
                    ComponentType::TranslateTracksComponent, pointProp, newLocalPos);
                CommandHandle::get(sceneId)->addCommand(lastCommand);
            }
        }
    }

    for (Entity& entity: selEntities){
        if (selectedTrackPointIndex >= 0 && entity == selectedTrackPointEntity){
            // the drag targets the track keyframe point (handled above), not the entity
            continue;
        }
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

                if (toolslayer.getGizmoSelected() == GizmoSelected::TRANSLATE && toolslayer.getGizmoSideSelected() != GizmoSideSelected::NONE){
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

                    if (selectedLinePointIndex >= 0 && entity == selectedLinePointEntity){
                        // move the selected line endpoint instead of the entity (the
                        // gizmo sits on the point, so newPos is its new world position)
                        LinesComponent* linesComp = scene->findComponent<LinesComponent>(entity);
                        Transform* lineTransform = scene->findComponent<Transform>(entity);
                        int lineIndex = selectedLinePointIndex / 2;
                        if (linesComp && lineTransform && lineIndex < (int)linesComp->lines.size()){
                            bool isPointA = (selectedLinePointIndex % 2 == 0);
                            Vector3 newLocalPos = lineTransform->modelMatrix.inverse() * newPos;

                            std::string pointProp = "lines[" + std::to_string(lineIndex) + (isPointA ? "].pointA" : "].pointB");
                            lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, entity,
                                ComponentType::LinesComponent, pointProp, newLocalPos);
                        }
                    }else if (selectedPolygonPointIndex >= 0 && entity == selectedPolygonPointEntity){
                        // move the selected polygon vertex instead of the entity
                        Transform* polyTransform = scene->findComponent<Transform>(entity);
                        if (polyTransform){
                            Vector3 newLocalPos = polyTransform->modelMatrix.inverse() * newPos;
                            std::string pointProp = "points[" + std::to_string(selectedPolygonPointIndex) + "].position";
                            lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, entity,
                                selectedPolygonPointIsMesh ? ComponentType::MeshPolygonComponent : ComponentType::PolygonComponent,
                                pointProp, newLocalPos);
                        }
                    }else{
                        Matrix4 gizmoMatrix = Matrix4::translateMatrix(newPos) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));
                        Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                        applyObjectTransform(objMatrix);
                    }
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

                    float angle = snapRotationAngle((sign < 0) ? -orig_angle : orig_angle, invertRotationSnap);

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
                    // Handle occluder polygon point sub-selection drag
                    if (selectedOccluderPointIndex >= 0 && entity == selectedOccluderPointEntity){
                        Occluder2DComponent* occluder = scene->findComponent<Occluder2DComponent>(entity);
                        Transform* occTransform = scene->findComponent<Transform>(entity);
                        if (occluder && occTransform && selectedOccluderPointIndex < (int)occluder->points.size()
                            && toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){

                            // new world position of the point, following the cursor
                            Vector3 newWorldPos = rretrun.point + cursorStartOffset;

                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    newWorldPos.x = std::round(newWorldPos.x / spacing) * spacing;
                                    newWorldPos.y = std::round(newWorldPos.y / spacing) * spacing;
                                }
                            }

                            Vector3 newLocalPos = occTransform->modelMatrix.inverse() * newWorldPos;

                            std::vector<Vector2> newPoints = occluder->points;
                            newPoints[selectedOccluderPointIndex] = Vector2(newLocalPos.x, newLocalPos.y);

                            lastCommand = new PropertyCmd<std::vector<Vector2>>(project, sceneProject->id, entity,
                                ComponentType::Occluder2DComponent, "points", newPoints);
                        }
                    }else
                    // Handle line endpoint sub-selection drag
                    if (selectedLinePointIndex >= 0 && entity == selectedLinePointEntity){
                        LinesComponent* linesComp = scene->findComponent<LinesComponent>(entity);
                        Transform* lineTransform = scene->findComponent<Transform>(entity);
                        int lineIndex = selectedLinePointIndex / 2;
                        if (linesComp && lineTransform && lineIndex < (int)linesComp->lines.size()
                            && toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){

                            // new world position of the endpoint, following the cursor
                            Vector3 newWorldPos = rretrun.point + cursorStartOffset;

                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    newWorldPos.x = std::round(newWorldPos.x / spacing) * spacing;
                                    newWorldPos.y = std::round(newWorldPos.y / spacing) * spacing;
                                }
                            }

                            bool isPointA = (selectedLinePointIndex % 2 == 0);
                            const Vector3& localPoint = isPointA ? linesComp->lines[lineIndex].pointA : linesComp->lines[lineIndex].pointB;

                            // the cursor plane is at the entity's Z: keep the endpoint's own local Z
                            Vector3 newLocalPos = lineTransform->modelMatrix.inverse() * newWorldPos;
                            newLocalPos.z = localPoint.z;

                            std::string pointProp = "lines[" + std::to_string(lineIndex) + (isPointA ? "].pointA" : "].pointB");
                            lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, entity,
                                ComponentType::LinesComponent, pointProp, newLocalPos);
                        }
                    }else
                    // Handle polygon/mesh-polygon vertex sub-selection drag
                    if (selectedPolygonPointIndex >= 0 && entity == selectedPolygonPointEntity){
                        Transform* polyTransform = scene->findComponent<Transform>(entity);
                        Vector3 worldOld;
                        bool hasPoint = getPolygonPointWorld(entity, selectedPolygonPointIsMesh, selectedPolygonPointIndex, worldOld);
                        if (polyTransform && hasPoint && toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){
                            // new world position of the vertex, following the cursor
                            Vector3 newWorldPos = rretrun.point + cursorStartOffset;

                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    newWorldPos.x = std::round(newWorldPos.x / spacing) * spacing;
                                    newWorldPos.y = std::round(newWorldPos.y / spacing) * spacing;
                                }
                            }

                            // the cursor plane is at the entity's Z: keep the vertex's own local Z
                            Matrix4 invModel = polyTransform->modelMatrix.inverse();
                            Vector3 oldLocal = invModel * worldOld;
                            Vector3 newLocalPos = invModel * newWorldPos;
                            newLocalPos.z = oldLocal.z;

                            std::string pointProp = "points[" + std::to_string(selectedPolygonPointIndex) + "].position";
                            lastCommand = new PropertyCmd<Vector3>(project, sceneProject->id, entity,
                                selectedPolygonPointIsMesh ? ComponentType::MeshPolygonComponent : ComponentType::PolygonComponent,
                                pointProp, newLocalPos);
                        }
                    }else
                    // Handle Body2D vertex sub-selection drag
                    if (selectedBody2DPointIndex >= 0 && entity == selectedBody2DPointEntity){
                        Transform* bodyTransform = scene->findComponent<Transform>(entity);
                        Vector3 worldOld;
                        bool hasPoint = getBody2DPointWorld(entity, selectedBody2DPointShape, selectedBody2DPointIndex, worldOld);
                        if (bodyTransform && hasPoint && toolslayer.getGizmo2DSideSelected() == Gizmo2DSideSelected::CENTER){
                            // new world position of the vertex, following the cursor
                            Vector3 newWorldPos = rretrun.point + cursorStartOffset;

                            if (displaySettings.snapToGrid) {
                                float spacing = displaySettings.gridSpacing2D;
                                if (spacing > 0.0f) {
                                    newWorldPos.x = std::round(newWorldPos.x / spacing) * spacing;
                                    newWorldPos.y = std::round(newWorldPos.y / spacing) * spacing;
                                }
                            }

                            Vector3 newLocalPos = body2DMatrix(*bodyTransform).inverse() * newWorldPos;

                            std::string pointProp = "shapes[" + std::to_string(selectedBody2DPointShape) + "].vertices[" + std::to_string(selectedBody2DPointIndex) + "]";
                            lastCommand = new PropertyCmd<Vector2>(project, sceneProject->id, entity,
                                ComponentType::Body2DComponent, pointProp, Vector2(newLocalPos.x, newLocalPos.y));
                        }
                    }else
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

                            if (preserveAspectRatio && isCorner2DGizmoSide(side)) {
                                Vector2 lockedSize = lockObject2DAspectRatio(Vector2(tileStartWidth, tileStartHeight), Vector2(newTileW, newTileH));
                                newTileW = lockedSize.x;
                                newTileH = lockedSize.y;
                                newTilePos.x = gizmo2DSideUsesNegativeX(side) ? (tileMaxX - newTileW) : tileStartPosition.x;
                                newTilePos.y = gizmo2DSideUsesNegativeY(side) ? (tileMaxY - newTileH) : tileStartPosition.y;
                            }

                            if (displaySettings.snapTile) {
                                // Snap in local tilemap space to the tile's own size so
                                // neighboring tiles pack edge-to-edge when grouping.
                                float spacingX = tileStartWidth;
                                float spacingY = tileStartHeight;
                                if (spacingX > 0.0f && spacingY > 0.0f) {
                                    auto snapX = [&](float lx) {
                                        return std::round(lx / spacingX) * spacingX;
                                    };
                                    auto snapY = [&](float ly) {
                                        return std::round(ly / spacingY) * spacingY;
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
                            } else if (displaySettings.snapToGrid) {
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

                    Gizmo2DSideSelected side = toolslayer.getGizmo2DSideSelected();
                    Vector3 deltaPos = gizmoRMatrix.inverse() * ((rretrun.point + cursorStartOffset) - gizmoStartPosition);
                    Vector3 deltaSize = gizmoRMatrix.inverse() * -(gizmoStartPosition - rretrun.point - cursorStartOffset);

                    Vector3 oScale = getMatrixScale(transform->modelMatrix);
                    if (oScale.x == 0.0f) oScale.x = 1.0f;
                    if (oScale.y == 0.0f) oScale.y = 1.0f;
                    Vector3 posOffset = Vector3::ZERO;
                    Vector2 sizeDeltaLocal = Vector2::ZERO;

                    if (side == Gizmo2DSideSelected::CENTER){
                        posOffset = deltaPos;
                    }else if (side == Gizmo2DSideSelected::NX){
                        posOffset.x = deltaPos.x;
                        sizeDeltaLocal.x = -deltaSize.x / oScale.x;
                    }else if (side == Gizmo2DSideSelected::NY){
                        posOffset.y = deltaPos.y;
                        sizeDeltaLocal.y = -deltaSize.y / oScale.y;
                    }else if (side == Gizmo2DSideSelected::PX){
                        sizeDeltaLocal.x = deltaSize.x / oScale.x;
                    }else if (side == Gizmo2DSideSelected::PY){
                        sizeDeltaLocal.y = deltaSize.y / oScale.y;
                    }else if (side == Gizmo2DSideSelected::NX_NY){
                        posOffset.x = deltaPos.x;
                        posOffset.y = deltaPos.y;
                        sizeDeltaLocal.x = -deltaSize.x / oScale.x;
                        sizeDeltaLocal.y = -deltaSize.y / oScale.y;
                    }else if (side == Gizmo2DSideSelected::NX_PY){
                        posOffset.x = deltaPos.x;
                        sizeDeltaLocal.x = -deltaSize.x / oScale.x;
                        sizeDeltaLocal.y = deltaSize.y / oScale.y;
                    }else if (side == Gizmo2DSideSelected::PX_NY){
                        posOffset.y = deltaPos.y;
                        sizeDeltaLocal.x = deltaSize.x / oScale.x;
                        sizeDeltaLocal.y = -deltaSize.y / oScale.y;
                    }else if (side == Gizmo2DSideSelected::PX_PY){
                        sizeDeltaLocal.x = deltaSize.x / oScale.x;
                        sizeDeltaLocal.y = deltaSize.y / oScale.y;
                    }

                    if (preserveAspectRatio && isCorner2DGizmoSide(side)){
                        Vector2 freeformSizeDeltaLocal = sizeDeltaLocal;
                        Vector2 lockedSize = lockObject2DAspectRatio(objectSizeOffset[entity], objectSizeOffset[entity] + sizeDeltaLocal);
                        sizeDeltaLocal = lockedSize - objectSizeOffset[entity];
                        if (gizmo2DSideUsesNegativeX(side)){
                            posOffset.x = (freeformSizeDeltaLocal.x != 0.0f) ? (posOffset.x * (sizeDeltaLocal.x / freeformSizeDeltaLocal.x)) : 0.0f;
                        }
                        if (gizmo2DSideUsesNegativeY(side)){
                            posOffset.y = (freeformSizeDeltaLocal.y != 0.0f) ? (posOffset.y * (sizeDeltaLocal.y / freeformSizeDeltaLocal.y)) : 0.0f;
                        }
                    }

                    Vector3 newPos = gizmoStartPosition + (gizmoRMatrix * posOffset);

                    if (side == Gizmo2DSideSelected::CENTER){
                        // Snap absolute world position to grid (must be done after newPos becomes world-space)
                        if (displaySettings.snapToGrid) {
                            float spacing = displaySettings.gridSpacing2D;
                            if (spacing > 0.0f) {
                                newPos.x = std::round(newPos.x / spacing) * spacing;
                                newPos.y = std::round(newPos.y / spacing) * spacing;
                            }
                        }
                    }

                    Matrix4 gizmoMatrix = Matrix4::translateMatrix(newPos) * gizmoRMatrix * Matrix4::scaleMatrix(Vector3(1,1,1));
                    Matrix4 objMatrix = gizmoMatrix * objectMatrixOffset[entity];

                    if (transformParent){
                        objMatrix = transformParent->modelMatrix.inverse() * objMatrix;
                    }

                    Vector2 size = objectSizeOffset[entity] + sizeDeltaLocal;
                    Vector3 pos = Vector3(objMatrix[3][0], objMatrix[3][1], objMatrix[3][2]);

                    if (size.x < 0) size.x = 0;
                    if (size.y < 0) size.y = 0;

                    if (isLayout){
                        // the matrix origin of a pivoted UI element is not its position
                        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                        pos -= getUIPivotShift(layout.pivot, size, transform->rotation, transform->scale);
                    }

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

                    if (side != Gizmo2DSideSelected::NONE){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        if (isLayout){
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::UILayoutComponent, "width", static_cast<unsigned int>(size.x));
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, ComponentType::UILayoutComponent, "height", static_cast<unsigned int>(size.y));
                            if (isText){
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

bool editor::SceneRender::isTerrainEditing() const{
    if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
        return terrainEditWindow->isEditingScene(scene);
    }
    return false;
}

bool editor::SceneRender::hasHoverFeedback() const{
    return toolslayer.isGizmoVisible() || isTerrainEditing();
}

float editor::SceneRender::snapRotationAngle(float angle, bool invertRotationSnap) const{
    bool snapRotation = displaySettings.snapRotation;
    if (invertRotationSnap){
        snapRotation = !snapRotation;
    }

    if (!snapRotation || displaySettings.rotationSnapDegrees <= 0.0f){
        return angle;
    }

    float snapStep = Angle::defaultToRad(displaySettings.rotationSnapDegrees);
    return std::round(angle / snapStep) * snapStep;
}

void editor::SceneRender::updateTerrainBrushCursor(){
    if (!terrainBrushLines){
        return;
    }

    TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow();
    TerrainBrushCursor cursor;
    if (!terrainEditWindow || !terrainEditWindow->updateCursor(scene, mouseRay, cursor) || !cursor.visible || cursor.outerPoints.size() < 2){
        terrainBrushLines->setVisible(false);
        return;
    }

    terrainBrushLines->clearLines();
    auto addLoop = [&](const std::vector<Vector3>& points, const Vector4& color){
        if (points.size() < 2){
            return;
        }
        for (size_t i = 0; i < points.size(); i++){
            terrainBrushLines->addLine(points[i], points[(i + 1) % points.size()], color);
        }
    };

    // Brush boundary plus the (dimmer) half-strength falloff contour, both draped
    // over the terrain surface by updateCursor.
    addLoop(cursor.outerPoints, Vector4(1.0f, 0.78f, 0.22f, 1.0f));
    addLoop(cursor.innerPoints, Vector4(0.75f, 0.58f, 0.16f, 1.0f));

    terrainBrushLines->setVisible(true);
}

TextureRender& editor::SceneRender::getTexture(){
    //return camera->getFramebuffer()->getRender().getColorTexture();
    return framebuffer.getRender().getColorTexture();
}

Camera* editor::SceneRender::getCamera(){
    return camera;
}

bool editor::SceneRender::isPreviewCameraUsable(Entity entity){
    return entity != NULL_ENTITY
        && scene
        && scene->isEntityCreated(entity)
        && scene->findComponent<CameraComponent>(entity)
        && scene->findComponent<Transform>(entity);
}

Entity editor::SceneRender::getActiveCameraEntity(){
    // isPreviewCameraActive() self-heals a stale preview entity, so this falls back
    // to the editor camera whenever the preview is no longer usable.
    if (isPreviewCameraActive()){
        return previewCameraEntity;
    }

    return camera->getEntity();
}

void editor::SceneRender::syncSceneCamera(){
    Entity activeCamera = getActiveCameraEntity();
    if (scene && activeCamera != NULL_ENTITY && scene->getCamera() != activeCamera){
        scene->setCamera(activeCamera);
    }
}

bool editor::SceneRender::setPreviewCamera(Entity entity){
    if (!isPreviewCameraUsable(entity)){
        return false;
    }

    previewCameraEntity = entity;
    if (!isPlaying){
        syncSceneCamera();
    }
    // Gizmos are hidden by update() every frame while the preview is active, which
    // runs before the scene is drawn, so there's no need to hide them here too.
    return true;
}

void editor::SceneRender::clearPreviewCamera(){
    previewCameraEntity = NULL_ENTITY;
    if (!isPlaying){
        syncSceneCamera();
    }
}

Entity editor::SceneRender::getPreviewCameraEntity() const{
    return previewCameraEntity;
}

bool editor::SceneRender::isPreviewCameraActive(){
    // Self-healing query: if the previewed camera entity was destroyed or lost its
    // required components, clear it here so callers (and the UI) fall back to the
    // editor camera on the same frame.
    if (isPreviewCameraUsable(previewCameraEntity)){
        return true;
    }

    previewCameraEntity = NULL_ENTITY;
    return false;
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

void editor::SceneRender::selectOccluderPoint(Entity entity, int pointIndex){
    selectedOccluderPointEntity = entity;
    selectedOccluderPointIndex = pointIndex;
}

void editor::SceneRender::clearOccluderPointSelection(){
    selectedOccluderPointEntity = 0;
    selectedOccluderPointIndex = -1;
}

// Returns the polygon point under the mouse (-1 when none). Only POLYGON
// occluders have editable points.
int editor::SceneRender::hitTestOccluderPoint(Entity entity, float x, float y){
    if (!scene->getComponentArray<Occluder2DComponent>()->hasEntity(entity)) return -1;
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return -1;

    Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    if (occluder.shape != Occluder2DShape::POLYGON) return -1;

    Ray ray = camera->screenToRay(x, y);
    RayReturn rr = ray.intersects(Plane(Vector3(0, 0, 1), transform.worldPosition));
    if (!rr) return -1;

    // handles are drawn at overlayPx(4)*zoom half-extent; use a slightly larger grab radius
    float hitRadius = overlayPx(8.0f) * zoom;
    int bestIndex = -1;
    float bestDistance = hitRadius;
    for (size_t i = 0; i < occluder.points.size(); i++){
        Vector3 worldPoint = transform.modelMatrix * Vector3(occluder.points[i].x, occluder.points[i].y, 0.0f);
        float dist = Vector2(worldPoint.x - rr.point.x, worldPoint.y - rr.point.y).length();
        if (dist <= bestDistance){
            bestDistance = dist;
            bestIndex = (int)i;
        }
    }

    return bestIndex;
}

OBB editor::SceneRender::getOccluderPointOBB(Entity entity, int pointIndex){
    if (!scene->getComponentArray<Occluder2DComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return OBB();

    Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    if (occluder.shape != Occluder2DShape::POLYGON) return OBB();
    if (pointIndex < 0 || pointIndex >= (int)occluder.points.size()) return OBB();

    Vector3 worldPoint = transform.modelMatrix * Vector3(occluder.points[pointIndex].x, occluder.points[pointIndex].y, 0.0f);
    float halfSize = overlayPx(8.0f) * zoom;
    AABB aabb(worldPoint - Vector3(halfSize, halfSize, 1.0f), worldPoint + Vector3(halfSize, halfSize, 1.0f));
    return aabb.getOBB();
}

void editor::SceneRender::selectLinePoint(Entity entity, int pointIndex){
    selectedLinePointEntity = entity;
    selectedLinePointIndex = pointIndex;
}

void editor::SceneRender::clearLinePointSelection(){
    selectedLinePointEntity = 0;
    selectedLinePointIndex = -1;
}

float editor::SceneRender::getPointHandleHalfSize(const Vector3& worldPoint){
    return billboardScreenScale(worldPoint, 8.0f);
}

// Returns the line endpoint under the mouse (-1 when none), flattened as
// lineIndex * 2 + endpoint (0 = pointA, 1 = pointB).
int editor::SceneRender::hitTestLinePoint(Entity entity, float x, float y){
    if (!scene->getComponentArray<LinesComponent>()->hasEntity(entity)) return -1;
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return -1;

    LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    Ray ray = camera->screenToRay(x, y);

    // ray test against each endpoint's handle box (works in both ortho and
    // perspective views), keeping the nearest hit like instance picking
    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (size_t i = 0; i < lines.lines.size(); i++){
        for (int e = 0; e < 2; e++){
            const Vector3& localPoint = (e == 0) ? lines.lines[i].pointA : lines.lines[i].pointB;
            Vector3 worldPoint = transform.modelMatrix * localPoint;
            float halfSize = getPointHandleHalfSize(worldPoint);
            AABB handleAABB(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));

            RayReturn rr = ray.intersects(handleAABB);
            if (rr && rr.distance < bestDistance){
                bestDistance = rr.distance;
                bestIndex = (int)i * 2 + e;
            }
        }
    }

    return bestIndex;
}

OBB editor::SceneRender::getLinePointOBB(Entity entity, int pointIndex){
    if (!scene->getComponentArray<LinesComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return OBB();

    LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    int lineIndex = pointIndex / 2;
    if (pointIndex < 0 || lineIndex >= (int)lines.lines.size()) return OBB();

    const Vector3& localPoint = (pointIndex % 2 == 0) ? lines.lines[lineIndex].pointA : lines.lines[lineIndex].pointB;
    Vector3 worldPoint = transform.modelMatrix * localPoint;
    float halfSize = getPointHandleHalfSize(worldPoint);
    AABB aabb(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));
    return aabb.getOBB();
}

void editor::SceneRender::selectPolygonPoint(Entity entity, bool isMesh, int pointIndex){
    selectedPolygonPointEntity = entity;
    selectedPolygonPointIsMesh = isMesh;
    selectedPolygonPointIndex = pointIndex;
}

void editor::SceneRender::clearPolygonPointSelection(){
    selectedPolygonPointEntity = 0;
    selectedPolygonPointIndex = -1;
}

// Polygon and MeshPolygon both store std::vector<PolygonPoint> (Vector3 position);
// isMesh chooses which component so one code path serves both.
bool editor::SceneRender::getPolygonPointWorld(Entity entity, bool isMesh, int pointIndex, Vector3& worldPoint){
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return false;
    Transform& transform = scene->getComponent<Transform>(entity);

    if (isMesh){
        if (!scene->getComponentArray<MeshPolygonComponent>()->hasEntity(entity)) return false;
        MeshPolygonComponent& polygon = scene->getComponent<MeshPolygonComponent>(entity);
        if (pointIndex < 0 || pointIndex >= (int)polygon.points.size()) return false;
        worldPoint = transform.modelMatrix * polygon.points[pointIndex].position;
    }else{
        if (!scene->getComponentArray<PolygonComponent>()->hasEntity(entity)) return false;
        PolygonComponent& polygon = scene->getComponent<PolygonComponent>(entity);
        if (pointIndex < 0 || pointIndex >= (int)polygon.points.size()) return false;
        worldPoint = transform.modelMatrix * polygon.points[pointIndex].position;
    }

    return true;
}

// Returns the polygon vertex under the mouse (-1 when none). Ray-vs-handle-box test
// so it works in both ortho and perspective views (like line endpoints).
int editor::SceneRender::hitTestPolygonPoint(Entity entity, bool isMesh, float x, float y){
    size_t count = 0;
    if (isMesh){
        if (!scene->getComponentArray<MeshPolygonComponent>()->hasEntity(entity)) return -1;
        count = scene->getComponent<MeshPolygonComponent>(entity).points.size();
    }else{
        if (!scene->getComponentArray<PolygonComponent>()->hasEntity(entity)) return -1;
        count = scene->getComponent<PolygonComponent>(entity).points.size();
    }

    Ray ray = camera->screenToRay(x, y);

    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (size_t i = 0; i < count; i++){
        Vector3 worldPoint;
        if (!getPolygonPointWorld(entity, isMesh, (int)i, worldPoint)) continue;
        float halfSize = getPointHandleHalfSize(worldPoint);
        AABB handleAABB(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));

        RayReturn rr = ray.intersects(handleAABB);
        if (rr && rr.distance < bestDistance){
            bestDistance = rr.distance;
            bestIndex = (int)i;
        }
    }

    return bestIndex;
}

OBB editor::SceneRender::getPolygonPointOBB(Entity entity, bool isMesh, int pointIndex){
    Vector3 worldPoint;
    if (!getPolygonPointWorld(entity, isMesh, pointIndex, worldPoint)) return OBB();

    float halfSize = getPointHandleHalfSize(worldPoint);
    AABB aabb(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));
    return aabb.getOBB();
}

void editor::SceneRender::selectTrackPoint(Entity entity, int pointIndex){
    selectedTrackPointEntity = entity;
    selectedTrackPointIndex = pointIndex;
}

void editor::SceneRender::clearTrackPointSelection(){
    selectedTrackPointEntity = 0;
    selectedTrackPointIndex = -1;
}

Matrix4 editor::SceneRender::getTrackPointsWorldMatrix(Entity entity){
    // values[] drive the action target's Transform::position, which is local to
    // the target's parent: that parent's model matrix maps them to world space
    ActionComponent* action = scene->findComponent<ActionComponent>(entity);
    if (!action || action->target == NULL_ENTITY || !scene->isEntityCreated(action->target)){
        return Matrix4();
    }
    Transform* targetTransform = scene->findComponent<Transform>(action->target);
    if (!targetTransform){
        return Matrix4();
    }

    Matrix4 matrix;
    if (targetTransform->parent != NULL_ENTITY){
        if (Transform* parentTransform = scene->findComponent<Transform>(targetTransform->parent)){
            matrix = parentTransform->modelMatrix;
        }
    }

    // relative values are offsets from where the track started, or from the target before it starts
    KeyframeTracksComponent* keyframe = scene->findComponent<KeyframeTracksComponent>(entity);
    if (keyframe && keyframe->relative){
        ActionSystem* actions = scene->getSystem<ActionSystem>().get();
        Vector3 base = keyframe->basePosition;
        if (!keyframe->hasBase){
            Vector3 scale;
            Quaternion rotation;
            actions->getRelativeTrackPose(action->target, base, rotation, scale);
        }
        matrix = matrix * actions->getRelativeTrackMatrix(action->target, base);
    }

    return matrix;
}

bool editor::SceneRender::getTrackPointWorld(Entity entity, int pointIndex, Vector3& worldPoint){
    TranslateTracksComponent* tracks = scene->findComponent<TranslateTracksComponent>(entity);
    if (!tracks) return false;
    if (pointIndex < 0 || pointIndex >= (int)tracks->values.size()) return false;

    worldPoint = getTrackPointsWorldMatrix(entity) * tracks->values[pointIndex];
    return true;
}

int editor::SceneRender::hitTestTrackPoint(Entity entity, float x, float y){
    TranslateTracksComponent* tracks = scene->findComponent<TranslateTracksComponent>(entity);
    if (!tracks) return -1;

    Ray ray = camera->screenToRay(x, y);
    Matrix4 worldMatrix = getTrackPointsWorldMatrix(entity);

    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (size_t i = 0; i < tracks->values.size(); i++){
        Vector3 worldPoint = worldMatrix * tracks->values[i];
        float halfSize = getPointHandleHalfSize(worldPoint);
        AABB handleAABB(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));

        RayReturn rr = ray.intersects(handleAABB);
        if (rr && rr.distance < bestDistance){
            bestDistance = rr.distance;
            bestIndex = (int)i;
        }
    }

    return bestIndex;
}

OBB editor::SceneRender::getTrackPointOBB(Entity entity, int pointIndex){
    Vector3 worldPoint;
    if (!getTrackPointWorld(entity, pointIndex, worldPoint)) return OBB();

    float halfSize = getPointHandleHalfSize(worldPoint);
    AABB aabb(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));
    return aabb.getOBB();
}

void editor::SceneRender::selectBody2DPoint(Entity entity, int shapeIndex, int pointIndex){
    selectedBody2DPointEntity = entity;
    selectedBody2DPointShape = shapeIndex;
    selectedBody2DPointIndex = pointIndex;
}

void editor::SceneRender::clearBody2DPointSelection(){
    selectedBody2DPointEntity = 0;
    selectedBody2DPointShape = -1;
    selectedBody2DPointIndex = -1;
}

bool editor::SceneRender::getBody2DPointWorld(Entity entity, int shapeIndex, int pointIndex, Vector3& worldPoint){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);
    Transform* transform = scene->findComponent<Transform>(entity);
    if (!body || !transform) return false;
    if (shapeIndex < 0 || shapeIndex >= (int)body->numShapes) return false;

    const Shape2D& shape = body->shapes[shapeIndex];
    bool hasVertices = shape.type == Shape2DType::CHAIN
        || (shape.type == Shape2DType::POLYGON && shape.source == Shape2DSource::NONE && shape.numVertices >= 3);
    if (!hasVertices || pointIndex < 0 || pointIndex >= (int)shape.numVertices) return false;

    worldPoint = body2DMatrix(*transform) * Vector3(shape.vertices[pointIndex].x, shape.vertices[pointIndex].y, 0.0f);
    return true;
}

// Returns the vertex under the mouse (-1 when none) and its shape in shapeIndex
int editor::SceneRender::hitTestBody2DPoint(Entity entity, float x, float y, int& shapeIndex){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);
    if (!body) return -1;

    Ray ray = camera->screenToRay(x, y);

    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (int s = 0; s < (int)body->numShapes; s++){
        for (int i = 0; i < (int)body->shapes[s].numVertices; i++){
            Vector3 worldPoint;
            if (!getBody2DPointWorld(entity, s, i, worldPoint)) break;
            float halfSize = getPointHandleHalfSize(worldPoint);
            AABB handleAABB(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));

            RayReturn rr = ray.intersects(handleAABB);
            if (rr && rr.distance < bestDistance){
                bestDistance = rr.distance;
                bestIndex = i;
                shapeIndex = s;
            }
        }
    }

    return bestIndex;
}

OBB editor::SceneRender::getBody2DPointOBB(Entity entity, int shapeIndex, int pointIndex){
    Vector3 worldPoint;
    if (!getBody2DPointWorld(entity, shapeIndex, pointIndex, worldPoint)) return OBB();

    float halfSize = getPointHandleHalfSize(worldPoint);
    AABB aabb(worldPoint - Vector3(halfSize), worldPoint + Vector3(halfSize));
    return aabb.getOBB();
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

OBB editor::SceneRender::getTileOBB(Entity entity, int tileIndex, bool visual){
    if (!scene->getComponentArray<TilemapComponent>()->hasEntity(entity)) return OBB();
    if (!scene->getComponentArray<Transform>()->hasEntity(entity)) return OBB();

    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
    Transform& transform = scene->getComponent<Transform>(entity);

    if (tileIndex < 0 || tileIndex >= (int)tilemap.numTiles) return OBB();

    const TileData& tile = tilemap.tiles[tileIndex];
    AABB tileAABB(tile.position.x, tile.position.y, 0,
                  tile.position.x + tile.width, tile.position.y + tile.height, 0);

    return visual ? transformAABBPreservingShear(transform.modelMatrix, tileAABB)
                  : transform.modelMatrix * tileAABB.getOBB();
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

OBB editor::SceneRender::getInstanceOBB(Entity entity, int instanceIndex, bool visual){
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

    return visual ? transformAABBPreservingShear(transform.modelMatrix * instanceMatrix, localAABB)
                  : (transform.modelMatrix * instanceMatrix) * localAABB.getOBB();
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

void editor::SceneRender::setupSoundIcon(SoundObjects& so){
    TextureData iconData;
    iconData.loadTextureFromMemory(audio_source_icon_png, audio_source_icon_png_len);
    so.icon->setTexture("editor:resources:sound_icon", iconData);
    so.icon->setSize(128, 128);
    so.icon->setReceiveLights(false);
    so.icon->setCastShadows(false);
    so.icon->setReceiveShadows(false);
    so.icon->setPivotPreset(PivotPreset::CENTER);
}

void editor::SceneRender::setupLight2DIcon(Light2DObjects& lo){
    TextureData iconData;
    iconData.loadTextureFromMemory(bulb_icon_png, bulb_icon_png_len);
    lo.icon->setTexture("editor:resources:bulb_icon", iconData);
    lo.icon->setSize(128, 128);
    lo.icon->setReceiveLights(false);
    lo.icon->setCastShadows(false);
    lo.icon->setReceiveShadows(false);
    lo.icon->setPivotPreset(PivotPreset::CENTER);
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
    if (co.fixedSizeFrustum != fixedSizeFrustum) changed = true;
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
    co.fixedSizeFrustum = fixedSizeFrustum;
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

    Vector2 nearFar = RenderSystem::getCameraNearFar(cameraComponent);
    float nearClip = nearFar.x;
    float farClip = nearFar.y;

    if (nearClip > farClip) {
        std::swap(nearClip, farClip);
    }

    if (fixedSizeFrustum){
        float depth = 2.0f;
        if (cameraComponent.type == CameraType::CAMERA_ORTHO){
            // a fixed depth would draw wide ortho views flat
            float width = std::abs(cameraComponent.rightClip - cameraComponent.leftClip);
            float height = std::abs(cameraComponent.topClip - cameraComponent.bottomClip);
            depth = std::max(width, height) / 2.0f;
        }
        farClip = std::min(farClip, nearClip + depth);
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
