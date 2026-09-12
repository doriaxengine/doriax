// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneRender2D.h"

#include "Project.h"
#include "LineDrawUtils.h"

#include <algorithm>
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
    camera->setDepthTest(false);

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

    for (auto& pair : light2DLines) {
        delete pair.second;
    }
    light2DLines.clear();

    for (auto& pair : occluder2DLines) {
        delete pair.second;
    }
    occluder2DLines.clear();

    for (auto& pair : linePointLines) {
        delete pair.second;
    }
    linePointLines.clear();

    for (auto& pair : polygonPointLines) {
        delete pair.second;
    }
    polygonPointLines.clear();

    for (auto& pair : trackLines) {
        delete pair.second;
    }
    trackLines.clear();

    for (auto& pair : cameraObjects) {
        delete pair.second.icon;
        delete pair.second.lines;
    }
    cameraObjects.clear();

    for (auto& pair : soundObjects) {
        delete pair.second.icon;
    }
    soundObjects.clear();

    for (auto& pair : light2DObjects) {
        delete pair.second.icon;
    }
    light2DObjects.clear();

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

bool editor::SceneRender2D::instanciateLight2DLines(Entity entity){
    if (light2DLines.find(entity) == light2DLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        light2DLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender2D::instanciateOccluder2DLines(Entity entity){
    if (occluder2DLines.find(entity) == occluder2DLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        occluder2DLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender2D::instanciateLinePointLines(Entity entity){
    if (linePointLines.find(entity) == linePointLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        linePointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender2D::instanciatePolygonPointLines(Entity entity){
    if (polygonPointLines.find(entity) == polygonPointLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        polygonPointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

// square marker for an editable point (occluder polygon points, line endpoints)
void editor::SceneRender2D::addPointHandle(Lines* linesObj, const Vector3& worldPoint, float halfSize, const Vector4& color){
    linesObj->addLine(worldPoint + Vector3(-halfSize, -halfSize, 0.0f), worldPoint + Vector3(halfSize, -halfSize, 0.0f), color);
    linesObj->addLine(worldPoint + Vector3(halfSize, -halfSize, 0.0f), worldPoint + Vector3(halfSize, halfSize, 0.0f), color);
    linesObj->addLine(worldPoint + Vector3(halfSize, halfSize, 0.0f), worldPoint + Vector3(-halfSize, halfSize, 0.0f), color);
    linesObj->addLine(worldPoint + Vector3(-halfSize, halfSize, 0.0f), worldPoint + Vector3(-halfSize, -halfSize, 0.0f), color);
}

void editor::SceneRender2D::createOrUpdateLight2DLines(Entity entity, const Transform& transform, const Light2DComponent& light, bool visible, bool highlighted){
    Lines* lightLinesObj = light2DLines[entity];

    lightLinesObj->clearLines();
    lightLinesObj->setVisible(transform.visible && visible);

    if (!transform.visible || !visible){
        return;
    }

    float alpha = highlighted ? 1.0f : 0.35f;
    const Vector4 lightColor(1.0f, 0.85f, 0.3f, alpha);
    const Vector3 center = transform.worldPosition;

    // range circle
    LineDrawUtils::addCircle2D(lightLinesObj, center, light.range, lightColor, 48);

    // small center cross
    float markSize = overlayPx(6.0f) * zoom;
    lightLinesObj->addLine(center + Vector3(-markSize, 0.0f, 0.0f), center + Vector3(markSize, 0.0f, 0.0f), lightColor);
    lightLinesObj->addLine(center + Vector3(0.0f, -markSize, 0.0f), center + Vector3(0.0f, markSize, 0.0f), lightColor);
}

void editor::SceneRender2D::createOrUpdateOccluder2DLines(Entity entity, const Transform& transform, const Occluder2DComponent& occluder, bool visible, bool highlighted){
    Lines* occluderLinesObj = occluder2DLines[entity];

    occluderLinesObj->clearLines();
    occluderLinesObj->setVisible(transform.visible && visible && occluder.enabled);

    if (!transform.visible || !visible || !occluder.enabled){
        return;
    }

    float alpha = highlighted ? 1.0f : 0.35f;
    const Vector4 occluderColor(0.9f, 0.35f, 0.9f, alpha);
    const Vector4 handleColor(1.0f, 1.0f, 1.0f, alpha);
    const Matrix4& modelMatrix = transform.modelMatrix;

    // same outline the shadow pass builds: polygon points or the sibling mesh AABB
    std::vector<Vector2> points;
    bool closed = true;

    if (occluder.shape == Occluder2DShape::AUTO_QUAD){
        MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
        if (!mesh || mesh->aabb == AABB::ZERO){
            return;
        }
        Vector3 mn = mesh->aabb.getMinimum();
        Vector3 mx = mesh->aabb.getMaximum();
        points.push_back(Vector2(mn.x, mn.y));
        points.push_back(Vector2(mx.x, mn.y));
        points.push_back(Vector2(mx.x, mx.y));
        points.push_back(Vector2(mn.x, mx.y));
    }else{
        points = occluder.points;
        closed = occluder.closed;
    }

    if (points.size() < 2){
        return;
    }

    std::vector<Vector3> worldPoints;
    worldPoints.reserve(points.size());
    for (const Vector2& point : points){
        worldPoints.push_back(modelMatrix * Vector3(point.x, point.y, 0.0f));
    }

    size_t numSegments = closed ? worldPoints.size() : (worldPoints.size() - 1);
    for (size_t s = 0; s < numSegments; s++){
        occluderLinesObj->addLine(worldPoints[s], worldPoints[(s + 1) % worldPoints.size()], occluderColor);
    }

    // point handles (visual markers for the editable polygon points); the
    // sub-selected point is drawn bigger and in the selection color
    if (highlighted && occluder.shape == Occluder2DShape::POLYGON){
        const Vector4 selectedHandleColor(1.0f, 0.6f, 0.0f, 1.0f);
        bool hasPointSelection = (getSelectedOccluderPointEntity() == entity);

        for (size_t i = 0; i < worldPoints.size(); i++){
            bool pointSelected = hasPointSelection && ((int)i == getSelectedOccluderPointIndex());
            addPointHandle(occluderLinesObj, worldPoints[i],
                           overlayPx(pointSelected ? 6.0f : 4.0f) * zoom,
                           pointSelected ? selectedHandleColor : handleColor);
        }
    }
}

// endpoint handles for a selected Lines entity (the lines themselves are the
// entity's own rendered geometry); the sub-selected endpoint is drawn bigger
// and in the selection color
void editor::SceneRender2D::createOrUpdateLinePointLines(Entity entity, const Transform& transform, const LinesComponent& lines, bool visible){
    Lines* handlesObj = linePointLines[entity];

    handlesObj->clearLines();
    handlesObj->setVisible(transform.visible && visible);

    if (!transform.visible || !visible){
        return;
    }

    const Vector4 handleColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 selectedHandleColor(1.0f, 0.6f, 0.0f, 1.0f);
    bool hasPointSelection = (getSelectedLinePointEntity() == entity);

    for (size_t i = 0; i < lines.lines.size(); i++){
        for (int e = 0; e < 2; e++){
            const Vector3& localPoint = (e == 0) ? lines.lines[i].pointA : lines.lines[i].pointB;
            Vector3 worldPoint = transform.modelMatrix * localPoint;

            bool pointSelected = hasPointSelection && ((int)(i * 2) + e == getSelectedLinePointIndex());
            addPointHandle(handlesObj, worldPoint,
                           overlayPx(pointSelected ? 6.0f : 4.0f) * zoom,
                           pointSelected ? selectedHandleColor : handleColor);
        }
    }
}

bool editor::SceneRender2D::instanciateTrackLines(Entity entity){
    if (trackLines.find(entity) == trackLines.end()){
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        trackLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

// path of a selected TranslateTracks entity: a polyline through its keyframe
// values (in the action target's parent space) with square point handles; the
// sub-selected point is drawn bigger and in the selection color
void editor::SceneRender2D::createOrUpdateTrackLines(Entity entity, const TranslateTracksComponent& tracks, bool visible){
    Lines* trackObj = trackLines[entity];

    trackObj->clearLines();
    trackObj->setVisible(visible);

    if (!visible){
        return;
    }

    const Vector4 pathColor(0.4f, 0.75f, 1.0f, 1.0f);
    const Vector4 handleColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 selectedHandleColor(1.0f, 0.6f, 0.0f, 1.0f);
    bool hasPointSelection = (getSelectedTrackPointEntity() == entity);

    Matrix4 worldMatrix = getTrackPointsWorldMatrix(entity);

    Vector3 prevWorldPoint;
    for (size_t i = 0; i < tracks.values.size(); i++){
        Vector3 worldPoint = worldMatrix * tracks.values[i];

        if (i > 0){
            trackObj->addLine(prevWorldPoint, worldPoint, pathColor);
        }
        prevWorldPoint = worldPoint;

        bool pointSelected = hasPointSelection && ((int)i == getSelectedTrackPointIndex());
        addPointHandle(trackObj, worldPoint,
                       overlayPx(pointSelected ? 6.0f : 4.0f) * zoom,
                       pointSelected ? selectedHandleColor : handleColor);
    }
}

// vertex handles for a selected Polygon / MeshPolygon entity; the sub-selected
// vertex is drawn bigger and in the selection color
void editor::SceneRender2D::createOrUpdatePolygonPointLines(Entity entity, const Transform& transform, const std::vector<PolygonPoint>& points, bool isMesh, bool visible){
    Lines* handlesObj = polygonPointLines[entity];

    handlesObj->clearLines();
    handlesObj->setVisible(transform.visible && visible);

    if (!transform.visible || !visible){
        return;
    }

    const Vector4 handleColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 selectedHandleColor(1.0f, 0.6f, 0.0f, 1.0f);
    bool hasPointSelection = (getSelectedPolygonPointEntity() == entity && isSelectedPolygonPointMesh() == isMesh);

    for (size_t i = 0; i < points.size(); i++){
        Vector3 worldPoint = transform.modelMatrix * points[i].position;

        bool pointSelected = hasPointSelection && ((int)i == getSelectedPolygonPointIndex());
        addPointHandle(handlesObj, worldPoint,
                       overlayPx(pointSelected ? 6.0f : 4.0f) * zoom,
                       pointSelected ? selectedHandleColor : handleColor);
    }
}

void editor::SceneRender2D::createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body2DComponent& body, bool visible, bool highlighted){
    Lines* bodyLinesObj = bodyLines[entity];

    bodyLinesObj->clearLines();
    bodyLinesObj->setVisible(transform.visible && visible);

    if (!transform.visible || !visible || body.numShapes == 0){
        return;
    }

    float alpha = highlighted ? 1.0f : 0.35f;
    const Vector4 bodyColor(0.2f, 0.95f, 0.95f, alpha);
    // must match how PhysicsSystem sizes the Box2D shape, parent scale included
    const Vector2 entityScale(transform.worldScale.x, transform.worldScale.y);
    const Vector2 absScale(std::fabs(entityScale.x), std::fabs(entityScale.y));
    const float radiusScale = std::max(absScale.x, absScale.y);
    const Matrix4 bodyMatrix = Matrix4::translateMatrix(transform.worldPosition) * transform.worldRotation.getRotationMatrix();

    auto scaledPoint = [&](const Vector2& point){
        return point * entityScale;
    };

    auto scaledRadius = [&](float radius){
        return radius * radiusScale;
    };

    auto toWorld = [&](const Vector2& point){
        return bodyMatrix * Vector3(point.x, point.y, 0.0f);
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

        auto addPolygonEdges = [&](){
            for (size_t j = 0; j < shape.numVertices; j++){
                Vector2 p0 = scaledPoint(shape.vertices[j]);
                Vector2 p1 = scaledPoint(shape.vertices[(j + 1) % shape.numVertices]);
                bodyLinesObj->addLine(toWorld(p0), toWorld(p1), bodyColor);
            }
        };

        if (shape.type == Shape2DType::POLYGON){
            if (shape.numVertices >= 3){
                if (shape.radius > 0.0f && shape.numVertices == 4){
                    Vector2 minPt = scaledPoint(shape.vertices[0]);
                    Vector2 maxPt = minPt;
                    for (size_t j = 1; j < shape.numVertices; j++){
                        Vector2 p = scaledPoint(shape.vertices[j]);
                        minPt.x = std::min(minPt.x, p.x);
                        minPt.y = std::min(minPt.y, p.y);
                        maxPt.x = std::max(maxPt.x, p.x);
                        maxPt.y = std::max(maxPt.y, p.y);
                    }
                    addRoundedRect(minPt, maxPt, scaledRadius(shape.radius));
                }else{
                    addPolygonEdges();
                }
            }else{
                Vector2 pointA = scaledPoint(shape.pointA);
                Vector2 pointB = scaledPoint(shape.pointB);
                Vector2 minPt(std::min(pointA.x, pointB.x), std::min(pointA.y, pointB.y));
                Vector2 maxPt(std::max(pointA.x, pointB.x), std::max(pointA.y, pointB.y));

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
            addCircle(scaledPoint(shape.pointA), scaledRadius(shape.radius), 24);
        }else if (shape.type == Shape2DType::CAPSULE){
            Vector2 pointA = scaledPoint(shape.pointA);
            Vector2 pointB = scaledPoint(shape.pointB);
            float radius = scaledRadius(shape.radius);

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
            bodyLinesObj->addLine(toWorld(scaledPoint(shape.pointA)), toWorld(scaledPoint(shape.pointB)), bodyColor);
        }else if (shape.type == Shape2DType::CHAIN){
            if (shape.numVertices >= 2){
                for (size_t j = 0; j < shape.numVertices - 1; j++){
                    bodyLinesObj->addLine(toWorld(scaledPoint(shape.vertices[j])), toWorld(scaledPoint(shape.vertices[j + 1])), bodyColor);
                }

                if (shape.loop){
                    bodyLinesObj->addLine(toWorld(scaledPoint(shape.vertices[shape.numVertices - 1])), toWorld(scaledPoint(shape.vertices[0])), bodyColor);
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

    auto getBodyWorldPosition = [&](Entity bodyEntity){
        if (bodyEntity != NULL_ENTITY){
            Transform* transform = scene->findComponent<Transform>(bodyEntity);
            if (transform){
                return transform->worldPosition;
            }
        }

        return Vector3::ZERO;
    };

    auto toWorldPoint = [](const Vector2& point){
        return Vector3(point.x, point.y, 0.0f);
    };

    float alpha = highlighted ? 1.0f : 0.35f;

    const Vector4 jointColor(0.95f, 0.75f, 0.2f, alpha);
    const Vector4 helperColor(0.85f, 0.85f, 0.85f, 0.8f * alpha);
    const Vector4 axisColor(0.2f, 0.9f, 1.0f, alpha);
    const Vector4 limitColor(1.0f, 0.35f, 0.35f, alpha);

    Vector3 anchorA = getBodyWorldPosition(joint.bodyA);
    Vector3 anchorB = getBodyWorldPosition(joint.bodyB);

    switch (joint.type){
        case Joint2DType::DISTANCE:
            if (!joint.autoAnchors){
                anchorA = toWorldPoint(joint.anchorA);
                anchorB = toWorldPoint(joint.anchorB);
            }
            break;
        case Joint2DType::REVOLUTE:
        case Joint2DType::PRISMATIC:
        case Joint2DType::WHEEL:
        case Joint2DType::WELD:
            anchorA = anchorB = toWorldPoint(joint.anchorA);
            break;
        case Joint2DType::MOUSE:
            anchorB = toWorldPoint(joint.target);
            break;
        case Joint2DType::MOTOR:
            break;
    }

    jointLinesObj->addLine(anchorA, anchorB, jointColor);

    const float sz = overlayScale * zoom;
    float markSize = 8.0f * sz;

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
            LineDrawUtils::addCircle2D(jointLinesObj, anchorA, 10.0f * sz, helperColor, 18);
            if ((anchorB - anchorA).length() > 0.001f){
                LineDrawUtils::addCircle2D(jointLinesObj, anchorB, 8.0f * sz, helperColor, 16);
            }
            break;
        }
        case Joint2DType::WHEEL:{
            LineDrawUtils::addCircle2D(jointLinesObj, anchorA, 10.0f * sz, helperColor, 18);
            if ((anchorB - anchorA).length() > 0.001f){
                LineDrawUtils::addCircle2D(jointLinesObj, anchorB, 8.0f * sz, helperColor, 16);
            }
            break;
        }
        case Joint2DType::WELD:{
            // Diamond shape to indicate rigid lock
            float d = 10.0f * sz;
            jointLinesObj->addLine(anchorA + Vector3(0, d, 0), anchorA + Vector3(d, 0, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(d, 0, 0), anchorA + Vector3(0, -d, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(0, -d, 0), anchorA + Vector3(-d, 0, 0), limitColor);
            jointLinesObj->addLine(anchorA + Vector3(-d, 0, 0), anchorA + Vector3(0, d, 0), limitColor);
            if ((anchorB - anchorA).length() > 0.001f){
                float d2 = 8.0f * sz;
                jointLinesObj->addLine(anchorB + Vector3(0, d2, 0), anchorB + Vector3(d2, 0, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(d2, 0, 0), anchorB + Vector3(0, -d2, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(0, -d2, 0), anchorB + Vector3(-d2, 0, 0), limitColor);
                jointLinesObj->addLine(anchorB + Vector3(-d2, 0, 0), anchorB + Vector3(0, d2, 0), limitColor);
            }
            break;
        }
        case Joint2DType::MOTOR:{
            // Arrow arc to indicate driven rotation
            float radius = 10.0f * sz;
            float startAngle = 0.0f;
            float endAngle = 1.5f * M_PI;
            LineDrawUtils::addArc2D(jointLinesObj, anchorA, radius, startAngle, endAngle, axisColor, 14);
            // Arrowhead at end of arc
            float tipX = anchorA.x + std::cos(endAngle) * radius;
            float tipY = anchorA.y + std::sin(endAngle) * radius;
            Vector3 tip(tipX, tipY, anchorA.z);
            float arrowSize = 4.0f * sz;
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

            float halfRail = 40.0f * sz;
            float railOffset = 5.0f * sz;

            Vector3 railStart = anchorA - axis3 * halfRail;
            Vector3 railEnd = anchorA + axis3 * halfRail;

            jointLinesObj->addLine(railStart + railPerp * railOffset, railEnd + railPerp * railOffset, axisColor);
            jointLinesObj->addLine(railStart - railPerp * railOffset, railEnd - railPerp * railOffset, axisColor);

            float endCap = 6.0f * sz;
            jointLinesObj->addLine(railStart - railPerp * endCap, railStart + railPerp * endCap, limitColor);
            jointLinesObj->addLine(railEnd - railPerp * endCap, railEnd + railPerp * endCap, limitColor);

            float t = (anchorB - anchorA).x * axis3.x + (anchorB - anchorA).y * axis3.y;
            t = std::max(-halfRail, std::min(halfRail, t));
            Vector3 sliderCenter = anchorA + axis3 * t;
            LineDrawUtils::addCircle2D(jointLinesObj, sliderCenter, 7.0f * sz, jointColor, 14);
            break;
        }
        case Joint2DType::MOUSE:{
            Vector3 target(joint.target.x, joint.target.y, 0.0f);
            LineDrawUtils::addCircle2D(jointLinesObj, target, 8.0f * sz, Vector4(1.0f, 0.4f, 0.4f, alpha), 16);
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
        Vector3 axisEnd = center + Vector3(axis.x, axis.y, 0.0f) * (20.0f * sz);
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
    for (auto& pair : light2DLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : occluder2DLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : linePointLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : polygonPointLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : trackLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : cameraObjects) {
        pair.second.icon->setVisible(false);
        pair.second.lines->setVisible(false);
    }
    for (auto& pair : soundObjects) {
        pair.second.icon->setVisible(false);
    }
    for (auto& pair : light2DObjects) {
        pair.second.icon->setVisible(false);
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

    if (isPlaying || isPreviewCameraActive()){
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
    std::set<Entity> currentLights2D;
    std::set<Entity> currentOccluders2D;
    std::set<Entity> currentLinePoints;
    std::set<Entity> currentPolygonPoints;
    std::set<Entity> currentTrackLines;
    std::set<Entity> currentCameras;
    std::set<Entity> currentSounds;
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
            bool highlighted = isDescendantSelected(entity);
            bool isVisible = displaySettings.showAllBodies || highlighted;

            createOrUpdateBodyLines(entity, transform, body, isVisible, highlighted);
        }

        if (signature.test(scene->getComponentId<Light2DComponent>()) && signature.test(scene->getComponentId<Transform>())){
            Light2DComponent& light2d = scene->getComponent<Light2DComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentLights2D.insert(entity);
            instanciateLight2DLines(entity);
            bool highlighted = isDescendantSelected(entity);

            createOrUpdateLight2DLines(entity, transform, light2d, highlighted, highlighted);

            bool newLight = false;
            if (light2DObjects.find(entity) == light2DObjects.end()){
                ScopedDefaultEntityPool sys(*toolslayer.getScene(), EntityPool::System);
                light2DObjects[entity].icon = new Sprite(toolslayer.getScene());
                newLight = true;
            }

            Light2DObjects& lo = light2DObjects[entity];
            if (newLight){
                setupLight2DIcon(lo);
            }

            lo.icon->setPosition(transform.worldPosition);
            lo.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));
            lo.icon->setVisible(transform.visible);
        }

        if (signature.test(scene->getComponentId<Occluder2DComponent>()) && signature.test(scene->getComponentId<Transform>())){
            Occluder2DComponent& occluder2d = scene->getComponent<Occluder2DComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentOccluders2D.insert(entity);
            instanciateOccluder2DLines(entity);
            bool highlighted = isDescendantSelected(entity);

            createOrUpdateOccluder2DLines(entity, transform, occluder2d, highlighted, highlighted);
        }

        if (signature.test(scene->getComponentId<LinesComponent>()) && signature.test(scene->getComponentId<Transform>())){
            LinesComponent& linesComp = scene->getComponent<LinesComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentLinePoints.insert(entity);
            instanciateLinePointLines(entity);
            bool highlighted = isDescendantSelected(entity);

            createOrUpdateLinePointLines(entity, transform, linesComp, highlighted);
        }

        if (signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<Transform>())){
            PolygonComponent& polygon = scene->getComponent<PolygonComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentPolygonPoints.insert(entity);
            instanciatePolygonPointLines(entity);
            bool highlighted = isDescendantSelected(entity);

            createOrUpdatePolygonPointLines(entity, transform, polygon.points, false, highlighted);
        }else if (signature.test(scene->getComponentId<MeshPolygonComponent>()) && signature.test(scene->getComponentId<Transform>())){
            MeshPolygonComponent& polygon = scene->getComponent<MeshPolygonComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            currentPolygonPoints.insert(entity);
            instanciatePolygonPointLines(entity);
            bool highlighted = isDescendantSelected(entity);

            createOrUpdatePolygonPointLines(entity, transform, polygon.points, true, highlighted);
        }

        if (signature.test(scene->getComponentId<TranslateTracksComponent>())){
            TranslateTracksComponent& tracks = scene->getComponent<TranslateTracksComponent>(entity);

            currentTrackLines.insert(entity);
            instanciateTrackLines(entity);
            // the tracks entity has no Transform, so gate on direct selection
            bool isSelected = selectedEntities.find(entity) != selectedEntities.end();

            createOrUpdateTrackLines(entity, tracks, isSelected);
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
                co.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));

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

        if (signature.test(scene->getComponentId<SoundComponent>()) && signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            currentSounds.insert(entity);

            bool newSound = false;
            if (soundObjects.find(entity) == soundObjects.end()){
                ScopedDefaultEntityPool sys(*toolslayer.getScene(), EntityPool::System);
                soundObjects[entity].icon = new Sprite(toolslayer.getScene());
                newSound = true;
            }

            SoundObjects& so = soundObjects[entity];
            if (newSound){
                setupSoundIcon(so);
            }

            so.icon->setPosition(transform.worldPosition);
            so.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));
            so.icon->setVisible(transform.visible);

            if (displaySettings.hideSoundIcons) {
                so.icon->setVisible(false);
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

    auto itLight2D = light2DLines.begin();
    while (itLight2D != light2DLines.end()) {
        if (currentLights2D.find(itLight2D->first) == currentLights2D.end()) {
            delete itLight2D->second;
            itLight2D = light2DLines.erase(itLight2D);
        } else {
            ++itLight2D;
        }
    }

    auto itLight2DObj = light2DObjects.begin();
    while (itLight2DObj != light2DObjects.end()) {
        if (currentLights2D.find(itLight2DObj->first) == currentLights2D.end()) {
            delete itLight2DObj->second.icon;
            itLight2DObj = light2DObjects.erase(itLight2DObj);
        } else {
            ++itLight2DObj;
        }
    }

    auto itOccluder2D = occluder2DLines.begin();
    while (itOccluder2D != occluder2DLines.end()) {
        if (currentOccluders2D.find(itOccluder2D->first) == currentOccluders2D.end()) {
            delete itOccluder2D->second;
            itOccluder2D = occluder2DLines.erase(itOccluder2D);
        } else {
            ++itOccluder2D;
        }
    }

    auto itLinePoints = linePointLines.begin();
    while (itLinePoints != linePointLines.end()) {
        if (currentLinePoints.find(itLinePoints->first) == currentLinePoints.end()) {
            delete itLinePoints->second;
            itLinePoints = linePointLines.erase(itLinePoints);
        } else {
            ++itLinePoints;
        }
    }

    auto itPolygonPoints = polygonPointLines.begin();
    while (itPolygonPoints != polygonPointLines.end()) {
        if (currentPolygonPoints.find(itPolygonPoints->first) == currentPolygonPoints.end()) {
            delete itPolygonPoints->second;
            itPolygonPoints = polygonPointLines.erase(itPolygonPoints);
        } else {
            ++itPolygonPoints;
        }
    }

    auto itTrackLines = trackLines.begin();
    while (itTrackLines != trackLines.end()) {
        if (currentTrackLines.find(itTrackLines->first) == currentTrackLines.end()) {
            delete itTrackLines->second;
            itTrackLines = trackLines.erase(itTrackLines);
        } else {
            ++itTrackLines;
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

    auto itSound = soundObjects.begin();
    while (itSound != soundObjects.end()) {
        if (currentSounds.find(itSound->first) == currentSounds.end()) {
            delete itSound->second.icon;
            itSound = soundObjects.erase(itSound);
        } else {
            ++itSound;
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

void editor::SceneRender2D::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio){
    SceneRender::mouseDragEvent(x, y, origX, origY, project, sceneId, selEntities, disableSelection, invertRotationSnap, preserveAspectRatio);
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
