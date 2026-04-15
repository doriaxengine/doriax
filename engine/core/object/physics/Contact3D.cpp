//
// (c) 2026 Eduardo Doria.
//

#include "Contact3D.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/ContactListener.h"

using namespace doriax;

Contact3D::Contact3D(Scene* scene, const JPH::Body* body1, const JPH::Body* body2, const JPH::ContactManifold* contactManifold, JPH::ContactSettings* contactSettings){
    this->scene = scene;
    this->body1 = body1;
    this->body2 = body2;
    this->contactManifold = contactManifold;
    this->contactSettings = contactSettings;
}

Contact3D::~Contact3D(){

}

Contact3D::Contact3D(const Contact3D& rhs){
    scene = rhs.scene;
    body1 = rhs.body1;
    body2 = rhs.body2;
    contactManifold = rhs.contactManifold;
    contactSettings = rhs.contactSettings;
}

Contact3D& Contact3D::operator=(const Contact3D& rhs){
    scene = rhs.scene;
    body1 = rhs.body1;
    body2 = rhs.body2;
    contactManifold = rhs.contactManifold;
    contactSettings = rhs.contactSettings;
    
    return *this;
}

const JPH::ContactManifold* Contact3D::getJoltContactManifold() const{
    return contactManifold;
}

JPH::ContactSettings* Contact3D::getJoltContactSettings() const{
    return contactSettings;
}

Vector3 Contact3D::getBaseOffset() const{
    return Vector3(contactManifold->mBaseOffset.GetX(), contactManifold->mBaseOffset.GetY(), contactManifold->mBaseOffset.GetZ());
}

Vector3 Contact3D::getWorldSpaceNormal() const{
    return Vector3(contactManifold->mWorldSpaceNormal.GetX(), contactManifold->mWorldSpaceNormal.GetY(), contactManifold->mWorldSpaceNormal.GetZ());
}

float Contact3D::getPenetrationDepth() const{
    return contactManifold->mPenetrationDepth;
}

size_t Contact3D::getShapeIndex1() const{
    return body1->GetShape()->GetSubShapeUserData(contactManifold->mSubShapeID1);
}

size_t Contact3D::getShapeIndex2() const{
    return body2->GetShape()->GetSubShapeUserData(contactManifold->mSubShapeID2);
}

Vector3 Contact3D::getRelativeContactPointsOnA(size_t index) const{
    JPH::Vec3 point = contactManifold->mRelativeContactPointsOn1[index];
    return Vector3(point.GetX(), point.GetY(), point.GetZ());
}

Vector3 Contact3D::getRelativeContactPointsOnB(size_t index) const{
    JPH::Vec3 point = contactManifold->mRelativeContactPointsOn2[index];
    return Vector3(point.GetX(), point.GetY(), point.GetZ());
}

float Contact3D::getCombinedFriction() const{
    return contactSettings->mCombinedFriction;
}

void Contact3D::setCombinedFriction(float combinedFriction){
    contactSettings->mCombinedFriction = combinedFriction;
}

float Contact3D::getCombinedRestitution() const{
    return contactSettings->mCombinedRestitution;
}

void Contact3D::setCombinedRestitution(float combinedRestitution){
    contactSettings->mCombinedRestitution = combinedRestitution;
}

bool Contact3D::isSensor() const{
    return contactSettings->mIsSensor;
}

void Contact3D::setIsSensor(bool sensor){
    contactSettings->mIsSensor = sensor;
}

