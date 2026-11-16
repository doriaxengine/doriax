// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef JoltPhysicsAux_h
#define JoltPhysicsAux_h

#include "subsystem/PhysicsSystem.h"
#include "object/physics/Body3D.h"
#include "object/physics/CollideShapeResult3D.h"
#include "object/physics/Contact3D.h"

#include <mutex>

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
#include "Jolt/Physics/Collision/CollideShape.h"

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

#include <algorithm>
#include <vector>

namespace doriax{

	class OnlyStaticBodyFilter : public JPH::BodyFilter{
	private:
		bool onlyStatic;
		const std::vector<Entity>* ignoreEntities = nullptr;

		bool isIgnored(Entity entity) const{
			if (!ignoreEntities){
				return false;
			}
			for (Entity ignored : *ignoreEntities){
				if (ignored != NULL_ENTITY && entity == ignored){
					return true;
				}
			}
			return false;
		}

	public:
		explicit OnlyStaticBodyFilter(const bool onlyStatic, const std::vector<Entity>* ignoreEntities = nullptr){
			this->onlyStatic = onlyStatic;
			this->ignoreEntities = ignoreEntities;
		}

		virtual bool ShouldCollideLocked(const JPH::Body &inBody) const override{
			if (onlyStatic && inBody.GetMotionType() != JPH::EMotionType::Static){
				return false;
			}
			if (isIgnored(static_cast<Entity>(inBody.GetUserData()))){
				return false;
			}
			return true;
		}
	};

	// Jolt calls listeners from its worker threads, but scripts run one at a time
	inline std::recursive_mutex joltCallbackMutex;

	// a script run while Jolt may hold bodies; restores the flag even when it throws
	struct JoltCallbackScope {
		PhysicsSystem* system;
		bool previous;
		explicit JoltCallbackScope(PhysicsSystem* s): system(s), previous(s->insideCallback3D) { s->insideCallback3D = true; }
		~JoltCallbackScope() { system->insideCallback3D = previous; }
	};

	// the body of a Body3D
	inline bool isBody3D(Scene* scene, const JPH::BodyID& id, Entity entity){
		Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);
		return body && body->body == id;
	}

	// slower than 1 cm/s it stands, as eased input never reaches zero
	inline bool isCharacterStanding3D(const Character3DComponent& character){
		return JPH::Vec3(character.moveVelocity.x, 0.0f, character.moveVelocity.z).IsNearZero(1.0e-4f);
	}

	// the inner body of a character, on an entity that is not a Body3D
	inline bool isCharacterBody3D(Scene* scene, const JPH::BodyID& id, Entity entity){
		Character3DComponent* character = scene->findComponent<Character3DComponent>(entity);
		return character && character->character && character->character->GetInnerBodyID() == id && !scene->findComponent<Body3DComponent>(entity);
	}

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
			if (physicsSystem->onBodyActivated3D.empty()){
				return;
			}

			std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);
			// a character's inner body is not a Body3D
			if (!isBody3D(scene, inBodyID, inBodyUserData)){
				return;
			}
			JoltCallbackScope scope(physicsSystem);
			physicsSystem->onBodyActivated3D(Body3D(scene, inBodyUserData));
		}

		virtual void OnBodyDeactivated(const JPH::BodyID &inBodyID, uint64_t inBodyUserData) override{
			if (physicsSystem->onBodyDeactivated3D.empty()){
				return;
			}

			std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);
			if (!isBody3D(scene, inBodyID, inBodyUserData)){
				return;
			}
			JoltCallbackScope scope(physicsSystem);
			physicsSystem->onBodyDeactivated3D(Body3D(scene, inBodyUserData));
		}
	};

	// What a character walks into: shouldCollide3D decides at once, the contact events run
	// once its update ends. Sensors find it through its inner body instead.
	class JoltCharacterListener : public JPH::CharacterContactListener{
	private:
		enum class Kind { ADDED, PERSISTED, REMOVED };

		struct Pending {
			Kind kind;
			const JPH::CharacterVirtual* source;
			Entity character;
			JPH::BodyID body;
			JPH::SubShapeID subShape;
			JPH::RVec3 position;
			JPH::Vec3 normal; // from the character to the body
			float distance;
		};

		Scene* scene;
		PhysicsSystem* physicsSystem;
		std::vector<Pending> pending;

		// an entity with a Body3D too counts as its body
		bool reports(const JPH::CharacterVirtual* character) const{
			return !scene->findComponent<Body3DComponent>(static_cast<Entity>(character->GetUserData()));
		}

		void add(Kind kind, const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact){
			const bool heard = kind == Kind::ADDED ? !physicsSystem->onContactAdded3D.empty() : !physicsSystem->onContactPersisted3D.empty();
			if (heard && !contact.mIsSensorB && reports(character)){
				pending.push_back({kind, character, static_cast<Entity>(character->GetUserData()), contact.mBodyB, contact.mSubShapeIDB,
					contact.mPosition, -contact.mContactNormal, contact.mDistance});
			}
		}

		size_t shapeIndex(const JPH::BodyID& id, const JPH::SubShapeID& subShape) const{
			JPH::BodyLockRead lock(physicsSystem->getWorld3D()->GetBodyLockInterfaceNoLock(), id);
			return lock.Succeeded() ? lock.GetBody().GetShape()->GetSubShapeUserData(subShape) : 0;
		}

	public:
		JoltCharacterListener(Scene* scene, PhysicsSystem* physicsSystem){
			this->scene = scene;
			this->physicsSystem = physicsSystem;
		}

		virtual bool OnContactValidate(const JPH::CharacterVirtual *inCharacter, const JPH::CharacterContact &inContact) override{
			const Entity body = static_cast<Entity>(inContact.mUserData);
			if (physicsSystem->shouldCollide3D.empty() || inContact.mIsSensorB || !isBody3D(scene, inContact.mBodyB, body) || !reports(inCharacter)){
				return true;
			}

			std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);

			JPH::CollideShapeResult result;
			result.mContactPointOn1 = JPH::Vec3::sZero();
			result.mContactPointOn2 = JPH::Vec3::sZero();
			result.mPenetrationAxis = -inContact.mContactNormal;
			result.mPenetrationDepth = -inContact.mDistance;
			result.mSubShapeID2 = inContact.mSubShapeIDB;
			result.mBodyID2 = inContact.mBodyB;
			const Vector3 position(inContact.mPosition.GetX(), inContact.mPosition.GetY(), inContact.mPosition.GetZ());

			// the character is still moving
			JoltCallbackScope scope(physicsSystem);
			return physicsSystem->shouldCollide3D.callRet(Collider3D(scene, static_cast<Entity>(inCharacter->GetUserData())), Collider3D(scene, body),
				position, CollideShapeResult3D(scene, 0, shapeIndex(inContact.mBodyB, inContact.mSubShapeIDB), &result), true);
		}

		virtual void OnContactAdded(const JPH::CharacterVirtual *inCharacter, const JPH::CharacterContact &inContact, JPH::CharacterContactSettings &ioSettings) override{
			add(Kind::ADDED, inCharacter, inContact);
		}

		virtual void OnContactPersisted(const JPH::CharacterVirtual *inCharacter, const JPH::CharacterContact &inContact, JPH::CharacterContactSettings &ioSettings) override{
			add(Kind::PERSISTED, inCharacter, inContact);
		}

		virtual void OnContactRemoved(const JPH::CharacterVirtual *inCharacter, const JPH::BodyID &inBodyID2, const JPH::SubShapeID &inSubShapeID2) override{
			if (physicsSystem->onContactRemoved3D.empty() || !reports(inCharacter)){
				return;
			}
			pending.push_back({Kind::REMOVED, inCharacter, static_cast<Entity>(inCharacter->GetUserData()), inBodyID2, inSubShapeID2, JPH::RVec3::sZero(), JPH::Vec3::sZero(), 0.0f});
		}

		// standing still, a character stays on a slope it can walk, yet is still pushed out of it
		virtual void OnContactSolve(const JPH::CharacterVirtual *inCharacter, const JPH::BodyID &inBodyID2, const JPH::SubShapeID &inSubShapeID2,
				JPH::RVec3Arg inContactPosition, JPH::Vec3Arg inContactNormal, JPH::Vec3Arg inContactVelocity, const JPH::PhysicsMaterial *inContactMaterial,
				JPH::Vec3Arg inCharacterVelocity, JPH::Vec3 &ioNewCharacterVelocity) override{
			Character3DComponent* character = scene->findComponent<Character3DComponent>(static_cast<Entity>(inCharacter->GetUserData()));
			if (character && isCharacterStanding3D(*character) && inContactVelocity.IsNearZero() && !inCharacter->IsSlopeTooSteep(inContactNormal)){
				ioNewCharacterVelocity = std::max(0.0f, ioNewCharacterVelocity.Dot(inContactNormal)) * inContactNormal;
			}
		}

		// a character destroyed, which an entity made later may reuse the id of
		void forget(const JPH::CharacterVirtual* character){
			pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending& event){ return event.source == character; }), pending.end());
		}

		// the contacts since the last call, without what a callback has destroyed since
		void dispatchContacts(){
			std::vector<Pending> events;
			events.swap(pending);

			for (const Pending& event : events){
				Character3DComponent* character = scene->findComponent<Character3DComponent>(event.character);
				if (!character || character->character != event.source){
					continue;
				}

				Entity entity = NULL_ENTITY;
				size_t shape = 0;
				JPH::ContactSettings settings;
				{
					JPH::BodyLockRead lock(physicsSystem->getWorld3D()->GetBodyLockInterfaceNoLock(), event.body);
					if (!lock.Succeeded()){
						continue;
					}
					const JPH::Body& body = lock.GetBody();
					entity = static_cast<Entity>(body.GetUserData());
					shape = body.GetShape()->GetSubShapeUserData(event.subShape);
					settings.mCombinedFriction = body.GetFriction();
					settings.mCombinedRestitution = body.GetRestitution();
				}
				Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);
				if (!body || body->body != event.body || body->sensor){
					continue;
				}

				if (event.kind == Kind::REMOVED){
					physicsSystem->onContactRemoved3D(Collider3D(scene, event.character), Collider3D(scene, entity), 0, (unsigned long)shape);
					continue;
				}

				// one point, where the character touches
				JPH::ContactManifold manifold;
				manifold.mBaseOffset = event.position;
				manifold.mWorldSpaceNormal = event.normal;
				manifold.mPenetrationDepth = -event.distance;
				manifold.mSubShapeID2 = event.subShape;
				manifold.mRelativeContactPointsOn1.push_back(JPH::Vec3::sZero());
				manifold.mRelativeContactPointsOn2.push_back(JPH::Vec3::sZero());

				Contact3D contact(scene, 0, shape, &manifold, &settings);
				if (event.kind == Kind::ADDED){
					physicsSystem->onContactAdded3D(Collider3D(scene, event.character), Collider3D(scene, entity), contact);
				}else{
					physicsSystem->onContactPersisted3D(Collider3D(scene, event.character), Collider3D(scene, entity), contact);
				}
			}
		}
	};

	class JoltContactListener : public JPH::ContactListener{
    private:
        Scene* scene;
        PhysicsSystem* physicsSystem;

        // a body, or the inner body of a character
        bool isCollider3D(const JPH::BodyID& id, Entity entity) const{
            return isBody3D(scene, id, entity) || isCharacterBody3D(scene, id, entity);
        }

        // two bodies, or a character in a sensor; the rest a character touches it reports itself
        bool isReported(const JPH::BodyID& id1, Entity entity1, const JPH::BodyID& id2, Entity entity2) const{
            if (isBody3D(scene, id1, entity1) && isBody3D(scene, id2, entity2)){
                return true;
            }
            auto sensor = [&](const JPH::BodyID& id, Entity entity){
                return isBody3D(scene, id, entity) && scene->getComponent<Body3DComponent>(entity).sensor;
            };
            return (isCharacterBody3D(scene, id1, entity1) && sensor(id2, entity2)) || (isCharacterBody3D(scene, id2, entity2) && sensor(id1, entity1));
        }

        template<typename Event>
        void dispatchContact(Event& event, const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings){
            if (event.empty()){
                return;
            }

            std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);

            const Entity entity1 = inBody1.GetUserData();
            const Entity entity2 = inBody2.GetUserData();
            if (!isReported(inBody1.GetID(), entity1, inBody2.GetID(), entity2)){
                return;
            }

            const size_t shape1 = inBody1.GetShape()->GetSubShapeUserData(inManifold.mSubShapeID1);
            const size_t shape2 = inBody2.GetShape()->GetSubShapeUserData(inManifold.mSubShapeID2);
            event(Collider3D(scene, entity1), Collider3D(scene, entity2), Contact3D(scene, shape1, shape2, &inManifold, &ioSettings));
        }

	public:
        JoltContactListener(Scene* scene, PhysicsSystem* physicsSystem){
            this->scene = scene;
            this->physicsSystem = physicsSystem;
        }

		// See: ContactListener
		virtual JPH::ValidateResult	OnContactValidate(const JPH::Body &inBody1, const JPH::Body &inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult &inCollisionResult) override{
			if (physicsSystem->shouldCollide3D.empty()){
				return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
			}

			std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);

			Entity entity1 = inBody1.GetUserData();
			Entity entity2 = inBody2.GetUserData();

			if (!isCollider3D(inBody1.GetID(), entity1) || !isCollider3D(inBody2.GetID(), entity2)){
				return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
			}

			const size_t shape1 = inBody1.GetShape()->GetSubShapeUserData(inCollisionResult.mSubShapeID1);
			const size_t shape2 = inBody2.GetShape()->GetSubShapeUserData(inCollisionResult.mSubShapeID2);
			if (!physicsSystem->shouldCollide3D.callRet(Collider3D(scene, entity1), Collider3D(scene, entity2), Vector3(inBaseOffset.GetX(), inBaseOffset.GetY(), inBaseOffset.GetZ()),
					CollideShapeResult3D(scene, shape1, shape2, &inCollisionResult), true)){
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			}

			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override{
			dispatchContact(physicsSystem->onContactAdded3D, inBody1, inBody2, inManifold, ioSettings);
		}

		virtual void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override{
			dispatchContact(physicsSystem->onContactPersisted3D, inBody1, inBody2, inManifold, ioSettings);
		}

		virtual void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override{
			if (physicsSystem->onContactRemoved3D.empty()){
				return;
			}

			std::lock_guard<std::recursive_mutex> lock(joltCallbackMutex);

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

			if (!isReported(body1Id, entity1, body2Id, entity2)){
				return;
			}

			size_t shapeIndex1 = shape1->GetSubShapeUserData(inSubShapePair.GetSubShapeID1());
			size_t shapeIndex2 = shape2->GetSubShapeUserData(inSubShapePair.GetSubShapeID2());

			physicsSystem->onContactRemoved3D(Collider3D(scene, entity1), Collider3D(scene, entity2), (unsigned long)shapeIndex1, (unsigned long)shapeIndex2);
		}
	};

}

#endif //JoltPhysicsAux_h
