#include "AnchorGizmo.h"

using namespace doriax;

editor::AnchorGizmo::AnchorGizmo(Scene* scene): Object(scene){
    area = Rect(0.0f, 0.0f, 0.0f, 0.0f);

    anchorLeft = 0.0f;
    anchorTop = 1.0f;
    anchorRight = 1.0f;
    anchorBottom = 0.0f;

    center = new Object(scene);

    Vector4 color(0.2f, 0.85f, 1.0f, 1.0f);

    // Arrowhead (kite shape) along each corner diagonal
    constexpr float S = 0.70710678f; // 1/sqrt(2)
    constexpr float arrowLen = 0.28f;   // tip-to-barbs distance along diagonal
    constexpr float wingWidth = 0.09f;  // barb spread perpendicular to diagonal
    constexpr float notchLen = 0.22f;   // tip-to-notch distance (concave base)

    // Diagonal direction toward center for each corner
    constexpr float dx[] = { -1, -1, 1, 1 };
    constexpr float dy[] = { 1, -1, -1, 1 };

    for (int i = 0; i < 4; i++){
        // Diagonal unit vector
        float ddx = dx[i] * S;
        float ddy = dy[i] * S;
        // Perpendicular (90° CCW)
        float pdx = -ddy;
        float pdy =  ddx;

        float lbX = arrowLen * ddx + wingWidth * pdx;
        float lbY = arrowLen * ddy + wingWidth * pdy;
        float rbX = arrowLen * ddx - wingWidth * pdx;
        float rbY = arrowLen * ddy - wingWidth * pdy;
        float nX  = notchLen * ddx;
        float nY  = notchLen * ddy;

        points[i] = new Polygon(scene);
        // Triangle-strip order: leftBarb, tip, notch, rightBarb
        points[i]->addVertex(lbX, lbY);
        points[i]->addVertex(0.0f, 0.0f); // tip at origin
        points[i]->addVertex(nX, nY);
        points[i]->addVertex(rbX, rbY);

        points[i]->setColor(color);
        center->addChild(points[i]);
    }

    this->addChild(center);
}

editor::AnchorGizmo::~AnchorGizmo(){
    for (int i = 0; i < 4; i++){
        delete points[i];
    }
}

void editor::AnchorGizmo::updateVisual(){
    float clampedLeft = std::fmax(0.0f, std::fmin(1.0f, anchorLeft));
    float clampedTop = std::fmax(0.0f, std::fmin(1.0f, anchorTop));
    float clampedRight = std::fmax(0.0f, std::fmin(1.0f, anchorRight));
    float clampedBottom = std::fmax(0.0f, std::fmin(1.0f, anchorBottom));

    float areaX = area.getX();
    float areaY = area.getY();
    float areaWidth = area.getWidth();
    float areaHeight = area.getHeight();

    float xLeft = areaX + (clampedLeft * areaWidth);
    float xRight = areaX + (clampedRight * areaWidth);
    float yBottom = areaY + (clampedBottom * areaHeight);
    float yTop = areaY + (clampedTop * areaHeight);

    Vector3 p0(xLeft, yBottom, 0);
    Vector3 p1(xLeft, yTop, 0);
    Vector3 p2(xRight, yTop, 0);
    Vector3 p3(xRight, yBottom, 0);

    points[0]->setPosition(p0);
    points[1]->setPosition(p1);
    points[2]->setPosition(p2);
    points[3]->setPosition(p3);
}

void editor::AnchorGizmo::setArea(Rect area){
    if (this->area != area){
        this->area = area;
        updateVisual();
    }
}

void editor::AnchorGizmo::setAnchors(float left, float top, float right, float bottom){
    if (anchorLeft != left || anchorTop != top || anchorRight != right || anchorBottom != bottom){
        anchorLeft = left;
        anchorTop = top;
        anchorRight = right;
        anchorBottom = bottom;

        updateVisual();
    }
}
