// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include "SubSystem.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#ifdef DORIAX_PHYSICS_2D
#include "component/Body2DComponent.h"
#include "component/Joint2DComponent.h"
#include "object/physics/Contact2D.h"
#include "object/physics/Manifold2D.h"
#include "box2d/box2d.h"
#endif

#ifdef DORIAX_PHYSICS_3D
#include "object/physics/Body3D.h"
#include "object/physics/CollideShapeResult3D.h"
#include "object/physics/Contact3D.h"
#include "Jolt/Jolt.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Physics/Body/MotionQuality.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterMask.h"
#endif

#include <unordered_set>

namespace doriax{

	class JoltActivationListener;
	class JoltContactListener;
	

	class DORIAX_API PhysicsSystem : public SubSystem {

	private:
		Vector2 gravity2D;
		Vector3 gravity3D;

#ifdef DORIAX_PHYSICS_2D
		b2WorldId world2D;
		float pointsToMeterScale2D;
#endif
#ifdef DORIAX_PHYSICS_3D
		bool lock3DBodies;
		bool steppingWorld3D = false;

		void flushPendingDestroys3D();
		bool useLockedInterfaces3D() const;

		JoltActivationListener* activationListener3D;
		JoltContactListener* contactListener3D;

        // Process-wide singletons shared by every scene; not owned here.
        JPH::TempAllocatorImpl* temp_allocator;
        JPH::JobSystemThreadPool* job_system;

		JPH::PhysicsSystem world3D;

		// Handles a teardown could not release while stepping. After world3D so they are
		// destroyed first, releasing while the world they belong to is still alive.
		std::vector<JPH::BodyID> pendingBodyDestroy3D;
		std::vector<JPH::Ref<JPH::TwoBodyConstraint>> pendingJointDestroy3D;

		JPH::BroadPhaseLayerInterfaceMask* broad_phase_layer_interface;
		JPH::ObjectVsBroadPhaseLayerFilterMask* object_vs_broadphase_layer_filter;
		JPH::ObjectLayerPairFilterMask* object_vs_object_layer_filter;

		std::unordered_set<Entity> warnedNonNormalizedRotations;
		std::unordered_set<Entity> reportedInvalidRotations;
#endif

#ifdef DORIAX_PHYSICS_2D
		void ensureWorld2D();
		void updateBody2DPosition(Signature signature, Entity entity, Body2DComponent& body);
		bool loadJoint2D(Entity entity, Joint2DComponent& joint);
		bool syncBody2DShapes(Entity entity, Body2DComponent& body);
		static Vector2 absScale2D(const Vector2& scale);
		static float maxScaleXY(const Vector2& scale);
#endif
#ifdef DORIAX_PHYSICS_3D
		void updateBody3DPosition(Signature signature, Entity entity, Body3DComponent& body);
		bool loadJoint3D(Entity entity, Joint3DComponent& joint);
		bool syncBody3DShapes(Entity entity, Body3DComponent& body);
		bool isShapeSourceLoading(Entity entity, const Shape3D& shapeData) const;
		bool createShape3DForIndex(Entity entity, Body3DComponent& body, size_t index);

		bool createGenericJoltBody(Entity entity, Body3DComponent& body, const JPH::ShapeRefC shape);

		static Vector3 absScale(const Vector3& scale);
		static float maxScaleXZ(const Vector3& scale);
		static float maxScaleXYZ(const Vector3& scale);
#endif

	public:
		PhysicsSystem(Scene* scene);
		virtual ~PhysicsSystem();

		Vector2 getGravity2D() const;
		void setGravity2D(Vector2 gravity);
		void setGravity2D(float x, float y);

		Vector3 getGravity3D() const;
		void setGravity3D(Vector3 gravity);
		void setGravity3D(float x, float y, float z);

		// sets both 2D (x, y) and 3D worlds
		Vector3 getGravity() const;
		void setGravity(Vector3 gravity);
		void setGravity(float x, float y);
		void setGravity(float x, float y, float z);

#ifdef DORIAX_PHYSICS_2D
		float getPointsToMeterScale2D() const;
		void setPointsToMeterScale2D(float pointsToMeterScale2D);
#endif

#ifdef DORIAX_PHYSICS_3D
        void setLock3DBodies(bool lock3DBodies);
        bool isLock3DBodies() const;

        // Everything reaching a Jolt body goes through these: during a step the locking
        // variants deadlock, as the calling thread already holds the locks. Queries take
        // body locks of their own, so a ray cast needs the no-lock one too.
        JPH::BodyInterface& getBodyInterface3D();
        const JPH::BodyLockInterface& getBodyLockInterface3D();
        const JPH::NarrowPhaseQuery& getNarrowPhaseQuery3D();

        // True inside a contact, activation or collision callback. Jolt keeps the broad
        // phase and the constraint list locked for the whole step, so a body or joint
        // cannot be created then (refused, retried next update) and teardown is queued.
        bool isSteppingWorld3D() const;

		static JPH::EMotionQuality getBody3DMotionQualityToJolt(Body3DMotionQuality motionQuality);
#endif

#ifdef DORIAX_PHYSICS_2D
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> beginContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> endContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> beginSensorContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> endSensorContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)> hitContact2D;
		FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)> preSolve2D;

		FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)> shouldCollide2D;
#endif

#ifdef DORIAX_PHYSICS_3D
		FunctionSubscribe<void(Body3D)> onBodyActivated3D;
		FunctionSubscribe<void(Body3D)> onBodyDeactivated3D;
		FunctionSubscribe<void(Body3D, Body3D, Contact3D)> onContactAdded3D;
		FunctionSubscribe<void(Body3D, Body3D, Contact3D)> onContactPersisted3D;
		FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)> onContactRemoved3D;

		FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)> shouldCollide3D;
#endif

#ifdef DORIAX_PHYSICS_2D
		void createBody2D(Entity entity);
		void removeBody2D(Entity entity);
#endif

#ifdef DORIAX_PHYSICS_3D
		void createBody3D(Entity entity);
		void removeBody3D(Entity entity);
#endif

#ifdef DORIAX_PHYSICS_2D
		b2WorldId getWorld2D() const;
		b2BodyId getBody(Entity entity);

		bool loadBody2D(Entity entity);
		void destroyBody2D(Body2DComponent& body);
#endif

#ifdef DORIAX_PHYSICS_3D
		JPH::PhysicsSystem* getWorld3D();
		bool loadBody3D(Entity entity);
		void destroyBody3D(Body3DComponent& body);
#endif

		// Writes a body pose back into its entity, so the transform sync of the next
		// step does not undo a pose written straight to the body.
#ifdef DORIAX_PHYSICS_2D
		void updateTransformFromBody2D(Entity entity, Vector2 position, float angle);
#endif
#if defined(DORIAX_PHYSICS_2D) || defined(DORIAX_PHYSICS_3D)
		void updateTransformFromBody3D(Entity entity, Vector3 position, Quaternion rotation);
#endif

#ifdef DORIAX_PHYSICS_3D
		JPH::Quat toValidatedJoltRotation(const Quaternion& rotation, Entity entity, int shapeIndex);
#endif

#ifdef DORIAX_PHYSICS_2D
		int loadShape2D(Body2DComponent& body, void* shape, Shape2DType type);
		void destroyShape2D(Body2DComponent& body, size_t index);
#endif

#ifdef DORIAX_PHYSICS_3D
		int loadShape3D(Body3DComponent& body, const Vector3& position, const Quaternion& rotation, JPH::ShapeSettings* shapeSettings);
		void destroyShape3D(Body3DComponent& body, size_t index);
#endif

#ifdef DORIAX_PHYSICS_2D
		bool loadDistanceJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchorA, Vector2 anchorB, bool autoAnchors, bool rope);
		bool loadRevoluteJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor);
		bool loadPrismaticJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis);
		//bool loadPulleyJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 groundAnchorA, Vector2 groundAnchorB, Vector2 anchorA, Vector2 anchorB, Vector2 axis, float ratio);
		//bool loadGearJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Entity revoluteJoint, Entity prismaticJoint, float ratio);
		bool loadMouseJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 target);
		bool loadWheelJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis);
		bool loadWeldJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor);
		bool loadMotorJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB);
		void destroyJoint2D(Joint2DComponent& joint);
#endif

#ifdef DORIAX_PHYSICS_3D
		bool loadFixedJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB);
		bool loadDistanceJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, bool autoAnchors);
		bool loadPointJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor);
		bool loadHingeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 axis, Vector3 normal);
		bool loadConeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis);
		bool loadPrismaticJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 sliderAxis, float limitsMin, float limitsMax);
		bool loadSwingTwistJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis, Vector3 planeAxis, float normalHalfConeAngle, float planeHalfConeAngle, float twistMinAngle, float twistMaxAngle);
		bool loadSixDOFJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 axisX, Vector3 axisY);
		bool loadPathJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, std::vector<Vector3> positions, std::vector<Vector3> tangents, std::vector<Vector3> normals, Vector3 pathPosition, bool isLooping);
		bool loadGearJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hingeA, Entity hingeB, int numTeethGearA, int numTeethGearB);
		bool loadRackAndPinionJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hinge, Entity slider, int numTeethRack, int numTeethGear, int rackLength);
		bool loadPulleyJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 fixedPointA, Vector3 fixedPointB);
		void destroyJoint3D(Joint3DComponent& joint);

		void addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude);
		void addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude, uint32_t groupsToExclude);
#endif

		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;
		void fixedUpdate(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //PHYSICSSYSTEM_H
