// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Manifold2D.h"

#include "PhysicsSystem.h"

using namespace doriax;

Manifold2D::Manifold2D(Scene* scene, const b2Manifold* manifold, b2Vec2 originA){
    this->scene = scene;
    this->manifold = manifold;
    this->originA = originA;
}

Manifold2D::~Manifold2D(){

}

Manifold2D::Manifold2D(const Manifold2D& rhs){
    scene = rhs.scene;
    manifold = rhs.manifold;
    originA = rhs.originA;

}

Manifold2D& Manifold2D::operator=(const Manifold2D& rhs){
    scene = rhs.scene;
    manifold = rhs.manifold;
    originA = rhs.originA;
    
    return *this;
}

const b2Manifold* Manifold2D::getBox2DManifold() const{
    return manifold;
}

bool Manifold2D::hasPoint(int32_t index) const{
    if (index < 0 || index >= manifold->pointCount){
        Log::error("Cannot find manifold point %i", index);
        return false;
    }
    return true;
}

Vector2 Manifold2D::getManifoldPointAnchorA(int32_t index) const{
    if (!hasPoint(index)) return Vector2::ZERO;

    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 point = manifold->points[index].anchorA;
    return Vector2(point.x * pointsToMeterScale, point.y * pointsToMeterScale);
}

Vector2 Manifold2D::getManifoldPointAnchorB(int32_t index) const{
    if (!hasPoint(index)) return Vector2::ZERO;

    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 point = manifold->points[index].anchorB;
    return Vector2(point.x * pointsToMeterScale, point.y * pointsToMeterScale);
}

Vector2 Manifold2D::getManifoldPointPosition(int32_t index) const{
    if (!hasPoint(index)) return Vector2::ZERO;

    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 point = originA + manifold->points[index].anchorA;
    return Vector2(point.x * pointsToMeterScale, point.y * pointsToMeterScale);
}

float Manifold2D::getManifoldPointNormalImpulse(int32_t index) const{
    if (!hasPoint(index)) return 0;

    return manifold->points[index].normalImpulse;
}

float Manifold2D::getManifoldPointNormalVelocity(int32_t index) const{
    if (!hasPoint(index)) return 0;

    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    return manifold->points[index].normalVelocity * pointsToMeterScale;
}

float Manifold2D::getManifoldPointTangentImpulse(int32_t index) const{
    if (!hasPoint(index)) return 0;

    return manifold->points[index].tangentImpulse;
}

float Manifold2D::getManifoldPointSeparation(int32_t index) const{
    if (!hasPoint(index)) return 0;

    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    return manifold->points[index].separation * pointsToMeterScale;
}

bool Manifold2D::isManifoldPointPersisted(int32_t index) const{
    if (!hasPoint(index)) return false;

    return manifold->points[index].persisted;
}

Vector2 Manifold2D::getNormal() const{
    return Vector2(manifold->normal.x, manifold->normal.y);
}

int32_t Manifold2D::getPointCount() const{
    return manifold->pointCount;
}