// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneRender3D.h"

#include "resources/icons/sun-icon_png.h"
#include "resources/icons/bulb-icon_png.h"
#include "resources/icons/spot-icon_png.h"

#include "Project.h"
#include "LineDrawUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace doriax;

uint64_t editor::SceneRender3D::quantizeHullKey(const Vector3& p) {
    auto q = [](float v) { return int32_t(std::lround(v * SceneRender3D::kHullQuantizeScale)); };
    uint64_t x = uint32_t(q(p.x));
    uint64_t y = uint32_t(q(p.y));
    uint64_t z = uint32_t(q(p.z));
    return (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
}

void editor::SceneRender3D::pushUniqueHullPoint(std::vector<Vector3>& points, std::unordered_set<uint64_t>& seen, const Vector3& p) {
    if (!p.isValid()) return;
    if (seen.insert(quantizeHullKey(p)).second) {
        points.push_back(p);
    }
}

// Reduce a large point cloud to its extremes along a fixed direction set.
// The convex hull of these extremes is contained in the original hull and
// is sufficient for visualization while bounding cost.
void editor::SceneRender3D::reduceToExtremes(std::vector<Vector3>& points) {
    if (points.size() <= SceneRender3D::kMaxHullInputPoints) return;

    // A 5^3 lattice of directions, dense enough that the drawn hull stays close to
    // the one Jolt builds from the same vertices.
    static const std::vector<Vector3> directions = [](){
        std::vector<Vector3> dirs;
        for (int x = -2; x <= 2; x++){
            for (int y = -2; y <= 2; y++){
                for (int z = -2; z <= 2; z++){
                    if (x != 0 || y != 0 || z != 0) dirs.push_back(Vector3(x, y, z));
                }
            }
        }
        return dirs;
    }();
    const int dirCount = int(directions.size());

    std::vector<int> bestIndex(dirCount, 0);
    std::vector<float> bestDot(dirCount, -std::numeric_limits<float>::infinity());

    for (size_t i = 0; i < points.size(); i++) {
        for (int d = 0; d < dirCount; d++) {
            float dot = points[i].dotProduct(directions[d]);
            if (dot > bestDot[d]) {
                bestDot[d] = dot;
                bestIndex[d] = int(i);
            }
        }
    }

    std::vector<Vector3> reduced;
    std::unordered_set<uint64_t> seen;
    reduced.reserve(dirCount);
    for (int d = 0; d < dirCount; d++) {
        pushUniqueHullPoint(reduced, seen, points[bestIndex[d]]);
    }
    points = std::move(reduced);
}

// Builds convex hull edges from a point set and emits each unique edge once.
// Uses an incremental QuickHull with axis-extreme seeding, cached face
// normals and a horizon-edge map. Falls back to a 2D Andrew monotone chain
// when the input is coplanar.
void editor::SceneRender3D::buildConvexHullEdges(std::vector<Vector3> points, const std::function<void(const Vector3&, const Vector3&)>& emit) {
    reduceToExtremes(points);
    if (points.size() < 2) return;
    if (points.size() == 2) { emit(points[0], points[1]); return; }

    Vector3 minPt = points[0];
    Vector3 maxPt = points[0];
    for (const Vector3& p : points) {
        minPt.x = std::min(minPt.x, p.x); minPt.y = std::min(minPt.y, p.y); minPt.z = std::min(minPt.z, p.z);
        maxPt.x = std::max(maxPt.x, p.x); maxPt.y = std::max(maxPt.y, p.y); maxPt.z = std::max(maxPt.z, p.z);
    }
    Vector3 extent = maxPt - minPt;
    float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
    float eps = std::max(1e-5f, maxExtent * 1e-5f);
    float epsSq = eps * eps;

    // Seed pair from the 6 axis extremes (min/max over x, y, z).
    int axisExt[6] = {0, 0, 0, 0, 0, 0};
    {
        float bestVal[6] = {points[0].x, -points[0].x, points[0].y, -points[0].y, points[0].z, -points[0].z};
        for (size_t i = 1; i < points.size(); i++) {
            float v[6] = {points[i].x, -points[i].x, points[i].y, -points[i].y, points[i].z, -points[i].z};
            for (int k = 0; k < 6; k++) {
                if (v[k] > bestVal[k]) { bestVal[k] = v[k]; axisExt[k] = int(i); }
            }
        }
    }

    int i0 = 0, i1 = 0;
    float maxDistSq = 0.0f;
    for (int a = 0; a < 6; a++) {
        for (int b = a + 1; b < 6; b++) {
            float d = points[axisExt[a]].squaredDistance(points[axisExt[b]]);
            if (d > maxDistSq) { maxDistSq = d; i0 = axisExt[a]; i1 = axisExt[b]; }
        }
    }
    if (maxDistSq <= epsSq) return;

    Vector3 baseLine = points[i1] - points[i0];
    int i2 = -1;
    float maxLineSq = 0.0f;
    for (size_t i = 0; i < points.size(); i++) {
        float d = (points[i] - points[i0]).crossProduct(baseLine).squaredLength() / maxDistSq;
        if (d > maxLineSq) { maxLineSq = d; i2 = int(i); }
    }
    if (i2 < 0 || maxLineSq <= epsSq) { emit(points[i0], points[i1]); return; }

    Vector3 planeNormal = (points[i1] - points[i0]).crossProduct(points[i2] - points[i0]);
    float planeNormalLen = planeNormal.length();

    int i3 = -1;
    float maxPlaneDist = 0.0f;
    for (size_t i = 0; i < points.size(); i++) {
        float d = std::fabs(planeNormal.dotProduct(points[i] - points[i0]) / planeNormalLen);
        if (d > maxPlaneDist) { maxPlaneDist = d; i3 = int(i); }
    }

    // Coplanar fallback: 2D convex hull (Andrew's monotone chain).
    if (i3 < 0 || maxPlaneDist <= eps) {
        Vector3 absN(std::fabs(planeNormal.x), std::fabs(planeNormal.y), std::fabs(planeNormal.z));
        int drop = (absN.y > absN.x && absN.y >= absN.z) ? 1 : ((absN.z > absN.x) ? 2 : 0);
        struct P2 { float x, y; int idx; };
        std::vector<P2> pp;
        pp.reserve(points.size());
        for (size_t i = 0; i < points.size(); i++) {
            const Vector3& p = points[i];
            float px = drop == 0 ? p.y : p.x;
            float py = drop == 2 ? p.y : p.z;
            pp.push_back({px, py, int(i)});
        }
        std::sort(pp.begin(), pp.end(), [](const P2& a, const P2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        auto cross2 = [](const P2& a, const P2& b, const P2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
        std::vector<P2> poly;
        for (const P2& p : pp) {
            while (poly.size() >= 2 && cross2(poly[poly.size() - 2], poly.back(), p) <= 0.0f) poly.pop_back();
            poly.push_back(p);
        }
        size_t lower = poly.size();
        for (auto it = pp.rbegin(); it != pp.rend(); ++it) {
            while (poly.size() > lower && cross2(poly[poly.size() - 2], poly.back(), *it) <= 0.0f) poly.pop_back();
            poly.push_back(*it);
        }
        if (!poly.empty()) poly.pop_back();
        for (size_t i = 0; i < poly.size(); i++) {
            emit(points[poly[i].idx], points[poly[(i + 1) % poly.size()].idx]);
        }
        return;
    }

    Vector3 interior = (points[i0] + points[i1] + points[i2] + points[i3]) * 0.25f;

    struct Face { int a, b, c; Vector3 n; bool removed; };
    std::vector<Face> faces;
    faces.reserve(64);

    auto pushFace = [&](int a, int b, int c) {
        Vector3 n = (points[b] - points[a]).crossProduct(points[c] - points[a]);
        if (n.squaredLength() <= epsSq) return;
        if (n.dotProduct(interior - points[a]) > 0.0f) { std::swap(b, c); n = -n; }
        faces.push_back({a, b, c, n, false});
    };

    pushFace(i0, i1, i2);
    pushFace(i0, i3, i1);
    pushFace(i0, i2, i3);
    pushFace(i1, i3, i2);

    auto edgeKey = [](int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); };

    std::vector<int> visible;
    std::unordered_map<uint64_t, int> horizon;

    for (size_t pi = 0; pi < points.size(); pi++) {
        if (int(pi) == i0 || int(pi) == i1 || int(pi) == i2 || int(pi) == i3) continue;

        visible.clear();
        for (size_t fi = 0; fi < faces.size(); fi++) {
            const Face& f = faces[fi];
            if (f.removed) continue;
            if (f.n.dotProduct(points[pi] - points[f.a]) > eps * f.n.length()) {
                visible.push_back(int(fi));
            }
        }
        if (visible.empty()) continue;

        horizon.clear();
        auto addEdge = [&](int s, int e) {
            uint64_t rev = edgeKey(e, s);
            auto it = horizon.find(rev);
            if (it != horizon.end()) { horizon.erase(it); return; }
            horizon[edgeKey(s, e)] = s; // value unused; key encodes the directed edge
        };
        for (int fi : visible) {
            Face& f = faces[fi];
            addEdge(f.a, f.b); addEdge(f.b, f.c); addEdge(f.c, f.a);
            f.removed = true;
        }
        for (const auto& kv : horizon) {
            int s = int(uint32_t(kv.first >> 32));
            int e = int(uint32_t(kv.first));
            pushFace(s, e, int(pi));
        }
    }

    std::unordered_set<uint64_t> drawn;
    auto emitEdge = [&](int a, int b) {
        if (a == b) return;
        int lo = std::min(a, b), hi = std::max(a, b);
        if (drawn.insert(edgeKey(lo, hi)).second) emit(points[a], points[b]);
    };
    for (const Face& f : faces) {
        if (f.removed) continue;
        emitEdge(f.a, f.b); emitEdge(f.b, f.c); emitEdge(f.c, f.a);
    }
}

editor::SceneRender3D::SceneRender3D(Scene* scene): SceneRender(scene, false, true, 40.0, 0.01){
    ScopedDefaultEntityPool sys(*scene, EntityPool::System);

    linesOffset = Vector2(0, 0);
    lastGridSpacing = 1.0f;

    lines = new Lines(scene);

    lightObjects.clear();
    cameraObjects.clear();
    soundObjects.clear();
    bodyObjects.clear();
    jointLines.clear();

    camera->setNearClip(DEFAULT_EDITOR_CAMERA_NEAR);
    camera->setFarClip(DEFAULT_EDITOR_CAMERA_FAR);
    camera->setPosition(10, 4, 10);
    camera->setTarget(0, 0, 0);

    int linesStepChange = std::max(1, (int)(camera->getFarClip() / 2));
    linesOffset = Vector2(
        (int)(camera->getWorldPosition().x / linesStepChange) * linesStepChange,
        (int)(camera->getWorldPosition().z / linesStepChange) * linesStepChange
    );
    lastGridFarClip = camera->getFarClip();
    createLines();

    //camera->setRenderToTexture(true);
    //camera->setUseFramebufferSizes(false);

    // Note: scene-appearance defaults (light state, global illumination, background
    // color) are applied when a new scene is created in Project::createScene(), not
    // here. Setting them in the constructor would clobber values loaded from disk,
    // since createSceneRender() runs after the scene has been decoded.

    uilayer.setViewportGizmoTexture(viewgizmo.getFramebuffer());

}

editor::SceneRender3D::~SceneRender3D(){
    delete lines;
    delete selLines;

    clearEntityOverlays();
}

void editor::SceneRender3D::clearEntityOverlays(){
    for (auto& pair : lightObjects) {
        delete pair.second.icon;
        delete pair.second.lines;
    }
    lightObjects.clear();

    for (auto& pair : cameraObjects) {
        delete pair.second.icon;
        delete pair.second.lines;
    }
    cameraObjects.clear();

    for (auto& pair : soundObjects) {
        delete pair.second.icon;
    }
    soundObjects.clear();

    for (auto& pair : bodyObjects) {
        delete pair.second.lines;
    }
    bodyObjects.clear();

    for (auto& pair : jointLines) {
        delete pair.second;
    }
    jointLines.clear();

    for (auto& pair : boneLines) {
        delete pair.second;
    }
    boneLines.clear();

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

    for (auto& pair : reflectionProbeLines) {
        delete pair.second;
    }
    reflectionProbeLines.clear();

    for (auto& pair : waterExclusionLines) {
        delete pair.second;
    }
    waterExclusionLines.clear();
}

void editor::SceneRender3D::createLines(){
    float spacing = displaySettings.gridSpacing3D;
    if (spacing <= 0.0f) spacing = 1.0f;

    int gridHeight = 0;
    float gridSize = camera->getFarClip() * 2;

    float xGridStart = -gridSize + linesOffset.x;
    float xGridEnd = gridSize + linesOffset.x;

    float yGridStart = -gridSize + linesOffset.y;
    float yGridEnd = gridSize + linesOffset.y;

    lines->clearLines();

    int majorEvery = 10;

    float startX = std::floor(xGridStart / spacing) * spacing;
    for (float x = startX; x <= xGridEnd; x += spacing){
        int ix = (int)std::round(x / spacing);
        if (ix == 0){
            lines->addLine(Vector3(x, gridHeight, yGridStart), Vector3(x, gridHeight, yGridEnd), Vector4(0.5, 0.5, 1.0, 1.0));
        }else{
            if (ix % majorEvery == 0){
                lines->addLine(Vector3(x, gridHeight, yGridStart), Vector3(x, gridHeight, yGridEnd), Vector4(0.5, 0.5, 0.5, 1.0));
            }else{
                lines->addLine(Vector3(x, gridHeight, yGridStart), Vector3(x, gridHeight, yGridEnd), Vector4(0.5, 0.5, 0.5, 0.5));
            }
        }
    }

    float startZ = std::floor(yGridStart / spacing) * spacing;
    for (float z = startZ; z <= yGridEnd; z += spacing){
        int iz = (int)std::round(z / spacing);
        if (iz == 0){
            lines->addLine(Vector3(xGridStart, gridHeight, z), Vector3(xGridEnd, gridHeight, z), Vector4(1.0, 0.5, 0.5, 1.0));
        }else{
            if (iz % majorEvery == 0){
                lines->addLine(Vector3(xGridStart, gridHeight, z), Vector3(xGridEnd, gridHeight, z), Vector4(0.5, 0.5, 0.5, 1.0));
            }else{
                lines->addLine(Vector3(xGridStart, gridHeight, z), Vector3(xGridEnd, gridHeight, z), Vector4(0.5, 0.5, 0.5, 0.5));
            }
        }
    }
    lines->addLine(Vector3(0, -gridSize, 0), Vector3(0, gridSize, 0), Vector4(0.5, 1.0, 0.5, 1.0));
}

bool editor::SceneRender3D::instanciateLightObject(Entity entity){
    if (lightObjects.find(entity) == lightObjects.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        lightObjects[entity].icon = new Sprite(scene);
        lightObjects[entity].lines = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateCameraObject(Entity entity){
    if (cameraObjects.find(entity) == cameraObjects.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        cameraObjects[entity].icon = new Sprite(scene);
        cameraObjects[entity].lines = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateSoundObject(Entity entity){
    if (soundObjects.find(entity) == soundObjects.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        soundObjects[entity].icon = new Sprite(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateBodyObject(Entity entity){
    if (bodyObjects.find(entity) == bodyObjects.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        bodyObjects[entity].lines = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateJointObject(Entity entity){
    if (jointLines.find(entity) == jointLines.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        jointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateLinePointLines(Entity entity){
    if (linePointLines.find(entity) == linePointLines.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        linePointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

bool editor::SceneRender3D::instanciateReflectionProbeLines(Entity entity){
    if (reflectionProbeLines.find(entity) == reflectionProbeLines.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        reflectionProbeLines[entity] = new Lines(scene);
        return true;
    }
    return false;
}

void editor::SceneRender3D::createOrUpdateReflectionProbeLines(Entity entity, const Transform& transform, const ReflectionProbeComponent& probe, bool visible){
    Lines* probeLines = reflectionProbeLines[entity];
    probeLines->clearLines();
    probeLines->setVisible(transform.visibleInHierarchy && visible);
    if (!transform.visibleInHierarchy || !visible) return;

    Vector3 center = transform.modelMatrix * probe.boxOffset;
    Vector3 half(
        std::fabs(probe.boxSize.x * transform.worldScale.x) * 0.5f,
        std::fabs(probe.boxSize.y * transform.worldScale.y) * 0.5f,
        std::fabs(probe.boxSize.z * transform.worldScale.z) * 0.5f);
    Vector3 mn = center - half;
    Vector3 mx = center + half;
    Vector3 p[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
    };
    const Vector4 color = probe.mode == ReflectionProbeMode::DYNAMIC
        ? Vector4(0.8f, 0.35f, 1.0f, 1.0f)
        : Vector4(0.2f, 0.8f, 1.0f, 1.0f);
    const int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };
    for (const auto& edge : edges) probeLines->addLine(p[edge[0]], p[edge[1]], color);

    float marker = std::max(0.05f, std::min(half.x, std::min(half.y, half.z)) * 0.08f);
    probeLines->addLine(center - Vector3(marker, 0, 0), center + Vector3(marker, 0, 0), color);
    probeLines->addLine(center - Vector3(0, marker, 0), center + Vector3(0, marker, 0), color);
    probeLines->addLine(center - Vector3(0, 0, marker), center + Vector3(0, 0, marker), color);

    // boxOffset moves only the influence volume. When its center differs from
    // the actual capture point, show the entity origin as a smaller gold marker.
    Vector3 capturePosition = transform.worldPosition;
    if ((capturePosition - center).length() > 0.0001f){
        float captureMarker = marker * 0.65f;
        const Vector4 captureColor(1.0f, 0.72f, 0.15f, 1.0f);
        probeLines->addLine(capturePosition - Vector3(captureMarker, 0, 0), capturePosition + Vector3(captureMarker, 0, 0), captureColor);
        probeLines->addLine(capturePosition - Vector3(0, captureMarker, 0), capturePosition + Vector3(0, captureMarker, 0), captureColor);
        probeLines->addLine(capturePosition - Vector3(0, 0, captureMarker), capturePosition + Vector3(0, 0, captureMarker), captureColor);
    }
}

bool editor::SceneRender3D::instanciateWaterExclusionLines(Entity entity){
    if (waterExclusionLines.find(entity) == waterExclusionLines.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        waterExclusionLines[entity] = new Lines(scene);
        return true;
    }
    return false;
}

void editor::SceneRender3D::createOrUpdateWaterExclusionLines(Entity entity, const Transform& transform, const WaterExclusionComponent& exclusion, bool visible){
    Lines* exclusionLines = waterExclusionLines[entity];
    exclusionLines->clearLines();
    exclusionLines->setVisible(transform.visibleInHierarchy && visible);
    if (!transform.visibleInHierarchy || !visible) return;

    const Vector4 color(1.0f, 0.6f, 0.2f, 1.0f);
    auto addLine = [&](const Vector3& a, const Vector3& b) {
        exclusionLines->addLine(transform.modelMatrix * a, transform.modelMatrix * b, color);
    };

    if (exclusion.shape == WaterExclusionShape::HULL) {
        buildConvexHullEdges(exclusion.hullPoints, addLine);
        return;
    }

    const Vector3 half = exclusion.size * 0.5f;
    const Vector3& c = exclusion.center;
    if (exclusion.shape == WaterExclusionShape::SPHERE) {
        const int segments = 48;
        for (int i = 0; i < segments; i++) {
            const float a0 = 2.0f * M_PI * i / segments;
            const float a1 = 2.0f * M_PI * (i + 1) / segments;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            addLine(c + Vector3(c0 * half.x, s0 * half.y, 0), c + Vector3(c1 * half.x, s1 * half.y, 0));
            addLine(c + Vector3(c0 * half.x, 0, s0 * half.z), c + Vector3(c1 * half.x, 0, s1 * half.z));
            addLine(c + Vector3(0, c0 * half.y, s0 * half.z), c + Vector3(0, c1 * half.y, s1 * half.z));
        }
        return;
    }

    Vector3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = c + Vector3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
    }
    const int edges[12][2] = {
        {0,1}, {2,3}, {4,5}, {6,7}, {0,2}, {1,3}, {4,6}, {5,7}, {0,4}, {1,5}, {2,6}, {3,7}
    };
    for (const auto& edge : edges) addLine(p[edge[0]], p[edge[1]]);
}

// endpoint handles for a selected Lines entity, drawn as small 3-axis crosses
// (orientation-independent in 3D); the sub-selected endpoint is drawn bigger
// and in the selection color
void editor::SceneRender3D::createOrUpdateLinePointLines(Entity entity, const Transform& transform, const LinesComponent& lines, bool visible){
    Lines* handlesObj = linePointLines[entity];

    handlesObj->clearLines();
    handlesObj->setVisible(transform.visibleInHierarchy && visible);

    if (!transform.visibleInHierarchy || !visible){
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
            const Vector4& color = pointSelected ? selectedHandleColor : handleColor;
            float halfSize = getPointHandleHalfSize(worldPoint) * (pointSelected ? 0.75f : 0.5f);

            handlesObj->addLine(worldPoint - Vector3(halfSize, 0, 0), worldPoint + Vector3(halfSize, 0, 0), color);
            handlesObj->addLine(worldPoint - Vector3(0, halfSize, 0), worldPoint + Vector3(0, halfSize, 0), color);
            handlesObj->addLine(worldPoint - Vector3(0, 0, halfSize), worldPoint + Vector3(0, 0, halfSize), color);
        }
    }
}

bool editor::SceneRender3D::instanciatePolygonPointLines(Entity entity){
    if (polygonPointLines.find(entity) == polygonPointLines.end()) {
        ScopedDefaultEntityPool sys(*scene, EntityPool::System);
        polygonPointLines[entity] = new Lines(scene);

        return true;
    }

    return false;
}

// vertex handles for a selected Polygon / MeshPolygon entity, drawn as small
// 3-axis crosses; the sub-selected vertex is drawn bigger and in the selection color
void editor::SceneRender3D::createOrUpdatePolygonPointLines(Entity entity, const Transform& transform, const std::vector<PolygonPoint>& points, bool isMesh, bool visible){
    Lines* handlesObj = polygonPointLines[entity];

    handlesObj->clearLines();
    handlesObj->setVisible(transform.visibleInHierarchy && visible);

    if (!transform.visibleInHierarchy || !visible){
        return;
    }

    const Vector4 handleColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 selectedHandleColor(1.0f, 0.6f, 0.0f, 1.0f);
    bool hasPointSelection = (getSelectedPolygonPointEntity() == entity && isSelectedPolygonPointMesh() == isMesh);

    for (size_t i = 0; i < points.size(); i++){
        Vector3 worldPoint = transform.modelMatrix * points[i].position;

        bool pointSelected = hasPointSelection && ((int)i == getSelectedPolygonPointIndex());
        const Vector4& color = pointSelected ? selectedHandleColor : handleColor;
        float halfSize = getPointHandleHalfSize(worldPoint) * (pointSelected ? 0.75f : 0.5f);

        handlesObj->addLine(worldPoint - Vector3(halfSize, 0, 0), worldPoint + Vector3(halfSize, 0, 0), color);
        handlesObj->addLine(worldPoint - Vector3(0, halfSize, 0), worldPoint + Vector3(0, halfSize, 0), color);
        handlesObj->addLine(worldPoint - Vector3(0, 0, halfSize), worldPoint + Vector3(0, 0, halfSize), color);
    }
}

bool editor::SceneRender3D::instanciateTrackLines(Entity entity){
    if (trackLines.find(entity) == trackLines.end()) {
        // tools-layer scene so the path stays visible on top of scene geometry
        // (waypoints usually sit exactly on floors/meshes), like bone lines
        ScopedDefaultEntityPool sys(*toolslayer.getScene(), EntityPool::System);
        trackLines[entity] = new Lines(toolslayer.getScene());

        return true;
    }

    return false;
}

// path of a selected TranslateTracks entity: a polyline through its keyframe
// values (in the action target's parent space) with small 3-axis cross handles;
// the sub-selected point is drawn bigger and in the selection color
void editor::SceneRender3D::createOrUpdateTrackLines(Entity entity, const TranslateTracksComponent& tracks, bool visible){
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
        const Vector4& color = pointSelected ? selectedHandleColor : handleColor;
        float halfSize = getPointHandleHalfSize(worldPoint) * (pointSelected ? 0.75f : 0.5f);

        trackObj->addLine(worldPoint - Vector3(halfSize, 0, 0), worldPoint + Vector3(halfSize, 0, 0), color);
        trackObj->addLine(worldPoint - Vector3(0, halfSize, 0), worldPoint + Vector3(0, halfSize, 0), color);
        trackObj->addLine(worldPoint - Vector3(0, 0, halfSize), worldPoint + Vector3(0, 0, halfSize), color);
    }
}

bool editor::SceneRender3D::instanciateBoneLines(Entity entity){
    if (boneLines.find(entity) == boneLines.end()) {
        ScopedDefaultEntityPool sys(*toolslayer.getScene(), EntityPool::System);
        boneLines[entity] = new Lines(toolslayer.getScene());

        return true;
    }

    return false;
}

void editor::SceneRender3D::createOrUpdateBoneLines(Entity entity, const ModelComponent& model, bool visible, bool highlighted){
    Lines* boneLinesObj = boneLines[entity];

    boneLinesObj->clearLines();
    boneLinesObj->setVisible(visible);

    if (!visible){
        return;
    }

    float alpha = highlighted ? 1.0f : 0.5f;
    const Vector4 boneColor(0.9f, 0.9f, 0.2f, alpha);
    const Vector4 tipColor(1.0f, 0.5f, 0.1f, alpha);

    for (const auto& [boneId, boneEntity] : model.bonesIdMapping) {
        Transform* boneTransform = scene->findComponent<Transform>(boneEntity);
        if (!boneTransform || !boneTransform->visibleInHierarchy) continue;

        Vector3 bonePos = boneTransform->worldPosition;

        Entity parentEntity = boneTransform->parent;
        if (parentEntity != NULL_ENTITY) {
            BoneComponent* parentBone = scene->findComponent<BoneComponent>(parentEntity);
            if (parentBone) {
                Transform* parentTransform = scene->findComponent<Transform>(parentEntity);
                if (parentTransform) {
                    Vector3 parentPos = parentTransform->worldPosition;
                    Vector3 dir = bonePos - parentPos;
                    float length = dir.length();

                    if (length < 1e-6f) continue;

                    dir = dir / length;

                    // Build perpendicular basis
                    Vector3 up = (std::abs(dir.y) < 0.99f) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
                    Vector3 right = dir.crossProduct(up).normalized();
                    up = right.crossProduct(dir).normalized();

                    // Wireframe pyramid: base near parent, tip at child
                    float baseOffset = length * 0.15f;
                    float baseSize = length * 0.08f;
                    Vector3 baseCenter = parentPos + dir * baseOffset;

                    Vector3 b0 = baseCenter + (right + up) * baseSize;
                    Vector3 b1 = baseCenter + (right - up) * baseSize;
                    Vector3 b2 = baseCenter + (right * -1.0f - up) * baseSize;
                    Vector3 b3 = baseCenter + (up - right) * baseSize;

                    // Base square
                    boneLinesObj->addLine(b0, b1, boneColor);
                    boneLinesObj->addLine(b1, b2, boneColor);
                    boneLinesObj->addLine(b2, b3, boneColor);
                    boneLinesObj->addLine(b3, b0, boneColor);

                    // Lines from parent to base
                    boneLinesObj->addLine(parentPos, b0, tipColor);
                    boneLinesObj->addLine(parentPos, b1, tipColor);
                    boneLinesObj->addLine(parentPos, b2, tipColor);
                    boneLinesObj->addLine(parentPos, b3, tipColor);

                    // Lines from base to child tip
                    boneLinesObj->addLine(b0, bonePos, boneColor);
                    boneLinesObj->addLine(b1, bonePos, boneColor);
                    boneLinesObj->addLine(b2, bonePos, boneColor);
                    boneLinesObj->addLine(b3, bonePos, boneColor);
                }
            }
        } else {
            // Root bone: draw a small octahedron marker
            float markSize = 0.03f;
            boneLinesObj->addLine(bonePos + Vector3(0, markSize, 0), bonePos + Vector3(markSize, 0, 0), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(markSize, 0, 0), bonePos + Vector3(0, -markSize, 0), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(0, -markSize, 0), bonePos + Vector3(-markSize, 0, 0), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(-markSize, 0, 0), bonePos + Vector3(0, markSize, 0), tipColor);

            boneLinesObj->addLine(bonePos + Vector3(0, markSize, 0), bonePos + Vector3(0, 0, markSize), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(0, 0, markSize), bonePos + Vector3(0, -markSize, 0), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(0, -markSize, 0), bonePos + Vector3(0, 0, -markSize), tipColor);
            boneLinesObj->addLine(bonePos + Vector3(0, 0, -markSize), bonePos + Vector3(0, markSize, 0), tipColor);
        }
    }
}

void editor::SceneRender3D::createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body3DComponent& body, bool visible, bool highlighted) {
    BodyObjects& bo = bodyObjects[entity];

    bo.lines->clearLines();
    bo.lines->setVisible(transform.visibleInHierarchy && visible);

    if (!transform.visibleInHierarchy || !visible || body.numShapes == 0) {
        return;
    }

    float alpha = highlighted ? 1.0f : 0.35f;
    const Vector4 bodyColor(0.2f, 0.95f, 0.95f, alpha);
    // must match how PhysicsSystem sizes the Jolt shape, parent scale included
    const Vector3 entityScale = transform.worldScale;
    const Vector3 absScale(std::fabs(entityScale.x), std::fabs(entityScale.y), std::fabs(entityScale.z));
    const Matrix4 bodyMatrix = Matrix4::translateMatrix(transform.worldPosition) * transform.worldRotation.getRotationMatrix();

    auto maxScaleXZ = [&](){
        return std::max(absScale.x, absScale.z);
    };

    auto maxScaleXYZ = [&](){
        return std::max(absScale.x, std::max(absScale.y, absScale.z));
    };

    auto addTransformedLine = [&](const Matrix4& shapeMatrix, const Vector3& from, const Vector3& to) {
        Vector3 worldFrom = bodyMatrix * (shapeMatrix * from);
        Vector3 worldTo = bodyMatrix * (shapeMatrix * to);
        bo.lines->addLine(worldFrom, worldTo, bodyColor);
    };

    auto addBox = [&](const Matrix4& shapeMatrix, const Vector3& minPt, const Vector3& maxPt) {
        Vector3 p000(minPt.x, minPt.y, minPt.z);
        Vector3 p100(maxPt.x, minPt.y, minPt.z);
        Vector3 p110(maxPt.x, maxPt.y, minPt.z);
        Vector3 p010(minPt.x, maxPt.y, minPt.z);

        Vector3 p001(minPt.x, minPt.y, maxPt.z);
        Vector3 p101(maxPt.x, minPt.y, maxPt.z);
        Vector3 p111(maxPt.x, maxPt.y, maxPt.z);
        Vector3 p011(minPt.x, maxPt.y, maxPt.z);

        addTransformedLine(shapeMatrix, p000, p100);
        addTransformedLine(shapeMatrix, p100, p110);
        addTransformedLine(shapeMatrix, p110, p010);
        addTransformedLine(shapeMatrix, p010, p000);

        addTransformedLine(shapeMatrix, p001, p101);
        addTransformedLine(shapeMatrix, p101, p111);
        addTransformedLine(shapeMatrix, p111, p011);
        addTransformedLine(shapeMatrix, p011, p001);

        addTransformedLine(shapeMatrix, p000, p001);
        addTransformedLine(shapeMatrix, p100, p101);
        addTransformedLine(shapeMatrix, p110, p111);
        addTransformedLine(shapeMatrix, p010, p011);
    };

    auto addBoxRings = [&](const Matrix4& shapeMatrix, const Vector3& minPt, const Vector3& maxPt) {
        const int ringCount = 3;
        for (int r = 1; r <= ringCount; r++) {
            float t = (float)r / (float)(ringCount + 1);
            float y = minPt.y + (maxPt.y - minPt.y) * t;

            Vector3 p0(minPt.x, y, minPt.z);
            Vector3 p1(maxPt.x, y, minPt.z);
            Vector3 p2(maxPt.x, y, maxPt.z);
            Vector3 p3(minPt.x, y, maxPt.z);

            addTransformedLine(shapeMatrix, p0, p1);
            addTransformedLine(shapeMatrix, p1, p2);
            addTransformedLine(shapeMatrix, p2, p3);
            addTransformedLine(shapeMatrix, p3, p0);
        }
    };

    auto addBoxVerticals = [&](const Matrix4& shapeMatrix, const Vector3& minPt, const Vector3& maxPt) {
        const int verticalCount = 3;

        for (int v = 1; v <= verticalCount; v++) {
            float t = (float)v / (float)(verticalCount + 1);

            float x = minPt.x + (maxPt.x - minPt.x) * t;
            float z = minPt.z + (maxPt.z - minPt.z) * t;

            addTransformedLine(shapeMatrix, Vector3(x, minPt.y, minPt.z), Vector3(x, maxPt.y, minPt.z));
            addTransformedLine(shapeMatrix, Vector3(x, minPt.y, maxPt.z), Vector3(x, maxPt.y, maxPt.z));

            addTransformedLine(shapeMatrix, Vector3(minPt.x, minPt.y, z), Vector3(minPt.x, maxPt.y, z));
            addTransformedLine(shapeMatrix, Vector3(maxPt.x, minPt.y, z), Vector3(maxPt.x, maxPt.y, z));
        }
    };

    auto addBoxCaps = [&](const Matrix4& shapeMatrix, const Vector3& minPt, const Vector3& maxPt) {
        const int capLineCount = 3;

        for (int c = 1; c <= capLineCount; c++) {
            float t = (float)c / (float)(capLineCount + 1);

            float x = minPt.x + (maxPt.x - minPt.x) * t;
            float z = minPt.z + (maxPt.z - minPt.z) * t;

            addTransformedLine(shapeMatrix, Vector3(x, minPt.y, minPt.z), Vector3(x, minPt.y, maxPt.z));
            addTransformedLine(shapeMatrix, Vector3(x, maxPt.y, minPt.z), Vector3(x, maxPt.y, maxPt.z));

            addTransformedLine(shapeMatrix, Vector3(minPt.x, minPt.y, z), Vector3(maxPt.x, minPt.y, z));
            addTransformedLine(shapeMatrix, Vector3(minPt.x, maxPt.y, z), Vector3(maxPt.x, maxPt.y, z));
        }
    };

    auto addCircle = [&](const Matrix4& shapeMatrix, const Vector3& center, const Vector3& axisA, const Vector3& axisB, float radius, int segments) {
        if (radius <= 0.0f) {
            return;
        }

        for (int i = 0; i < segments; i++) {
            float a0 = (2.0f * M_PI * i) / segments;
            float a1 = (2.0f * M_PI * (i + 1)) / segments;

            Vector3 p0 = center + axisA * (std::cos(a0) * radius) + axisB * (std::sin(a0) * radius);
            Vector3 p1 = center + axisA * (std::cos(a1) * radius) + axisB * (std::sin(a1) * radius);

            addTransformedLine(shapeMatrix, p0, p1);
        }
    };

    auto addHemisphere = [&](const Matrix4& shapeMatrix, const Vector3& center, float radius, bool topHemisphere) {
        if (radius <= 0.0f) {
            return;
        }

        const int elevationSegments = 6;
        const int azimuthSegments = 12;
        const float ySign = topHemisphere ? 1.0f : -1.0f;

        for (int a = 0; a < azimuthSegments; a++) {
            float az = (2.0f * M_PI * a) / azimuthSegments;
            Vector3 dirXZ(std::cos(az), 0.0f, std::sin(az));

            for (int e = 0; e < elevationSegments; e++) {
                float phi0 = (0.5f * M_PI * e) / elevationSegments;
                float phi1 = (0.5f * M_PI * (e + 1)) / elevationSegments;

                Vector3 p0 = center + dirXZ * (std::cos(phi0) * radius) + Vector3(0.0f, ySign * std::sin(phi0) * radius, 0.0f);
                Vector3 p1 = center + dirXZ * (std::cos(phi1) * radius) + Vector3(0.0f, ySign * std::sin(phi1) * radius, 0.0f);

                addTransformedLine(shapeMatrix, p0, p1);
            }
        }

        const int ringCount = 3;
        for (int r = 1; r <= ringCount; r++) {
            float phi = (0.5f * M_PI * r) / (ringCount + 1);
            float ringRadius = std::cos(phi) * radius;
            float y = ySign * std::sin(phi) * radius;

            addCircle(shapeMatrix, center + Vector3(0.0f, y, 0.0f), Vector3(1, 0, 0), Vector3(0, 0, 1), ringRadius, 18);
        }
    };

    auto addCapsuleMiddle = [&](const Matrix4& shapeMatrix, float halfHeight, float topRadius, float bottomRadius) {
        const int azimuthSegments = 12;
        for (int a = 0; a < azimuthSegments; a++) {
            float az = (2.0f * M_PI * a) / azimuthSegments;
            Vector3 dirXZ(std::cos(az), 0.0f, std::sin(az));

            Vector3 topPoint = Vector3(0.0f, halfHeight, 0.0f) + dirXZ * topRadius;
            Vector3 bottomPoint = Vector3(0.0f, -halfHeight, 0.0f) + dirXZ * bottomRadius;
            addTransformedLine(shapeMatrix, topPoint, bottomPoint);
        }
    };

    auto addCylinderRings = [&](const Matrix4& shapeMatrix, float halfHeight, float radius) {
        const int ringCount = 3;
        for (int r = 1; r <= ringCount; r++) {
            float t = (float)r / (float)(ringCount + 1);
            float y = halfHeight - (2.0f * halfHeight * t);
            addCircle(shapeMatrix, Vector3(0.0f, y, 0.0f), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 18);
        }
    };

    auto addSpherePattern = [&](const Matrix4& shapeMatrix, float radius) {
        if (radius <= 0.0f) {
            return;
        }

        const int azimuthSegments = 12;
        const int elevationSegments = 12;

        for (int a = 0; a < azimuthSegments; a++) {
            float az = (2.0f * M_PI * a) / azimuthSegments;
            Vector3 dirXZ(std::cos(az), 0.0f, std::sin(az));

            for (int e = 0; e < elevationSegments; e++) {
                float phi0 = -0.5f * M_PI + (M_PI * e) / elevationSegments;
                float phi1 = -0.5f * M_PI + (M_PI * (e + 1)) / elevationSegments;

                Vector3 p0 = dirXZ * (std::cos(phi0) * radius) + Vector3(0.0f, std::sin(phi0) * radius, 0.0f);
                Vector3 p1 = dirXZ * (std::cos(phi1) * radius) + Vector3(0.0f, std::sin(phi1) * radius, 0.0f);

                addTransformedLine(shapeMatrix, p0, p1);
            }
        }

        const int latitudeRings = 5;
        for (int r = 1; r <= latitudeRings; r++) {
            float phi = -0.5f * M_PI + (M_PI * r) / (latitudeRings + 1);
            float ringRadius = std::cos(phi) * radius;
            float y = std::sin(phi) * radius;

            addCircle(shapeMatrix, Vector3(0.0f, y, 0.0f), Vector3(1, 0, 0), Vector3(0, 0, 1), ringRadius, 20);
        }
    };

    // An empty name keeps the buffer already in view; an unknown one returns null so the
    // attribute is dropped, since its offset only means anything against its own buffer.
    auto resolveNamedBuffer = [](const std::map<std::string, Buffer*>& buffers, const std::string& bufferName, Buffer* current) -> Buffer* {
        if (bufferName.empty()) {
            return current;
        }

        auto it = buffers.find(bufferName);
        return it != buffers.end() ? it->second : nullptr;
    };

    // A multi-node glTF root holds no geometry of its own, so a shape sourced from it
    // reads the model's mesh-node children instead.
    auto shapeSourceEntities = [&](const Shape3D& shapeData) {
        std::vector<Entity> sources;
        Entity meshEntity = shapeData.sourceEntity == NULL_ENTITY ? entity : shapeData.sourceEntity;

        MeshComponent* mesh = scene->findComponent<MeshComponent>(meshEntity);
        if (mesh && mesh->numSubmeshes > 0) {
            sources.push_back(meshEntity);
            return sources;
        }

        if (ModelComponent* model = scene->findComponent<ModelComponent>(meshEntity)) {
            for (auto const& node : model->meshNodesMapping) {
                MeshComponent* nodeMesh = scene->findComponent<MeshComponent>(node.second);
                if (nodeMesh && nodeMesh->numSubmeshes > 0) {
                    sources.push_back(node.second);
                }
            }
        }

        return sources;
    };

    // Source geometry through its world matrix and back into the body's local
    // (rotation-free, world-scaled) space, the same mapping PhysicsSystem applies.
    auto sourceToBodyMatrix = [&](Entity sourceEntity) -> Matrix4 {
        Transform* sourceTransform = scene->findComponent<Transform>(sourceEntity);
        if (sourceEntity == entity || !sourceTransform) {
            return Matrix4::scaleMatrix(sourceTransform ? sourceTransform->worldScale : Vector3::UNIT_SCALE);
        }

        return transform.worldRotation.inverse().getRotationMatrix() *
               Matrix4::translateMatrix(-transform.worldPosition) *
               sourceTransform->modelMatrix;
    };

    auto forEachSourceSubmesh = [&](const std::vector<Entity>& sources, auto&& callback) {
        for (Entity sourceEntity : sources) {
            MeshComponent* mesh = scene->findComponent<MeshComponent>(sourceEntity);
            if (!mesh) continue;

            std::map<std::string, Buffer*> buffers;
            if (mesh->buffer.getSize() > 0) buffers["vertices"] = &mesh->buffer;
            if (mesh->indices.getSize() > 0) buffers["indices"] = &mesh->indices;
            for (int bufferIndex = 0; bufferIndex < mesh->numExternalBuffers; bufferIndex++) {
                buffers[mesh->eBuffers[bufferIndex].getName()] = &mesh->eBuffers[bufferIndex];
            }

            Buffer* indexBuffer = nullptr;
            Attribute indexAttr;
            Buffer* vertexBuffer = nullptr;
            Attribute vertexAttr;

            for (auto const& buf : buffers) {
                if (buf.second->getAttribute(AttributeType::INDEX)) {
                    indexBuffer = buf.second;
                    indexAttr = *buf.second->getAttribute(AttributeType::INDEX);
                }
                if (buf.second->getAttribute(AttributeType::POSITION)) {
                    vertexBuffer = buf.second;
                    vertexAttr = *buf.second->getAttribute(AttributeType::POSITION);
                }
            }

            const Matrix4 toBody = sourceToBodyMatrix(sourceEntity);

            for (size_t submeshIndex = 0; submeshIndex < mesh->numSubmeshes; submeshIndex++) {
                for (auto const& attr : mesh->submeshes[submeshIndex].attributes) {
                    if (attr.first == AttributeType::INDEX) {
                        if (Buffer* resolved = resolveNamedBuffer(buffers, attr.second.getBufferName(), indexBuffer)) {
                            indexBuffer = resolved;
                            indexAttr = attr.second;
                        }
                    }
                    if (attr.first == AttributeType::POSITION) {
                        if (Buffer* resolved = resolveNamedBuffer(buffers, attr.second.getBufferName(), vertexBuffer)) {
                            vertexBuffer = resolved;
                            vertexAttr = attr.second;
                        }
                    }
                }

                callback(vertexBuffer, vertexAttr, indexBuffer, indexAttr, toBody);
            }
        }
    };

    // Placement comes from the local chain up to the body, not from the mapping matrix:
    // the two agree, but the matrix jitters in its low bits when the body itself moves.
    auto sourceSignature = [&](const Shape3D& shapeData, const std::vector<Entity>& sources) {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](const void* data, size_t size) {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            for (size_t byteIndex = 0; byteIndex < size; byteIndex++) {
                hash ^= bytes[byteIndex];
                hash *= 1099511628211ull;
            }
        };

        // One slot serves every shape type at this index
        int shapeType = int(shapeData.type);
        int shapeSource = int(shapeData.source);
        mix(&shapeType, sizeof(shapeType));
        mix(&shapeSource, sizeof(shapeSource));
        mix(&transform.worldScale, sizeof(Vector3));

        for (Entity sourceEntity : sources) {
            Entity current = sourceEntity;
            size_t hops = 0;

            while (current != NULL_ENTITY && current != entity && hops <= kMaxSourceParentHops) {
                Transform* sourceTransform = scene->findComponent<Transform>(current);
                if (!sourceTransform) break;

                mix(&sourceTransform->position, sizeof(Vector3));
                mix(&sourceTransform->rotation, sizeof(Quaternion));
                mix(&sourceTransform->scale, sizeof(Vector3));

                current = sourceTransform->parent;
                hops++;
            }

            // Outside the body's subtree there is no chain to walk, so quantize instead
            if (current != entity) {
                const Matrix4 toBody = sourceToBodyMatrix(sourceEntity);
                for (int element = 0; element < 16; element++) {
                    int64_t quantized = std::llround(double(static_cast<const float*>(toBody)[element]) * kHullQuantizeScale);
                    mix(&quantized, sizeof(quantized));
                }
            }
        }

        forEachSourceSubmesh(sources, [&](Buffer* vbuf, Attribute vattr, Buffer* ibuf, Attribute iattr, const Matrix4&) {
            const void* vdata = vbuf ? static_cast<const void*>(vbuf->getData()) : nullptr;
            const void* idata = ibuf ? static_cast<const void*>(ibuf->getData()) : nullptr;
            size_t vertexCount = vbuf ? vattr.getCount() : 0;
            size_t indexCount = ibuf ? iattr.getCount() : 0;

            mix(&vdata, sizeof(vdata));
            mix(&idata, sizeof(idata));
            mix(&vertexCount, sizeof(vertexCount));
            mix(&indexCount, sizeof(indexCount));
        });

        return hash;
    };

    auto shapeEdgeCache = [&](size_t shapeIndex) -> BodyObjects::ShapeEdgeCache& {
        if (bo.shapeEdgeCaches.size() != body.numShapes) {
            bo.shapeEdgeCaches.assign(body.numShapes, BodyObjects::ShapeEdgeCache{});
        }
        return bo.shapeEdgeCaches[shapeIndex];
    };

    for (size_t i = 0; i < body.numShapes; i++) {
        const Shape3D& shapeData = body.shapes[i];

        Matrix4 shapeMatrix = Matrix4::translateMatrix(shapeData.position * entityScale) * shapeData.rotation.getRotationMatrix();

        if (shapeData.type == Shape3DType::BOX) {
            Vector3 halfSize(shapeData.width * absScale.x * 0.5f, shapeData.height * absScale.y * 0.5f, shapeData.depth * absScale.z * 0.5f);
            addBox(shapeMatrix, -halfSize, halfSize);
            addBoxRings(shapeMatrix, -halfSize, halfSize);
            addBoxVerticals(shapeMatrix, -halfSize, halfSize);
            addBoxCaps(shapeMatrix, -halfSize, halfSize);
        } else if (shapeData.type == Shape3DType::SPHERE) {
            addSpherePattern(shapeMatrix, shapeData.radius * maxScaleXYZ());
        } else if (shapeData.type == Shape3DType::CAPSULE) {
            const float halfHeight = shapeData.halfHeight * absScale.y;
            const float radius = shapeData.radius * maxScaleXZ();
            Vector3 top(0, halfHeight, 0);
            Vector3 bottom(0, -halfHeight, 0);

            addCircle(shapeMatrix, top, Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCircle(shapeMatrix, bottom, Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, radius, radius);

            addHemisphere(shapeMatrix, top, radius, true);
            addHemisphere(shapeMatrix, bottom, radius, false);
        } else if (shapeData.type == Shape3DType::TAPERED_CAPSULE) {
            const float halfHeight = shapeData.halfHeight * absScale.y;
            const float radiusScale = maxScaleXZ();
            const float topRadius = shapeData.topRadius * radiusScale;
            const float bottomRadius = shapeData.bottomRadius * radiusScale;

            addCircle(shapeMatrix, Vector3(0, halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), topRadius, 20);
            addCircle(shapeMatrix, Vector3(0, -halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), bottomRadius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, topRadius, bottomRadius);

            addHemisphere(shapeMatrix, Vector3(0, halfHeight, 0), topRadius, true);
            addHemisphere(shapeMatrix, Vector3(0, -halfHeight, 0), bottomRadius, false);
        } else if (shapeData.type == Shape3DType::CYLINDER) {
            const float halfHeight = shapeData.halfHeight * absScale.y;
            const float radius = shapeData.radius * maxScaleXZ();

            addCircle(shapeMatrix, Vector3(0, halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCircle(shapeMatrix, Vector3(0, -halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, radius, radius);
            addCylinderRings(shapeMatrix, halfHeight, radius);
        } else if (shapeData.type == Shape3DType::CONVEX_HULL) {
            if (shapeData.source == Shape3DSource::ENTITY_MESH) {
                // Walking every source vertex and rebuilding the hull is far too heavy
                // to repeat per frame
                BodyObjects::ShapeEdgeCache& cache = shapeEdgeCache(i);
                std::vector<Entity> sources = shapeSourceEntities(shapeData);
                uint64_t signature = sourceSignature(shapeData, sources);

                if (!cache.valid || cache.signature != signature) {
                    cache.valid = true;
                    cache.signature = signature;
                    cache.edges.clear();

                    std::vector<Vector3> hullPoints;
                    std::unordered_set<uint64_t> seen;
                    forEachSourceSubmesh(sources, [&](Buffer* vbuf, Attribute vattr, Buffer*, Attribute, const Matrix4& toBody) {
                        if (!vbuf) return;
                        int count = int(vattr.getCount());
                        for (int v = 0; v < count; v++) {
                            pushUniqueHullPoint(hullPoints, seen, toBody * vbuf->getVector3(&vattr, v));
                        }
                    });

                    buildConvexHullEdges(hullPoints, [&](const Vector3& a, const Vector3& b) {
                        cache.edges.emplace_back(a, b);
                    });
                }

                for (const auto& e : cache.edges) {
                    addTransformedLine(shapeMatrix, e.first, e.second);
                }
            } else if (shapeData.numVertices >= 2) {
                // Raw vertices are inspector-editable and bounded, so build them inline.
                std::vector<Vector3> hullPoints;
                std::unordered_set<uint64_t> seen;
                hullPoints.reserve(shapeData.numVertices);
                for (size_t vertexIndex = 0; vertexIndex < shapeData.numVertices; vertexIndex++) {
                    pushUniqueHullPoint(hullPoints, seen, shapeData.vertices[vertexIndex] * entityScale);
                }

                buildConvexHullEdges(hullPoints, [&](const Vector3& a, const Vector3& b) {
                    addTransformedLine(shapeMatrix, a, b);
                });
            }
        } else if (shapeData.type == Shape3DType::MESH) {
            if (shapeData.source == Shape3DSource::ENTITY_MESH) {
                // Cached in body-local space, so a frame only costs two matrix
                // transforms + addLine per edge
                BodyObjects::ShapeEdgeCache& cache = shapeEdgeCache(i);
                std::vector<Entity> sources = shapeSourceEntities(shapeData);
                uint64_t signature = sourceSignature(shapeData, sources);

                if (!cache.valid || cache.signature != signature) {
                    cache.valid = true;
                    cache.signature = signature;
                    cache.edges.clear();

                    // Interior edges belong to 2 triangles, so deduping by index pair
                    // halves the lines. Per submesh: indices are local to their own.
                    std::unordered_set<uint64_t> seenEdges;
                    forEachSourceSubmesh(sources, [&](Buffer* vbuf, Attribute vattr, Buffer* ibuf, Attribute iattr, const Matrix4& toBody) {
                        seenEdges.clear();
                        if (!vbuf) return;
                        uint32_t vcount = uint32_t(vattr.getCount());
                        auto pushEdge = [&](uint32_t a, uint32_t b) {
                            if (a == b || a >= vcount || b >= vcount) return;
                            uint32_t lo = std::min(a, b);
                            uint32_t hi = std::max(a, b);
                            uint64_t key = (uint64_t(lo) << 32) | hi;
                            if (!seenEdges.insert(key).second) return;
                            cache.edges.emplace_back(toBody * vbuf->getVector3(&vattr, lo), toBody * vbuf->getVector3(&vattr, hi));
                        };

                        if (!ibuf) {
                            uint32_t triCount = vcount / 3;
                            for (uint32_t t = 0; t < triCount; t++) {
                                uint32_t a = t * 3, b = a + 1, c = a + 2;
                                pushEdge(a, b);
                                pushEdge(b, c);
                                pushEdge(c, a);
                            }
                            return;
                        }
                        uint32_t triCount = uint32_t(iattr.getCount() / 3);
                        auto readIndex = [&](uint32_t idx) -> uint32_t {
                            if (iattr.getDataType() == AttributeDataType::UNSIGNED_INT) return ibuf->getUInt32(&iattr, idx);
                            if (iattr.getDataType() == AttributeDataType::UNSIGNED_SHORT) return ibuf->getUInt16(&iattr, idx);
                            return UINT32_MAX;
                        };
                        for (uint32_t t = 0; t < triCount; t++) {
                            uint32_t i0 = readIndex(t * 3);
                            uint32_t i1 = readIndex(t * 3 + 1);
                            uint32_t i2 = readIndex(t * 3 + 2);
                            if (i0 == UINT32_MAX || i1 == UINT32_MAX || i2 == UINT32_MAX) continue;
                            pushEdge(i0, i1);
                            pushEdge(i1, i2);
                            pushEdge(i2, i0);
                        }
                    });
                }

                for (const auto& e : cache.edges) {
                    addTransformedLine(shapeMatrix, e.first, e.second);
                }
            } else if (shapeData.numVertices >= 3 && shapeData.numIndices >= 3) {
                int triangles = int(shapeData.numIndices / 3);
                for (int tri = 0; tri < triangles; tri++) {
                    uint16_t i0 = shapeData.indices[tri * 3 + 0];
                    uint16_t i1 = shapeData.indices[tri * 3 + 1];
                    uint16_t i2 = shapeData.indices[tri * 3 + 2];

                    if (i0 < shapeData.numVertices && i1 < shapeData.numVertices && i2 < shapeData.numVertices) {
                        Vector3 p0 = shapeData.vertices[i0] * entityScale;
                        Vector3 p1 = shapeData.vertices[i1] * entityScale;
                        Vector3 p2 = shapeData.vertices[i2] * entityScale;

                        addTransformedLine(shapeMatrix, p0, p1);
                        addTransformedLine(shapeMatrix, p1, p2);
                        addTransformedLine(shapeMatrix, p2, p0);
                    }
                }
            }
        } else if (shapeData.type == Shape3DType::HEIGHTFIELD) {
            Entity terrainEntity = shapeData.sourceEntity == NULL_ENTITY ? entity : shapeData.sourceEntity;
            TerrainComponent* terrain = scene->findComponent<TerrainComponent>(terrainEntity);
            if (terrain) {
                Vector3 minPt(-terrain->terrainSize * 0.5f, 0.0f, -terrain->terrainSize * 0.5f);
                Vector3 maxPt(terrain->terrainSize * 0.5f, terrain->maxHeight, terrain->terrainSize * 0.5f);
                addBox(shapeMatrix, minPt, maxPt);
            }
        }
    }
}

void editor::SceneRender3D::createOrUpdateJointLines(Entity entity, const Joint3DComponent& joint, bool visible, bool highlighted){
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

    float alpha = highlighted ? 1.0f : 0.35f;

    const Vector4 jointColor(0.95f, 0.75f, 0.2f, alpha);
    const Vector4 helperColor(0.85f, 0.85f, 0.85f, 0.8f * alpha);
    const Vector4 axisColor(0.2f, 0.9f, 1.0f, alpha);
    const Vector4 limitColor(1.0f, 0.35f, 0.35f, alpha);

    Vector3 anchorA = getBodyWorldPosition(joint.bodyA);
    Vector3 anchorB = getBodyWorldPosition(joint.bodyB);

    switch (joint.type){
        case Joint3DType::DISTANCE:
            if (!joint.autoAnchors){
                anchorA = joint.anchorA;
                anchorB = joint.anchorB;
            }
            break;
        case Joint3DType::POINT:
        case Joint3DType::HINGE:
        case Joint3DType::CONE:
        case Joint3DType::SWINGTWIST:
            anchorA = anchorB = joint.anchor;
            break;
        case Joint3DType::SIXDOF:
            anchorA = joint.anchorA;
            anchorB = joint.anchorB;
            break;
        case Joint3DType::PRISMATIC:
            anchorA = anchorB = (anchorA + anchorB) * 0.5f;
            break;
        case Joint3DType::FIXED:
        case Joint3DType::PATH:
        case Joint3DType::GEAR:
        case Joint3DType::RACKANDPINON:
        case Joint3DType::PULLEY:
        default:
            break;
    }

    jointLinesObj->addLine(anchorA, anchorB, jointColor);

    float markSize = 0.12f;

    jointLinesObj->addLine(anchorA + Vector3(-markSize, 0.0f, 0.0f), anchorA + Vector3(markSize, 0.0f, 0.0f), helperColor);
    jointLinesObj->addLine(anchorA + Vector3(0.0f, -markSize, 0.0f), anchorA + Vector3(0.0f, markSize, 0.0f), helperColor);
    jointLinesObj->addLine(anchorA + Vector3(0.0f, 0.0f, -markSize), anchorA + Vector3(0.0f, 0.0f, markSize), helperColor);

    jointLinesObj->addLine(anchorB + Vector3(-markSize, 0.0f, 0.0f), anchorB + Vector3(markSize, 0.0f, 0.0f), helperColor);
    jointLinesObj->addLine(anchorB + Vector3(0.0f, -markSize, 0.0f), anchorB + Vector3(0.0f, markSize, 0.0f), helperColor);
    jointLinesObj->addLine(anchorB + Vector3(0.0f, 0.0f, -markSize), anchorB + Vector3(0.0f, 0.0f, markSize), helperColor);

    Vector3 dirAB = anchorB - anchorA;
    float lenAB = dirAB.length();
    Vector3 dirABNorm = lenAB > 0.0001f ? dirAB / lenAB : Vector3(1.0f, 0.0f, 0.0f);

    switch (joint.type){
        case Joint3DType::DISTANCE:{
            // Connection line + anchor crosses already drawn above
            break;
        }
        case Joint3DType::HINGE:{
            Vector3 axis = joint.axis.length() > 0.0001f ? joint.axis.normalized() : dirABNorm;
            Vector3 center = (anchorA + anchorB) * 0.5f;
            LineDrawUtils::addRing3D(jointLinesObj, center, axis, 0.25f, axisColor, 28);
            LineDrawUtils::addRing3D(jointLinesObj, center, axis, 0.33f, helperColor, 28);
            jointLinesObj->addLine(center - axis * 0.35f, center + axis * 0.35f, axisColor);
            break;
        }
        case Joint3DType::PRISMATIC:{
            Vector3 axis = joint.axis.length() > 0.0001f ? joint.axis.normalized() : dirABNorm;
            auto [railAxis, side, up] = LineDrawUtils::makeBasis(axis);
            float limitMin = joint.limitsMin;
            float limitMax = joint.limitsMax;
            if (limitMax < limitMin){
                std::swap(limitMin, limitMax);
            }
            if (limitMin > 0.0f){
                float offset = limitMin;
                limitMin -= offset;
                limitMax -= offset;
            }else if (limitMax < 0.0f){
                float offset = limitMax;
                limitMin -= offset;
                limitMax -= offset;
            }
            if (std::fabs(limitMax - limitMin) < 0.000001f){
                limitMin = -0.4f;
                limitMax = 0.4f;
            }

            Vector3 railStart = anchorA + railAxis * limitMin;
            Vector3 railEnd = anchorA + railAxis * limitMax;

            float railOffset = 0.06f;
            jointLinesObj->addLine(railStart + side * railOffset, railEnd + side * railOffset, axisColor);
            jointLinesObj->addLine(railStart - side * railOffset, railEnd - side * railOffset, axisColor);

            jointLinesObj->addLine(railStart - side * 0.12f, railStart + side * 0.12f, limitColor);
            jointLinesObj->addLine(railEnd - side * 0.12f, railEnd + side * 0.12f, limitColor);

            Vector3 sliderCenter = anchorA;

            LineDrawUtils::addRing3D(jointLinesObj, sliderCenter, railAxis, 0.09f, jointColor, 18);
            jointLinesObj->addLine(sliderCenter - up * 0.1f, sliderCenter + up * 0.1f, helperColor);
            break;
        }
        case Joint3DType::CONE:{
            Vector3 axis = joint.twistAxis.length() > 0.0001f ? joint.twistAxis.normalized() : dirABNorm;
            auto [coneAxis, right, forward] = LineDrawUtils::makeBasis(axis);
            (void)right;
            float coneLen = 0.9f;
            float angle = std::clamp(joint.normalHalfConeAngle, 0.0f, 179.9f) * (M_PI / 180.0f);
            float radius = coneLen * std::tan(angle);
            Vector3 end = anchorA + coneAxis * coneLen;
            LineDrawUtils::addRing3D(jointLinesObj, end, coneAxis, radius, axisColor, 22);
            jointLinesObj->addLine(anchorA, end + forward * radius, helperColor);
            jointLinesObj->addLine(anchorA, end - forward * radius, helperColor);
            break;
        }
        case Joint3DType::SWINGTWIST:{
            Vector3 axis = joint.twistAxis.length() > 0.0001f ? joint.twistAxis.normalized() : (joint.axis.length() > 0.0001f ? joint.axis.normalized() : dirABNorm);
            Vector3 center = (anchorA + anchorB) * 0.5f;
            LineDrawUtils::addRing3D(jointLinesObj, center, axis, 0.28f, axisColor, 24);

            float twistMin = joint.twistMinAngle * (M_PI / 180.0f);
            float twistMax = joint.twistMaxAngle * (M_PI / 180.0f);
            auto [axisN, right, forward] = LineDrawUtils::makeBasis(axis);
            (void)axisN;
            LineDrawUtils::addArc3D(jointLinesObj, center, axis, right, twistMin, twistMax, 0.36f, limitColor, 16);
            jointLinesObj->addLine(center, center + right * 0.36f, helperColor);
            jointLinesObj->addLine(center, center + (right * std::cos(twistMax) + forward * std::sin(twistMax)) * 0.36f, helperColor);
            break;
        }
        case Joint3DType::PATH:{
            if (joint.pathPoints.size() >= 2){
                for (size_t i = 0; i < joint.pathPoints.size() - 1; i++){
                    jointLinesObj->addLine(joint.pathPoints[i], joint.pathPoints[i + 1], axisColor);
                }
                if (joint.isLooping){
                    jointLinesObj->addLine(joint.pathPoints.back(), joint.pathPoints.front(), axisColor);
                }
                LineDrawUtils::addRing3D(jointLinesObj, joint.pathPosition, Vector3(0.0f, 1.0f, 0.0f), 0.08f, jointColor, 16);
            }
            break;
        }
        case Joint3DType::GEAR:
        case Joint3DType::RACKANDPINON:
        case Joint3DType::PULLEY:
        case Joint3DType::SIXDOF:
        case Joint3DType::FIXED:
        case Joint3DType::POINT:
        default:{
            break;
        }
    }

    // Only draw generic axis for types that don't already visualize it
    if (joint.axis.length() > 0.0001f
        && joint.type != Joint3DType::HINGE
        && joint.type != Joint3DType::PRISMATIC
        && joint.type != Joint3DType::CONE){
        Vector3 axis = joint.axis;
        axis.normalize();

        Vector3 center = (anchorA + anchorB) * 0.5f;
        jointLinesObj->addLine(center, center + axis, axisColor);
    }
}

void editor::SceneRender3D::createOrUpdateLightIcon(Entity entity, const Transform& transform, LightType lightType, bool newLight) {
    LightObjects& lo = lightObjects[entity];

    if (newLight) {
        TextureData iconData;
        if (lightType == LightType::DIRECTIONAL) {
            iconData.loadTextureFromMemory(sun_icon_png, sun_icon_png_len);
            lo.icon->setTexture("editor:resources:sun_icon", iconData);
        } else if (lightType == LightType::POINT) {
            iconData.loadTextureFromMemory(bulb_icon_png, bulb_icon_png_len);
            lo.icon->setTexture("editor:resources:bulb_icon", iconData);
        } else if (lightType == LightType::SPOT) {
            iconData.loadTextureFromMemory(spot_icon_png, spot_icon_png_len);
            lo.icon->setTexture("editor:resources:spot_icon", iconData);
        }

        lo.icon->setBillboard(true);
        lo.icon->setSize(128, 128);
        lo.icon->setReceiveLights(false);
        lo.icon->setCastShadows(false);
        lo.icon->setReceiveShadows(false);
        lo.icon->setPivotPreset(PivotPreset::CENTER);
    }

    // Update light icon position
    lo.icon->setPosition(transform.worldPosition);
    lo.icon->setVisible(transform.visibleInHierarchy);
    lo.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));
}

void editor::SceneRender3D::createOrUpdateCameraIcon(Entity entity, const Transform& transform, bool newCamera) {
    CameraObjects& co = cameraObjects[entity];

    if (newCamera) {
        setupCameraIcon(co);
        co.icon->setBillboard(true);
    }

    // Update camera icon position
    co.icon->setPosition(transform.worldPosition);
    co.icon->setVisible(transform.visibleInHierarchy);
    co.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));
}

void editor::SceneRender3D::createOrUpdateSoundIcon(Entity entity, const Transform& transform, bool newSound) {
    SoundObjects& so = soundObjects[entity];

    if (newSound) {
        setupSoundIcon(so);
        so.icon->setBillboard(true);
    }

    so.icon->setPosition(transform.worldPosition);
    so.icon->setVisible(transform.visibleInHierarchy);
    so.icon->setScale(billboardScreenScale(transform.worldPosition, 0.25f));
}

void editor::SceneRender3D::createCameraFrustum(Entity entity, const Transform& transform, const CameraComponent& cameraComponent, bool fixedSizeFrustum, bool isMainCamera) {
    CameraObjects& co = cameraObjects[entity];

    co.lines->setPosition(transform.worldPosition);
    co.lines->setVisible(transform.visibleInHierarchy);

    Quaternion rotation;
    rotation.fromRotationMatrix(cameraComponent.viewMatrix.inverse());
    co.lines->setRotation(rotation);

    updateCameraFrustum(co, cameraComponent, isMainCamera, fixedSizeFrustum);
}


void editor::SceneRender3D::createDirectionalLightArrow(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected) {
    LightObjects& lo = lightObjects[entity];

    lo.lines->setPosition(transform.worldPosition);
    lo.lines->setRotation(transform.worldRotation);
    lo.lines->setVisible(isSelected);

    if (light.direction == Vector3::ZERO){
        return;
    }

    if (lo.type == light.type && lo.direction == light.direction) {
        return;
    }

    lo.type = light.type;
    lo.direction = light.direction;

    lo.lines->clearLines();

    Vector3 position = Vector3(0, 0, 0);  // Start position
    float arrowLength = 4.0f;  // Length of the main arrow shaft
    float arrowHeadLength = 3.6f;  // Length of the arrow head
    float arrowHeadWidth = 1.0f;   // Width of the arrow head

    Vector4 arrowColor = Vector4(1.0, 1.0, 0.0, 1.0); // Yellow color for directional light
    Vector3 direction = light.direction.normalized();

    // Main arrow shaft
    Vector3 endPos = position + direction * arrowLength;
    lo.lines->addLine(position, endPos, arrowColor);

    // Use the same local projection frame as runtime lighting and shadows.
    Vector3 up = light.getLocalSpotProjectionUp();
    Vector3 right = direction.crossProduct(up).normalized();

    // Arrow head base position
    Vector3 arrowHeadBase = endPos - direction * arrowHeadLength;

    // Arrow head vertices (4 points forming a diamond shape)
    Vector3 arrowHead1 = arrowHeadBase + right * arrowHeadWidth;
    Vector3 arrowHead2 = arrowHeadBase + up * arrowHeadWidth;
    Vector3 arrowHead3 = arrowHeadBase - right * arrowHeadWidth;
    Vector3 arrowHead4 = arrowHeadBase - up * arrowHeadWidth;

    // Draw arrow head lines
    lo.lines->addLine(endPos, arrowHead1, arrowColor);
    lo.lines->addLine(endPos, arrowHead2, arrowColor);
    lo.lines->addLine(endPos, arrowHead3, arrowColor);
    lo.lines->addLine(endPos, arrowHead4, arrowColor);

    // Connect arrow head base points to form a diamond
    lo.lines->addLine(arrowHead1, arrowHead2, arrowColor);
    lo.lines->addLine(arrowHead2, arrowHead3, arrowColor);
    lo.lines->addLine(arrowHead3, arrowHead4, arrowColor);
    lo.lines->addLine(arrowHead4, arrowHead1, arrowColor);

    // Optional: Add some additional lines for better visualization
    lo.lines->addLine(arrowHead1, arrowHead3, arrowColor * 0.7f); // Cross lines with slightly dimmer color
    lo.lines->addLine(arrowHead2, arrowHead4, arrowColor * 0.7f);
}

void editor::SceneRender3D::createPointLightSphere(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected) {
    LightObjects& lo = lightObjects[entity];

    lo.lines->setPosition(transform.worldPosition);
    lo.lines->setRotation(transform.worldRotation);
    lo.lines->setVisible(isSelected);

    float range = (light.range > 0.0f) ? light.range : camera->getFarClip();

    if (lo.type == light.type && lo.range == range) {
        return;
    }

    lo.type = light.type;
    lo.range = range;

    lo.lines->clearLines();

    Vector3 position = Vector3(0, 0, 0);  // Start position
    Vector4 sphereColor = Vector4(0.0, 1.0, 1.0, 0.8); // Cyan color for point light

    const int numSegments = 16;
    const float angleStep = 2.0f * M_PI / numSegments;

    // Draw three orthogonal circles to represent the sphere

    // XY plane circle
    for (int i = 0; i < numSegments; i++) {
        float angle1 = i * angleStep;
        float angle2 = ((i + 1) % numSegments) * angleStep;

        Vector3 point1 = position + Vector3(std::cos(angle1) * range, std::sin(angle1) * range, 0);
        Vector3 point2 = position + Vector3(std::cos(angle2) * range, std::sin(angle2) * range, 0);

        lo.lines->addLine(point1, point2, sphereColor);
    }

    // XZ plane circle
    for (int i = 0; i < numSegments; i++) {
        float angle1 = i * angleStep;
        float angle2 = ((i + 1) % numSegments) * angleStep;

        Vector3 point1 = position + Vector3(std::cos(angle1) * range, 0, std::sin(angle1) * range);
        Vector3 point2 = position + Vector3(std::cos(angle2) * range, 0, std::sin(angle2) * range);

        lo.lines->addLine(point1, point2, sphereColor);
    }

    // YZ plane circle
    for (int i = 0; i < numSegments; i++) {
        float angle1 = i * angleStep;
        float angle2 = ((i + 1) % numSegments) * angleStep;

        Vector3 point1 = position + Vector3(0, std::cos(angle1) * range, std::sin(angle1) * range);
        Vector3 point2 = position + Vector3(0, std::cos(angle2) * range, std::sin(angle2) * range);

        lo.lines->addLine(point1, point2, sphereColor);
    }
}

void editor::SceneRender3D::createSpotLightCones(Entity entity, const Transform& transform, const LightComponent& light, bool isSelected) {
    LightObjects& lo = lightObjects[entity];

    lo.lines->setPosition(transform.worldPosition);
    lo.lines->setRotation(transform.worldRotation);
    lo.lines->setVisible(isSelected);

    if (light.direction == Vector3::ZERO){
        return;
    }

    float range = (light.range > 0.0f) ? light.range : camera->getFarClip();

    // if light.range = 0.0 then light.shadowCameraNearFar.y is camera.farClip
    if (lo.type == light.type && 
        lo.innerConeCos == light.innerConeCos && 
        lo.outerConeCos == light.outerConeCos && 
        lo.direction == light.direction && 
        lo.range == range &&
        lo.spotMaskReady == light.spotMaskReady &&
        lo.spotMaskAspect == light.spotMaskAspect) {
        return;
    }

    lo.type = light.type;
    lo.innerConeCos = light.innerConeCos;
    lo.outerConeCos = light.outerConeCos;
    lo.direction = light.direction;
    lo.range = range;
    lo.spotMaskReady = light.spotMaskReady;
    lo.spotMaskAspect = light.spotMaskAspect;

    lo.lines->clearLines();

    Vector3 position = Vector3(0,0,0);  // Start position
    Vector3 direction = light.direction.normalized();

    // Calculate cone radii at the end of the light range
    float innerRadius = range * std::tan(std::acos(light.innerConeCos));
    float outerRadius = range * std::tan(std::acos(light.outerConeCos));

    // Use the same local projection frame as runtime lighting and shadows.
    Vector3 up = light.getLocalSpotProjectionUp();
    Vector3 right = direction.crossProduct(up).normalized();

    // End position of the cone
    Vector3 endPos = position + direction * range;

    const int numSegments = 12;
    const float angleStep = 2.0f * M_PI / numSegments;

    // Colors for inner and outer cones
    Vector4 innerConeColor = Vector4(1.0, 1.0, 0.0, 0.8); // Yellow for inner cone
    Vector4 outerConeColor = Vector4(1.0, 0.5, 0.0, 0.6); // Orange for outer cone

    if (light.spotMaskReady){
        float outerHalfWidth = outerRadius * light.spotMaskAspect;
        Vector3 corners[4] = {
            endPos - right * outerHalfWidth - up * outerRadius,
            endPos + right * outerHalfWidth - up * outerRadius,
            endPos + right * outerHalfWidth + up * outerRadius,
            endPos - right * outerHalfWidth + up * outerRadius
        };

        for (int i = 0; i < 4; i++){
            lo.lines->addLine(position, corners[i], outerConeColor);
            lo.lines->addLine(corners[i], corners[(i + 1) % 4], outerConeColor);
        }
        lo.lines->addLine(position, endPos, Vector4(0.8, 0.8, 0.8, 1.0));
        return;
    }

    // Draw outer cone
    for (int i = 0; i < numSegments; i++) {
        float angle1 = i * angleStep;
        float angle2 = ((i + 1) % numSegments) * angleStep;

        // Calculate points on the outer cone circle
        Vector3 point1 = endPos + (right * std::cos(angle1) + up * std::sin(angle1)) * outerRadius;
        Vector3 point2 = endPos + (right * std::cos(angle2) + up * std::sin(angle2)) * outerRadius;

        // Lines from light position to circle points
        lo.lines->addLine(position, point1, outerConeColor);

        // Circle at the end of the cone
        lo.lines->addLine(point1, point2, outerConeColor);
    }

    // Draw inner cone
    for (int i = 0; i < numSegments; i++) {
        float angle1 = i * angleStep;
        float angle2 = ((i + 1) % numSegments) * angleStep;

        // Calculate points on the inner cone circle
        Vector3 point1 = endPos + (right * std::cos(angle1) + up * std::sin(angle1)) * innerRadius;
        Vector3 point2 = endPos + (right * std::cos(angle2) + up * std::sin(angle2)) * innerRadius;

        // Lines from light position to circle points
        lo.lines->addLine(position, point1, innerConeColor);

        // Circle at the end of the cone
        lo.lines->addLine(point1, point2, innerConeColor);
    }

    // Draw central direction line
    lo.lines->addLine(position, endPos, Vector4(0.8, 0.8, 0.8, 1.0));
}

void editor::SceneRender3D::hideAllGizmos(){
    SceneRender::hideAllGizmos();

    lines->setVisible(false);
    for (auto& pair : lightObjects) {
        pair.second.icon->setVisible(false);
        pair.second.lines->setVisible(false);
    }
    for (auto& pair : cameraObjects) {
        pair.second.icon->setVisible(false);
        pair.second.lines->setVisible(false);
    }
    for (auto& pair : soundObjects) {
        pair.second.icon->setVisible(false);
    }
    for (auto& pair : bodyObjects) {
        pair.second.lines->setVisible(false);
    }
    for (auto& pair : jointLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : boneLines) {
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
    for (auto& pair : reflectionProbeLines) {
        pair.second->setVisible(false);
    }
    for (auto& pair : waterExclusionLines) {
        pair.second->setVisible(false);
    }
}

void editor::SceneRender3D::activate(){
    SceneRender::activate();

    if (!isPreviewCameraActive()){
        Engine::addSceneLayer(viewgizmo.getScene());
    }
}

void editor::SceneRender3D::setOverlayScale(float scale){
    float previous = overlayScale;
    SceneRender::setOverlayScale(scale);
    if (overlayScale == previous){
        return;
    }
    viewgizmo.setFramebufferSize(std::max(128, (int)std::lround(100.0f * overlayScale)));
}

void editor::SceneRender3D::zoomCamera(float amount){
    if (amount == 0.0f){
        return;
    }

    float distance = camera->getDistanceFromTarget();
    float newDistance = std::clamp(distance - amount, MIN_EDITOR_CAMERA_DISTANCE, MAX_EDITOR_CAMERA_DISTANCE);

    camera->zoom(distance - newDistance);
}

float editor::SceneRender3D::getWalkSpeedOffset() const{
    return walkSpeedOffset;
}

void editor::SceneRender3D::setWalkSpeedOffset(float offset){
    walkSpeedOffset = std::clamp(
        offset,
        MIN_EDITOR_WALK_SPEED - DEFAULT_EDITOR_WALK_SPEED,
        MAX_EDITOR_WALK_SPEED - DEFAULT_EDITOR_WALK_SPEED);
}

void editor::SceneRender3D::updateSelLines(std::vector<OBB> obbs){
    Vector4 color = Vector4(1.0, 0.6, 0.0, 1.0);

    if (selLines->getNumLines() != obbs.size() * 12){
        selLines->clearLines();
        for (OBB& obb : obbs){
            selLines->addLine(obb.getCorner(OBB::FAR_LEFT_BOTTOM), obb.getCorner(OBB::FAR_LEFT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::FAR_LEFT_TOP), obb.getCorner(OBB::FAR_RIGHT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::FAR_RIGHT_TOP), obb.getCorner(OBB::FAR_RIGHT_BOTTOM), color);
            selLines->addLine(obb.getCorner(OBB::FAR_RIGHT_BOTTOM), obb.getCorner(OBB::FAR_LEFT_BOTTOM), color);

            selLines->addLine(obb.getCorner(OBB::NEAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_LEFT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), color);
            selLines->addLine(obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM), color);

            selLines->addLine(obb.getCorner(OBB::FAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM), color);
            selLines->addLine(obb.getCorner(OBB::FAR_LEFT_TOP), obb.getCorner(OBB::NEAR_LEFT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::FAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP), color);
            selLines->addLine(obb.getCorner(OBB::FAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), color);
        }
    }else{
        int i = 0;
        for (OBB& obb : obbs){
            selLines->updateLine(i * 12 + 0, obb.getCorner(OBB::FAR_LEFT_BOTTOM), obb.getCorner(OBB::FAR_LEFT_TOP));
            selLines->updateLine(i * 12 + 1, obb.getCorner(OBB::FAR_LEFT_TOP), obb.getCorner(OBB::FAR_RIGHT_TOP));
            selLines->updateLine(i * 12 + 2, obb.getCorner(OBB::FAR_RIGHT_TOP), obb.getCorner(OBB::FAR_RIGHT_BOTTOM));
            selLines->updateLine(i * 12 + 3, obb.getCorner(OBB::FAR_RIGHT_BOTTOM), obb.getCorner(OBB::FAR_LEFT_BOTTOM));

            selLines->updateLine(i * 12 + 4, obb.getCorner(OBB::NEAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_TOP));
            selLines->updateLine(i * 12 + 5, obb.getCorner(OBB::NEAR_LEFT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP));
            selLines->updateLine(i * 12 + 6, obb.getCorner(OBB::NEAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM));
            selLines->updateLine(i * 12 + 7, obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM));

            selLines->updateLine(i * 12 + 8, obb.getCorner(OBB::FAR_LEFT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM));
            selLines->updateLine(i * 12 + 9, obb.getCorner(OBB::FAR_LEFT_TOP), obb.getCorner(OBB::NEAR_LEFT_TOP));
            selLines->updateLine(i * 12 + 10, obb.getCorner(OBB::FAR_RIGHT_TOP), obb.getCorner(OBB::NEAR_RIGHT_TOP));
            selLines->updateLine(i * 12 + 11, obb.getCorner(OBB::FAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_RIGHT_BOTTOM));
            i++;
        }
    }
}

void editor::SceneRender3D::update(std::vector<Entity> selEntities, std::vector<Entity> entities, Entity mainCamera, const SceneDisplaySettings& settings){
    SceneRender::update(selEntities, entities, mainCamera, settings);

    if (isGameView() || isPreviewCameraActive()){
        return;
    }

    lines->setVisible(displaySettings.showGrid3D);

    float spacing = displaySettings.gridSpacing3D;
    if (spacing <= 0.0f) spacing = 1.0f;

    int linesStepChange = std::max(1, (int)(camera->getFarClip() / 2));
    int cameraLineStepX = (int)(camera->getWorldPosition().x / linesStepChange) * linesStepChange;
    int cameraLineStepZ = (int)(camera->getWorldPosition().z / linesStepChange) * linesStepChange;
    bool spacingChanged = (lastGridSpacing != spacing);
    bool farClipChanged = (lastGridFarClip != camera->getFarClip());
    if (cameraLineStepX != linesOffset.x || cameraLineStepZ != linesOffset.y || spacingChanged || farClipChanged){
        linesOffset = Vector2(cameraLineStepX, cameraLineStepZ);
        lastGridSpacing = spacing;
        lastGridFarClip = camera->getFarClip();

        createLines();
    }

    viewgizmo.applyRotation(camera);

    uilayer.updateCameraGauge(camera->getDistanceFromTarget(), MIN_EDITOR_CAMERA_DISTANCE, MAX_EDITOR_CAMERA_DISTANCE);

    std::set<Entity> selectedEntities(selEntities.begin(), selEntities.end());

    auto isDescendantSelected = [&](Entity ancestor) -> bool {
        for (Entity sel : selectedEntities) {
            if (sel == ancestor || scene->isParentOf(ancestor, sel)) return true;
        }
        return false;
    };

    std::set<Entity> currentIconLights;
    std::set<Entity> currentIconCameras;
    std::set<Entity> currentIconSounds;
    std::set<Entity> currentBodyObjects;
    std::set<Entity> currentJointObjects;
    std::set<Entity> currentBoneModels;
    std::set<Entity> currentLinePoints;
    std::set<Entity> currentPolygonPoints;
    std::set<Entity> currentTrackLines;
    std::set<Entity> currentReflectionProbes;
    std::set<Entity> currentWaterExclusions;

    // each block creates its lines first, a new lines entity can move the components
    for (Entity& entity: entities){
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<LightComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            LightComponent& light = scene->getComponent<LightComponent>(entity);

            bool isSelected = std::find(selEntities.begin(), selEntities.end(), entity) != selEntities.end();

            currentIconLights.insert(entity);
            bool newLight = instanciateLightObject(entity) || lightObjects[entity].type != light.type;
            Transform& transform = scene->getComponent<Transform>(entity);
            if (light.type == LightType::DIRECTIONAL){
                createOrUpdateLightIcon(entity, transform, LightType::DIRECTIONAL, newLight);
                createDirectionalLightArrow(entity, transform, light, isSelected);
            }else if (light.type == LightType::POINT){
                createOrUpdateLightIcon(entity, transform, LightType::POINT, newLight);
                createPointLightSphere(entity, transform, light, isSelected);
            }else if (light.type == LightType::SPOT){
                createOrUpdateLightIcon(entity, transform, LightType::SPOT, newLight);
                createSpotLightCones(entity, transform, light, isSelected);
            }

            if (displaySettings.hideLightIcons && lightObjects.find(entity) != lightObjects.end()) {
                lightObjects[entity].icon->setVisible(false);
            }
        }

        if (signature.test(scene->getComponentId<CameraComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            if (entity != camera->getEntity()) {
                currentIconCameras.insert(entity);
                bool newCamera = instanciateCameraObject(entity);
                Transform& transform = scene->getComponent<Transform>(entity);
                CameraComponent& cameraComp = scene->getComponent<CameraComponent>(entity);

                createOrUpdateCameraIcon(entity, transform, newCamera);
                createCameraFrustum(entity, transform, cameraComp, !displaySettings.showFullFrustum, mainCamera == entity);

                if (displaySettings.hideCameraView) {
                    cameraObjects[entity].icon->setVisible(false);
                    cameraObjects[entity].lines->setVisible(false);
                }
            }
        }

        if (signature.test(scene->getComponentId<SoundComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentIconSounds.insert(entity);
            bool newSound = instanciateSoundObject(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            createOrUpdateSoundIcon(entity, transform, newSound);

            if (displaySettings.hideSoundIcons && soundObjects.find(entity) != soundObjects.end()) {
                soundObjects[entity].icon->setVisible(false);
            }
        }

        if (signature.test(scene->getComponentId<Body3DComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentBodyObjects.insert(entity);
            instanciateBodyObject(entity);
            Transform& transform = scene->getComponent<Transform>(entity);
            Body3DComponent& body = scene->getComponent<Body3DComponent>(entity);

            bool highlighted = isDescendantSelected(entity);
            bool isVisible = displaySettings.showAllBodies || highlighted;

            createOrUpdateBodyLines(entity, transform, body, isVisible, highlighted);
        }

        if (signature.test(scene->getComponentId<LinesComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentLinePoints.insert(entity);
            instanciateLinePointLines(entity);
            LinesComponent& linesComp = scene->getComponent<LinesComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            bool highlighted = isDescendantSelected(entity);

            createOrUpdateLinePointLines(entity, transform, linesComp, highlighted);
        }

        if (signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentPolygonPoints.insert(entity);
            instanciatePolygonPointLines(entity);
            PolygonComponent& polygon = scene->getComponent<PolygonComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            bool highlighted = isDescendantSelected(entity);

            createOrUpdatePolygonPointLines(entity, transform, polygon.points, false, highlighted);
        }else if (signature.test(scene->getComponentId<MeshPolygonComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentPolygonPoints.insert(entity);
            instanciatePolygonPointLines(entity);
            MeshPolygonComponent& polygon = scene->getComponent<MeshPolygonComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            bool highlighted = isDescendantSelected(entity);

            createOrUpdatePolygonPointLines(entity, transform, polygon.points, true, highlighted);
        }

        if (signature.test(scene->getComponentId<TranslateTracksComponent>())) {
            TranslateTracksComponent& tracks = scene->getComponent<TranslateTracksComponent>(entity);

            currentTrackLines.insert(entity);
            instanciateTrackLines(entity);
            // the tracks entity has no Transform, so gate on direct selection
            bool isSelected = selectedEntities.find(entity) != selectedEntities.end();

            createOrUpdateTrackLines(entity, tracks, isSelected);
        }

        if (signature.test(scene->getComponentId<ReflectionProbeComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentReflectionProbes.insert(entity);
            instanciateReflectionProbeLines(entity);
            ReflectionProbeComponent& probe = scene->getComponent<ReflectionProbeComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);
            createOrUpdateReflectionProbeLines(entity, transform, probe, selectedEntities.find(entity) != selectedEntities.end());
        }

        if (signature.test(scene->getComponentId<WaterExclusionComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            currentWaterExclusions.insert(entity);
            instanciateWaterExclusionLines(entity);
            WaterExclusionComponent& exclusion = scene->getComponent<WaterExclusionComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);
            createOrUpdateWaterExclusionLines(entity, transform, exclusion, selectedEntities.find(entity) != selectedEntities.end());
        }

        if (signature.test(scene->getComponentId<Joint3DComponent>())) {
            Joint3DComponent& joint = scene->getComponent<Joint3DComponent>(entity);

            currentJointObjects.insert(entity);
            instanciateJointObject(entity);

            bool isSelectedJoint = selectedEntities.find(entity) != selectedEntities.end();
            bool isBodyASelected = joint.bodyA != NULL_ENTITY && isDescendantSelected(joint.bodyA);
            bool isBodyBSelected = joint.bodyB != NULL_ENTITY && isDescendantSelected(joint.bodyB);
            bool highlighted = isSelectedJoint || isBodyASelected || isBodyBSelected;
            bool isVisible = displaySettings.showAllJoints || highlighted;

            createOrUpdateJointLines(entity, joint, isVisible, highlighted);
        }

        if (signature.test(scene->getComponentId<ModelComponent>())) {
            ModelComponent& model = scene->getComponent<ModelComponent>(entity);

            if (!model.bonesIdMapping.empty()) {
                currentBoneModels.insert(entity);
                instanciateBoneLines(entity);

                bool isSelected = isDescendantSelected(entity);
                bool isVisible = displaySettings.showAllBones || isSelected;

                createOrUpdateBoneLines(entity, model, isVisible, isSelected);
            }
        }
    }

    // Remove sun icons for entities that are no longer directional lights
    auto it = lightObjects.begin();
    while (it != lightObjects.end()) {
        if (currentIconLights.find(it->first) == currentIconLights.end()) {
            delete it->second.icon;
            delete it->second.lines;
            it = lightObjects.erase(it);
        } else {
            ++it;
        }
    }

    // Remove camera icons
    auto itCam = cameraObjects.begin();
    while (itCam != cameraObjects.end()) {
        if (currentIconCameras.find(itCam->first) == currentIconCameras.end()) {
            delete itCam->second.icon;
            delete itCam->second.lines;
            itCam = cameraObjects.erase(itCam);
        } else {
            ++itCam;
        }
    }

    auto itSound = soundObjects.begin();
    while (itSound != soundObjects.end()) {
        if (currentIconSounds.find(itSound->first) == currentIconSounds.end()) {
            delete itSound->second.icon;
            itSound = soundObjects.erase(itSound);
        } else {
            ++itSound;
        }
    }

    auto itBody = bodyObjects.begin();
    while (itBody != bodyObjects.end()) {
        if (currentBodyObjects.find(itBody->first) == currentBodyObjects.end()) {
            delete itBody->second.lines;
            itBody = bodyObjects.erase(itBody);
        } else {
            ++itBody;
        }
    }

    auto itJoint = jointLines.begin();
    while (itJoint != jointLines.end()) {
        if (currentJointObjects.find(itJoint->first) == currentJointObjects.end()) {
            delete itJoint->second;
            itJoint = jointLines.erase(itJoint);
        } else {
            ++itJoint;
        }
    }

    auto itBone = boneLines.begin();
    while (itBone != boneLines.end()) {
        if (currentBoneModels.find(itBone->first) == currentBoneModels.end()) {
            delete itBone->second;
            itBone = boneLines.erase(itBone);
        } else {
            ++itBone;
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

    auto itProbeLines = reflectionProbeLines.begin();
    while (itProbeLines != reflectionProbeLines.end()) {
        if (currentReflectionProbes.find(itProbeLines->first) == currentReflectionProbes.end()) {
            delete itProbeLines->second;
            itProbeLines = reflectionProbeLines.erase(itProbeLines);
        } else {
            ++itProbeLines;
        }
    }

    auto itExclusionLines = waterExclusionLines.begin();
    while (itExclusionLines != waterExclusionLines.end()) {
        if (currentWaterExclusions.find(itExclusionLines->first) == currentWaterExclusions.end()) {
            delete itExclusionLines->second;
            itExclusionLines = waterExclusionLines.erase(itExclusionLines);
        } else {
            ++itExclusionLines;
        }
    }
}

void editor::SceneRender3D::mouseHoverEvent(float x, float y){
    SceneRender::mouseHoverEvent(x, y);
}

void editor::SceneRender3D::mouseClickEvent(float x, float y, std::vector<Entity> selEntities){
    SceneRender::mouseClickEvent(x, y, selEntities);
}

void editor::SceneRender3D::mouseReleaseEvent(float x, float y){
    SceneRender::mouseReleaseEvent(x, y);
}

void editor::SceneRender3D::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection, bool invertRotationSnap, bool preserveAspectRatio){
    SceneRender::mouseDragEvent(x, y, origX, origY, project, sceneId, selEntities, disableSelection, invertRotationSnap, preserveAspectRatio);
}

editor::ViewportGizmo* editor::SceneRender3D::getViewportGizmo(){
    return &viewgizmo;
}
