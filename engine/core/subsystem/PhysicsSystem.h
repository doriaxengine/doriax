//
// (c) 2026 Eduardo Doria.
//

#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include "SubSystem.h"
#include "component/Body2DComponent.h"
#include "component/Joint2DComponent.h"
#include "object/physics/Contact2D.h"
#include "object/physics/Manifold2D.h"
#include "object/physics/Body3D.h"
#include "object/physics/CollideShapeResult3D.h"
#include "object/physics/Contact3D.h"

#include "box2d/box2d.h"

#include "Jolt/Jolt.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterMask.h"

namespace doriax{

	class JoltActivationListener;
	class JoltContactListener;
	

	class DORIAX_API PhysicsSystem : public SubSystem {

	private:
		Vector3 gravity;

		b2WorldId world2D;
		float pointsToMeterScale2D;
		bool lock3DBodies;

		JoltActivationListener* activationListener3D;
		JoltContactListener* contactListener3D;

        JPH::TempAllocatorImpl* temp_allocator;
        JPH::JobSystemThreadPool* job_system;

		JPH::PhysicsSystem world3D;

		JPH::BroadPhaseLayerInterfaceMask* broad_phase_layer_interface;
		JPH::ObjectVsBroadPhaseLayerFilterMask* object_vs_broadphase_layer_filter;
		JPH::ObjectLayerPairFilterMask* object_vs_object_layer_filter;

		void updateBody2DPosition(Signature signature, Entity entity, Body2DComponent& body);
		void updateBody3DPosition(Signature signature, Entity entity, Body3DComponent& body);
		bool loadJoint2D(Entity entity, Joint2DComponent& joint);
		bool loadJoint3D(Entity entity, Joint3DComponent& joint);

		bool syncBody2DShapes(Body2DComponent& body);
		bool syncBody3DShapes(Entity entity, Body3DComponent& body);
		bool createShape3DForIndex(Entity entity, Body3DComponent& body, size_t index);

		void createGenericJoltBody(Entity entity, Body3DComponent& body, const JPH::ShapeRefC shape);

	public:
		PhysicsSystem(Scene* scene);
		virtual ~PhysicsSystem();

		Vector3 getGravity() const;
		void setGravity(Vector3 gravity);
		void setGravity(float x, float y);
		void setGravity(float x, float y, float z);

		float getPointsToMeterScale2D() const;
		void setPointsToMeterScale2D(float pointsToMeterScale2D);

        void setLock3DBodies(bool lock3DBodies);
        bool isLock3DBodies() const;

		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> beginContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> endContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> beginSensorContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)> endSensorContact2D;
		FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)> hitContact2D;
		FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)> preSolve2D;

		FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)> shouldCollide2D;

		FunctionSubscribe<void(Body3D)> onBodyActivated3D;
		FunctionSubscribe<void(Body3D)> onBodyDeactivated3D;
		FunctionSubscribe<void(Body3D, Body3D, Contact3D)> onContactAdded3D;
		FunctionSubscribe<void(Body3D, Body3D, Contact3D)> onContactPersisted3D;
		FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)> onContactRemoved3D;

		FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)> shouldCollide3D;

		void createBody2D(Entity entity);
		void removeBody2D(Entity entity);

		void createBody3D(Entity entity);
		void removeBody3D(Entity entity);

		b2WorldId getWorld2D() const;
		JPH::PhysicsSystem* getWorld3D();

		b2BodyId getBody(Entity entity);

		bool loadBody2D(Entity entity);
		void destroyBody2D(Body2DComponent& body);

		bool loadBody3D(Entity entity);
		void destroyBody3D(Body3DComponent& body);

		int loadShape2D(Body2DComponent& body, void* shape, Shape2DType type);
		void destroyShape2D(Body2DComponent& body, size_t index);

		int loadShape3D(Body3DComponent& body, const Vector3& position, const Quaternion& rotation, JPH::ShapeSettings* shapeSettings);
		void destroyShape3D(Body3DComponent& body, size_t index);

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

		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //PHYSICSSYSTEM_H