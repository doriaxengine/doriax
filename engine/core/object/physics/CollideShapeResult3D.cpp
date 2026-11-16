// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CollideShapeResult3D.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/CollideShape.h"

using namespace doriax;

CollideShapeResult3D::CollideShapeResult3D(Scene* scene, size_t shapeIndex1, size_t shapeIndex2, const JPH::CollideShapeResult* collideShapeResult){
    this->scene = scene;
    this->shapeIndex1 = shapeIndex1;
    this->shapeIndex2 = shapeIndex2;
    this->collideShapeResult = collideShapeResult;
}

CollideShapeResult3D::~CollideShapeResult3D(){

}

CollideShapeResult3D::CollideShapeResult3D(const CollideShapeResult3D& rhs){
    scene = rhs.scene;
    shapeIndex1 = rhs.shapeIndex1;
    shapeIndex2 = rhs.shapeIndex2;
    collideShapeResult = rhs.collideShapeResult;
}

CollideShapeResult3D& CollideShapeResult3D::operator=(const CollideShapeResult3D& rhs){
    scene = rhs.scene;
    shapeIndex1 = rhs.shapeIndex1;
    shapeIndex2 = rhs.shapeIndex2;
    collideShapeResult = rhs.collideShapeResult;
    
    return *this;
}

const JPH::CollideShapeResult* CollideShapeResult3D::getJoltCollideShapeResult() const{
    return collideShapeResult;
}

Vector3 CollideShapeResult3D::getContactPointOnA() const{
    return Vector3(collideShapeResult->mContactPointOn1.GetX(), collideShapeResult->mContactPointOn1.GetY(), collideShapeResult->mContactPointOn1.GetZ());
}

Vector3 CollideShapeResult3D::getContactPointOnB() const{
    return Vector3(collideShapeResult->mContactPointOn2.GetX(), collideShapeResult->mContactPointOn2.GetY(), collideShapeResult->mContactPointOn2.GetZ());
}

Vector3 CollideShapeResult3D::getPenetrationAxis() const{
    return Vector3(collideShapeResult->mPenetrationAxis.GetX(), collideShapeResult->mPenetrationAxis.GetY(), collideShapeResult->mPenetrationAxis.GetZ());
}

size_t CollideShapeResult3D::getShapeIndex1() const{
    return shapeIndex1;
}

size_t CollideShapeResult3D::getShapeIndex2() const{
    return shapeIndex2;
}