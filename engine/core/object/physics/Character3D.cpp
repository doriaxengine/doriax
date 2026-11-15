// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Character3D.h"

#include "object/Object.h"
#include "subsystem/PhysicsSystem.h"
#include "util/Angle.h"

using namespace doriax;

static_assert((int)JPH::CharacterVirtual::EGroundState::OnGround == (int)Character3DGroundState::ON_GROUND
    && (int)JPH::CharacterVirtual::EGroundState::InAir == (int)Character3DGroundState::IN_AIR, "Character3DGroundState must keep Jolt's order");

Character3D::Character3D(Scene* scene, Entity entity): Collider3D(scene, entity){
}

Character3D::~Character3D(){
}

Character3D::Character3D(const Character3D& rhs): Collider3D(rhs){
}

Character3D& Character3D::operator=(const Character3D& rhs){
    Collider3D::operator = (rhs);

    return *this;
}

void Character3D::checkCharacter(const Character3DComponent& character) const{
    if (!character.character){
        // a script's init runs before the scene's first load
        scene->getSystem<PhysicsSystem>()->loadCharacter3D(entity);

        if (!character.character){
            Log::error("Character3D is not loaded");
            throw std::runtime_error("Character3D is not loaded");
        }
    }
}

JPH::CharacterVirtual* Character3D::getJoltCharacter() const{
    Character3DComponent& character = getComponent<Character3DComponent>();

    checkCharacter(character);

    return character.character;
}

void Character3D::load(){
    Character3DComponent& character = getComponent<Character3DComponent>();
    character.needReloadCharacter = true;

    scene->getSystem<PhysicsSystem>()->loadCharacter3D(entity);
}

Vector3 Character3D::getMoveVelocity() const{
    return getComponent<Character3DComponent>().moveVelocity;
}

void Character3D::setMoveVelocity(Vector3 moveVelocity){
    getComponent<Character3DComponent>().moveVelocity = moveVelocity;
}

void Character3D::jump(float speed){
    Character3DComponent& character = getComponent<Character3DComponent>();

    character.jumpSpeed = speed;
    character.needJump = true;
}

Vector3 Character3D::getLinearVelocity() const{
    JPH::Vec3 vec = getJoltCharacter()->GetLinearVelocity();

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

void Character3D::setLinearVelocity(Vector3 linearVelocity){
    getJoltCharacter()->SetLinearVelocity(JPH::Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z));
}

Vector3 Character3D::getPosition() const{
    JPH::RVec3 position = getJoltCharacter()->GetPosition();

    return Vector3(position.GetX(), position.GetY(), position.GetZ());
}

void Character3D::setPosition(Vector3 position){
    Character3DComponent& character = getComponent<Character3DComponent>();

    checkCharacter(character);
    scene->getSystem<PhysicsSystem>()->setCharacter3DPosition(entity, character, position);
}

Character3DGroundState Character3D::getGroundState() const{
    return static_cast<Character3DGroundState>(getJoltCharacter()->GetGroundState());
}

bool Character3D::isOnGround() const{
    return getJoltCharacter()->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

bool Character3D::isSupported() const{
    return getJoltCharacter()->IsSupported();
}

Vector3 Character3D::getGroundPosition() const{
    JPH::RVec3 position = getJoltCharacter()->GetGroundPosition();

    return Vector3(position.GetX(), position.GetY(), position.GetZ());
}

Vector3 Character3D::getGroundNormal() const{
    JPH::Vec3 vec = getJoltCharacter()->GetGroundNormal();

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Vector3 Character3D::getGroundVelocity() const{
    JPH::Vec3 vec = getJoltCharacter()->GetGroundVelocity();

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Entity Character3D::getGroundEntity() const{
    return static_cast<Entity>(getJoltCharacter()->GetGroundUserData());
}

bool Character3D::isSlopeTooSteep(Vector3 normal) const{
    return getJoltCharacter()->IsSlopeTooSteep(JPH::Vec3(normal.x, normal.y, normal.z));
}

bool Character3D::setShape(float height, float radius){
    Character3DComponent& character = getComponent<Character3DComponent>();

    checkCharacter(character);

    return scene->getSystem<PhysicsSystem>()->setCharacter3DShape(entity, character, height, radius, true);
}

float Character3D::getHeight() const{
    return getComponent<Character3DComponent>().height;
}

float Character3D::getRadius() const{
    return getComponent<Character3DComponent>().radius;
}

float Character3D::getMaxSlopeAngle() const{
    return getComponent<Character3DComponent>().maxSlopeAngle;
}

void Character3D::setMaxSlopeAngle(float maxSlopeAngle){
    Character3DComponent& character = getComponent<Character3DComponent>();
    character.maxSlopeAngle = maxSlopeAngle;

    if (character.character){
        character.character->SetMaxSlopeAngle(Angle::defaultToRad(maxSlopeAngle));
    }
}

float Character3D::getStepUp() const{
    return getComponent<Character3DComponent>().stepUp;
}

void Character3D::setStepUp(float stepUp){
    getComponent<Character3DComponent>().stepUp = stepUp;
}

float Character3D::getStepDown() const{
    return getComponent<Character3DComponent>().stepDown;
}

void Character3D::setStepDown(float stepDown){
    getComponent<Character3DComponent>().stepDown = stepDown;
}

float Character3D::getMass() const{
    return getComponent<Character3DComponent>().mass;
}

void Character3D::setMass(float mass){
    Character3DComponent& character = getComponent<Character3DComponent>();
    character.mass = mass;

    if (character.character){
        character.character->SetMass(mass);
    }
}

float Character3D::getMaxStrength() const{
    return getComponent<Character3DComponent>().maxStrength;
}

void Character3D::setMaxStrength(float maxStrength){
    Character3DComponent& character = getComponent<Character3DComponent>();
    character.maxStrength = maxStrength;

    if (character.character){
        character.character->SetMaxStrength(maxStrength);
    }
}

float Character3D::getGravityFactor() const{
    return getComponent<Character3DComponent>().gravityFactor;
}

void Character3D::setGravityFactor(float gravityFactor){
    getComponent<Character3DComponent>().gravityFactor = gravityFactor;
}

float Character3D::getAirControl() const{
    return getComponent<Character3DComponent>().airControl;
}

void Character3D::setAirControl(float airControl){
    getComponent<Character3DComponent>().airControl = airControl;
}
