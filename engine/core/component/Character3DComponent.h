// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CHARACTER3D_COMPONENT_H
#define CHARACTER3D_COMPONENT_H

#include "Engine.h"
#include "math/Vector3.h"

#ifdef DORIAX_PHYSICS_3D
#include "Jolt/Jolt.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#endif

namespace doriax{

    enum class Character3DGroundState{
        ON_GROUND,
        ON_STEEP_GROUND,
        NOT_SUPPORTED,
        IN_AIR
    };

    struct DORIAX_API Character3DComponent{
#ifdef DORIAX_PHYSICS_3D
        JPH::CharacterVirtual* character = NULL;
#endif
        // capsule standing on the entity origin
        float height = 1.8f;
        float radius = 0.3f;

        float maxSlopeAngle = 45.0f;
        float stepUp = 0.4f; // 0 does not climb steps
        float stepDown = 0.5f; // 0 does not stick to the ground
        float mass = 70.0f; // kg
        float maxStrength = 100.0f; // N, to push bodies
        float padding = 0.02f;
        float gravityFactor = 1.0f;
        float airControl = 1.0f;
        bool innerBody = true;
        // only follows its Transform, no gravity or movement of its own
        bool kinematic = false;

        Vector3 moveVelocity = Vector3::ZERO;
        float jumpSpeed = 0.0f;
        bool needJump = false;

        bool needReloadCharacter = true;
        Vector3 loadedScale = Vector3::UNIT_SCALE;
        // position before the last step, drawn towards the current one
        Vector3 previousPosition;
        // position physics last wrote to the Transform
        Vector3 syncedPosition;
    };

}

#endif //CHARACTER3D_COMPONENT_H
