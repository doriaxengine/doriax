#include "SceneRender3D.h"

#include "resources/icons/sun-icon_png.h"
#include "resources/icons/bulb-icon_png.h"
#include "resources/icons/spot-icon_png.h"

#include "Project.h"
#include "LineDrawUtils.h"

#include <cmath>

using namespace doriax;

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

    createLines();

    //camera->setType(CameraType::CAMERA_UI);
    camera->setPosition(10, 4, 10);
    camera->setTarget(0, 0, 0);

    //camera->setRenderToTexture(true);
    //camera->setUseFramebufferSizes(false);

    scene->setLightState(LightState::ON);
    scene->setGlobalIllumination(0.2);
    scene->setBackgroundColor(Vector4(0.25, 0.45, 0.65, 1.0));

    uilayer.setViewportGizmoTexture(viewgizmo.getFramebuffer());

}

editor::SceneRender3D::~SceneRender3D(){
    delete lines;
    delete selLines;

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
        if (!boneTransform || !boneTransform->visible) continue;

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

void editor::SceneRender3D::createOrUpdateBodyLines(Entity entity, const Transform& transform, const Body3DComponent& body) {
    BodyObjects& bo = bodyObjects[entity];

    bo.lines->clearLines();
    bo.lines->setVisible(transform.visible);

    if (!transform.visible || body.numShapes == 0) {
        return;
    }

    const Vector4 bodyColor(0.2f, 0.95f, 0.95f, 1.0f);
    const Matrix4 modelMatrix = transform.modelMatrix;

    auto addTransformedLine = [&](const Matrix4& shapeMatrix, const Vector3& from, const Vector3& to) {
        Vector3 worldFrom = modelMatrix * (shapeMatrix * from);
        Vector3 worldTo = modelMatrix * (shapeMatrix * to);
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

    for (size_t i = 0; i < body.numShapes; i++) {
        const Shape3D& shapeData = body.shapes[i];

        Matrix4 shapeMatrix = Matrix4::translateMatrix(shapeData.position) * shapeData.rotation.getRotationMatrix();

        if (shapeData.type == Shape3DType::BOX) {
            Vector3 halfSize(shapeData.width * 0.5f, shapeData.height * 0.5f, shapeData.depth * 0.5f);
            addBox(shapeMatrix, -halfSize, halfSize);
            addBoxRings(shapeMatrix, -halfSize, halfSize);
            addBoxVerticals(shapeMatrix, -halfSize, halfSize);
            addBoxCaps(shapeMatrix, -halfSize, halfSize);
        } else if (shapeData.type == Shape3DType::SPHERE) {
            addSpherePattern(shapeMatrix, shapeData.radius);
        } else if (shapeData.type == Shape3DType::CAPSULE) {
            const float halfHeight = shapeData.halfHeight;
            const float radius = shapeData.radius;
            Vector3 top(0, halfHeight, 0);
            Vector3 bottom(0, -halfHeight, 0);

            addCircle(shapeMatrix, top, Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCircle(shapeMatrix, bottom, Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, radius, radius);

            addHemisphere(shapeMatrix, top, radius, true);
            addHemisphere(shapeMatrix, bottom, radius, false);
        } else if (shapeData.type == Shape3DType::TAPERED_CAPSULE) {
            const float halfHeight = shapeData.halfHeight;
            const float topRadius = shapeData.topRadius;
            const float bottomRadius = shapeData.bottomRadius;

            addCircle(shapeMatrix, Vector3(0, halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), topRadius, 20);
            addCircle(shapeMatrix, Vector3(0, -halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), bottomRadius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, topRadius, bottomRadius);

            addHemisphere(shapeMatrix, Vector3(0, halfHeight, 0), topRadius, true);
            addHemisphere(shapeMatrix, Vector3(0, -halfHeight, 0), bottomRadius, false);
        } else if (shapeData.type == Shape3DType::CYLINDER) {
            const float halfHeight = shapeData.halfHeight;
            const float radius = shapeData.radius;

            addCircle(shapeMatrix, Vector3(0, halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCircle(shapeMatrix, Vector3(0, -halfHeight, 0), Vector3(1, 0, 0), Vector3(0, 0, 1), radius, 20);
            addCapsuleMiddle(shapeMatrix, halfHeight, radius, radius);
            addCylinderRings(shapeMatrix, halfHeight, radius);
        } else if (shapeData.type == Shape3DType::CONVEX_HULL) {
            if (shapeData.numVertices >= 2) {
                Vector3 minPt = shapeData.vertices[0];
                Vector3 maxPt = shapeData.vertices[0];

                for (size_t v = 1; v < shapeData.numVertices; v++) {
                    minPt.x = std::min(minPt.x, shapeData.vertices[v].x);
                    minPt.y = std::min(minPt.y, shapeData.vertices[v].y);
                    minPt.z = std::min(minPt.z, shapeData.vertices[v].z);
                    maxPt.x = std::max(maxPt.x, shapeData.vertices[v].x);
                    maxPt.y = std::max(maxPt.y, shapeData.vertices[v].y);
                    maxPt.z = std::max(maxPt.z, shapeData.vertices[v].z);
                }

                addBox(shapeMatrix, minPt, maxPt);
            }
        } else if (shapeData.type == Shape3DType::MESH) {
            if (shapeData.numVertices >= 3 && shapeData.numIndices >= 3) {
                int triangles = int(shapeData.numIndices / 3);
                for (int tri = 0; tri < triangles; tri++) {
                    uint16_t i0 = shapeData.indices[tri * 3 + 0];
                    uint16_t i1 = shapeData.indices[tri * 3 + 1];
                    uint16_t i2 = shapeData.indices[tri * 3 + 2];

                    if (i0 < shapeData.numVertices && i1 < shapeData.numVertices && i2 < shapeData.numVertices) {
                        const Vector3& p0 = shapeData.vertices[i0];
                        const Vector3& p1 = shapeData.vertices[i1];
                        const Vector3& p2 = shapeData.vertices[i2];

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

    auto getBodyWorldPoint = [&](Entity bodyEntity, const Vector3& localPoint){
        if (bodyEntity != NULL_ENTITY){
            Transform* transform = scene->findComponent<Transform>(bodyEntity);
            if (transform){
                return transform->modelMatrix * localPoint;
            }
        }

        return localPoint;
    };

    float alpha = highlighted ? 1.0f : 0.35f;

    const Vector4 jointColor(0.95f, 0.75f, 0.2f, alpha);
    const Vector4 helperColor(0.85f, 0.85f, 0.85f, 0.8f * alpha);
    const Vector4 axisColor(0.2f, 0.9f, 1.0f, alpha);
    const Vector4 limitColor(1.0f, 0.35f, 0.35f, alpha);

    Vector3 anchorA = getBodyWorldPoint(joint.bodyA, joint.anchorA);
    Vector3 anchorB = getBodyWorldPoint(joint.bodyB, joint.anchorB);

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
            float halfRail = 0.8f;

            Vector3 railStart = anchorA - railAxis * halfRail;
            Vector3 railEnd = anchorA + railAxis * halfRail;

            float railOffset = 0.06f;
            jointLinesObj->addLine(railStart + side * railOffset, railEnd + side * railOffset, axisColor);
            jointLinesObj->addLine(railStart - side * railOffset, railEnd - side * railOffset, axisColor);

            jointLinesObj->addLine(railStart - side * 0.12f, railStart + side * 0.12f, limitColor);
            jointLinesObj->addLine(railEnd - side * 0.12f, railEnd + side * 0.12f, limitColor);

            float sliderPos = (anchorB - anchorA).dotProduct(railAxis);
            sliderPos = std::max(-halfRail, std::min(halfRail, sliderPos));
            Vector3 sliderCenter = anchorA + railAxis * sliderPos;

            LineDrawUtils::addRing3D(jointLinesObj, sliderCenter, railAxis, 0.09f, jointColor, 18);
            jointLinesObj->addLine(sliderCenter - up * 0.1f, sliderCenter + up * 0.1f, helperColor);
            break;
        }
        case Joint3DType::CONE:{
            Vector3 axis = joint.axis.length() > 0.0001f ? joint.axis.normalized() : dirABNorm;
            auto [coneAxis, right, forward] = LineDrawUtils::makeBasis(axis);
            (void)right;
            float coneLen = 0.9f;
            float angle = std::max(1.0f, joint.normalHalfConeAngle) * (M_PI / 180.0f);
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
    lo.icon->setVisible(transform.visible);

    // Update light icon scale
    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(camera->getEntity());
    float lightIconScale = 0.25f;
    float scale = lightIconScale * zoom;

    if (cameracomp.type == CameraType::CAMERA_PERSPECTIVE){
        float dist = (lo.icon->getPosition() - camera->getWorldPosition()).length();
        scale = std::tan(cameracomp.yfov) * dist * (lightIconScale / (float)framebuffer.getHeight());
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
    }

    lo.icon->setScale(scale);
}

void editor::SceneRender3D::createOrUpdateCameraIcon(Entity entity, const Transform& transform, bool newCamera) {
    CameraObjects& co = cameraObjects[entity];

    if (newCamera) {
        setupCameraIcon(co);
        co.icon->setBillboard(true);
    }

    // Update camera icon position
    co.icon->setPosition(transform.worldPosition);
    co.icon->setVisible(transform.visible);

    // Update camera icon scale
    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(camera->getEntity());
    float iconScale = 0.25f;
    float scale = iconScale * zoom;

    if (cameracomp.type == CameraType::CAMERA_PERSPECTIVE){
        float dist = (co.icon->getPosition() - camera->getWorldPosition()).length();
        scale = std::tan(cameracomp.yfov) * dist * (iconScale / (float)framebuffer.getHeight());
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
    }

    co.icon->setScale(scale);
}

void editor::SceneRender3D::createOrUpdateSoundIcon(Entity entity, const Transform& transform, bool newSound) {
    SoundObjects& so = soundObjects[entity];

    if (newSound) {
        setupSoundIcon(so);
        so.icon->setBillboard(true);
    }

    so.icon->setPosition(transform.worldPosition);
    so.icon->setVisible(transform.visible);

    CameraComponent& cameracomp = scene->getComponent<CameraComponent>(camera->getEntity());
    float iconScale = 0.25f;
    float scale = iconScale * zoom;

    if (cameracomp.type == CameraType::CAMERA_PERSPECTIVE){
        float dist = (so.icon->getPosition() - camera->getWorldPosition()).length();
        scale = std::tan(cameracomp.yfov) * dist * (iconScale / (float)framebuffer.getHeight());
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
    }

    so.icon->setScale(scale);
}

void editor::SceneRender3D::createCameraFrustum(Entity entity, const Transform& transform, const CameraComponent& cameraComponent, bool fixedSizeFrustum, bool isMainCamera) {
    CameraObjects& co = cameraObjects[entity];

    co.lines->setPosition(transform.worldPosition);
    co.lines->setVisible(transform.visible);

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

    // Create orthonormal basis vectors perpendicular to light direction
    Vector3 up = Vector3(0, 1, 0);
    if (std::abs(direction.dotProduct(up)) > 0.9f) {
        up = Vector3(1, 0, 0);
    }
    Vector3 right = direction.crossProduct(up).normalized();
    up = right.crossProduct(direction).normalized();

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
        lo.range == range) {
        return;
    }

    lo.type = light.type;
    lo.innerConeCos = light.innerConeCos;
    lo.outerConeCos = light.outerConeCos;
    lo.direction = light.direction;
    lo.range = range;

    lo.lines->clearLines();

    Vector3 position = Vector3(0,0,0);  // Start position
    Vector3 direction = light.direction.normalized();

    // Calculate cone radii at the end of the light range
    float innerRadius = range * std::tan(std::acos(light.innerConeCos));
    float outerRadius = range * std::tan(std::acos(light.outerConeCos));

    // Create orthonormal basis vectors perpendicular to light direction
    Vector3 up = Vector3(0, 1, 0);
    if (std::abs(direction.dotProduct(up)) > 0.9f) {
        up = Vector3(1, 0, 0);
    }
    Vector3 right = direction.crossProduct(up).normalized();
    up = right.crossProduct(direction).normalized();

    // End position of the cone
    Vector3 endPos = position + direction * range;

    const int numSegments = 12;
    const float angleStep = 2.0f * M_PI / numSegments;

    // Colors for inner and outer cones
    Vector4 innerConeColor = Vector4(1.0, 1.0, 0.0, 0.8); // Yellow for inner cone
    Vector4 outerConeColor = Vector4(1.0, 0.5, 0.0, 0.6); // Orange for outer cone

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
}

void editor::SceneRender3D::activate(){
    SceneRender::activate();

    Engine::addSceneLayer(viewgizmo.getScene());
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
            selLines->addLine(obb.getCorner(OBB::NEAR_RIGHT_BOTTOM), obb.getCorner(OBB::NEAR_LEFT_BOTTOM)), color;

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

    if (isPlaying){
        return;
    }

    lines->setVisible(displaySettings.showGrid3D);

    float spacing = displaySettings.gridSpacing3D;
    if (spacing <= 0.0f) spacing = 1.0f;

    int linesStepChange = (int)(camera->getFarClip() / 2);
    int cameraLineStepX = (int)(camera->getWorldPosition().x / linesStepChange) * linesStepChange;
    int cameraLineStepZ = (int)(camera->getWorldPosition().z / linesStepChange) * linesStepChange;
    bool spacingChanged = (lastGridSpacing != spacing);
    if (cameraLineStepX != linesOffset.x || cameraLineStepZ != linesOffset.y || spacingChanged){
        linesOffset = Vector2(cameraLineStepX, cameraLineStepZ);
        lastGridSpacing = spacing;

        createLines();
    }

    viewgizmo.applyRotation(camera);

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

    for (Entity& entity: entities){
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<LightComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            LightComponent& light = scene->getComponent<LightComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);

            bool isSelected = std::find(selEntities.begin(), selEntities.end(), entity) != selEntities.end();

            currentIconLights.insert(entity);
            bool newLight = instanciateLightObject(entity) || lightObjects[entity].type != light.type;
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
                Transform& transform = scene->getComponent<Transform>(entity);
                CameraComponent& cameraComp = scene->getComponent<CameraComponent>(entity);

                currentIconCameras.insert(entity);
                bool newCamera = instanciateCameraObject(entity);
                createOrUpdateCameraIcon(entity, transform, newCamera);
                createCameraFrustum(entity, transform, cameraComp, true, mainCamera == entity);

                if (displaySettings.hideCameraView) {
                    cameraObjects[entity].icon->setVisible(false);
                    cameraObjects[entity].lines->setVisible(false);
                }
            }
        }

        if (signature.test(scene->getComponentId<AudioComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            Transform& transform = scene->getComponent<Transform>(entity);

            currentIconSounds.insert(entity);
            bool newSound = instanciateSoundObject(entity);
            createOrUpdateSoundIcon(entity, transform, newSound);
        }

        if (signature.test(scene->getComponentId<Body3DComponent>()) && signature.test(scene->getComponentId<Transform>())) {
            Transform& transform = scene->getComponent<Transform>(entity);
            Body3DComponent& body = scene->getComponent<Body3DComponent>(entity);

            currentBodyObjects.insert(entity);
            instanciateBodyObject(entity);
            createOrUpdateBodyLines(entity, transform, body);
            if (displaySettings.hideAllBodies && bodyObjects.find(entity) != bodyObjects.end()) {
                bodyObjects[entity].lines->setVisible(false);
            }
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

void editor::SceneRender3D::mouseDragEvent(float x, float y, float origX, float origY, Project* project, size_t sceneId, std::vector<Entity> selEntities, bool disableSelection){
    SceneRender::mouseDragEvent(x, y, origX, origY, project, sceneId, selEntities, disableSelection);
}

editor::ViewportGizmo* editor::SceneRender3D::getViewportGizmo(){
    return &viewgizmo;
}