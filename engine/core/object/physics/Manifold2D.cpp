//
// (c) 2026 Eduardo Doria.
//

#include "Manifold2D.h"

#include "PhysicsSystem.h"

using namespace doriax;

Manifold2D::Manifold2D(Scene* scene, const b2Manifold* manifold){
    this->scene = scene;
    this->manifold = manifold;
}

Manifold2D::~Manifold2D(){

}

Manifold2D::Manifold2D(const Manifold2D& rhs){
    scene = rhs.scene;
    manifold = rhs.manifold;

}

Manifold2D& Manifold2D::operator=(const Manifold2D& rhs){
    scene = rhs.scene;
    manifold = rhs.manifold;
    
    return *this;
}

const b2Manifold* Manifold2D::getBox2DManifold() const{
    return manifold;
}

Vector2 Manifold2D::getManifoldPointAnchorA(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 point = manifold->points[index].anchorA;
    return Vector2(point.x * pointsToMeterScale, point.y * pointsToMeterScale);
}

Vector2 Manifold2D::getManifoldPointAnchorB(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 point = manifold->points[index].anchorB;
    return Vector2(point.x * pointsToMeterScale, point.y * pointsToMeterScale);
}

Vector2 Manifold2D::getManifoldPointPosition(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    b2Vec2 localPoint = manifold->points[index].point;
    return Vector2(localPoint.x * pointsToMeterScale, localPoint.y * pointsToMeterScale);
}

float Manifold2D::getManifoldPointNormalImpulse(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    return manifold->points[index].normalImpulse;
}

float Manifold2D::getManifoldPointNormalVelocity(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    return manifold->points[index].normalVelocity;
}

float Manifold2D::getManifoldPointTangentImpulse(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    return manifold->points[index].tangentImpulse;
}

float Manifold2D::getManifoldPointSeparation(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    return manifold->points[index].separation;
}

bool Manifold2D::isManifoldPointPersisted(int32_t index) const{
    //TODO: check index by b2_maxManifoldPoints
    return manifold->points[index].persisted;
}

Vector2 Manifold2D::getNormal() const{
    float pointsToMeterScale = scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
    return Vector2(manifold->normal.x * pointsToMeterScale, manifold->normal.y * pointsToMeterScale);
}

int32_t Manifold2D::getPointCount() const{
    return manifold->pointCount;
}