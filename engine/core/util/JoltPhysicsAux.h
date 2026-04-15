//
// (c) 2026 Eduardo Doria.
//

#ifndef JoltPhysicsAux_h
#define JoltPhysicsAux_h

#include "subsystem/PhysicsSystem.h"
#include "object/physics/Body3D.h"
#include "object/physics/CollideShapeResult3D.h"
#include "object/physics/Contact3D.h"

#include "Jolt/Jolt.h"

#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"

#include "Jolt/Physics/Body/BodyLockMulti.h"

#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"

#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/HeightFieldShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"

#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterMask.h"

#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Collision/GroupFilterTable.h"

#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "Jolt/Physics/Constraints/PointConstraint.h"
#include "Jolt/Physics/Constraints/HingeConstraint.h"
#include "Jolt/Physics/Constraints/ConeConstraint.h"
#include "Jolt/Physics/Constraints/SliderConstraint.h"
#include "Jolt/Physics/Constraints/SwingTwistConstraint.h"
#include "Jolt/Physics/Constraints/SixDOFConstraint.h"
#include "Jolt/Physics/Constraints/PathConstraint.h"
#include "Jolt/Physics/Constraints/PathConstraintPathHermite.h"
#include "Jolt/Physics/Constraints/GearConstraint.h"
#include "Jolt/Physics/Constraints/RackAndPinionConstraint.h"
#include "Jolt/Physics/Constraints/PulleyConstraint.h"

namespace doriax{

	class OnlyStaticBodyFilter : public JPH::BodyFilter{
	private:
		bool onlyStatic;

	public:
		explicit OnlyStaticBodyFilter(const bool onlyStatic){
			this->onlyStatic = onlyStatic;
		}

		virtual bool ShouldCollideLocked(const JPH::Body &inBody) const override{
			if (onlyStatic && inBody.GetMotionType() != JPH::EMotionType::Static){
				return false;
			}
			return true;
		}
	};

	class JoltActivationListener : public JPH::BodyActivationListener{
	private:
        Scene* scene;
        PhysicsSystem* physicsSystem;
	public:
        JoltActivationListener(Scene* scene, PhysicsSystem* physicsSystem){
            this->scene = scene;
            this->physicsSystem = physicsSystem;
        }

		virtual void OnBodyActivated(const JPH::BodyID &inBodyID, uint64_t inBodyUserData) override{
			physicsSystem->onBodyActivated3D(Body3D(scene, inBodyUserData));
		}

		virtual void OnBodyDeactivated(const JPH::BodyID &inBodyID, uint64_t inBodyUserData) override{
			physicsSystem->onBodyDeactivated3D(Body3D(scene, inBodyUserData));
		}
	};

	class JoltContactListener : public JPH::ContactListener{
    private:
        Scene* scene;
        PhysicsSystem* physicsSystem;
	public:
        JoltContactListener(Scene* scene, PhysicsSystem* physicsSystem){
            this->scene = scene;
            this->physicsSystem = physicsSystem;
        }

		// See: ContactListener
		virtual JPH::ValidateResult	OnContactValidate(const JPH::Body &inBody1, const JPH::Body &inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult &inCollisionResult) override{
			Entity entity1 = inBody1.GetUserData();
			Body3D body1(scene, entity1);

			Entity entity2 = inBody2.GetUserData();
			Body3D body2(scene, entity2);

			if (!physicsSystem->shouldCollide3D.callRet(body1, body2, Vector3(inBaseOffset.GetX(), inBaseOffset.GetY(), inBaseOffset.GetZ()), CollideShapeResult3D(scene, &inBody1, &inBody2, &inCollisionResult), true)){
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			}

			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override{
			Entity entity1 = inBody1.GetUserData();
			Body3D body1(scene, entity1);

			Entity entity2 = inBody2.GetUserData();
			Body3D body2(scene, entity2);

			physicsSystem->onContactAdded3D(body1, body2, Contact3D(scene, &inBody1, &inBody2, &inManifold, &ioSettings));
		}

		virtual void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override{
			Entity entity1 = inBody1.GetUserData();
			Body3D body1(scene, entity1);

			Entity entity2 = inBody2.GetUserData();
			Body3D body2(scene, entity2);

			physicsSystem->onContactPersisted3D(body1, body2, Contact3D(scene, &inBody1, &inBody2, &inManifold, &ioSettings));
		}

		virtual void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override{
			JPH::BodyInterface &body_interface = physicsSystem->getWorld3D()->GetBodyInterfaceNoLock();

			const JPH::BodyID body1Id = inSubShapePair.GetBody1ID();
			const JPH::BodyID body2Id = inSubShapePair.GetBody2ID();

			if (body1Id.IsInvalid() || body2Id.IsInvalid()){
				return;
			}

			if (!body_interface.IsAdded(body1Id) || !body_interface.IsAdded(body2Id)){
				return;
			}

			auto shape1 = body_interface.GetShape(body1Id);
			auto shape2 = body_interface.GetShape(body2Id);

			if (!shape1 || !shape2){
				return;
			}

			Entity entity1 = body_interface.GetUserData(body1Id);
			Entity entity2 = body_interface.GetUserData(body2Id);

			size_t shapeIndex1 = shape1->GetSubShapeUserData(inSubShapePair.GetSubShapeID1());
			size_t shapeIndex2 = shape2->GetSubShapeUserData(inSubShapePair.GetSubShapeID2());

			physicsSystem->onContactRemoved3D(Body3D(scene, entity1), Body3D(scene, entity2), (unsigned long)shapeIndex1, (unsigned long)shapeIndex2);
		}
	};

}

#endif //JoltPhysicsAux_h
