// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "PhysicsSystem.h"
#include "Scene.h"

using namespace doriax;

#if !defined(DORIAX_PHYSICS_2D) && !defined(DORIAX_PHYSICS_3D)

PhysicsSystem::PhysicsSystem(Scene* scene): SubSystem(scene){
    SceneSettings settingsDefaults;
    gravity2D = settingsDefaults.gravity2D;
    gravity3D = settingsDefaults.gravity3D;
}

PhysicsSystem::~PhysicsSystem() = default;

Vector2 PhysicsSystem::getGravity2D() const{
    return gravity2D;
}

void PhysicsSystem::setGravity2D(Vector2 gravity){
    gravity2D = gravity;
}

void PhysicsSystem::setGravity2D(float x, float y){
    setGravity2D(Vector2(x, y));
}

Vector3 PhysicsSystem::getGravity3D() const{
    return gravity3D;
}

void PhysicsSystem::setGravity3D(Vector3 gravity){
    gravity3D = gravity;
}

void PhysicsSystem::setGravity3D(float x, float y, float z){
    setGravity3D(Vector3(x, y, z));
}

Vector3 PhysicsSystem::getGravity() const{
    return gravity3D;
}

void PhysicsSystem::setGravity(Vector3 gravity){
    setGravity2D(gravity.x, gravity.y);
    setGravity3D(gravity);
}

void PhysicsSystem::setGravity(float x, float y){
    setGravity(Vector3(x, y, 0));
}

void PhysicsSystem::setGravity(float x, float y, float z){
    setGravity(Vector3(x, y, z));
}

void PhysicsSystem::load(){}
void PhysicsSystem::draw(){}
void PhysicsSystem::destroy(){}
void PhysicsSystem::update(double dt){ (void)dt; }
void PhysicsSystem::fixedUpdate(double dt){ (void)dt; }
void PhysicsSystem::onComponentAdded(Entity entity, ComponentId componentId){ (void)entity; (void)componentId; }
void PhysicsSystem::onComponentRemoved(Entity entity, ComponentId componentId){ (void)entity; (void)componentId; }

#endif
