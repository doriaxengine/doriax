//
// (c) 2026 Eduardo Doria.
//

#include "CollideShapeResult3D.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/CollideShape.h"

using namespace doriax;

CollideShapeResult3D::CollideShapeResult3D(Scene* scene, const JPH::Body* body1, const JPH::Body* body2, const JPH::CollideShapeResult* collideShapeResult){
    this->scene = scene;
    this->body1 = body1;
    this->body2 = body2;
    this->collideShapeResult = collideShapeResult;
}

CollideShapeResult3D::~CollideShapeResult3D(){

}

CollideShapeResult3D::CollideShapeResult3D(const CollideShapeResult3D& rhs){
    scene = rhs.scene;
    body1 = rhs.body1;
    body2 = rhs.body2;
    collideShapeResult = rhs.collideShapeResult;
}

CollideShapeResult3D& CollideShapeResult3D::operator=(const CollideShapeResult3D& rhs){
    scene = rhs.scene;
    body1 = rhs.body1;
    body2 = rhs.body2;
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
    return body1->GetShape()->GetSubShapeUserData(collideShapeResult->mSubShapeID1);
}

size_t CollideShapeResult3D::getShapeIndex2() const{
    return body2->GetShape()->GetSubShapeUserData(collideShapeResult->mSubShapeID2);
}