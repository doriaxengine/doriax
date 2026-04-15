#include "ScaleGizmo.h"

using namespace doriax;

const Vector3 editor::ScaleGizmo::centerColor = Vector3(0.8, 0.8, 0.8);
const Vector3 editor::ScaleGizmo::xaxisColor = Vector3(0.7, 0.2, 0.2);
const Vector3 editor::ScaleGizmo::yaxisColor = Vector3(0.2, 0.7, 0.2);
const Vector3 editor::ScaleGizmo::zaxisColor = Vector3(0.2, 0.2, 0.7);
const Vector3 editor::ScaleGizmo::centerColorHightlight = Vector3(0.9, 0.9, 0.9);
const Vector3 editor::ScaleGizmo::xaxisColorHightlight = Vector3(0.9, 0.7, 0.7);
const Vector3 editor::ScaleGizmo::yaxisColorHightlight = Vector3(0.7, 0.9, 0.7);
const Vector3 editor::ScaleGizmo::zaxisColorHightlight = Vector3(0.7, 0.7, 0.9);
const float editor::ScaleGizmo::rectAlpha = 0.6;

editor::ScaleGizmo::ScaleGizmo(Scene* scene, bool use2DGizmo): Object(scene){
    float cylinderRadius = 0.05;
    float cylinderHeight = 2;
    float centerSize = 0.3;
    float cubeSize = 0.2;

    this->use2DGizmo = use2DGizmo;

    centerbox = new Shape(scene);
    xaxis = new Shape(scene);
    xbox = new Shape(scene);

    yaxis = new Shape(scene);
    ybox = new Shape(scene);

    if (!use2DGizmo){
        zaxis = new Shape(scene);
        zbox = new Shape(scene);
        xyrect = new Shape(scene);
        xzrect = new Shape(scene);
        yzrect = new Shape(scene);
    }
    
    centerbox->createBox(centerSize, centerSize, centerSize);
    centerbox->setColor(Vector4(centerColor, 1.0));

    xaxis->createCylinder(cylinderRadius, cylinderHeight);
    xaxis->setColor(Vector4(xaxisColor, 1.0));
    xaxis->setPosition(cylinderHeight/2.0, 0, 0);
    xaxis->setRotation(0,0,90);

    yaxis->createCylinder(cylinderRadius, cylinderHeight);
    yaxis->setColor(Vector4(yaxisColor, 1.0));
    yaxis->setPosition(0, cylinderHeight/2.0, 0);

    xbox->createBox(cubeSize, cubeSize, cubeSize);
    xbox->setPosition(cylinderHeight, 0, 0);
    xbox->setColor(Vector4(xaxisColor, 1.0));

    ybox->createBox(cubeSize, cubeSize, cubeSize);
    ybox->setPosition(0, cylinderHeight, 0);
    ybox->setColor(Vector4(yaxisColor, 1.0));

    if (!use2DGizmo) {
        zaxis->createCylinder(cylinderRadius, cylinderHeight);
        zaxis->setColor(Vector4(zaxisColor, 1.0));
        zaxis->setPosition(0, 0, cylinderHeight/2.0);
        zaxis->setRotation(90,0,0);

        zbox->createBox(cubeSize, cubeSize, cubeSize);
        zbox->setPosition(0, 0, cylinderHeight);
        zbox->setColor(Vector4(zaxisColor, 1.0));

        xyrect->createBox(cylinderHeight/4.0, cylinderHeight/4.0, cylinderRadius);
        xzrect->createBox(cylinderHeight/4.0, cylinderRadius, cylinderHeight/4.0);
        yzrect->createBox(cylinderRadius, cylinderHeight/4.0, cylinderHeight/4.0);

        xyrect->setPosition(cylinderHeight/3.0, cylinderHeight/3.0, 0);
        xzrect->setPosition(cylinderHeight/3.0, 0, cylinderHeight/3.0);
        yzrect->setPosition(0, cylinderHeight/3.0, cylinderHeight/3.0);

        xyrect->setColor(Vector4(zaxisColor, rectAlpha));
        xzrect->setColor(Vector4(yaxisColor, rectAlpha));
        yzrect->setColor(Vector4(xaxisColor, rectAlpha));
    }

    this->addChild(centerbox);
    this->addChild(xaxis);
    this->addChild(xbox);
    this->addChild(yaxis);
    this->addChild(ybox);

    if (!use2DGizmo) {
        this->addChild(zaxis);
        this->addChild(zbox);
        this->addChild(xyrect);
        this->addChild(xzrect);
        this->addChild(yzrect);
    }
}

editor::ScaleGizmo::~ScaleGizmo(){
    delete centerbox;
    delete xaxis;
    delete xbox;
    delete yaxis;
    delete ybox;

    if (!use2DGizmo) {
        delete zaxis;
        delete zbox;
        delete xyrect;
        delete xzrect;
        delete yzrect;
    }
}

editor::GizmoSideSelected editor::ScaleGizmo::checkHover(const Ray& ray){

    editor::GizmoSideSelected gizmoSideSelected = GizmoSideSelected::NONE;

    AABB xaabb = xaxis->getModelMatrix() * Matrix4::scaleMatrix(Vector3(2, 1, 2)) * xaxis->getAABB().merge(xbox->getAABB());
    AABB yaabb = yaxis->getModelMatrix() * Matrix4::scaleMatrix(Vector3(2, 1, 2)) * yaxis->getAABB().merge(ybox->getAABB());
    AABB boxaabb = centerbox->getModelMatrix() * Matrix4::scaleMatrix(Vector3(1.5, 1.5, 1.5)) * centerbox->getAABB();
    AABB superboxaabb = centerbox->getModelMatrix() * Matrix4::scaleMatrix(Vector3(10, 10, 10)) * centerbox->getAABB();

    RayReturn rreturn[7] = {0};

    rreturn[0] = ray.intersects(xaabb);
    rreturn[1] = ray.intersects(yaabb);
    rreturn[6] = ray.intersects(boxaabb);

    if (!use2DGizmo) {
        AABB zaabb = zaxis->getModelMatrix() * Matrix4::scaleMatrix(Vector3(2, 1, 2)) * zaxis->getAABB().merge(zbox->getAABB());
        AABB xyaabb = xyrect->getWorldAABB();
        AABB xzaabb = xzrect->getWorldAABB();
        AABB yzaabb = yzrect->getWorldAABB();

        rreturn[2] = ray.intersects(zaabb);
        rreturn[3] = ray.intersects(xyaabb);
        rreturn[4] = ray.intersects(xzaabb);
        rreturn[5] = ray.intersects(yzaabb);
    }

    int axis = -1;
    float minDist = FLT_MAX;

    for (int i = 0; i < 7; i++){
        // Skip indices 2-5 for 2D gizmo
        if (use2DGizmo && (i >= 2 && i <= 5))
            continue;

        if (rreturn[i]){
            if (rreturn[i].distance <= minDist || (i >= 3 && axis <= 2)){
                minDist = rreturn[i].distance;
                axis = i;
            }
        }
    }

    if (axis == -1){
        if (RayReturn creturn = ray.intersects(superboxaabb)){
            axis = 6;
        }
    }

    if (axis == 0){
        xaxis->setColor(Vector4(xaxisColorHightlight, 1.0));
        xbox->setColor(Vector4(xaxisColorHightlight, 1.0));
        gizmoSideSelected = GizmoSideSelected::X;
    }else{
        xaxis->setColor(Vector4(xaxisColor, 1.0));
        xbox->setColor(Vector4(xaxisColor, 1.0));
    }

    if (axis == 1){
        yaxis->setColor(Vector4(yaxisColorHightlight, 1.0));
        ybox->setColor(Vector4(yaxisColorHightlight, 1.0));
        gizmoSideSelected = GizmoSideSelected::Y;
    }else{
        yaxis->setColor(Vector4(yaxisColor, 1.0));
        ybox->setColor(Vector4(yaxisColor, 1.0));
    }

    if (!use2DGizmo) {
        if (axis == 2){
            zaxis->setColor(Vector4(zaxisColorHightlight, 1.0));
            zbox->setColor(Vector4(zaxisColorHightlight, 1.0));
            gizmoSideSelected = GizmoSideSelected::Z;
        }else{
            zaxis->setColor(Vector4(zaxisColor, 1.0));
            zbox->setColor(Vector4(zaxisColor, 1.0));
        }

        if (axis == 3){
            xyrect->setColor(Vector4(zaxisColorHightlight, rectAlpha));
            gizmoSideSelected = GizmoSideSelected::XY;
        }else{
            xyrect->setColor(Vector4(zaxisColor, rectAlpha));
        }

        if (axis == 4){
            xzrect->setColor(Vector4(yaxisColorHightlight, rectAlpha));
            gizmoSideSelected = GizmoSideSelected::XZ;
        }else{
            xzrect->setColor(Vector4(yaxisColor, rectAlpha));
        }

        if (axis == 5){
            yzrect->setColor(Vector4(xaxisColorHightlight, rectAlpha));
            gizmoSideSelected = GizmoSideSelected::YZ;
        }else{
            yzrect->setColor(Vector4(xaxisColor, rectAlpha));
        }
    }

    if (axis == 6){
        centerbox->setColor(Vector4(centerColorHightlight, 1.0));
        gizmoSideSelected = use2DGizmo ? GizmoSideSelected::XY : GizmoSideSelected::XYZ;
    }else{
        centerbox->setColor(Vector4(centerColor, 1.0));
    }

    return gizmoSideSelected;
}
