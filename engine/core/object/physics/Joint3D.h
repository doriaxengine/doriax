// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef JOINT3D_H
#define JOINT3D_H

#include "EntityHandle.h"
#include "component/Joint3DComponent.h"

namespace doriax{

    class DORIAX_API Joint3D: public EntityHandle{
    public:
        Joint3D(Scene* scene);
        Joint3D(Scene* scene, Entity entity);
        virtual ~Joint3D();

        Joint3D(const Joint3D& rhs);
        Joint3D& operator=(const Joint3D& rhs);

        JPH::TwoBodyConstraint* getJoltJoint() const;

        void setFixedJoint(Entity bodyA, Entity bodyB); 
        void setDistanceJoint(Entity bodyA, Entity bodyB);
        void setDistanceJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB);    
        void setPointJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor);
        void setHingeJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 axis, Vector3 normal);
        void setConeJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 twistAxis);
        void setPrismaticJoint(Entity bodyA, Entity bodyB, Vector3 sliderAxis, float limitsMin, float limitsMax);
        void setSwingTwistJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 twistAxis, Vector3 planeAxis, float normalHalfConeAngle, float planeHalfConeAngle, float twistMinAngle, float twistMaxAngle);
        void setSixDOFJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB, Vector3 axisX, Vector3 axisY);
        void setPathJoint(Entity bodyA, Entity bodyB, std::vector<Vector3> positions, std::vector<Vector3> tangents, std::vector<Vector3> normals, Vector3 pathPosition, bool isLooping);
        void setGearJoint(Entity bodyA, Entity bodyB, Entity hingeA, Entity hingeB, int numTeethGearA, int numTeethGearB);
        void setRackAndPinionJoint(Entity bodyA, Entity bodyB, Entity hinge, Entity slider, int numTeethRack, int numTeethGear, int rackLength);
        void setPulleyJoint(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB, Vector3 fixedPointA, Vector3 fixedPointB);
        
        Joint3DType getType();

        // hinge and prismatic only
        void setMotorState(Joint3DMotorState state);
        Joint3DMotorState getMotorState() const;

        void setMotorTargetVelocity(float velocity);
        float getMotorTargetVelocity() const;

        void setMotorTargetPosition(float position);
        float getMotorTargetPosition() const;

        void setMotorFrequency(float frequency);
        float getMotorFrequency() const;

        void setMotorDamping(float damping);
        float getMotorDamping() const;

        void setMotorMaxForce(float maxForce);
        float getMotorMaxForce() const;

        // hinge angle or prismatic position
        float getCurrentPosition() const;
    };
}

#endif //BODY3D_H