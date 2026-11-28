// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CHARACTER3D_H
#define CHARACTER3D_H

#ifndef DORIAX_PHYSICS_3D
#error "Character3D needs 3D Physics, enabled in Project Settings > Build"
#endif

#include "Collider3D.h"
#include "component/Character3DComponent.h"

namespace doriax{

    class DORIAX_API Character3D: public Collider3D{
    protected:
        void checkCharacter(const Character3DComponent& character) const;

    public:
        Character3D(Scene* scene, Entity entity);
        virtual ~Character3D();

        Character3D(const Character3D& rhs);
        Character3D& operator=(const Character3D& rhs);

        JPH::CharacterVirtual* getJoltCharacter() const;

        void load();

        // along the ground, kept until changed
        Vector3 getMoveVelocity() const;
        void setMoveVelocity(Vector3 moveVelocity);

        // leaves the ground with this up speed at the next step
        void jump(float speed);

        Vector3 getLinearVelocity() const;
        void setLinearVelocity(Vector3 linearVelocity);

        // feet in world space
        Vector3 getPosition() const;
        void setPosition(Vector3 position);

        Character3DGroundState getGroundState() const;
        bool isOnGround() const;
        // on the ground or on a slope too steep to walk
        bool isSupported() const;
        Vector3 getGroundPosition() const;
        Vector3 getGroundNormal() const;
        Vector3 getGroundVelocity() const;
        Entity getGroundEntity() const;
        bool isSlopeTooSteep(Vector3 normal) const;

        // false when it does not fit, like under a low ceiling
        bool setShape(float height, float radius);
        float getHeight() const;
        float getRadius() const;

        float getMaxSlopeAngle() const;
        void setMaxSlopeAngle(float maxSlopeAngle);

        // from the bottom of the lower sphere, lower drops it off edges sooner, never under maxSlopeAngle
        float getSupportAngle() const;
        void setSupportAngle(float supportAngle);

        float getStepUp() const;
        void setStepUp(float stepUp);

        float getStepDown() const;
        void setStepDown(float stepDown);

        float getMass() const;
        void setMass(float mass);

        float getMaxStrength() const;
        void setMaxStrength(float maxStrength);

        float getGravityFactor() const;
        void setGravityFactor(float gravityFactor);

        // 1 steers in the air as on the ground, 0 keeps the momentum
        float getAirControl() const;
        void setAirControl(float airControl);

        // a kinematic character only follows its Transform
        bool isKinematic() const;
        void setKinematic(bool kinematic);
    };
}

#endif //CHARACTER3D_H
