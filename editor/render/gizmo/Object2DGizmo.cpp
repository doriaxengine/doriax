// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Object2DGizmo.h"

using namespace doriax;

const float editor::Object2DGizmo::rectSize = 0.20;
const float editor::Object2DGizmo::sizeOffset = 0.1;
const float editor::Object2DGizmo::crossSize = 0.5f;

editor::Object2DGizmo::Object2DGizmo(Scene* scene): Object(scene){
    width = 0.0;
    height = 0.0;
    showRects = true;
    showCross = false;

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
    Vector4 color(0.9f, 0.5f, 0.3f, 1.0f);

    if (showCross){
        // Draw a fat cross: 3 tightly grouped vertical lines + 3 tightly grouped horizontal lines
        const float s = crossSize * 0.6f;
        const float gap = crossSize * 0.03f;  // small spacing between the 3 parallel lines
        // vertical group
        lines->addLine(Vector3(-gap, -s, 0), Vector3(-gap,  s, 0), color);
        lines->addLine(Vector3(   0, -s, 0), Vector3(   0,  s, 0), color);
        lines->addLine(Vector3( gap, -s, 0), Vector3( gap,  s, 0), color);
        // horizontal group
        lines->addLine(Vector3(-s, -gap, 0), Vector3( s, -gap, 0), color);
        lines->addLine(Vector3(-s,    0, 0), Vector3( s,    0, 0), color);
        lines->addLine(Vector3(-s,  gap, 0), Vector3( s,  gap, 0), color);
    }

    lines->setVisible(showCross);
}

void editor::Object2DGizmo::setCenter(Vector3 point){
    center->setPosition(point);
}

void editor::Object2DGizmo::setSize(float width, float height){
    if (this->width != width || this->height != height){
        this->width = width;
        this->height = height;

        updateRects();
    }
}

void editor::Object2DGizmo::setShowRects(bool showRects){
    this->showRects = showRects;
    updateRects();
}

void editor::Object2DGizmo::setCrossVisible(bool show){
    if (showCross == show) return;
    showCross = show;
    updateLines();
}

editor::Gizmo2DSideSelected editor::Object2DGizmo::checkHover(const Ray& ray, const OBB& obb){
    Gizmo2DSideSelected gizmoSideSelected = Gizmo2DSideSelected::NONE;

    if (showCross){
        // Compute the cross world position directly from the locally-set values,
        // avoiding reliance on TransformSystem having propagated worldPosition yet.
        Vector3 scale = getScale();
        Vector3 centerPos = getPosition() + getRotation() * (center->getPosition() * scale);
        float halfSize = crossSize * scale.x * 0.6f;
        AABB crossAABB(centerPos - Vector3(halfSize, halfSize, 0),
                       centerPos + Vector3(halfSize, halfSize, 0));
        if (ray.intersects(crossAABB)){
            gizmoSideSelected = Gizmo2DSideSelected::CENTER;
        }
    } else if (ray.intersects(obb)){
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