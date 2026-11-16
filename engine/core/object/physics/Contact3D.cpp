// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Contact3D.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/ContactListener.h"

using namespace doriax;

Contact3D::Contact3D(Scene* scene, size_t shapeIndex1, size_t shapeIndex2, const JPH::ContactManifold* contactManifold, JPH::ContactSettings* contactSettings){
    this->scene = scene;
    this->shapeIndex1 = shapeIndex1;
    this->shapeIndex2 = shapeIndex2;
    this->contactManifold = contactManifold;
    this->contactSettings = contactSettings;
}

Contact3D::~Contact3D(){

}

Contact3D::Contact3D(const Contact3D& rhs){
    scene = rhs.scene;
    shapeIndex1 = rhs.shapeIndex1;
    shapeIndex2 = rhs.shapeIndex2;
    contactManifold = rhs.contactManifold;
    contactSettings = rhs.contactSettings;
}

Contact3D& Contact3D::operator=(const Contact3D& rhs){
    scene = rhs.scene;
    shapeIndex1 = rhs.shapeIndex1;
    shapeIndex2 = rhs.shapeIndex2;
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
    return shapeIndex1;
}

size_t Contact3D::getShapeIndex2() const{
    return shapeIndex2;
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

