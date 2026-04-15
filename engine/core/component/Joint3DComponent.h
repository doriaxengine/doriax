//
// (c) 2026 Eduardo Doria.
//

#ifndef JOINT3D_COMPONENT_H
#define JOINT3D_COMPONENT_H

#include "Engine.h"
#include "Jolt/Physics/Constraints/TwoBodyConstraint.h"

namespace doriax{

    enum class Joint3DType{
        FIXED,
        DISTANCE,
        POINT,
        HINGE,
        CONE,
        PRISMATIC, // slider in Jolt
        SWINGTWIST,
        SIXDOF,
        PATH,
        GEAR,
        RACKANDPINON,
        PULLEY
    };

    struct DORIAX_API Joint3DComponent{
        JPH::TwoBodyConstraint* joint = NULL;
        Joint3DType type = Joint3DType::FIXED;

        Entity bodyA = NULL_ENTITY;
        Entity bodyB = NULL_ENTITY;

        Vector3 anchorA = Vector3::ZERO;
        Vector3 anchorB = Vector3::ZERO;
        Vector3 anchor = Vector3::ZERO;
        Vector3 axis = Vector3::ZERO;
        Vector3 normal = Vector3::ZERO;
        Vector3 twistAxis = Vector3::ZERO;
        Vector3 planeAxis = Vector3::ZERO;
        Vector3 axisX = Vector3::ZERO;
        Vector3 axisY = Vector3::ZERO;

        float limitsMin = 0.0f;
        float limitsMax = 0.0f;
        float normalHalfConeAngle = 45.0f;
        float planeHalfConeAngle = 45.0f;
        float twistMinAngle = -45.0f;
        float twistMaxAngle = 45.0f;

        Vector3 fixedPointA = Vector3::ZERO;
        Vector3 fixedPointB = Vector3::ZERO;

        Entity hingeA = NULL_ENTITY;
        Entity hingeB = NULL_ENTITY;
        Entity hinge = NULL_ENTITY;
        Entity slider = NULL_ENTITY;

        int numTeethGearA = 20;
        int numTeethGearB = 20;
        int numTeethRack = 20;
        int numTeethGear = 20;
        int rackLength = 100;

        std::vector<Vector3> pathPoints;
        Vector3 pathPosition = Vector3::ZERO;
        bool isLooping = false;
        bool autoAnchors = true;

        bool needUpdateJoint = true;
    };

}

#endif //JOINT3D_COMPONENT_H