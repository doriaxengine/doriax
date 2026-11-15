// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Collider3D.h"

#include "Body3D.h"
#include "Character3D.h"
#include "object/Object.h"

using namespace doriax;

Collider3D::Collider3D(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

Object Collider3D::getAttachedObject(){
    return Object(scene, entity);
}

bool Collider3D::isBody3D() const{
    return scene->findComponent<Body3DComponent>(entity) != NULL;
}

bool Collider3D::isCharacter3D() const{
    return !isBody3D() && scene->findComponent<Character3DComponent>(entity) != NULL;
}

Body3D Collider3D::getBody3D() const{
    return Body3D(scene, entity);
}

Character3D Collider3D::getCharacter3D() const{
    return Character3D(scene, entity);
}

Vector3 Collider3D::getPosition() const{
    return isCharacter3D() ? getCharacter3D().getPosition() : getBody3D().getPosition();
}

Vector3 Collider3D::getLinearVelocity() const{
    return isCharacter3D() ? getCharacter3D().getLinearVelocity() : getBody3D().getLinearVelocity();
}
