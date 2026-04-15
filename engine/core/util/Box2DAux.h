
//
// (c) 2026 Eduardo Doria.
//

#ifndef Box2DAux_h
#define Box2DAux_h

#include "box2d/box2d.h"
#include "subsystem/PhysicsSystem.h"

namespace doriax{

    struct Box2DWorldRayCastOutput{
        b2BodyId bodyId;
        b2ShapeId shapeId;
        b2Vec2 point;
        b2Vec2 normal;
        float fraction;
    };

    typedef struct Box2DWorldRayCastContext{
        std::vector<Box2DWorldRayCastOutput>* outputs;
        bool onlyStatic;
    } Box2DWorldRayCastContext;


    class Box2DAux{
    public:

        static bool CollisionFilter(b2ShapeId shapeIdA, b2ShapeId shapeIdB, void* context){
            Scene* scene = (Scene*)context;

            size_t shapeIndexA = reinterpret_cast<size_t>(b2Shape_GetUserData(shapeIdA));
            size_t shapeIndexB = reinterpret_cast<size_t>(b2Shape_GetUserData(shapeIdB));

            b2BodyId bodyIdA = b2Shape_GetBody(shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(shapeIdB);

            Entity entityA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdA));
            Entity entityB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdB));

            return scene->getSystem<PhysicsSystem>()->shouldCollide2D.callRet(Body2D(scene, entityA), shapeIndexA, Body2D(scene, entityB), shapeIndexB, true);
        }

        static bool PreSolve(b2ShapeId shapeIdA, b2ShapeId shapeIdB, b2Manifold* manifold, void* context){
            Scene* scene = (Scene*)context;

            size_t shapeIndexA = reinterpret_cast<size_t>(b2Shape_GetUserData(shapeIdA));
            size_t shapeIndexB = reinterpret_cast<size_t>(b2Shape_GetUserData(shapeIdB));

            b2BodyId bodyIdA = b2Shape_GetBody(shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(shapeIdB);

            Entity entityA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdA));
            Entity entityB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdB));

            return scene->getSystem<PhysicsSystem>()->preSolve2D.callRet(Body2D(scene, entityA), shapeIndexA, Body2D(scene, entityB), shapeIndexB, Manifold2D(scene, manifold), true);
        }

        static void manageEvents(Scene* scene, b2WorldId world){
            b2ContactEvents contactEvents = b2World_GetContactEvents(world);
            b2SensorEvents sensorEvents = b2World_GetSensorEvents(world);

            for (int i = 0; i < contactEvents.beginCount; ++i){
                b2ContactBeginTouchEvent* beginEvent = contactEvents.beginEvents + i;

                size_t shapeIndexA = reinterpret_cast<size_t>(b2Shape_GetUserData(beginEvent->shapeIdA));
                size_t shapeIndexB = reinterpret_cast<size_t>(b2Shape_GetUserData(beginEvent->shapeIdB));

                b2BodyId bodyIdA = b2Shape_GetBody(beginEvent->shapeIdA);
                b2BodyId bodyIdB = b2Shape_GetBody(beginEvent->shapeIdB);

                Entity entityA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdA));
                Entity entityB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdB));

                scene->getSystem<PhysicsSystem>()->beginContact2D.call(Body2D(scene, entityA), shapeIndexA, Body2D(scene, entityB), shapeIndexB);
            }

            for (int i = 0; i < contactEvents.endCount; ++i){
                b2ContactEndTouchEvent* endEvent = contactEvents.endEvents + i;

                size_t shapeIndexA = reinterpret_cast<size_t>(b2Shape_GetUserData(endEvent->shapeIdA));
                size_t shapeIndexB = reinterpret_cast<size_t>(b2Shape_GetUserData(endEvent->shapeIdB));

                b2BodyId bodyIdA = b2Shape_GetBody(endEvent->shapeIdA);
                b2BodyId bodyIdB = b2Shape_GetBody(endEvent->shapeIdB);

                Entity entityA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdA));
                Entity entityB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdB));

                scene->getSystem<PhysicsSystem>()->endContact2D.call(Body2D(scene, entityA), shapeIndexA, Body2D(scene, entityB), shapeIndexB);
            }

            for (int i = 0; i < contactEvents.hitCount; ++i){
                b2ContactHitEvent* hitEvent = contactEvents.hitEvents + i;

                size_t shapeIndexA = reinterpret_cast<size_t>(b2Shape_GetUserData(hitEvent->shapeIdA));
                size_t shapeIndexB = reinterpret_cast<size_t>(b2Shape_GetUserData(hitEvent->shapeIdB));

                b2BodyId bodyIdA = b2Shape_GetBody(hitEvent->shapeIdA);
                b2BodyId bodyIdB = b2Shape_GetBody(hitEvent->shapeIdB);

                Entity entityA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdA));
                Entity entityB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdB));

                Vector2 point(hitEvent->point.x, hitEvent->point.y);
                Vector2 normal(hitEvent->normal.x, hitEvent->normal.y);

                scene->getSystem<PhysicsSystem>()->hitContact2D.call(Body2D(scene, entityA), shapeIndexA, Body2D(scene, entityB), shapeIndexB, point, normal, hitEvent->approachSpeed);
            }

            for (int i = 0; i < sensorEvents.beginCount; ++i){
                b2SensorBeginTouchEvent* beginTouch = sensorEvents.beginEvents + i;

                size_t shapeIndexSensor = reinterpret_cast<size_t>(b2Shape_GetUserData(beginTouch->sensorShapeId));
                size_t shapeIndexVisitor = reinterpret_cast<size_t>(b2Shape_GetUserData(beginTouch->visitorShapeId));

                b2BodyId bodyIdSensor = b2Shape_GetBody(beginTouch->sensorShapeId);
                b2BodyId bodyIdVisitor = b2Shape_GetBody(beginTouch->visitorShapeId);

                Entity entitySensor = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdSensor));
                Entity entityVisitor = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdVisitor));

                scene->getSystem<PhysicsSystem>()->beginSensorContact2D.call(Body2D(scene, entitySensor), shapeIndexSensor, Body2D(scene, entityVisitor), shapeIndexVisitor);
            }

            for (int i = 0; i < sensorEvents.endCount; ++i){
                b2SensorEndTouchEvent* endTouch = sensorEvents.endEvents + i;

                size_t shapeIndexSensor = reinterpret_cast<size_t>(b2Shape_GetUserData(endTouch->sensorShapeId));
                size_t shapeIndexVisitor = reinterpret_cast<size_t>(b2Shape_GetUserData(endTouch->visitorShapeId));

                b2BodyId bodyIdSensor = b2Shape_GetBody(endTouch->sensorShapeId);
                b2BodyId bodyIdVisitor = b2Shape_GetBody(endTouch->visitorShapeId);

                Entity entitySensor = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdSensor));
                Entity entityVisitor = reinterpret_cast<uintptr_t>(b2Body_GetUserData(bodyIdVisitor));

                scene->getSystem<PhysicsSystem>()->endSensorContact2D.call(Body2D(scene, entitySensor), shapeIndexSensor, Body2D(scene, entityVisitor), shapeIndexVisitor);
            }
        }

        // fraction return to fint the closest intersection
        // return 1 to store all intersections
        // https://www.iforce2d.net/b2dtut/world-querying
        static float CastCallback(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context){
            Box2DWorldRayCastContext* ctx = (Box2DWorldRayCastContext*)context;

            b2BodyId bodyId = b2Shape_GetBody(shapeId);
            if (ctx->onlyStatic && (b2Body_GetType(bodyId) != b2_staticBody)){
                return 1.0;
            }
            ctx->outputs->push_back({bodyId, shapeId, point, normal, fraction});

            return 1.0f;
        }

    };

/*
    class Box2DContactFilter : public b2ContactFilter{
    private:
        Scene* scene;
        PhysicsSystem* physicsSystem;

    public:

        Box2DContactFilter(Scene* scene, PhysicsSystem* physicsSystem){
            this->scene = scene;
            this->physicsSystem = physicsSystem;
        }
        
        bool ShouldCollide(b2Fixture* fixtureA, b2Fixture* fixtureB){
            unsigned long shapeIndexA = fixtureA->GetUserData().pointer;
            Entity entityA = fixtureA->GetBody()->GetUserData().pointer;
            Body2D bodyA(scene, entityA);

            unsigned long shapeIndexB = fixtureB->GetUserData().pointer;
            Entity entityB = fixtureB->GetBody()->GetUserData().pointer;
            Body2D bodyB(scene, entityB);

            return physicsSystem->shouldCollide2D.callRet(bodyA, shapeIndexA, bodyB, shapeIndexB, true);
        }
    };


    class Box2DContactListener : public b2ContactListener{
    private:
        Scene* scene;
        PhysicsSystem* physicsSystem;

    public:

        Box2DContactListener(Scene* scene, PhysicsSystem* physicsSystem){
            this->scene = scene;
            this->physicsSystem = physicsSystem;
        }
        
        void BeginContact(b2Contact* contact){
            physicsSystem->beginContact2D.call(Contact2D(scene, contact));
        }
        
        void EndContact(b2Contact* contact){
            physicsSystem->endContact2D.call(Contact2D(scene, contact));
        }
        
        void PreSolve(b2Contact* contact, const b2Manifold* oldManifold){
            physicsSystem->preSolve2D.call(Contact2D(scene, contact), Manifold2D(scene, oldManifold));
        }
        
        void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse){
            physicsSystem->postSolve2D.call(Contact2D(scene, contact), ContactImpulse2D(impulse));
        }
    };


    struct B2_API Box2DWorldRayCastOutput{
        b2Fixture* fixture;
        b2Vec2 point;
	    b2Vec2 normal;
	    float fraction;
    };


    class Box2DRayCastCallback : public b2RayCastCallback{
    private:

        std::vector<Box2DWorldRayCastOutput>* outputs;
        bool returnAllResults;
        bool onlyStatic;
        uint16_t collisionGroup;
        uint16_t collisionMask;

	public:

        Box2DRayCastCallback(std::vector<Box2DWorldRayCastOutput>* outputs){
            this->outputs = outputs;
            this->returnAllResults = true;
            this->onlyStatic = false;
            this->collisionGroup = (uint16_t)~0u;
            this->collisionMask = (uint16_t)~0u;
        }

        Box2DRayCastCallback(std::vector<Box2DWorldRayCastOutput>* outputs, bool returnAllResults){
            this->outputs = outputs;
            this->returnAllResults = returnAllResults;
            this->onlyStatic = false;
            this->collisionGroup = (uint16_t)~0u;
            this->collisionMask = (uint16_t)~0u;
        }

        Box2DRayCastCallback(std::vector<Box2DWorldRayCastOutput>* outputs, bool returnAllResults, bool onlyStatic){
            this->outputs = outputs;
            this->returnAllResults = returnAllResults;
            this->onlyStatic = onlyStatic;
            this->collisionGroup = (uint16_t)~0u;
            this->collisionMask = (uint16_t)~0u;
        }

        Box2DRayCastCallback(std::vector<Box2DWorldRayCastOutput>* outputs, bool returnAllResults, bool onlyStatic, uint16_t collisionGroup, uint16_t collisionMask){
            this->outputs = outputs;
            this->returnAllResults = returnAllResults;
            this->onlyStatic = onlyStatic;
            this->collisionGroup = collisionGroup;
            this->collisionMask = collisionMask;
        }

		float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction){
            if (fixture->IsSensor())
                return -1.f;

            if (onlyStatic && fixture->GetBody()->GetType() != b2BodyType::b2_staticBody)
                return -1.f;

            if ((fixture->GetFilterData().categoryBits & collisionMask) && (fixture->GetFilterData().maskBits & collisionGroup)){
                // fraction return to fint the closest intersection
                // return 1 to store all intersections
                // https://www.iforce2d.net/b2dtut/world-querying
                if (returnAllResults){
                    outputs->push_back({fixture, point, normal, fraction});
                    return 1.0f;
                }else{
                    outputs->clear();
                    outputs->push_back({fixture, point, normal, fraction});
                    return fraction;
                }

            }

            return -1.f;
        }
    };
*/
}

#endif //Box2DAux_h
