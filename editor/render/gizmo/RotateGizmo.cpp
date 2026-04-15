#include "RotateGizmo.h"

#include "subsystem/MeshSystem.h"
#include "util/Angle.h"
#include "math/Sphere.h"

using namespace doriax;

const Vector3 editor::RotateGizmo::mainColor = Vector3(0.8, 0.8, 0.8);
const Vector3 editor::RotateGizmo::xaxisColor = Vector3(0.7, 0.2, 0.2);
const Vector3 editor::RotateGizmo::yaxisColor = Vector3(0.2, 0.7, 0.2);
const Vector3 editor::RotateGizmo::zaxisColor = Vector3(0.2, 0.2, 0.7);
const Vector3 editor::RotateGizmo::lineColor = Vector3(0.2, 0.1, 0.2);
const Vector3 editor::RotateGizmo::mainColorHightlight = Vector3(0.9, 0.9, 0.9);
const Vector3 editor::RotateGizmo::xaxisColorHightlight = Vector3(0.9, 0.7, 0.7);
const Vector3 editor::RotateGizmo::yaxisColorHightlight = Vector3(0.7, 0.9, 0.7);
const Vector3 editor::RotateGizmo::zaxisColorHightlight = Vector3(0.7, 0.7, 0.9);
const float editor::RotateGizmo::circleAlpha = 0.6;

editor::RotateGizmo::RotateGizmo(Scene* scene, bool use2DGizmo): Object(scene){
    float mainRadius = 0.02;
    float axisRadius = 0.05;
    float torusHeight = 2;
    float center2DRadius = 0.2;

    this->use2DGizmo = use2DGizmo;

    maincircle = new Shape(scene);
    line = new Lines(scene); // not a direct child of the gizmo to keep invisible if parent set visible to true

    // For 2D gizmo, we only need the z-circle for rotation
    if (!use2DGizmo) {
        xcircle = new Shape(scene);
        ycircle = new Shape(scene);
    }
    zcircle = new Shape(scene);

    line->addLine(Vector3::ZERO, Vector3::ZERO, Vector4(lineColor, 1.0));
    line->setVisible(false);

    maincircle->createTorus((use2DGizmo)?center2DRadius:torusHeight, mainRadius, 32, 32);
    maincircle->setColor(Vector4(mainColor, circleAlpha));
    maincircle->setBillboardRotation(90,0,0);
    maincircle->setBillboard((!use2DGizmo), false, false);

    if (!use2DGizmo) {
        xcircleAABBs = createHalfTorus(xcircle->getEntity(), torusHeight, axisRadius, 32, 16);
        ycircleAABBs = createHalfTorus(ycircle->getEntity(), torusHeight, axisRadius, 32, 16);
        zcircleAABBs = createHalfTorus(zcircle->getEntity(), torusHeight, axisRadius, 32, 16);

        xcircle->setColor(Vector4(xaxisColor, 1.0));
        ycircle->setColor(Vector4(yaxisColor, 1.0));
        zcircle->setColor(Vector4(zaxisColor, 1.0));
    } else {
        // For 2D mode, create a full torus for Z rotation
        zcircle->createTorus(torusHeight, axisRadius, 32, 32);
        zcircle->setColor(Vector4(zaxisColor, 1.0));
        // Create a dummy AABB for the full circle
        zcircleAABBs.resize(1);
        zcircleAABBs[0] = zcircle->getAABB();
    }

    this->addChild(maincircle);
    if (!use2DGizmo) {
        this->addChild(xcircle);
        this->addChild(ycircle);
    }
    this->addChild(zcircle);
}

editor::RotateGizmo::~RotateGizmo(){
    delete maincircle;
    if (!use2DGizmo) {
        delete xcircle;
        delete ycircle;
    }
    delete zcircle;
    delete line;
}

std::vector<AABB> editor::RotateGizmo::createHalfTorus(Entity entity, float radius, float ringRadius, unsigned int sides, unsigned int rings) {
    std::vector<AABB> aabbs;
    aabbs.resize(rings);

    MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

    mesh.submeshes[0].primitiveType = PrimitiveType::TRIANGLES;
    mesh.numSubmeshes = 1;

    mesh.buffer.clear();
    mesh.buffer.addAttribute(AttributeType::POSITION, 3);
    mesh.buffer.addAttribute(AttributeType::TEXCOORD1, 2);
    mesh.buffer.addAttribute(AttributeType::NORMAL, 3);
    mesh.buffer.addAttribute(AttributeType::COLOR, 4);

    Attribute* attVertex = mesh.buffer.getAttribute(AttributeType::POSITION);
    Attribute* attTexcoord = mesh.buffer.getAttribute(AttributeType::TEXCOORD1);
    Attribute* attNormal = mesh.buffer.getAttribute(AttributeType::NORMAL);
    Attribute* attColor = mesh.buffer.getAttribute(AttributeType::COLOR);

    const float pi = M_PI;
    const float dv = 1.0f / sides;
    const float du = 1.0f / rings;

    // Generate vertices for half-circle torus
    for (uint32_t side = 0; side <= sides; side++) {
        const float phi = (side * 2.0f * pi) / sides;
        const float sin_phi = sinf(phi);
        const float cos_phi = cosf(phi);
        for (uint32_t ring = 0; ring <= rings; ring++) {
            const float theta = (ring * pi) / rings;
            const float sin_theta = sinf(theta);
            const float cos_theta = cosf(theta);

            const float spx = sin_theta * (radius - (ringRadius * cos_phi));
            const float spy = sin_phi * ringRadius;
            const float spz = cos_theta * (radius - (ringRadius * cos_phi));

            const float ipx = sin_theta * radius;
            const float ipy = 0.0f;
            const float ipz = cos_theta * radius;

            mesh.buffer.addVector3(attVertex, Vector3(spx, spy, spz));
            mesh.buffer.addVector3(attNormal, Vector3(spx - ipx, spy - ipy, spz - ipz));
            mesh.buffer.addVector2(attTexcoord, Vector2(ring * du, 1.0f - side * dv));
            mesh.buffer.addVector4(attColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));

            if (ring < rings){
                aabbs[ring].merge(Vector3(spx, spy, spz));
            }
            if (ring > 0){
                aabbs[ring-1].merge(Vector3(spx, spy, spz));
            }
        }
    }

    // Add cap vertices
    uint32_t baseIndex = (sides + 1) * (rings + 1);

    // For both ends
    for (int end = 0; end <= 1; end++) {
        float theta = end * pi;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);

        // Center vertex of the cap
        Vector3 center(sin_theta * radius, 0, cos_theta * radius);
        mesh.buffer.addVector3(attVertex, center);
        mesh.buffer.addVector3(attNormal, Vector3(sin_theta, 0, cos_theta));
        mesh.buffer.addVector2(attTexcoord, Vector2(0.5f, 0.5f));
        mesh.buffer.addVector4(attColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        // Circle vertices for the cap
        for (uint32_t i = 0; i <= sides; i++) {
            float phi = (i * 2.0f * pi) / sides;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            Vector3 pos(
                sin_theta * (radius - ringRadius * cos_phi),
                ringRadius * sin_phi,
                cos_theta * (radius - ringRadius * cos_phi)
            );

            mesh.buffer.addVector3(attVertex, pos);
            mesh.buffer.addVector3(attNormal, Vector3(sin_theta, 0, cos_theta));
            mesh.buffer.addVector2(attTexcoord, Vector2(cos_phi * 0.5f + 0.5f, sin_phi * 0.5f + 0.5f));
            mesh.buffer.addVector4(attColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    // Generate torus indices
    std::vector<uint16_t> indices;
    for (uint16_t side = 0; side < sides; side++) {
        const uint16_t row_a = side * (rings + 1);
        const uint16_t row_b = row_a + rings + 1;
        for (uint16_t ring = 0; ring < rings; ring++) {
            indices.push_back(row_a + ring);
            indices.push_back(row_b + ring + 1);
            indices.push_back(row_a + ring + 1);

            indices.push_back(row_a + ring);
            indices.push_back(row_b + ring);
            indices.push_back(row_b + ring + 1);
        }
    }

    // Generate cap indices
    for (int end = 0; end < 2; end++) {
        uint16_t centerIndex = baseIndex + end * (sides + 2);
        uint16_t startVertex = centerIndex + 1;

        for (uint16_t i = 0; i < sides; i++) {
            if (end == 1) {
                indices.push_back(centerIndex);
                indices.push_back(startVertex + i);
                indices.push_back(startVertex + i + 1);
            } else {
                indices.push_back(centerIndex);
                indices.push_back(startVertex + i + 1);
                indices.push_back(startVertex + i);
            }
        }
    }

    mesh.indices.clear();

    mesh.indices.setValues(
        0, mesh.indices.getAttribute(AttributeType::INDEX),
        indices.size(), (char*)&indices[0], sizeof(uint16_t));

    if (mesh.loaded)
        mesh.needReload = true;

    scene->getSystem<MeshSystem>()->calculateMeshAABB(mesh);

    return aabbs;
}

void editor::RotateGizmo::updateRotations(Camera* camera){
    Matrix4 rotMatrixInv = getWorldRotation().getRotationMatrix().inverse();

    Plane planeZ(Vector3(0,0,1), 0);
    Vector3 viewZ = planeZ.projectVector(rotMatrixInv * camera->getWorldDirection()).normalize();

    if (!use2DGizmo) {
        Plane planeX(Vector3(1,0,0), 0);
        Plane planeY(Vector3(0,1,0), 0);

        Vector3 viewX = planeX.projectVector(rotMatrixInv * camera->getWorldDirection()).normalize();
        Vector3 viewY = planeY.projectVector(rotMatrixInv * camera->getWorldDirection()).normalize();

        xcircle->setRotation(Quaternion(Angle::radToDefault(atan2(viewX.z, viewX.y)), planeX.normal) * Quaternion(0, 0, 90));
        ycircle->setRotation(Quaternion(Angle::radToDefault(atan2(viewY.x, viewY.z) - M_PI_2), planeY.normal));
        zcircle->setRotation(Quaternion(Angle::radToDefault(atan2(viewZ.y, viewZ.x)), planeZ.normal) * Quaternion(90, 0, 0));
    } else {
        // For 2D mode, we just need to keep the z-circle flat on the XY plane
        zcircle->setRotation(Quaternion(90, 0, 0));
    }
}

void editor::RotateGizmo::drawLine(Vector3 point){
    line->setVisible(true);
    line->updateLine(0, getWorldPosition(), point);
}

void editor::RotateGizmo::removeLine(){
    line->setVisible(false);
}

editor::GizmoSideSelected editor::RotateGizmo::checkHover(const Ray& ray){

    editor::GizmoSideSelected gizmoSideSelected = GizmoSideSelected::NONE;

    Sphere sphere(getWorldPosition(), 2 * getWorldScale().x);

    int axis = -1;

    if (!use2DGizmo) {
        for (int i = 0; i < xcircleAABBs.size(); i++){
            if (RayReturn creturn = ray.intersects(xcircle->getModelMatrix() * xcircleAABBs[i])){
                axis = 0;
                break;
            }
        }

        for (int i = 0; i < ycircleAABBs.size(); i++){
            if (RayReturn creturn = ray.intersects(ycircle->getModelMatrix() * ycircleAABBs[i])){
                axis = 1;
                break;
            }
        }
    }

    // Always check z-circle for rotation
    for (int i = 0; i < zcircleAABBs.size(); i++){
        if (RayReturn creturn = ray.intersects(zcircle->getModelMatrix() * zcircleAABBs[i])){
            axis = 2;
            break;
        }
    }

    if (axis == -1){
        if (RayReturn creturn = ray.intersects(sphere)){
            axis = 3;
        }
    }

    if (!use2DGizmo) {
        if (axis == 0){
            xcircle->setColor(Vector4(xaxisColorHightlight, 1.0));
            gizmoSideSelected = GizmoSideSelected::X;
        }else{
            xcircle->setColor(Vector4(xaxisColor, 1.0));
        }

        if (axis == 1){
            ycircle->setColor(Vector4(yaxisColorHightlight, 1.0));
            gizmoSideSelected = GizmoSideSelected::Y;
        }else{
            ycircle->setColor(Vector4(yaxisColor, 1.0));
        }
    }

    if (axis == 2){
        zcircle->setColor(Vector4(zaxisColorHightlight, 1.0));
        gizmoSideSelected = GizmoSideSelected::Z;
    }else{
        zcircle->setColor(Vector4(zaxisColor, 1.0));
    }

    if (axis == 3){
        maincircle->setColor(Vector4(mainColorHightlight, circleAlpha));
        gizmoSideSelected = use2DGizmo ? GizmoSideSelected::Z : GizmoSideSelected::XYZ;
    }else{
        maincircle->setColor(Vector4(mainColor, circleAlpha));
    }

    return gizmoSideSelected;
}