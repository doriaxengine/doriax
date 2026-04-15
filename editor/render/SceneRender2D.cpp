#include "SceneRender2D.h"

#include "Project.h"
#include "LineDrawUtils.h"

#include <cmath>


using namespace doriax;

editor::SceneRender2D::SceneRender2D(Scene* scene, unsigned int width, unsigned int height, bool isUI): SceneRender(scene, true, false, 40, 2){
    this->isUI = isUI;

    if (isUI){
        camera->setType(CameraType::CAMERA_UI);
    }else{
        camera->setType(CameraType::CAMERA_ORTHO);
    }
    camera->setNearClip(-10000.0f);
    camera->setFarClip(10000.0f);
    camera->setTransparentSort(false);

    camera->slide(-50);
    camera->slideUp(-50);

    scene->setDefaultEntityPool(EntityPool::System);
    lines = new Lines(scene);
    gridLines = new Lines(scene);
    gridLines->addLine(Vector3::ZERO, Vector3::ZERO, Vector4::ZERO);
    gridLines->setVisible(false);
    tileLines = new Lines(scene);
    tileLines->addLine(Vector3::ZERO, Vector3::ZERO, Vector4::ZERO);
    tileLines->setVisible(false);
    scene->setDefaultEntityPool(EntityPool::User);

    createLines(width, height);

    scene->setLightState(LightState::OFF);

    if (isUI){
        scene->setBackgroundColor(Vector4(0.525, 0.525, 0.525, 1.0));
    }else{
        scene->setBackgroundColor(Vector4(0.231, 0.298, 0.475, 1.0));
    }

    viewportWidth = width;
    viewportHeight = height;
    applyZoomProjection();
}

editor::SceneRender2D::~SceneRender2D(){
    for (auto& pair : containerLines) {
        delete pair.second;
    }
    containerLines.clear();

    for (auto& pair : bodyLines) {
        delete pair.second;
    }
    bodyLines.clear();

    for (auto& pair : jointLines) {
        delete pair.second;
    }
    jointLines.clear();

    for (auto& pair : cameraObjects) {
        delete pair.second.icon;
        delete pair.second.lines;
    }
    cameraObjects.clear();

    delete gridLines;
    delete tileLines;
}

bool editor::SceneRender2D::instanciateBodyLines(Entity entity){
    if (bodyLines.find(entity) == bodyLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        bodyLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender2D::instanciateJointLines(Entity entity){
    if (jointLines.find(entity) == jointLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        jointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

void editor::SceneRender2D::createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body2DComponent& body){
    Lines* bodyLinesObj = bodyLines[entity];

    bodyLinesObj->clearLines();
    bodyLinesObj->setVisible(transform.visible);

    if (!transform.visible || body.numShapes == 0){
        return;
    }

    const Vector4 bodyColor(0.2f, 0.95f, 0.95f, 1.0f);
    const Matrix4 modelMatrix = transform.modelMatrix;

    auto toWorld = [&](const Vector2& point){
        return modelMatrix * Vector3(point.x, point.y, 0.0f);
    };

    auto addCircle = [&](const Vector2& center, float radius, int segments){
        if (radius <= 0.0f){
            return;
        }

        for (int i = 0; i < segments; i++){
            float a0 = (2.0f * M_PI * i) / segments;
            float a1 = (2.0f * M_PI * (i + 1)) / segments;

            Vector2 p0 = center + Vector2(std::cos(a0), std::sin(a0)) * radius;
            Vector2 p1 = center + Vector2(std::cos(a1), std::sin(a1)) * radius;

            bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
        }
    };

    auto addArc = [&](const Vector2& center, float radius, float startAngle, float endAngle, int segments){
        if (radius <= 0.0f){
            return;
        }

        for (int i = 0; i < segments; i++){
            float t0 = (float)i / (float)segments;
            float t1 = (float)(i + 1) / (float)segments;
            float a0 = startAngle + (endAngle - startAngle) * t0;
            float a1 = startAngle + (endAngle - startAngle) * t1;

            Vector2 p0 = center + Vector2(std::cos(a0), std::sin(a0)) * radius;
            Vector2 p1 = center + Vector2(std::cos(a1), std::sin(a1)) * radius;

            bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
        }
    };

    auto addRoundedRect = [&](const Vector2& a, const Vector2& b, float radius){
        Vector2 minPt(std::min(a.x, b.x), std::min(a.y, b.y));
        Vector2 maxPt(std::max(a.x, b.x), std::max(a.y, b.y));

        const float width = maxPt.x - minPt.x;
        const float height = maxPt.y - minPt.y;
        if (width <= 0.0f || height <= 0.0f){
            return;
        }

        const float clampedRadius = std::max(0.0f, std::min(radius, std::min(width, height) * 0.5f));
        if (clampedRadius <= 0.0f){
            Vector2 p0(minPt.x, minPt.y);
            Vector2 p1(maxPt.x, minPt.y);
            Vector2 p2(maxPt.x, maxPt.y);
            Vector2 p3(minPt.x, maxPt.y);

            bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
            bodyLinesObj->addLine(toWorld(p1), toWorld(p2), bodyColor);
            bodyLinesObj->addLine(toWorld(p2), toWorld(p3), bodyColor);
            bodyLinesObj->addLine(toWorld(p3), toWorld(p0), bodyColor);
            return;
        }

        const float x0 = minPt.x;
        const float y0 = minPt.y;
        const float x1 = maxPt.x;
        const float y1 = maxPt.y;
        const float r = clampedRadius;

        bodyLinesObj->addLine(toWorld(Vector2(x0 + r, y0)), toWorld(Vector2(x1 - r, y0)), bodyColor);
        bodyLinesObj->addLine(toWorld(Vector2(x1, y0 + r)), toWorld(Vector2(x1, y1 - r)), bodyColor);
        bodyLinesObj->addLine(toWorld(Vector2(x1 - r, y1)), toWorld(Vector2(x0 + r, y1)), bodyColor);
        bodyLinesObj->addLine(toWorld(Vector2(x0, y1 - r)), toWorld(Vector2(x0, y0 + r)), bodyColor);

        addArc(Vector2(x0 + r, y0 + r), r, M_PI, 1.5f * M_PI, 8);
        addArc(Vector2(x1 - r, y0 + r), r, 1.5f * M_PI, 2.0f * M_PI, 8);
        addArc(Vector2(x1 - r, y1 - r), r, 0.0f, 0.5f * M_PI, 8);
        addArc(Vector2(x0 + r, y1 - r), r, 0.5f * M_PI, M_PI, 8);
    };

    for (size_t i = 0; i < body.numShapes; i++){
        const Shape2D& shape = body.shapes[i];

        if (shape.type == Shape2DType::POLYGON){
            if (shape.numVertices >= 3){
                if (shape.radius > 0.0f){
                    addRoundedRect(shape.pointA, shape.pointB, shape.radius);
                }else{
                    for (size_t j = 0; j < shape.numVertices; j++){
                        const Vector2& p0 = shape.vertices[j];
                        const Vector2& p1 = shape.vertices[(j + 1) % shape.numVertices];
                        bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
                    }
                }
            }else{
                Vector2 minPt(std::min(shape.pointA.x, shape.pointB.x), std::min(shape.pointA.y, shape.pointB.y));
                Vector2 maxPt(std::max(shape.pointA.x, shape.pointB.x), std::max(shape.pointA.y, shape.pointB.y));

                Vector2 p0(minPt.x, minPt.y);
                Vector2 p1(maxPt.x, minPt.y);
                Vector2 p2(maxPt.x, maxPt.y);
                Vector2 p3(minPt.x, maxPt.y);

                bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
                bodyLinesObj->addLine(toWorld(p1), toWorld(p2), bodyColor);
                bodyLinesObj->addLine(toWorld(p2), toWorld(p3), bodyColor);
                bodyLinesObj->addLine(toWorld(p3), toWorld(p0), bodyColor);
            }
        }else if (shape.type == Shape2DType::CIRCLE){
            addCircle(shape.pointA, shape.radius, 24);
        }else if (shape.type == Shape2DType::CAPSULE){
            Vector2 pointA = shape.pointA;
            Vector2 pointB = shape.pointB;
            float radius = shape.radius;

            if (radius <= 0.0f){
                continue;
            }

            Vector2 axis = pointB - pointA;
            if (axis.length() <= 0.0f){
                addCircle(pointA, radius, 24);
                continue;
            }

            axis.normalize();
            Vector2 perp(-axis.y, axis.x);

            Vector2 sideA1 = pointA + perp * radius;
            Vector2 sideB1 = pointB + perp * radius;
            Vector2 sideA2 = pointA - perp * radius;
            Vector2 sideB2 = pointB - perp * radius;

            bodyLinesObj->addLine(toWorld(sideA1), toWorld(sideB1), bodyColor);
            bodyLinesObj->addLine(toWorld(sideA2), toWorld(sideB2), bodyColor);

            float baseAngle = std::atan2(axis.y, axis.x);
            addArc(pointA, radius, baseAngle + 0.5f * M_PI, baseAngle + 1.5f * M_PI, 12);
            addArc(pointB, radius, baseAngle - 0.5f * M_PI, baseAngle + 0.5f * M_PI, 12);
        }else if (shape.type == Shape2DType::SEGMENT){
            bodyLinesObj->addLine(toWorld(shape.pointA), toWorld(shape.pointB), bodyColor);
        }else if (shape.type == Shape2DType::CHAIN){
            if (shape.numVertices >= 2){
                for (size_t j = 0; j < shape.numVertices - 1; j++){
                    bodyLinesObj->addLine(toWorld(shape.vertices[j]), toWorld(shape.vertices[j + 1]), bodyColor);
                }

                if (shape.loop){
                    bodyLinesObj->addLine(toWorld(shape.vertices[shape.numVertices - 1]), toWorld(shape.vertices[0]), bodyColor);
                }
            }
        }
    }
}

void editor::SceneRender2D::createOrUpdateJointLines(Entity entity, const Joint2DComponent& joint, bool visible, bool highlighted){
    Lines* jointLinesObj = jointLines[entity];

    jointLinesObj->clearLines();
    jointLinesObj->setVisible(visible);

    if (!visible){
        return;
    }

    auto getBodyWorldPoint = [&](Entity bodyEntity, const Vector2& localPoint){
        if (bodyEntity != NULL_ENTITY){
            Transform* transform = scene->findComponent<Transform>(bodyEntity);
            if (transform){
                return transform->modelMatrix * Vector3(localPoint.x, localPoint.y, 0.0f);
            }
        }

        return Vector3(localPoint.x, localPoint.y, 0.0f);
    };

    float alpha = highlighted ? 1.0f : 0.35f;

    const Vector4 jointColor(0.95f, 0.75f, 0.2f, alpha);
    const Vector4 helperColor(0.85f, 0.85f, 0.85f, 0.8f * alpha);
    const Vector4 axisColor(0.2f, 0.9f, 1.0f, alpha);
    const Vector4 limitColor(1.0f, 0.35f, 0.35f, alpha);

    Vector3 anchorA = getBodyWorldPoint(joint.bodyA, joint.anchorA);
    Vector3 anchorB = getBodyWorldPoint(joint.bodyB, joint.anchorB);

    jointLinesObj->addLine(anchorA, anchorB, jointColor);

    float markSize = 8.0f * zoom;

    jointLinesObj->addLine(anchorA + Vector3(-markSize, 0.0f, 0.0f), anchorA + Vector3(markSize, 0.0f, 0.0f), helperColor);
    jointLinesObj->addLine(anchorA + Vector3(0.0f, -markSize, 0.0f), anchorA + Vector3(0.0f, markSize, 0.0f), helperColor);

    jointLinesObj->addLine(anchorB + Vector3(-markSize, 0.0f, 0.0f), anchorB + Vector3(markSize, 0.0f, 0.0f), helperColor);
    jointLinesObj->addLine(anchorB + Vector3(0.0f, -markSize, 0.0f), anchorB + Vector3(0.0f, markSize, 0.0f), helperColor);

    switch (joint.type){
        case Joint2DType::DISTANCE:{
            // Connection line + anchor crosses already drawn above
            break;
        }
        case Joint2DType::REVOLUTE:{
            LineDrawUtils::addCircle2D(jointLinesObj, anchorA, 10.0f * zoom, helperColor, 18);
            if ((anchorB - anchorA).length() > 0.001f){
                LineDrawUtils::addCircle2D(jointLinesObj, anchorB, 8.0f * zoom, helperColor, 16);
            }
            break;
        }
        case Joint2DType::WHEEL:{
            LineDrawUtils::addCircle2D(jointLinesObj, anchorA, 10.0f * zoom, helperColor, 18);
            if ((anchorB - anchorA).length() > 0.001f){
                LineDrawUtils::addCircle2D(jointLinesObj, anchorB, 8.0f * zoom, helperColor, 16);
            }
            break;
        }
        case Joint2DType::WELD:{
            // Diamond shape to indicate rigid lock
            float d = 10.0f * zoom;
            jointLinesObj->addLine(anchorA + Vector3(0, d, 0), anchorA + Vector3(d, 0, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(d, 0, 0), anchorA + Vector3(0, -d, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(0, -d, 0), anchorA + Vector3(-d, 0, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(-d, 0, 0), anchorA + Vector3(0, d, 0), limitColor);
            if ((anchorB - anchorA).length() > 0.001f){
                float d2 = 8.0f * zoom;
                jointLinesObj->addLine(anchorB + Vector3(0, d2, 0), anchorB + Vector3(d2, 0, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(d2, 0, 0), anchorB + Vector3(0, -d2, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(0, -d2, 0), anchorB + Vector3(-d2, 0, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(-d2, 0, 0), anchorB + Vector3(0, d2, 0), limitColor);
            }
            break;
        }
        case Joint2DType::MOTOR:{
            // Arrow arc to indicate driven rotation
            float radius = 10.0f * zoom;
            float startAngle = 0.0f;
            float endAngle = 1.5f * M_PI;
            LineDrawUtils::addArc2D(jointLinesObj, anchorA, radius, startAngle, endAngle, axisColor, 14);
            // Arrowhead at end of arc
            float tipX = anchorA.x + std::cos(endAngle) * radius;
            float tipY = anchorA.y + std::sin(endAngle) * radius;
            Vector3 tip(tipX, tipY, anchorA.z);
            float arrowSize = 4.0f * zoom;
            Vector3 dir(std::cos(endAngle + M_PI * 0.5f), std::sin(endAngle + M_PI * 0.5f), 0.0f);
            Vector3 perp(-dir.y, dir.x, 0.0f);
            jointLinesObj->addLine(tip, tip - dir * arrowSize + perp * arrowSize * 0.5f, axisColor);
            jointLinesObj->addLine(tip, tip - dir * arrowSize - perp * arrowSize * 0.5f, axisColor);
            break;
        }
        case Joint2DType::PRISMATIC:{
            Vector2 axis2 = joint.axis;
            if (axis2.length() <= 0.0001f){
                axis2 = Vector2(1.0f, 0.0f);
            }else{
                axis2.normalize();
            }

            Vector3 axis3(axis2.x, axis2.y, 0.0f);
            Vector3 railPerp(-axis3.y, axis3.x, 0.0f);

            float halfRail = 40.0f * zoom;
            float railOffset = 5.0f * zoom;

            Vector3 railStart = anchorA - axis3 * halfRail;
            Vector3 railEnd = anchorA + axis3 * halfRail;

            jointLinesObj->addLine(railStart + railPerp * railOffset, railEnd + railPerp * railOffset, axisColor);
            jointLinesObj->addLine(railStart - railPerp * railOffset, railEnd - railPerp * railOffset, axisColor);

            float endCap = 6.0f * zoom;
            jointLinesObj->addLine(railStart - railPerp * endCap, railStart + railPerp * endCap, limitColor);
            jointLinesObj->addLine(railEnd - railPerp * endCap, railEnd + railPerp * endCap, limitColor);

            float t = (anchorB - anchorA).x * axis3.x + (anchorB - anchorA).y * axis3.y;
            t = std::max(-halfRail, std::min(halfRail, t));
            Vector3 sliderCenter = anchorA + axis3 * t;
            LineDrawUtils::addCircle2D(jointLinesObj, sliderCenter, 7.0f * zoom, jointColor, 14);
            break;
        }
        case Joint2DType::MOUSE:{
            Vector3 target(joint.target.x, joint.target.y, 0.0f);
            jointLinesObj->addLine(anchorA, target, Vector4(1.0f, 0.4f, 0.4f, alpha));
            LineDrawUtils::addCircle2D(jointLinesObj, target, 8.0f * zoom, Vector4(1.0f, 0.4f, 0.4f, alpha), 16);
            jointLinesObj->addLine(target + Vector3(-markSize, 0.0f, 0.0f), target + Vector3(markSize, 0.0f, 0.0f), limitColor);
            jointLinesObj->addLine(target + Vector3(0.0f, -markSize, 0.0f), target + Vector3(0.0f, markSize, 0.0f), limitColor);
            break;
        }
    }

    // Only draw generic axis for types that don't already visualize it
    if (joint.axis.length() > 0.0001f && joint.type != Joint2DType::PRISMATIC){
        Vector2 axis = joint.axis;
        axis.normalize();

        Vector3 center = (anchorA + anchorB) * 0.5f;
        Vector3 axisEnd = center + Vector3(axis.x, axis.y, 0.0f) * (20.0f * zoom);
        jointLinesObj->addLine(center, axisEnd, axisColor);
    }

}

void editor::SceneRender2D::createLines(unsigned int width, unsigned int height){
    lines->clearLines();

    lines->addLine(Vector3(0, -1000000, 0), Vector3(0, 1000000, 0), Vector4(0.2, 0.8, 0.4, 1.0));
    lines->addLine(Vector3(-1000000, 0, 0), Vector3(1000000, 0, 0), Vector4(0.8, 0.2, 0.4, 1.0));

    lines->addLine(Vector3(0, height, 0), Vector3(width, height, 0), Vector4(0.8, 0.8, 0.8, 1.0));
    lines->addLine(Vector3(width, height, 0), Vector3(width, 0, 0), Vector4(0.8, 0.8, 0.8, 1.0));
}

void editor::SceneRender2D::updateGridLines(){
    gridLines->clearLines();

    if (!displaySettings.showGrid2D || displaySettings.gridSpacing2D <= 0.0f){
        gridLines->setVisible(false);
        return;
    }

    gridLines->setVisible(true);

    float spacing = displaySettings.gridSpacing2D;

    Vector3 camPos = camera->getWorldPosition();

    float left = camera->getLeftClip() + camPos.x;
    float right = camera->getRightClip() + camPos.x;
    float bottom = camera->getBottomClip() + camPos.y;
    float top = camera->getTopClip() + camPos.y;

    // Add margin to avoid popping at edges
    float margin = spacing * 2.0f;
    left -= margin;
    right += margin;
    bottom -= margin;
    top += margin;

    Vector4 gridColor(0.5f, 0.5f, 0.5f, 0.3f);
    Vector4 gridColorMajor(0.5f, 0.5f, 0.5f, 0.6f);
    int majorEvery = 5;

    // Vertical lines
    float startX = std::floor(left / spacing) * spacing;
    for (float x = startX; x <= right; x += spacing){
        int ix = (int)std::round(x / spacing);
        Vector4 color = (ix % majorEvery == 0) ? gridColorMajor : gridColor;
        gridLines->addLine(Vector3(x, bottom, 0), Vector3(x, top, 0), color);
    }

    // Horizontal lines
    float startY = std::floor(bottom / spacing) * spacing;
    for (float y = startY; y <= top; y += spacing){
        int iy = (int)std::round(y / spacing);
        Vector4 color = (iy % majorEvery == 0) ? gridColorMajor : gridColor;
        gridLines->addLine(Vector3(left, y, 0), Vector3(right, y, 0), color);
    }
}

void editor::SceneRender2D::hideAllGizmos(){
    SceneRender::hideAllGizmos();

    lines->setVisible(false);
    gridLines->setVisible(false);
    tileLines->setVisible(false);
    for (auto& pair : containerLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : bodyLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : jointLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : cameraObjects) {
        pair.second.icon->setVisible(false);
        pair.second.lines->setVisible(false);
    }
}

void editor::SceneRender2D::activate(){
    SceneRender::activate();
}

void editor::SceneRender2D::applyZoomProjection(){
    Entity cameraEntity = camera->getEntity();
    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(cameraEntity);
    Transform& cameratransform = scene->getComponent<Transform>(cameraEntity);

    float newWidth = viewportWidth * zoom;
    float newHeight = viewportHeight * zoom;

    if (cameracomp.leftClip != 0.0f || cameracomp.bottomClip != 0.0f || cameracomp.rightClip != newWidth || cameracomp.topClip != newHeight){
        cameracomp.leftClip = 0.0f;
        cameracomp.bottomClip = 0.0f;
        cameracomp.rightClip = newWidth;
        cameracomp.topClip = newHeight;
        cameracomp.needUpdate = true;
    }

    cameracomp.autoResize = false;

    toolslayer.updateCamera(cameracomp, cameratransform);
}

void editor::SceneRender2D::setCanvasFrameSize(unsigned int width, unsigned int height){
    createLines(width, height);
}

void editor::SceneRender2D::updateSize(int width, int height){
    SceneRender::updateSize(width, height);

    viewportWidth = width;
    viewportHeight = height;
    applyZoomProjection();
}

void editor::SceneRender2D::updateSelLines(std::vector<OBB> obbs){
    Vector4 color = Vector4(1.0, 0.6, 0.0, 1.0);

    if (selLines->getNumLines() != obbs.size() * 4){
        selLines->clearLines();
        for (OBB& obb : obbs){
            selLines->addLine(obb.getCorner(OBB::NEAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_LEFT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM), color);
        }
    }else{
        int i = 0;
        for (OBB& obb : obbs){
            selLines->updateLine(i * 4 + 0, obb.getCorner(OBB::NEAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_TOP));
            selLines->updateLine(i * 4 + 1, obb.getCorner(OBB::NEAR_LEFT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP));
            selLines->updateLine(i * 4 + 2, obb.getCorner(OBB::NEAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM));
            selLines->updateLine(i * 4 + 3, obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM));
            i++;
        }
    }
}

void editor::SceneRender2D::update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings){
    SceneRender::update(selEntities, entities, mainCamera, settings);

    if (isPlaying){
        return;
    }

    lines->setVisible(displaySettings.showOrigin);

    updateGridLines();

    std::set<Entity> selectedEntities(selEntities.begin(), selEntities.end());

    auto isDescendantSelected = [&](Entity ancestor) -> bool {
        for (Entity sel : selectedEntities) {
            if (sel == ancestor || scene->isParentOf(ancestor, sel)) return true;
        }
        return false;
    };

    std::set<Entity> currentContainers;
    std::set<Entity> currentBodies;
    std::set<Entity> currentJoints;
    std::set<Entity> currentCameras;
    for (Entity& entity: entities){
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<UIContainerComponent>()) && 
            signature.test(scene->getComponentId<Transform>()) && 
            signature.test(scene->getComponentId<UILayoutComponent>())){

            currentContainers.insert(entity);

            if (containerLines.find(entity) == containerLines.end()){
                ScopedDefaultEntityPool sys(*scene, EntityPool::System);
                containerLines[entity] = new Lines(scene);
            }

            Lines* containerLinesObj = containerLines[entity];
            containerLinesObj->clearLines();

            Transform& transform = scene->getComponent<Transform>(entity);
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            UIContainerComponent& container = scene->getComponent<UIContainerComponent>(entity);

            if (transform.visible && !displaySettings.hideContainerGuides){
                containerLinesObj->setVisible(true);

                Vector4 borderColor(0.55f, 0.35f, 0.85f, 1.0f); // Purple color for container
                Vector4 divColor(0.55f, 0.35f, 0.85f, 0.5f); // Semi-transparent purple for divisions

                Matrix4 modelMatrix = transform.modelMatrix;

                // Container borders
                Vector3 p1 = modelMatrix * Vector3(0, 0, 0);
                Vector3 p2 = modelMatrix * Vector3(layout.width, 0, 0);
                Vector3 p3 = modelMatrix * Vector3(layout.width, layout.height, 0);
                Vector3 p4 = modelMatrix * Vector3(0, layout.height, 0);

                containerLinesObj->addLine(p1, p2, borderColor);
                containerLinesObj->addLine(p2, p3, borderColor);
                containerLinesObj->addLine(p3, p4, borderColor);
                containerLinesObj->addLine(p4, p1, borderColor);

                // Divisions
                for (int b = 0; b < container.numBoxes; b++){
                    if (container.boxes[b].layout != NULL_ENTITY){
                        Rect rect = container.boxes[b].rect;

                        Vector3 bp1 = modelMatrix * Vector3(rect.getX(), rect.getY(), 0);
                        Vector3 bp2 = modelMatrix * Vector3(rect.getX() + rect.getWidth(), rect.getY(), 0);
                        Vector3 bp3 = modelMatrix * Vector3(rect.getX() + rect.getWidth(), rect.getY() + rect.getHeight(), 0);
                        Vector3 bp4 = modelMatrix * Vector3(rect.getX(), rect.getY() + rect.getHeight(), 0);

                        containerLinesObj->addLine(bp1, bp2, divColor);
                        containerLinesObj->addLine(bp2, bp3, divColor);
                        containerLinesObj->addLine(bp3, bp4, divColor);
                        containerLinesObj->addLine(bp4, bp1, divColor);
                    }
                }
            }else{
                containerLinesObj->setVisible(false);
            }
        }

        if (signature.test(scene->getComponentId<Body2DComponent>()) && signature.test(scene->getComponentId<Transform>())){
            Body2DComponent& body = scene->getComponent<Body2DComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentBodies.insert(entity);
            instanciateBodyLines(entity);
            createOrUpdateBodyLines(entity, transform, body);
            if (displaySettings.hideAllBodies && bodyLines.find(entity) != bodyLines.end()) {
                bodyLines[entity]->setVisible(false);
            }
        }

        if (signature.test(scene->getComponentId<Joint2DComponent>())){
            Joint2DComponent& joint = scene->getComponent<Joint2DComponent>(entity);

            currentJoints.insert(entity);
            instanciateJointLines(entity);

            bool isSelectedJoint = selectedEntities.find(entity) != selectedEntities.end();
            bool isBodyASelected = joint.bodyA != NULL_ENTITY && isDescendantSelected(joint.bodyA);
            bool isBodyBSelected = joint.bodyB != NULL_ENTITY && isDescendantSelected(joint.bodyB);
            bool highlighted = isSelectedJoint || isBodyASelected || isBodyBSelected;
            bool isVisible = displaySettings.showAllJoints || highlighted;

            createOrUpdateJointLines(entity, joint, isVisible, highlighted);
        }

        if (signature.test(scene->getComponentId<CameraComponent>()) && signature.test(scene->getComponentId<Transform>())){
            if (entity != camera->getEntity()){
                Transform& transform = scene->getComponent<Transform>(entity);
                CameraComponent& cameraComp = scene->getComponent<CameraComponent>(entity);

                currentCameras.insert(entity);

                bool newCamera = false;
                if (cameraObjects.find(entity) == cameraObjects.end()){
                    ScopedDefaultEntityPool sys(*toolslayer.getScene(), EntityPool::System);
                    cameraObjects[entity].icon = new Sprite(toolslayer.getScene());
                    cameraObjects[entity].lines = new Lines(toolslayer.getScene());
                    newCamera = true;
                }

                CameraObjects& co = cameraObjects[entity];

                if (newCamera){
                    setupCameraIcon(co);
                }

                // Update icon position and scale
                co.icon->setPosition(transform.worldPosition);
                float iconScale = 0.25f * zoom;
                co.icon->setScale(iconScale);

                // Update frustum lines
                co.lines->setPosition(transform.worldPosition);

                Quaternion rotation;
                rotation.fromRotationMatrix(cameraComp.viewMatrix.inverse());
                co.lines->setRotation(rotation);

                if (transform.visible && !displaySettings.hideCameraView){
                    co.icon->setVisible(true);
                    co.lines->setVisible(true);

                    bool isMainCam = (mainCamera == entity);
                    updateCameraFrustum(co, cameraComp, isMainCam);
                }else{
                    co.icon->setVisible(false);
                    co.lines->clearLines();
                    co.lines->setVisible(false);
                    // Reset cache so lines are redrawn when visible again
                    co.type = CameraType::CAMERA_UI;
                }
            }
        }
    }

    // --- Tile outlines for selected tilemap ---
    tileLines->clearLines();
    tileLines->setVisible(false);
    if (selEntities.size() == 1){
        Entity selEntity = selEntities[0];
        Signature selSig = scene->getSignature(selEntity);
        if (selSig.test(scene->getComponentId<TilemapComponent>()) && selSig.test(scene->getComponentId<Transform>())){
            TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(selEntity);
            Transform& transform = scene->getComponent<Transform>(selEntity);
            const Matrix4& modelMatrix = transform.modelMatrix;

            if (transform.visible && tilemap.numTiles > 0){
                tileLines->setVisible(true);

                const Vector4 normalColor(0.4f, 0.7f, 0.9f, 0.5f);
                const Vector4 selectedColor(1.0f, 0.8f, 0.2f, 1.0f);

                for (unsigned int i = 0; i < tilemap.numTiles; i++){
                    const TileData& tile = tilemap.tiles[i];
                    bool isSelected = ((int)i == getSelectedTileIndex() && selEntity == getSelectedTileEntity());
                    const Vector4& color = isSelected ? selectedColor : normalColor;

                    Vector3 p1 = modelMatrix * Vector3(tile.position.x, tile.position.y, 0);
                    Vector3 p2 = modelMatrix * Vector3(tile.position.x + tile.width, tile.position.y, 0);
                    Vector3 p3 = modelMatrix * Vector3(tile.position.x + tile.width, tile.position.y + tile.height, 0);
                    Vector3 p4 = modelMatrix * Vector3(tile.position.x, tile.position.y + tile.height, 0);

                    tileLines->addLine(p1, p2, color);
                    tileLines->addLine(p2, p3, color);
                    tileLines->addLine(p3, p4, color);
                    tileLines->addLine(p4, p1, color);
                }
            }
        }
    }

    auto it = containerLines.begin();
    while (it != containerLines.end()) {
        if (currentContainers.find(it->first) == currentContainers.end()) {
            delete it->second;
            it = containerLines.erase(it);
        } else {
            ++it;
        }
    }

    auto itBody = bodyLines.begin();
    while (itBody != bodyLines.end()) {
        if (currentBodies.find(itBody->first) == currentBodies.end()) {
            delete itBody->second;
            itBody = bodyLines.erase(itBody);
        } else {
            ++itBody;
        }
    }

    auto itJoint = jointLines.begin();
    while (itJoint != jointLines.end()) {
        if (currentJoints.find(itJoint->first) == currentJoints.end()) {
            delete itJoint->second;
            itJoint = jointLines.erase(itJoint);
        } else {
            ++itJoint;
        }
    }

    auto itCam = cameraObjects.begin();
    while (itCam != cameraObjects.end()) {
        if (currentCameras.find(itCam->first) == currentCameras.end()) {
            delete itCam->second.icon;
            delete itCam->second.lines;
            itCam = cameraObjects.erase(itCam);
        } else {
            ++itCam;
        }
    }
}

void editor::SceneRender2D::mouseHoverEvent(float x, float y){
    SceneRender::mouseHoverEvent(x, y);
}

void editor::SceneRender2D::mouseClickEvent(float x, float y, std::vector<Entity> selEntities){
    SceneRender::mouseClickEvent(x, y, selEntities);
}

void editor::SceneRender2D::mouseReleaseEvent(float x, float y){
    SceneRender::mouseReleaseEvent(x, y);
}

void editor::SceneRender2D::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection){
    SceneRender::mouseDragEvent(x, y, origX, origY, project, sceneId, selEntities, disableSelection);
}

void editor::SceneRender2D::zoomAtPosition(float width, float height, Vector2 pos, float zoomFactor){
    float shiftX = pos.x * zoom * (1.0f - zoomFactor);
    float shiftY = pos.y * zoom * (1.0f - zoomFactor);

    camera->slide(shiftX);
    camera->slideUp(shiftY);

    zoom *= zoomFactor;

    applyZoomProjection();
}

float editor::SceneRender2D::getZoom() const {
    return zoom;
}

void editor::SceneRender2D::setZoom(float zoom) {
    this->zoom = zoom;

    if (viewportWidth > 0 && viewportHeight > 0) {
        applyZoomProjection();
    }
}