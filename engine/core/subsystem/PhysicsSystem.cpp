//
// (c) 2026 Eduardo Doria.
//

#include "PhysicsSystem.h"
#include "Scene.h"
#include "util/Angle.h"

#include "util/Box2DAux.h"
#include "util/JoltPhysicsAux.h"

#include <algorithm>
#include <cmath>
#include <map>


using namespace doriax;


PhysicsSystem::PhysicsSystem(Scene* scene): SubSystem(scene){
	signature.set(scene->getComponentId<Body2DComponent>());

	this->scene = scene;

    this->gravity = Vector3(0, -9.81f, 0);
    this->pointsToMeterScale2D = 64.0;

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {this->gravity.x, this->gravity.y};
    world2D = b2CreateWorld(&worldDef);

    b2World_SetPreSolveCallback(world2D, Box2DAux::PreSolve, scene);
    // TODO: could there be a performance issue by checking this often?
    b2World_SetCustomFilterCallback(world2D, Box2DAux::CollisionFilter, scene);

    // https://github.com/jrouwe/JoltPhysics/issues/244
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    //JPH::Trace = TraceImpl;
    //JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

    // Create a factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    const unsigned int cMaxBodies = 1024;
    const unsigned int cNumBodyMutexes = 0;
    const unsigned int cMaxBodyPairs = 1024;
    const unsigned int cMaxContactConstraints = 1024;

    broad_phase_layer_interface = new JPH::BroadPhaseLayerInterfaceMask(MAX_BROADPHASELAYER_3D);
    object_vs_broadphase_layer_filter = new JPH::ObjectVsBroadPhaseLayerFilterMask(*broad_phase_layer_interface);
    object_vs_object_layer_filter = new JPH::ObjectLayerPairFilterMask();

    // Now we can create the actual physics system.
    world3D.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *broad_phase_layer_interface, *object_vs_broadphase_layer_filter, *object_vs_object_layer_filter);
    world3D.SetGravity(JPH::Vec3(this->gravity.x, this->gravity.y, this->gravity.z));

    temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    job_system = new JPH::JobSystemThreadPool (JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, JPH::thread::hardware_concurrency() - 1);

    activationListener3D = new JoltActivationListener(scene, this);
    world3D.SetBodyActivationListener(activationListener3D);

    contactListener3D = new JoltContactListener(scene, this);
    world3D.SetContactListener(contactListener3D);

    lock3DBodies = true;
}

PhysicsSystem::~PhysicsSystem(){
    b2DestroyWorld(world2D);
    world2D = b2_nullWorldId;

    delete activationListener3D;
    delete contactListener3D;

    //delete world3D;
    delete temp_allocator;
    delete job_system;
    delete broad_phase_layer_interface;
    delete object_vs_broadphase_layer_filter;
    delete object_vs_object_layer_filter;

    //delete JPH::Factory::sInstance;
    //JPH::UnregisterTypes();
}

Vector3 PhysicsSystem::getGravity() const{
    return gravity;
}

void PhysicsSystem::setGravity(Vector3 gravity){
    if (this->gravity != gravity){
        this->gravity = gravity;
        b2World_SetGravity(world2D, {gravity.x, gravity.y});
        world3D.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }
}

void PhysicsSystem::setGravity(float x, float y){
    setGravity(Vector3(x, y, 0));
}

void PhysicsSystem::setGravity(float x, float y, float z){
    setGravity(Vector3(x, y, z));
}

float PhysicsSystem::getPointsToMeterScale2D() const{
    return pointsToMeterScale2D;
}

void PhysicsSystem::setPointsToMeterScale2D(float pointsToMeterScale2D){
    this->pointsToMeterScale2D = pointsToMeterScale2D;
}


void PhysicsSystem::setLock3DBodies(bool lock3DBodies){
    this->lock3DBodies = lock3DBodies;
}

bool PhysicsSystem::isLock3DBodies() const{
    return this->lock3DBodies;
}

void PhysicsSystem::updateBody2DPosition(Signature signature, Entity entity, Body2DComponent& body){
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        if (b2Body_IsValid(body.body)){

            b2Vec2 bNewPosition = {transform.worldPosition.x / pointsToMeterScale2D, transform.worldPosition.y / pointsToMeterScale2D};
            float bNewAngle = Angle::defaultToRad(transform.worldRotation.getRoll());

            if (body.newBody && transform.needUpdate){
                bNewPosition = {transform.position.x / pointsToMeterScale2D, transform.position.y / pointsToMeterScale2D};
                bNewAngle = Angle::defaultToRad(transform.rotation.getRoll());
                if (transform.parent != NULL_ENTITY){
                    Log::warn("Body position and rotation cannot be obtained from world: %u (%s)", entity, scene->getEntityName(entity).c_str());
                }
            }

            b2Transform bTransform = b2Body_GetTransform(body.body);

            if (bTransform.p != bNewPosition || b2Rot_GetAngle(bTransform.q) != bNewAngle){
                b2Body_SetTransform(body.body, bNewPosition, b2MakeRot(bNewAngle));
                b2Body_SetAwake(body.body, true);
            }
        }
    }
}

void PhysicsSystem::updateBody3DPosition(Signature signature, Entity entity, Body3DComponent& body){
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        if (!body.body.IsInvalid()){
            JPH::Vec3 jNewPosition(transform.worldPosition.x, transform.worldPosition.y, transform.worldPosition.z);
            JPH::Quat jNewQuat(transform.worldRotation.x, transform.worldRotation.y, transform.worldRotation.z, transform.worldRotation.w);

            if (body.newBody && transform.needUpdate){
                jNewPosition = JPH::Vec3(transform.position.x, transform.position.y, transform.position.z);
                jNewQuat = JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
                if (transform.parent != NULL_ENTITY){
                    Log::warn("Body position and rotation cannot be obtained from world: %u (%s)", entity, scene->getEntityName(entity).c_str());
                }
            }

            JPH::BodyInterface &body_interface = world3D.GetBodyInterfaceNoLock();
            JPH::Vec3 jPosition;
            JPH::Quat jQuat;
            body_interface.GetPositionAndRotation(body.body, jPosition, jQuat);

            if (jPosition != jNewPosition || jQuat != jNewQuat){
                body_interface.SetPositionAndRotation(body.body, jNewPosition, jNewQuat, JPH::EActivation::Activate);
            }
        }
    }
}

bool PhysicsSystem::loadJoint2D(Entity entity, Joint2DComponent& joint){
    if (b2Joint_IsValid(joint.joint)){
        destroyJoint2D(joint);
    }

    if (joint.bodyA == NULL_ENTITY){
        return false;
    }

    switch (joint.type){
        case Joint2DType::DISTANCE:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadDistanceJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.anchorB, joint.autoAnchors, joint.rope);
        case Joint2DType::REVOLUTE:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadRevoluteJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.anchorA);
        case Joint2DType::PRISMATIC:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadPrismaticJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.axis);
        case Joint2DType::MOUSE:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadMouseJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.target);
        case Joint2DType::WHEEL:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadWheelJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.axis);
        case Joint2DType::WELD:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadWeldJoint2D(entity, joint, joint.bodyA, joint.bodyB, joint.anchorA);
        case Joint2DType::MOTOR:
            if (joint.bodyB == NULL_ENTITY) return false;
            return loadMotorJoint2D(entity, joint, joint.bodyA, joint.bodyB);
        default:
            return false;
    }
}

bool PhysicsSystem::loadJoint3D(Entity entity, Joint3DComponent& joint){
    if (joint.joint){
        destroyJoint3D(joint);
    }

    if (joint.bodyA == NULL_ENTITY || joint.bodyB == NULL_ENTITY){
        return false;
    }

    Signature signatureA = scene->getSignature(joint.bodyA);
    Signature signatureB = scene->getSignature(joint.bodyB);

    if (!signatureA.test(scene->getComponentId<Body3DComponent>()) || !signatureB.test(scene->getComponentId<Body3DComponent>())){
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    if (!loadBody3D(joint.bodyA) || !loadBody3D(joint.bodyB)){
        Log::error("Cannot create 3D joint, bodyA or bodyB has no valid physics shape/body");
        return false;
    }

    Body3DComponent& bodyAComp = scene->getComponent<Body3DComponent>(joint.bodyA);
    Body3DComponent& bodyBComp = scene->getComponent<Body3DComponent>(joint.bodyB);

    if (bodyAComp.body.IsInvalid() || bodyBComp.body.IsInvalid()){
        Log::error("Cannot create 3D joint, bodyA or bodyB body id is invalid");
        return false;
    }

    switch (joint.type){
        case Joint3DType::FIXED:
            return loadFixedJoint3D(joint, joint.bodyA, joint.bodyB);
        case Joint3DType::DISTANCE:
            return loadDistanceJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.anchorB, joint.autoAnchors);
        case Joint3DType::POINT:
            return loadPointJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchor);
        case Joint3DType::HINGE:
            return loadHingeJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchor, joint.axis, joint.normal);
        case Joint3DType::CONE:
            return loadConeJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchor, joint.twistAxis);
        case Joint3DType::PRISMATIC:
            return loadPrismaticJoint3D(joint, joint.bodyA, joint.bodyB, joint.axis, joint.limitsMin, joint.limitsMax);
        case Joint3DType::SWINGTWIST:
            return loadSwingTwistJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchor, joint.twistAxis, joint.planeAxis, joint.normalHalfConeAngle, joint.planeHalfConeAngle, joint.twistMinAngle, joint.twistMaxAngle);
        case Joint3DType::SIXDOF:
            return loadSixDOFJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.anchorB, joint.axisX, joint.axisY);
        case Joint3DType::PATH: {
            if (joint.pathPoints.size() < 2){
                Log::error("Cannot create path 3D joint, need at least 2 path points");
                return false;
            }

            std::vector<Vector3> tangents;
            std::vector<Vector3> normals;
            tangents.reserve(joint.pathPoints.size());
            normals.reserve(joint.pathPoints.size());

            for (size_t i = 0; i < joint.pathPoints.size(); i++){
                size_t prev = (i == 0) ? (joint.isLooping ? joint.pathPoints.size() - 1 : 0) : i - 1;
                size_t next = (i + 1 >= joint.pathPoints.size()) ? (joint.isLooping ? 0 : joint.pathPoints.size() - 1) : i + 1;

                Vector3 tangent(
                    joint.pathPoints[next].x - joint.pathPoints[prev].x,
                    joint.pathPoints[next].y - joint.pathPoints[prev].y,
                    joint.pathPoints[next].z - joint.pathPoints[prev].z
                );

                float tangentLengthSq = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;
                if (tangentLengthSq < 1.0e-6f){
                    tangent = Vector3(1.0f, 0.0f, 0.0f);
                }else{
                    float invTangentLength = 1.0f / std::sqrt(tangentLengthSq);
                    tangent = Vector3(tangent.x * invTangentLength, tangent.y * invTangentLength, tangent.z * invTangentLength);
                }

                Vector3 normal = (std::fabs(tangent.y) < 0.999f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
                float tangentDotNormal = tangent.x * normal.x + tangent.y * normal.y + tangent.z * normal.z;
                normal = Vector3(
                    normal.x - tangent.x * tangentDotNormal,
                    normal.y - tangent.y * tangentDotNormal,
                    normal.z - tangent.z * tangentDotNormal
                );

                float normalLengthSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
                if (normalLengthSq < 1.0e-6f){
                    normal = Vector3(0.0f, 0.0f, 1.0f);
                }else{
                    float invNormalLength = 1.0f / std::sqrt(normalLengthSq);
                    normal = Vector3(normal.x * invNormalLength, normal.y * invNormalLength, normal.z * invNormalLength);
                }

                tangents.push_back(tangent);
                normals.push_back(normal);
            }

            return loadPathJoint3D(joint, joint.bodyA, joint.bodyB, joint.pathPoints, tangents, normals, joint.pathPosition, joint.isLooping);
        }
        case Joint3DType::GEAR:
            if (joint.hingeA == NULL_ENTITY || joint.hingeB == NULL_ENTITY) return false;
            return loadGearJoint3D(joint, joint.bodyA, joint.bodyB, joint.hingeA, joint.hingeB, joint.numTeethGearA, joint.numTeethGearB);
        case Joint3DType::RACKANDPINON:
            if (joint.hinge == NULL_ENTITY || joint.slider == NULL_ENTITY) return false;
            return loadRackAndPinionJoint3D(joint, joint.bodyA, joint.bodyB, joint.hinge, joint.slider, joint.numTeethRack, joint.numTeethGear, joint.rackLength);
        case Joint3DType::PULLEY:
            return loadPulleyJoint3D(joint, joint.bodyA, joint.bodyB, joint.anchorA, joint.anchorB, joint.fixedPointA, joint.fixedPointB);
        default:
            return false;
    }
}

bool PhysicsSystem::syncBody2DShapes(Body2DComponent& body){
    if (!b2Body_IsValid(body.body)){
        return false;
    }

    for (size_t i = 0; i < body.numShapes; i++){
        destroyShape2D(body, i);
    }

    for (size_t i = 0; i < body.numShapes; i++){
        Shape2D& shapeData = body.shapes[i];

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.userData = reinterpret_cast<void*>(i);
        shapeDef.density = shapeData.density;
        shapeDef.friction = shapeData.friction;
        shapeDef.restitution = shapeData.restitution;
        shapeDef.enableHitEvents = shapeData.enableHitEvents;
        shapeDef.enableContactEvents = shapeData.contactEvents;
        shapeDef.enablePreSolveEvents = shapeData.preSolveEvents;
        shapeDef.enableSensorEvents = shapeData.sensorEvents;
        shapeDef.filter.categoryBits = shapeData.categoryBits;
        shapeDef.filter.maskBits = shapeData.maskBits;
        shapeDef.filter.groupIndex = shapeData.groupIndex;

        shapeData.shape = b2_nullShapeId;
        shapeData.chain = b2_nullChainId;

        if (shapeData.type == Shape2DType::POLYGON){
            std::vector<b2Vec2> b2vertices;

            if (shapeData.numVertices >= 3){
                b2vertices.resize(shapeData.numVertices);
                for (size_t j = 0; j < shapeData.numVertices; j++){
                    b2vertices[j] = {shapeData.vertices[j].x / pointsToMeterScale2D, shapeData.vertices[j].y / pointsToMeterScale2D};
                }
            }else{
                const Vector2 minPt(std::min(shapeData.pointA.x, shapeData.pointB.x), std::min(shapeData.pointA.y, shapeData.pointB.y));
                const Vector2 maxPt(std::max(shapeData.pointA.x, shapeData.pointB.x), std::max(shapeData.pointA.y, shapeData.pointB.y));

                if (minPt == maxPt){
                    Log::error("Cannot create polygon shape %zu: less than 3 vertices and invalid pointA/pointB fallback", i);
                    continue;
                }

                b2vertices.resize(4);
                b2vertices[0] = {minPt.x / pointsToMeterScale2D, minPt.y / pointsToMeterScale2D};
                b2vertices[1] = {maxPt.x / pointsToMeterScale2D, minPt.y / pointsToMeterScale2D};
                b2vertices[2] = {maxPt.x / pointsToMeterScale2D, maxPt.y / pointsToMeterScale2D};
                b2vertices[3] = {minPt.x / pointsToMeterScale2D, maxPt.y / pointsToMeterScale2D};
            }

            b2Hull hull = b2ComputeHull(&b2vertices[0], (int)b2vertices.size());
            if (!b2ValidateHull(&hull)){
                const Vector2 minPt(std::min(shapeData.pointA.x, shapeData.pointB.x), std::min(shapeData.pointA.y, shapeData.pointB.y));
                const Vector2 maxPt(std::max(shapeData.pointA.x, shapeData.pointB.x), std::max(shapeData.pointA.y, shapeData.pointB.y));

                if (minPt != maxPt){
                    b2vertices.resize(4);
                    b2vertices[0] = {minPt.x / pointsToMeterScale2D, minPt.y / pointsToMeterScale2D};
                    b2vertices[1] = {maxPt.x / pointsToMeterScale2D, minPt.y / pointsToMeterScale2D};
                    b2vertices[2] = {maxPt.x / pointsToMeterScale2D, maxPt.y / pointsToMeterScale2D};
                    b2vertices[3] = {minPt.x / pointsToMeterScale2D, maxPt.y / pointsToMeterScale2D};
                    hull = b2ComputeHull(&b2vertices[0], (int)b2vertices.size());
                }

                if (!b2ValidateHull(&hull)){
                    Log::error("Cannot create polygon shape %zu: invalid hull", i);
                    continue;
                }
            }

            b2Polygon polygon = b2MakePolygon(&hull, std::max(0.0f, shapeData.radius / pointsToMeterScale2D));
            shapeData.shape = b2CreatePolygonShape(body.body, &shapeDef, &polygon);
        }else if (shapeData.type == Shape2DType::CIRCLE){
            if (shapeData.radius <= 0.0f){
                Log::error("Cannot create circle shape %zu: radius must be > 0", i);
                continue;
            }

            b2Circle circle = {0};
            circle.center = {shapeData.pointA.x / pointsToMeterScale2D, shapeData.pointA.y / pointsToMeterScale2D};
            circle.radius = shapeData.radius / pointsToMeterScale2D;
            shapeData.shape = b2CreateCircleShape(body.body, &shapeDef, &circle);
        }else if (shapeData.type == Shape2DType::CAPSULE){
            if (shapeData.radius <= 0.0f){
                Log::error("Cannot create capsule shape %zu: radius must be > 0", i);
                continue;
            }

            if (shapeData.pointA == shapeData.pointB){
                Log::error("Cannot create capsule shape %zu: pointA and pointB are equal", i);
                continue;
            }

            b2Capsule capsule = {0};
            capsule.center1 = {shapeData.pointA.x / pointsToMeterScale2D, shapeData.pointA.y / pointsToMeterScale2D};
            capsule.center2 = {shapeData.pointB.x / pointsToMeterScale2D, shapeData.pointB.y / pointsToMeterScale2D};
            capsule.radius = shapeData.radius / pointsToMeterScale2D;
            shapeData.shape = b2CreateCapsuleShape(body.body, &shapeDef, &capsule);
        }else if (shapeData.type == Shape2DType::SEGMENT){
            if (shapeData.pointA == shapeData.pointB){
                Log::error("Cannot create segment shape %zu: pointA and pointB are equal", i);
                continue;
            }

            b2Segment segment = {0};
            segment.point1 = {shapeData.pointA.x / pointsToMeterScale2D, shapeData.pointA.y / pointsToMeterScale2D};
            segment.point2 = {shapeData.pointB.x / pointsToMeterScale2D, shapeData.pointB.y / pointsToMeterScale2D};
            shapeData.shape = b2CreateSegmentShape(body.body, &shapeDef, &segment);
        }else if (shapeData.type == Shape2DType::CHAIN){
            if (shapeData.numVertices < 4){
                Log::error("Cannot create chain shape %zu: Box2D requires at least 4 vertices", i);
                continue;
            }

            std::vector<b2Vec2> b2vertices(shapeData.numVertices);
            for (size_t j = 0; j < shapeData.numVertices; j++){
                b2vertices[j] = {shapeData.vertices[j].x / pointsToMeterScale2D, shapeData.vertices[j].y / pointsToMeterScale2D};
            }

            b2ChainDef chainDef = b2DefaultChainDef();
            chainDef.points = &b2vertices[0];
            chainDef.count = (int)shapeData.numVertices;
            chainDef.isLoop = shapeData.loop;
            chainDef.filter.categoryBits = shapeData.categoryBits;
            chainDef.filter.maskBits = shapeData.maskBits;
            chainDef.filter.groupIndex = shapeData.groupIndex;
            chainDef.friction = shapeData.friction;
            chainDef.restitution = shapeData.restitution;

            shapeData.chain = b2CreateChain(body.body, &chainDef);
        }
    }

    body.needUpdateShapes = false;

    return true;
}

bool PhysicsSystem::createShape3DForIndex(Entity entity, Body3DComponent& body, size_t index){
    Shape3D& shapeData = body.shapes[index];
    shapeData.shape = NULL;

    if (shapeData.type == Shape3DType::BOX){
        JPH::BoxShapeSettings shape_settings(JPH::Vec3(shapeData.width / 2.0f, shapeData.height / 2.0f, shapeData.depth / 2.0f));
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::SPHERE){
        JPH::SphereShapeSettings shape_settings(shapeData.radius);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::CAPSULE){
        JPH::CapsuleShapeSettings shape_settings(shapeData.halfHeight, shapeData.radius);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::TAPERED_CAPSULE){
        JPH::TaperedCapsuleShapeSettings shape_settings(shapeData.halfHeight, shapeData.topRadius, shapeData.bottomRadius);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::CYLINDER){
        JPH::CylinderShapeSettings shape_settings(shapeData.halfHeight, shapeData.radius);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::CONVEX_HULL){
        if (shapeData.source == Shape3DSource::NONE){
            shapeData.source = Shape3DSource::ENTITY_MESH;
            if (shapeData.sourceEntity == NULL_ENTITY){
                shapeData.sourceEntity = entity;
            }
            Log::warn("Body3D entity %u has CONVEX_HULL with Source=None. Falling back to Entity Mesh source.", entity);
        }

        JPH::Array<JPH::Vec3> jvertices;

        if (shapeData.source == Shape3DSource::RAW_VERTICES){
            jvertices.resize(shapeData.numVertices);
            for (size_t i = 0; i < shapeData.numVertices; i++){
                jvertices[i] = JPH::Vec3(shapeData.vertices[i].x, shapeData.vertices[i].y, shapeData.vertices[i].z);
            }
        }else if (shapeData.source == Shape3DSource::ENTITY_MESH){
            Entity meshEntity = shapeData.sourceEntity == NULL_ENTITY ? entity : shapeData.sourceEntity;
            MeshComponent* mesh = scene->findComponent<MeshComponent>(meshEntity);
            Transform* transform = scene->findComponent<Transform>(meshEntity);
            if (!mesh || !transform){
                Log::error("Cannot create convex hull shape: mesh or transform not found for entity %u", meshEntity);
                return false;
            }

            std::map<std::string, Buffer*> buffers;
            if (mesh->buffer.getSize() > 0) buffers["vertices"] = &mesh->buffer;
            for (int i = 0; i < mesh->numExternalBuffers; i++) buffers[mesh->eBuffers[i].getName()] = &mesh->eBuffers[i];

            Buffer* vertexBuffer = NULL;
            Attribute vertexAttr;

            for (auto const& buf : buffers){
                if (buf.second->getAttribute(AttributeType::POSITION)) {
                    vertexBuffer = buf.second;
                    vertexAttr = *buf.second->getAttribute(AttributeType::POSITION);
                }
            }

            for (size_t i = 0; i < mesh->numSubmeshes; i++) {
                for (auto const& attr : mesh->submeshes[i].attributes){
                    if (attr.first == AttributeType::POSITION){
                        vertexBuffer = buffers[attr.second.getBufferName()];
                        vertexAttr = attr.second;
                    }
                }

                int verticesize = int(vertexAttr.getCount());
                for (int i = 0; i < verticesize; i++){
                    Vector3 vertice = vertexBuffer->getVector3(&vertexAttr, i) * transform->scale;
                    jvertices.push_back(JPH::Vec3(vertice.x, vertice.y, vertice.z));
                }
            }
        }

        if (jvertices.empty()){
            Log::error("Cannot create convex hull shape without vertices for 3D Body entity: %u", entity);
            return false;
        }

        JPH::ConvexHullShapeSettings shape_settings(jvertices);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::MESH){
        if (shapeData.source == Shape3DSource::NONE){
            shapeData.source = Shape3DSource::ENTITY_MESH;
            if (shapeData.sourceEntity == NULL_ENTITY){
                shapeData.sourceEntity = entity;
            }
            Log::warn("Body3D entity %u has MESH with Source=None. Falling back to Entity Mesh source.", entity);
        }

        JPH::VertexList jvertices;
        JPH::IndexedTriangleList jindices;

        if (shapeData.source == Shape3DSource::RAW_MESH){
            jvertices.resize(shapeData.numVertices);
            for (size_t i = 0; i < shapeData.numVertices; i++){
                jvertices[i] = JPH::Float3(shapeData.vertices[i].x, shapeData.vertices[i].y, shapeData.vertices[i].z);
            }

            int indicesize = int(shapeData.numIndices / 3);
            jindices.resize(indicesize);
            for (int i = 0; i < indicesize; i++){
                for (int j = 0; j < 3; j++){
                    jindices[i].mIdx[j] = shapeData.indices[(3 * i) + j];
                }
            }
        }else if (shapeData.source == Shape3DSource::ENTITY_MESH){
            Entity meshEntity = shapeData.sourceEntity == NULL_ENTITY ? entity : shapeData.sourceEntity;
            MeshComponent* mesh = scene->findComponent<MeshComponent>(meshEntity);
            Transform* transform = scene->findComponent<Transform>(meshEntity);
            if (!mesh || !transform){
                Log::error("Cannot create mesh shape: mesh or transform not found for entity %u", meshEntity);
                return false;
            }

            std::map<std::string, Buffer*> buffers;
            if (mesh->buffer.getSize() > 0) buffers["vertices"] = &mesh->buffer;
            if (mesh->indices.getSize() > 0) buffers["indices"] = &mesh->indices;
            for (int i = 0; i < mesh->numExternalBuffers; i++) buffers[mesh->eBuffers[i].getName()] = &mesh->eBuffers[i];

            Buffer* indexBuffer = NULL;
            Attribute indexAttr;
            Buffer* vertexBuffer = NULL;
            Attribute vertexAttr;

            for (auto const& buf : buffers){
                if (buf.second->getAttribute(AttributeType::INDEX)){
                    indexBuffer = buf.second;
                    indexAttr = *buf.second->getAttribute(AttributeType::INDEX);
                }
                if (buf.second->getAttribute(AttributeType::POSITION)) {
                    vertexBuffer = buf.second;
                    vertexAttr = *buf.second->getAttribute(AttributeType::POSITION);
                }
            }

            for (size_t i = 0; i < mesh->numSubmeshes; i++) {
                for (auto const& attr : mesh->submeshes[i].attributes){
                    if (attr.first == AttributeType::INDEX){
                        indexBuffer = buffers[attr.second.getBufferName()];
                        indexAttr = attr.second;
                    }
                    if (attr.first == AttributeType::POSITION){
                        vertexBuffer = buffers[attr.second.getBufferName()];
                        vertexAttr = attr.second;
                    }
                }

                int numIdxTriangles = int(indexAttr.getCount() / 3);
                for (int i = 0; i < numIdxTriangles; i++){
                    JPH::uint32 it[3];
                    for (int j = 0; j < 3; j++){
                        uint32_t indice = 0;
                        if (indexAttr.getDataType() == AttributeDataType::UNSIGNED_INT){
                            indice = indexBuffer->getUInt32(&indexAttr, (3 * i) + j);
                        }else if (indexAttr.getDataType() == AttributeDataType::UNSIGNED_SHORT){
                            indice = indexBuffer->getUInt16(&indexAttr, (3 * i) + j);
                        }
                        it[j] = indice;
                    }
                    jindices.push_back(JPH::IndexedTriangle(it[0], it[1], it[2]));
                }

                int verticesize = int(vertexAttr.getCount());
                for (int i = 0; i < verticesize; i++){
                    Vector3 vertice = vertexBuffer->getVector3(&vertexAttr, i) * transform->scale;
                    jvertices.push_back(JPH::Float3(vertice.x, vertice.y, vertice.z));
                }
            }
        }

        if (jvertices.empty() || jindices.empty()){
            Log::error("Cannot create mesh shape without vertices and indices for 3D Body entity: %u", entity);
            return false;
        }

        JPH::MeshShapeSettings shape_settings(jvertices, jindices);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }else if (shapeData.type == Shape3DType::HEIGHTFIELD){
        if (shapeData.source == Shape3DSource::NONE){
            shapeData.source = Shape3DSource::ENTITY_HEIGHTFIELD;
            if (shapeData.sourceEntity == NULL_ENTITY){
                shapeData.sourceEntity = entity;
            }
            Log::warn("Body3D entity %u has HEIGHTFIELD with Source=None. Falling back to Entity Heightfield source.", entity);
        }

        Entity terrainEntity = shapeData.sourceEntity == NULL_ENTITY ? entity : shapeData.sourceEntity;
        TerrainComponent* terrain = scene->findComponent<TerrainComponent>(terrainEntity);
        if (!terrain || !terrain->heightMapLoaded){
            Log::error("Cannot create heightfield shape without loaded terrain heightmap");
            return false;
        }

        TextureData& textureData = terrain->heightMap.getData();
        unsigned int samplesSize = shapeData.samplesSize;
        if (samplesSize == 0){
            samplesSize = textureData.getMinNearestPowerOfTwo();
            if (samplesSize > std::min(textureData.getOriginalWidth(), textureData.getOriginalHeight())){
                samplesSize = samplesSize / 2;
            }
        }

        JPH::Vec3 terrainOffset = JPH::Vec3(-terrain->terrainSize / 2.0, 0.0f, -terrain->terrainSize / 2.0);
        JPH::Vec3 terrainScale = JPH::Vec3(terrain->terrainSize / samplesSize, terrain->maxHeight, terrain->terrainSize / samplesSize);

        float* samples = new float[samplesSize * samplesSize];
        for (unsigned int x = 0; x < samplesSize; x++){
            for (unsigned int y = 0; y < samplesSize; y++){
                int posX = floor(textureData.getWidth() * x / samplesSize);
                int posY = floor(textureData.getHeight() * y / samplesSize);
                float val = textureData.getColorComponent(posX, posY, 0) / 255.0f;
                samples[x + (y * samplesSize)] = val;
            }
        }

        JPH::HeightFieldShapeSettings shape_settings(samples, terrainOffset, terrainScale, samplesSize);
        JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        delete[] samples;

        if (!shape_result.IsValid()) return false;
        shapeData.shape = shape_result.Get();
    }

    if (!shapeData.shape){
        return false;
    }

    JPH::Shape* mutableShape = const_cast<JPH::Shape*>(shapeData.shape.GetPtr());
    mutableShape->SetEmbedded();
    mutableShape->SetUserData(index);

    if (shapeData.shape->GetType() == JPH::EShapeType::Convex){
        JPH::ConvexShape* convexShape = (JPH::ConvexShape*)shapeData.shape.GetPtr();
        convexShape->SetDensity(shapeData.density);
    }

    return true;
}

bool PhysicsSystem::syncBody3DShapes(Entity entity, Body3DComponent& body){
    if (!body.body.IsInvalid()){
        destroyBody3D(body);
        body.body = JPH::BodyID();
    }

    for (size_t i = 0; i < body.numShapes; i++){
        if (!createShape3DForIndex(entity, body, i)){
            Log::error("Cannot create runtime shape %i for 3D Body entity: %u", i, entity);
            return false;
        }
    }

    if (body.numShapes == 0){
        return false;
    }

    if (body.numShapes > 1){
        JPH::StaticCompoundShapeSettings compound_shape;
        for (size_t i = 0; i < body.numShapes; i++){
            JPH::Vec3 jPosition(body.shapes[i].position.x, body.shapes[i].position.y, body.shapes[i].position.z);
            JPH::Quat jQuat(body.shapes[i].rotation.x, body.shapes[i].rotation.y, body.shapes[i].rotation.z, body.shapes[i].rotation.w);
            compound_shape.AddShape(jPosition, jQuat, body.shapes[i].shape);
        }

        JPH::ShapeSettings::ShapeResult shape_result = compound_shape.Create();
        if (!shape_result.IsValid()){
            Log::error("Cannot create StaticCompoundShape for 3D Body entity: %u", entity);
            return false;
        }

        createGenericJoltBody(entity, body, shape_result.Get());
    }else{
        Shape3D& shapeData = body.shapes[0];
        if (shapeData.position != Vector3::ZERO || shapeData.rotation != Quaternion::IDENTITY){
            JPH::Vec3 jPosition(shapeData.position.x, shapeData.position.y, shapeData.position.z);
            JPH::Quat jQuat(shapeData.rotation.x, shapeData.rotation.y, shapeData.rotation.z, shapeData.rotation.w);
            JPH::RotatedTranslatedShapeSettings rtShape(jPosition, jQuat, shapeData.shape);
            JPH::ShapeSettings::ShapeResult shape_result = rtShape.Create();
            if (!shape_result.IsValid()){
                Log::error("Cannot create RotatedTranslatedShapeSettings for 3D Body entity: %u", entity);
                return false;
            }

            createGenericJoltBody(entity, body, shape_result.Get());
        }else{
            createGenericJoltBody(entity, body, shapeData.shape);
        }
    }

    body.needReloadBody = false;
    body.needUpdateShapes = false;
    body.newBody = true;

    return true;
}

void PhysicsSystem::createGenericJoltBody(Entity entity, Body3DComponent& body, const JPH::ShapeRefC shape){
    JPH::ObjectLayer layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
    JPH::EMotionType joltType = JPH::EMotionType::Static;
    JPH::EActivation activation = JPH::EActivation::DontActivate;
    if (body.type == BodyType::DYNAMIC){
        layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
        joltType = JPH::EMotionType::Dynamic;
        activation = JPH::EActivation::Activate;
    }else if (body.type == BodyType::KINEMATIC){
        layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
        joltType = JPH::EMotionType::Kinematic;
        activation = JPH::EActivation::Activate;
    }

    if (joltType != JPH::EMotionType::Static && shape && shape->MustBeStatic()){
        Log::warn("Body3D entity %u uses a static-only shape (Mesh/HeightField/Plane/...) and cannot be %s. Forcing body type to Static.",
                  entity,
                  body.type == BodyType::DYNAMIC ? "Dynamic" : "Kinematic");
        body.type = BodyType::STATIC;
        joltType = JPH::EMotionType::Static;
        activation = JPH::EActivation::DontActivate;
    }

    JPH::BodyCreationSettings settings(shape, JPH::Vec3(0.0, 0.0, 0.0), JPH::Quat::sIdentity(), joltType, layer);

    JPH::BodyInterface &body_interface = world3D.GetBodyInterface();

    if(body.overrideMassProperties){
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3(body.solidBoxSize.x, body.solidBoxSize.y, body.solidBoxSize.z), body.solidBoxDensity);
    }

    JPH::Body* jbody = body_interface.CreateBody(settings);
    jbody->SetUserData(entity);
    //if (type != BodyType::STATIC){
    //    jbody->SetAllowSleeping(false);
    //}

    body.body = jbody->GetID();

    body_interface.AddBody(body.body, activation);
}

void PhysicsSystem::createBody2D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (!signature.test(scene->getComponentId<Body2DComponent>())){
        scene->addComponent<Body2DComponent>(entity);
        //loadBody2D(entity);
    }
}

void PhysicsSystem::removeBody2D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<Body2DComponent>())){
        destroyBody2D(scene->getComponent<Body2DComponent>(entity));
        scene->removeComponent<Body2DComponent>(entity);
    }
}


void PhysicsSystem::createBody3D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (!signature.test(scene->getComponentId<Body3DComponent>())){
        scene->addComponent<Body3DComponent>(entity);
        //loadBody3D(entity);
    }
}

void PhysicsSystem::removeBody3D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<Body3DComponent>())){
        destroyBody3D(scene->getComponent<Body3DComponent>(entity));
        scene->removeComponent<Body3DComponent>(entity);
    }
}

b2WorldId PhysicsSystem::getWorld2D() const{
     return world2D;
}

JPH::PhysicsSystem* PhysicsSystem::getWorld3D(){
    return &world3D;
}

b2BodyId PhysicsSystem::getBody(Entity entity){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        return body->body;
    }

    return b2_nullBodyId;
}

bool PhysicsSystem::loadBody2D(Entity entity){
    Body2DComponent& body = scene->getComponent<Body2DComponent>(entity);

    if (!b2Body_IsValid(body.body) || body.needReloadBody){
        if (b2Body_IsValid(body.body)){
            b2DestroyBody(body.body);
            body.body = b2_nullBodyId;
        }

        b2BodyDef bodyDef = b2DefaultBodyDef();

        if (body.type == BodyType::STATIC){
            bodyDef.type = b2_staticBody;
        }else if (body.type == BodyType::KINEMATIC){
            bodyDef.type = b2_kinematicBody;
        }else{
            bodyDef.type = b2_dynamicBody;
        }
        bodyDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        body.body = b2CreateBody(world2D, &bodyDef);
        body.newBody = true;
        body.needReloadBody = false;
        body.needUpdateShapes = true;

        return syncBody2DShapes(body);
    }

    if (body.needUpdateShapes){
        return syncBody2DShapes(body);
    }

    return true;
}

void PhysicsSystem::destroyBody2D(Body2DComponent& body){
    // When a world leaves scope or is deleted by calling delete on a pointer
    // all the memory reserved for bodies, fixtures, and joints is freed
    if (b2Body_IsValid(body.body)){
        for (int i = 0; i < body.numShapes; i++){
            destroyShape2D(body, i);
        }
        body.numShapes = 0;

        b2DestroyBody(body.body);

        body.body = b2_nullBodyId;
    }
}

bool PhysicsSystem::loadBody3D(Entity entity){
    Body3DComponent& body = scene->getComponent<Body3DComponent>(entity);

    if (body.needReloadBody || body.needUpdateShapes || body.body.IsInvalid()){
        return syncBody3DShapes(entity, body);
    }

    return true;
}

void PhysicsSystem::destroyBody3D(Body3DComponent& body){
    if (!body.body.IsInvalid()){
        JPH::BodyInterface &body_interface = world3D.GetBodyInterface();
        body_interface.RemoveBody(body.body);
        body_interface.DestroyBody(body.body);

        for (int i = 0; i < body.numShapes; i++){
            destroyShape3D(body, i);
        }

        body.body = JPH::BodyID();
    }
}

int PhysicsSystem::loadShape2D(Body2DComponent& body, void* shape, Shape2DType type){
    if (!b2Shape_IsValid(body.shapes[body.numShapes].shape)){
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.userData = reinterpret_cast<void*>(body.numShapes);

        if (!b2Body_IsValid(body.body)){
            Log::error("Cannot create 2D body shape without loaded body");
            return -1;
        }

        body.shapes[body.numShapes].shape = b2_nullShapeId;
        body.shapes[body.numShapes].chain = b2_nullChainId;

        if (type == Shape2DType::POLYGON){
            body.shapes[body.numShapes].shape = b2CreatePolygonShape(body.body, &shapeDef, (b2Polygon*)shape);
        }else if (type == Shape2DType::CIRCLE){
            body.shapes[body.numShapes].shape = b2CreateCircleShape(body.body, &shapeDef, (b2Circle*)shape);
        }else if (type == Shape2DType::CAPSULE){
            body.shapes[body.numShapes].shape = b2CreateCapsuleShape(body.body, &shapeDef, (b2Capsule*)shape);
        }else if (type == Shape2DType::SEGMENT){
            body.shapes[body.numShapes].shape = b2CreateSegmentShape(body.body, &shapeDef, (b2Segment*)shape);
        }else if (type == Shape2DType::CHAIN){
            body.shapes[body.numShapes].chain = b2CreateChain(body.body, (b2ChainDef*)shape);
        }

        body.numShapes++;

        return (body.numShapes - 1);
    }

    return -1;
}

void PhysicsSystem::destroyShape2D(Body2DComponent& body, size_t index){
    if (b2Shape_IsValid(body.shapes[index].shape)){
        b2DestroyShape(body.shapes[index].shape, true);

        body.shapes[index].shape = b2_nullShapeId;
    }
    if (b2Chain_IsValid(body.shapes[index].chain)){
        b2DestroyChain(body.shapes[index].chain);

        body.shapes[index].chain = b2_nullChainId;
    }
}

int PhysicsSystem::loadShape3D(Body3DComponent& body, const Vector3& position, const Quaternion& rotation, JPH::ShapeSettings* shapeSettings){
    JPH::ShapeSettings::ShapeResult shape_result = shapeSettings->Create();
    if (shape_result.IsValid()){
        JPH::Shape* shape = shape_result.Get();
        shape->SetEmbedded();
        shape->SetUserData(body.numShapes);

        body.shapes[body.numShapes].shape = shape;
        body.shapes[body.numShapes].position = position;
        body.shapes[body.numShapes].rotation = rotation;

        body.numShapes++;

        return (body.numShapes - 1);
    }else{
        Log::error("Cannot create shape for 3D Body");
    }

    return -1;
}

void PhysicsSystem::destroyShape3D(Body3DComponent& body, size_t index){
    if (body.shapes[index].shape){
        body.shapes[index].shape->Release();

        body.shapes[index].shape = NULL;
    }
}

bool PhysicsSystem::loadDistanceJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchorA, Vector2 anchorB, bool autoAnchors, bool rope){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivotA = {anchorA.x / pointsToMeterScale2D, anchorA.y / pointsToMeterScale2D};
        b2Vec2 worldPivotB = {anchorB.x / pointsToMeterScale2D, anchorB.y / pointsToMeterScale2D};

        const bool isZeroAnchorA = (anchorA == Vector2::ZERO);
        const bool isZeroAnchorB = (anchorB == Vector2::ZERO);
        if (autoAnchors){
            worldPivotA = b2Body_GetPosition(myBodyA.body);
            worldPivotB = b2Body_GetPosition(myBodyB.body);
        }else if (isZeroAnchorA && isZeroAnchorB){
            Log::error("Cannot create distance 2D joint: Auto Anchors is disabled and both anchors are zero");
            return false;
        }

        b2DistanceJointDef jointDef = b2DefaultDistanceJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivotA);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivotB);
        jointDef.length = b2Distance(worldPivotA, worldPivotB);
        if (rope){
            jointDef.minLength = 0;
        }
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateDistanceJoint(world2D, &jointDef);
        joint.type = Joint2DType::DISTANCE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadRevoluteJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};

        b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateRevoluteJoint(world2D, &jointDef);
        joint.type = Joint2DType::REVOLUTE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPrismaticJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};
        float axisLengthSq = axis.x * axis.x + axis.y * axis.y;
        if (axisLengthSq < 1.0e-6f){
            Log::warn("Prismatic 2D joint axis is zero. Using default axis (1, 0).");
            axis = Vector2(1.0f, 0.0f);
        }
        b2Vec2 worldAxis = {axis.x, axis.y};

        b2PrismaticJointDef jointDef = b2DefaultPrismaticJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.localAxisA = b2Body_GetLocalVector(myBodyA.body, worldAxis);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreatePrismaticJoint(world2D, &jointDef);
        joint.type = Joint2DType::PRISMATIC;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadMouseJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 target){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        if (!loadBody2D(bodyA) || !loadBody2D(bodyB)){
            Log::error("Cannot create mouse 2D joint, bodyA or bodyB could not be loaded");
            return false;
        }

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        if (myBodyA.type != BodyType::DYNAMIC){
            Log::error("Cannot create mouse 2D joint, bodyA must be dynamic");
            return false;
        }

        if (bodyA == bodyB){
            Log::error("Cannot create mouse 2D joint with the same bodyA and bodyB");
            return false;
        }

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        if (!b2Body_IsValid(myBodyA.body) || !b2Body_IsValid(myBodyB.body)){
            Log::error("Cannot create mouse 2D joint, bodyA or bodyB has no valid physics body");
            return false;
        }

        b2Vec2 worldTarget = {target.x / pointsToMeterScale2D, target.y / pointsToMeterScale2D};

        b2MouseJointDef jointDef = b2DefaultMouseJointDef();
        jointDef.bodyIdA = myBodyB.body;
        jointDef.bodyIdB = myBodyA.body;
        jointDef.target = worldTarget;
        jointDef.maxForce = std::max(10.0f, 1000.0f * b2Body_GetMass(myBodyA.body));
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateMouseJoint(world2D, &jointDef);
        if (!b2Joint_IsValid(joint.joint)){
            Log::error("Cannot create mouse 2D joint");
            return false;
        }
        joint.type = Joint2DType::MOUSE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadWheelJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};
        float axisLengthSq = axis.x * axis.x + axis.y * axis.y;
        if (axisLengthSq < 1.0e-6f){
            Log::warn("Wheel 2D joint axis is zero. Using default axis (1, 0).");
            axis = Vector2(1.0f, 0.0f);
        }
        b2Vec2 worldAxis = {axis.x, axis.y};

        b2WheelJointDef jointDef = b2DefaultWheelJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.localAxisA = b2Body_GetLocalVector(myBodyA.body, worldAxis);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateWheelJoint(world2D, &jointDef);
        joint.type = Joint2DType::WHEEL;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadWeldJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};

        b2WeldJointDef jointDef = b2DefaultWeldJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateWeldJoint(world2D, &jointDef);
        joint.type = Joint2DType::WELD;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadMotorJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2MotorJointDef jointDef = b2DefaultMotorJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateMotorJoint(world2D, &jointDef);
        joint.type = Joint2DType::MOTOR;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

void PhysicsSystem::destroyJoint2D(Joint2DComponent& joint){
    if (b2Joint_IsValid(joint.joint)){
        b2DestroyJoint(joint.joint);
        joint.joint = b2_nullJointId;
    }
}

bool PhysicsSystem::loadFixedJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::FixedConstraintSettings settings;
        settings.mAutoDetectPoint = true;

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create fixed 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::FIXED;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadDistanceJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, bool autoAnchors){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create distance 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        if (autoAnchors){
            JPH::RVec3 bodyPositionA = lockBodyA->GetPosition();
            anchorA = Vector3((float)bodyPositionA.GetX(), (float)bodyPositionA.GetY(), (float)bodyPositionA.GetZ());
        }

        if (autoAnchors){
            JPH::RVec3 bodyPositionB = lockBodyB->GetPosition();
            anchorB = Vector3((float)bodyPositionB.GetX(), (float)bodyPositionB.GetY(), (float)bodyPositionB.GetZ());
        }

        JPH::DistanceConstraintSettings settings;
        settings.mPoint1 = JPH::RVec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mPoint2 = JPH::RVec3(anchorB.x, anchorB.y, anchorB.z);
        //settings.mMinDistance = 4.0f;
        //settings.mMaxDistance = 8.0f;

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::DISTANCE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPointJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::PointConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create point 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::POINT;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadHingeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 axis, Vector3 normal){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        float axisLengthSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (axisLengthSq < 1.0e-6f){
            Log::warn("Hinge 3D joint axis is zero. Using default axis (1, 0, 0).");
            axis = Vector3(1.0f, 0.0f, 0.0f);
        }else{
            float invLength = 1.0f / std::sqrt(axisLengthSq);
            axis = Vector3(axis.x * invLength, axis.y * invLength, axis.z * invLength);
        }

        float normalLengthSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        if (normalLengthSq < 1.0e-6f){
            Log::warn("Hinge 3D joint normal is zero. Using default normal.");
            normal = (std::fabs(axis.y) < 0.999f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        }

        float axisDotNormal = axis.x * normal.x + axis.y * normal.y + axis.z * normal.z;
        normal = Vector3(normal.x - axis.x * axisDotNormal, normal.y - axis.y * axisDotNormal, normal.z - axis.z * axisDotNormal);
        normalLengthSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;

        if (normalLengthSq < 1.0e-6f){
            normal = Vector3(-axis.z, 0.0f, axis.x);
            normalLengthSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        }

        if (normalLengthSq < 1.0e-6f){
            normal = Vector3(0.0f, 0.0f, 1.0f);
        }else{
            float invNormalLength = 1.0f / std::sqrt(normalLengthSq);
            normal = Vector3(normal.x * invNormalLength, normal.y * invNormalLength, normal.z * invNormalLength);
        }

        JPH::HingeConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mHingeAxis1 = settings.mHingeAxis2 = JPH::Vec3(axis.x, axis.y, axis.z);
        settings.mNormalAxis1 = settings.mNormalAxis2 = JPH::Vec3(normal.x, normal.y, normal.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create hinge 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::HINGE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadConeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        float axisLengthSq = twistAxis.x * twistAxis.x + twistAxis.y * twistAxis.y + twistAxis.z * twistAxis.z;
        if (axisLengthSq < 1.0e-6f){
            Log::warn("Cone 3D joint twist axis is zero. Using default axis (1, 0, 0).");
            twistAxis = Vector3(1.0f, 0.0f, 0.0f);
        }else{
            float invLength = 1.0f / std::sqrt(axisLengthSq);
            twistAxis = Vector3(twistAxis.x * invLength, twistAxis.y * invLength, twistAxis.z * invLength);
        }

        JPH::ConeConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mTwistAxis1 = settings.mTwistAxis2 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create cone 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::CONE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPrismaticJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 sliderAxis, float limitsMin, float limitsMax){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        if (limitsMax < limitsMin){
            std::swap(limitsMin, limitsMax);
        }

        // Jolt's slider starts at position 0 and requires 0 to be inside [min, max].
        // Older scenes may serialize absolute-only positive/negative ranges (e.g. [1.8, 27.4]),
        // so normalize them to a valid interval while preserving range size.
        if (limitsMin > 0.0f){
            float offset = limitsMin;
            limitsMin -= offset;
            limitsMax -= offset;
            Log::warn("Prismatic 3D joint limits did not include 0. Normalized range to [{0}, {1}]", limitsMin, limitsMax);
        }else if (limitsMax < 0.0f){
            float offset = limitsMax;
            limitsMin -= offset;
            limitsMax -= offset;
            Log::warn("Prismatic 3D joint limits did not include 0. Normalized range to [{0}, {1}]", limitsMin, limitsMax);
        }

        if (std::fabs(limitsMax - limitsMin) < 1.0e-6f){
            Log::warn("Prismatic 3D joint with equal limits is unsupported by Jolt SliderConstraint. Using fixed joint instead.");
            return loadFixedJoint3D(joint, bodyA, bodyB);
        }

        float axisLengthSq = sliderAxis.x * sliderAxis.x + sliderAxis.y * sliderAxis.y + sliderAxis.z * sliderAxis.z;
        if (axisLengthSq < 1.0e-6f){
            Log::warn("Prismatic 3D joint axis is zero. Using default axis (1, 0, 0).");
            sliderAxis = Vector3(1.0f, 0.0f, 0.0f);
        }else{
            float invLength = 1.0f / std::sqrt(axisLengthSq);
            sliderAxis = Vector3(sliderAxis.x * invLength, sliderAxis.y * invLength, sliderAxis.z * invLength);
        }

        JPH::SliderConstraintSettings settings;
        settings.mAutoDetectPoint = true;
        settings.SetSliderAxis(JPH::Vec3(sliderAxis.x, sliderAxis.y, sliderAxis.z));
		settings.mLimitsMin = limitsMin;
		settings.mLimitsMax = limitsMax;

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create prismatic 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PRISMATIC;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadSwingTwistJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis, Vector3 planeAxis, float normalHalfConeAngle, float planeHalfConeAngle, float twistMinAngle, float twistMaxAngle){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        float twistAxisLengthSq = twistAxis.x * twistAxis.x + twistAxis.y * twistAxis.y + twistAxis.z * twistAxis.z;
        if (twistAxisLengthSq < 1.0e-6f){
            Log::warn("SwingTwist 3D joint twist axis is zero. Using default axis (1, 0, 0).");
            twistAxis = Vector3(1.0f, 0.0f, 0.0f);
        }else{
            float invLength = 1.0f / std::sqrt(twistAxisLengthSq);
            twistAxis = Vector3(twistAxis.x * invLength, twistAxis.y * invLength, twistAxis.z * invLength);
        }

        float planeAxisLengthSq = planeAxis.x * planeAxis.x + planeAxis.y * planeAxis.y + planeAxis.z * planeAxis.z;
        if (planeAxisLengthSq < 1.0e-6f){
            Log::warn("SwingTwist 3D joint plane axis is zero. Using default axis.");
            planeAxis = (std::fabs(twistAxis.y) < 0.999f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        }

        float twistDotPlane = twistAxis.x * planeAxis.x + twistAxis.y * planeAxis.y + twistAxis.z * planeAxis.z;
        planeAxis = Vector3(
            planeAxis.x - twistAxis.x * twistDotPlane,
            planeAxis.y - twistAxis.y * twistDotPlane,
            planeAxis.z - twistAxis.z * twistDotPlane
        );

        planeAxisLengthSq = planeAxis.x * planeAxis.x + planeAxis.y * planeAxis.y + planeAxis.z * planeAxis.z;
        if (planeAxisLengthSq < 1.0e-6f){
            planeAxis = Vector3(-twistAxis.z, 0.0f, twistAxis.x);
            planeAxisLengthSq = planeAxis.x * planeAxis.x + planeAxis.y * planeAxis.y + planeAxis.z * planeAxis.z;
        }

        if (planeAxisLengthSq < 1.0e-6f){
            planeAxis = Vector3(0.0f, 0.0f, 1.0f);
        }else{
            float invPlaneLength = 1.0f / std::sqrt(planeAxisLengthSq);
            planeAxis = Vector3(planeAxis.x * invPlaneLength, planeAxis.y * invPlaneLength, planeAxis.z * invPlaneLength);
        }

        if (twistMaxAngle < twistMinAngle){
            std::swap(twistMinAngle, twistMaxAngle);
        }

        constexpr float kMinConeAngle = 0.0f;
        constexpr float kMaxConeAngle = 179.9f;
        normalHalfConeAngle = std::clamp(normalHalfConeAngle, kMinConeAngle, kMaxConeAngle);
        planeHalfConeAngle = std::clamp(planeHalfConeAngle, kMinConeAngle, kMaxConeAngle);

        JPH::SwingTwistConstraintSettings settings;
        settings.mPosition1 = settings.mPosition2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mTwistAxis1 = settings.mTwistAxis2 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);
        settings.mPlaneAxis1 = settings.mPlaneAxis2 = JPH::Vec3(planeAxis.x, planeAxis.y, planeAxis.z);
        settings.mNormalHalfConeAngle = Angle::defaultToRad(normalHalfConeAngle);
        settings.mPlaneHalfConeAngle = Angle::defaultToRad(planeHalfConeAngle);
        settings.mTwistMinAngle = Angle::defaultToRad(twistMinAngle);
        settings.mTwistMaxAngle = Angle::defaultToRad(twistMaxAngle);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create swing twist 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::SWINGTWIST;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadSixDOFJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 axisX, Vector3 axisY){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        float axisXLengthSq = axisX.x * axisX.x + axisX.y * axisX.y + axisX.z * axisX.z;
        if (axisXLengthSq < 1.0e-6f){
            Log::warn("SixDOF 3D joint axisX is zero. Using default axis (1, 0, 0).");
            axisX = Vector3(1.0f, 0.0f, 0.0f);
        }else{
            float invLength = 1.0f / std::sqrt(axisXLengthSq);
            axisX = Vector3(axisX.x * invLength, axisX.y * invLength, axisX.z * invLength);
        }

        float axisYLengthSq = axisY.x * axisY.x + axisY.y * axisY.y + axisY.z * axisY.z;
        if (axisYLengthSq < 1.0e-6f){
            Log::warn("SixDOF 3D joint axisY is zero. Using default axis.");
            axisY = (std::fabs(axisX.y) < 0.999f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        }

        float axisDot = axisX.x * axisY.x + axisX.y * axisY.y + axisX.z * axisY.z;
        axisY = Vector3(
            axisY.x - axisX.x * axisDot,
            axisY.y - axisX.y * axisDot,
            axisY.z - axisX.z * axisDot
        );

        axisYLengthSq = axisY.x * axisY.x + axisY.y * axisY.y + axisY.z * axisY.z;
        if (axisYLengthSq < 1.0e-6f){
            axisY = Vector3(-axisX.z, 0.0f, axisX.x);
            axisYLengthSq = axisY.x * axisY.x + axisY.y * axisY.y + axisY.z * axisY.z;
        }

        if (axisYLengthSq < 1.0e-6f){
            axisY = Vector3(0.0f, 0.0f, 1.0f);
        }else{
            float invAxisYLength = 1.0f / std::sqrt(axisYLengthSq);
            axisY = Vector3(axisY.x * invAxisYLength, axisY.y * invAxisYLength, axisY.z * invAxisYLength);
        }

        JPH::SixDOFConstraintSettings settings;
        settings.mPosition1 = JPH::Vec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mPosition2 = JPH::Vec3(anchorB.x, anchorB.y, anchorB.z);
        settings.mAxisX1 = settings.mAxisX2 = JPH::Vec3(axisX.x, axisX.y, axisX.z);
        settings.mAxisY1 = settings.mAxisY2 = JPH::Vec3(axisY.x, axisY.y, axisY.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create six dof 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::SIXDOF;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPathJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, std::vector<Vector3> positions, std::vector<Vector3> tangents, std::vector<Vector3> normals, Vector3 pathPosition, bool isLooping){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::Ref<JPH::PathConstraintPathHermite> path = new JPH::PathConstraintPathHermite;

        if (positions.size() != normals.size() || positions.size() != tangents.size()){
            Log::error("Cannot create joint, positions size is different from normals and tangents");
            return false;
        }

        for (int i = 0; i < positions.size(); i++){
            path->AddPoint(JPH::Vec3(positions[i].x, positions[i].y, positions[i].z), JPH::Vec3(tangents[i].x, tangents[i].y, tangents[i].z), JPH::Vec3(normals[i].x, normals[i].y, normals[i].z));
        }
        path->SetIsLooping(isLooping);

        JPH::PathConstraintSettings settings;
        settings.mPath = path;
        settings.mPathPosition = JPH::Vec3(pathPosition.x, pathPosition.y, pathPosition.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create path 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PATH;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadGearJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hingeA, Entity hingeB, int numTeethGearA, int numTeethGearB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);
    Signature signatureHingeA = scene->getSignature(hingeA);
    Signature signatureHingeB = scene->getSignature(hingeB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        if (signatureHingeA.test(scene->getComponentId<Joint3DComponent>()) && signatureHingeB.test(scene->getComponentId<Joint3DComponent>())){

            Joint3DComponent myHingeA = scene->getComponent<Joint3DComponent>(hingeA);
            Joint3DComponent myHingeB = scene->getComponent<Joint3DComponent>(hingeB);

            if ((myHingeA.type != Joint3DType::HINGE) || (myHingeB.type != Joint3DType::HINGE)){
                Log::error("Cannot create joint, hingeA or hingeB is not hinge type");
                return false;
            }

            const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
            JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
            JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

            JPH::Body* lockBodyA = lock.GetBody(0);
            JPH::Body* lockBodyB = lock.GetBody(1);
            if (!lockBodyA || !lockBodyB){
                Log::error("Cannot create gear 3D joint, bodyA or bodyB is invalid");
                return false;
            }

            // Disable collision between gears
            JPH::Ref<JPH::GroupFilterTable> group_filter = new JPH::GroupFilterTable(2);
            group_filter->DisableCollision(0, 1);
            lockBodyA->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 0));
            lockBodyB->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 1));

            JPH::HingeConstraintSettings* hingeSetA = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHingeA.joint)->GetConstraintSettings().GetPtr();
            JPH::HingeConstraintSettings* hingeSetB = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHingeB.joint)->GetConstraintSettings().GetPtr();

            if (numTeethGearA <= 0 || numTeethGearB <= 0){
                Log::warn("Gear 3D joint received non-positive teeth count. Clamping to at least 1.");
                numTeethGearA = std::max(1, numTeethGearA);
                numTeethGearB = std::max(1, numTeethGearB);
            }

            JPH::GearConstraintSettings settings;
            settings.mHingeAxis1 = hingeSetA->mHingeAxis1;
            settings.mHingeAxis2 = hingeSetB->mHingeAxis1;
            settings.SetRatio(numTeethGearA, numTeethGearB);

            joint.joint = settings.Create(*lockBodyA, *lockBodyB);

            ((JPH::GearConstraint *)joint.joint)->SetConstraints(myHingeA.joint, myHingeB.joint);
            world3D.AddConstraint(joint.joint);
            joint.type = Joint3DType::GEAR;
        }else{
            Log::error("Cannot create joint, error in hingeA or hingeB");
            return false;
        }

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadRackAndPinionJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hinge, Entity slider, int numTeethRack, int numTeethGear, int rackLength){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);
    Signature signatureHinge = scene->getSignature(hinge);
    Signature signatureSlider = scene->getSignature(slider);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        if (signatureHinge.test(scene->getComponentId<Joint3DComponent>()) && signatureSlider.test(scene->getComponentId<Joint3DComponent>())){

            Joint3DComponent myHinge = scene->getComponent<Joint3DComponent>(hinge);
            Joint3DComponent mySlider = scene->getComponent<Joint3DComponent>(slider);

            if ((myHinge.type != Joint3DType::HINGE) || (mySlider.type != Joint3DType::PRISMATIC)){
                Log::error("Cannot create joint, hinge or slider is not hinge and prismatic (slider) types");
                return false;
            }

            const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
            JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
            JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

            JPH::Body* lockBodyA = lock.GetBody(0);
            JPH::Body* lockBodyB = lock.GetBody(1);
            if (!lockBodyA || !lockBodyB){
                Log::error("Cannot create rack and pinion 3D joint, bodyA or bodyB is invalid");
                return false;
            }

            // Disable collision between gears
            JPH::Ref<JPH::GroupFilterTable> group_filter = new JPH::GroupFilterTable(2);
            group_filter->DisableCollision(0, 1);
            lockBodyA->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 0));
            lockBodyB->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 1));

            JPH::HingeConstraintSettings* hingeSetA = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHinge.joint)->GetConstraintSettings().GetPtr();
            JPH::SliderConstraintSettings* sliderSetB = (JPH::SliderConstraintSettings*)((JPH::SliderConstraint*)mySlider.joint)->GetConstraintSettings().GetPtr();

            if (numTeethRack <= 0 || numTeethGear <= 0 || rackLength <= 0){
                Log::warn("RackAndPinion 3D joint received non-positive ratio inputs. Clamping to at least 1.");
                numTeethRack = std::max(1, numTeethRack);
                numTeethGear = std::max(1, numTeethGear);
                rackLength = std::max(1, rackLength);
            }

            JPH::RackAndPinionConstraintSettings settings;
            settings.mHingeAxis = hingeSetA->mHingeAxis1;
            settings.mSliderAxis = sliderSetB->mSliderAxis2;
            settings.SetRatio(numTeethRack, rackLength, numTeethGear);

            joint.joint = settings.Create(*lockBodyA, *lockBodyB);

            ((JPH::GearConstraint *)joint.joint)->SetConstraints(myHinge.joint, mySlider.joint);
            world3D.AddConstraint(joint.joint);
            joint.type = Joint3DType::RACKANDPINON;
        }else{
            Log::error("Cannot create joint, error in hinge or slider");
            return false;
        }

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPulleyJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 fixedPointA, Vector3 fixedPointB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::PulleyConstraintSettings settings;
        settings.mBodyPoint1 = JPH::Vec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mBodyPoint2 = JPH::Vec3(anchorB.x, anchorB.y, anchorB.z);
        settings.mFixedPoint1 = JPH::Vec3(fixedPointA.x, fixedPointA.y, fixedPointA.z);
        settings.mFixedPoint2 = JPH::Vec3(fixedPointB.x, fixedPointB.y, fixedPointB.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        JPH::Body* lockBodyA = lock.GetBody(0);
        JPH::Body* lockBodyB = lock.GetBody(1);
        if (!lockBodyA || !lockBodyB){
            Log::error("Cannot create pulley 3D joint, bodyA or bodyB is invalid");
            return false;
        }

        joint.joint = settings.Create(*lockBodyA, *lockBodyB);

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PULLEY;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

void PhysicsSystem::destroyJoint3D(Joint3DComponent& joint){
    if (joint.joint){
        world3D.RemoveConstraint(joint.joint);

        joint.joint = NULL;
    }
}

void PhysicsSystem::addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude){
    addBroadPhaseLayer3D(index, groupsToInclude, 0);
}

void PhysicsSystem::addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude, uint32_t groupsToExclude){
    if (index >= 0 && index < MAX_BROADPHASELAYER_3D){
        broad_phase_layer_interface->ConfigureLayer(JPH::BroadPhaseLayer(index), groupsToInclude, groupsToExclude);

        auto bodies3d = scene->getComponentArray<Body3DComponent>();
        if (bodies3d->size() > 0){
            Log::warn("BroadPhaseLayers should be add before any 3D body creation");
        }
    }else{
        Log::error("BroadPhaseLayer index should be from 0 to %i, increase MAX_BROADPHASELAYER_3D to more layers", (MAX_BROADPHASELAYER_3D-1));
    }
}

void PhysicsSystem::load(){
}

void PhysicsSystem::destroy(){

}

void PhysicsSystem::update(double dt){
    if (paused) {
        return;
    }

	auto bodies2d = scene->getComponentArray<Body2DComponent>();

	for (int i = 0; i < bodies2d->size(); i++){
		Body2DComponent& body = bodies2d->getComponentFromIndex(i);
		Entity entity = bodies2d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (!b2Body_IsValid(body.body) || body.needReloadBody || body.needUpdateShapes){
            loadBody2D(entity);
        }

        if (b2Body_IsValid(body.body)){
            updateBody2DPosition(signature, entity, body);

            body.newBody = false;
        }
    }
    auto joints2d = scene->getComponentArray<Joint2DComponent>();
    for (int i = 0; i < joints2d->size(); i++){
        Joint2DComponent& joint = joints2d->getComponentFromIndex(i);
        Entity entity = joints2d->getEntity(i);

        if (joint.needUpdateJoint || !b2Joint_IsValid(joint.joint)){
            if (loadJoint2D(entity, joint)){
                joint.needUpdateJoint = false;
            }
        }
    }

    if (bodies2d->size() > 0){
        int32_t subSteps = 4;
        b2World_Step(world2D, dt, subSteps);
    }

    b2BodyEvents events = b2World_GetBodyEvents(world2D);
    for (int i = 0; i < events.moveCount; ++i){
        const b2BodyMoveEvent* event = events.moveEvents + i;

        Entity entity = reinterpret_cast<uintptr_t>(event->userData);
        Signature signature = scene->getSignature(entity);

        b2Transform bTransform = event->transform;
        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            Vector3 nPosition = Vector3(bTransform.p.x * pointsToMeterScale2D, bTransform.p.y * pointsToMeterScale2D, transform.worldPosition.z);
            Quaternion nRotation = Quaternion(Angle::radToDefault(b2Rot_GetAngle(bTransform.q)), Vector3(0, 0, 1));

            if (transform.parent != NULL_ENTITY){
                Transform& transformParent = scene->getComponent<Transform>(transform.parent);

                nPosition = transformParent.modelMatrix.inverse() * nPosition;
                nRotation = transformParent.worldRotation.inverse() * nRotation;
            }

            if (transform.position != nPosition){
                transform.position = nPosition;
                transform.needUpdate = true;
            }

            if (transform.rotation != nRotation){
                transform.rotation = nRotation;
                transform.needUpdate = true;
            }
        }
    }

    Box2DAux::manageEvents(scene, world2D);

    auto bodies3d = scene->getComponentArray<Body3DComponent>();

	for (int i = 0; i < bodies3d->size(); i++){
		Body3DComponent& body = bodies3d->getComponentFromIndex(i);
		Entity entity = bodies3d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (body.body.IsInvalid() || body.needReloadBody || body.needUpdateShapes){
            loadBody3D(entity);
        }

        if (!body.body.IsInvalid()){
            updateBody3DPosition(signature, entity, body);

            body.newBody = false;
        }
    }
    auto joints3d = scene->getComponentArray<Joint3DComponent>();
    for (int i = 0; i < joints3d->size(); i++){
        Joint3DComponent& joint = joints3d->getComponentFromIndex(i);
        Entity entity = joints3d->getEntity(i);

        if (joint.needUpdateJoint || !joint.joint){
            if (loadJoint3D(entity, joint)){
                joint.needUpdateJoint = false;
            }
        }
    }

    if (bodies3d->size() > 0){
		const int cCollisionSteps = 1;

		world3D.Update(dt, cCollisionSteps, temp_allocator, job_system);
	}

	for (int i = 0; i < bodies3d->size(); i++){
		Body3DComponent& body = bodies3d->getComponentFromIndex(i);
		Entity entity = bodies3d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (!body.body.IsInvalid()){
            JPH::BodyInterface &body_interface = world3D.GetBodyInterfaceNoLock();
            JPH::RVec3 position = body_interface.GetPosition(body.body);
            JPH::Quat rotation = body_interface.GetRotation(body.body);
            if (signature.test(scene->getComponentId<Transform>())){
                Transform& transform = scene->getComponent<Transform>(entity);

                if (!std::isnan(position.GetX()) && !std::isnan(position.GetY()) && !std::isnan(position.GetZ())){
                    Vector3 nPosition = Vector3(position.GetX(), position.GetY(), position.GetZ());
                    Quaternion nRotation = Quaternion(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());

                    if (transform.parent != NULL_ENTITY){
                        Transform& transformParent = scene->getComponent<Transform>(transform.parent);

                        nPosition = transformParent.modelMatrix.inverse() * nPosition;
                        nRotation = transformParent.worldRotation.inverse() * nRotation;
                    }

                    if (transform.position != nPosition){
                        transform.position = nPosition;
                        transform.needUpdate = true;
                    }

                    if (transform.rotation != nRotation){
                        transform.rotation = nRotation;
                        transform.needUpdate = true;
                    }
                }

            }
        }
    }
    
}

void PhysicsSystem::draw(){

}

void PhysicsSystem::onComponentAdded(Entity entity, ComponentId componentId) {

}

void PhysicsSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<Body2DComponent>()) {
		Body2DComponent& body2d = scene->getComponent<Body2DComponent>(entity);
		destroyBody2D(body2d);
	} else if (componentId == scene->getComponentId<Body3DComponent>()) {
		Body3DComponent& body3d = scene->getComponent<Body3DComponent>(entity);
		destroyBody3D(body3d);
	} else if (componentId == scene->getComponentId<Joint2DComponent>()) {
		Joint2DComponent& joint2d = scene->getComponent<Joint2DComponent>(entity);
		destroyJoint2D(joint2d);
	} else if (componentId == scene->getComponentId<Joint3DComponent>()) {
		Joint3DComponent& joint3d = scene->getComponent<Joint3DComponent>(entity);
		destroyJoint3D(joint3d);
	}
}