#include "Object2DGizmo.h"

using namespace doriax;

const float editor::Object2DGizmo::rectSize = 0.20;
const float editor::Object2DGizmo::sizeOffset = 0.1;

editor::Object2DGizmo::Object2DGizmo(Scene* scene): Object(scene){
    width = 0.0;
    height = 0.0;
    showRects = true;

    center = new Object(scene);
    lines = new Lines(scene);
    lines->setVisible(false);

    for (int i = 0; i < 8; i++){
        rects[i] = new Polygon(scene);

        rects[i]->addVertex(0, 0);
        rects[i]->addVertex(rectSize, 0);
        rects[i]->addVertex(0, rectSize);
        rects[i]->addVertex(rectSize, rectSize);
        rects[i]->setColor(0.9f, 0.5f, 0.3f, 1.0f);

        center->addChild(rects[i]);
    }

    center->addChild(lines);
    this->addChild(center);
}

editor::Object2DGizmo::~Object2DGizmo(){
    for (int i = 0; i < 8; i++){
        delete rects[i];
    }
    delete lines;
}

void editor::Object2DGizmo::updateRects(){
    float halfRect = rectSize / 2.0;
    float halfWidth = width / 2.0;
    float halfHeight = height / 2.0;

    if (!showRects) {
        for (int i = 0; i < 8; i++){
            rects[i]->setScale(Vector3(0.0f, 0.0f, 0.0f));
        }
        return;
    }

    for (int i = 0; i < 8; i++){
        rects[i]->setScale(Vector3(1.0f, 1.0f, 1.0f));
    }

    rects[0]->setPosition(-halfWidth-halfRect-sizeOffset, -halfHeight-halfRect-sizeOffset, 0);
    rects[1]->setPosition(-halfWidth-halfRect-sizeOffset, -halfRect, 0);
    rects[2]->setPosition(-halfWidth-halfRect-sizeOffset, halfHeight-halfRect+sizeOffset, 0);
    rects[3]->setPosition(-halfRect, halfHeight-halfRect+sizeOffset, 0);
    rects[4]->setPosition(halfWidth-halfRect+sizeOffset, halfHeight-halfRect+sizeOffset, 0);
    rects[5]->setPosition(halfWidth-halfRect+sizeOffset, -halfRect, 0);
    rects[6]->setPosition(halfWidth-halfRect+sizeOffset, -halfHeight-halfRect-sizeOffset, 0);
    rects[7]->setPosition(-halfRect, -halfHeight-halfRect-sizeOffset, 0);
}

void editor::Object2DGizmo::updateLines(){
    if (!lines){
        return;
    }

    lines->clearLines();

    float halfWidth = width / 2.0f;
    float halfHeight = height / 2.0f;
    Vector4 color(0.9f, 0.5f, 0.3f, 1.0f);

    Vector3 bl(-halfWidth, -halfHeight, 0);
    Vector3 br(halfWidth, -halfHeight, 0);
    Vector3 tr(halfWidth, halfHeight, 0);
    Vector3 tl(-halfWidth, halfHeight, 0);

    lines->addLine(bl, br, color);
    lines->addLine(br, tr, color);
    lines->addLine(tr, tl, color);
    lines->addLine(tl, bl, color);
}

void editor::Object2DGizmo::setCenter(Vector3 point){
    center->setPosition(point);
}

void editor::Object2DGizmo::setSize(float width, float height){
    if (this->width != width || this->height != height){
        this->width = width;
        this->height = height;

        updateRects();
        updateLines();
    }
}

void editor::Object2DGizmo::setShowRects(bool showRects){
    this->showRects = showRects;
    updateRects();
}

editor::Gizmo2DSideSelected editor::Object2DGizmo::checkHover(const Ray& ray, const OBB& obb){
    editor::Gizmo2DSideSelected gizmoSideSelected = Gizmo2DSideSelected::NONE;

    if (RayReturn rreturn = ray.intersects(obb)){
        gizmoSideSelected = Gizmo2DSideSelected::CENTER;
    }

    if (isVisible() && showRects){
        for (int i = 0; i < 8; i++){
            if (RayReturn rreturn = ray.intersects(rects[i]->getWorldAABB())){
                if (i == 0){
                    gizmoSideSelected = Gizmo2DSideSelected::NX_NY;
                } else if (i == 1){
                    gizmoSideSelected = Gizmo2DSideSelected::NX;
                } else if (i == 2){
                    gizmoSideSelected = Gizmo2DSideSelected::NX_PY;
                } else if (i == 3){
                    gizmoSideSelected = Gizmo2DSideSelected::PY;
                } else if (i == 4){
                    gizmoSideSelected = Gizmo2DSideSelected::PX_PY;
                } else if (i == 5){
                    gizmoSideSelected = Gizmo2DSideSelected::PX;
                } else if (i == 6){
                    gizmoSideSelected = Gizmo2DSideSelected::PX_NY;
                } else if (i == 7){
                    gizmoSideSelected = Gizmo2DSideSelected::NY;
                }
            }
        }
    }

    return gizmoSideSelected;
}