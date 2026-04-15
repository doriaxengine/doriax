//
// (c) 2026 Eduardo Doria.
//

#include "Body3D.h"

#include "object/Object.h"
#include "subsystem/PhysicsSystem.h"
#include "component/Body3DComponent.h"
#include "util/JoltPhysicsAux.h"

#include <iostream>
#include <algorithm>

using namespace doriax;

JPH::EMotionType getBodyTypeToJolt(BodyType type){
    if (type == BodyType::STATIC){
        return JPH::EMotionType::Static;
    }else if (type == BodyType::KINEMATIC){
        return JPH::EMotionType::Kinematic;
    }else if (type == BodyType::DYNAMIC){
        return JPH::EMotionType::Dynamic;
    }

    return JPH::EMotionType::Static;;
}

BodyType getJoltToBodyType(JPH::EMotionType type){
    if (type == JPH::EMotionType::Static){
        return BodyType::STATIC;
    }else if (type == JPH::EMotionType::Kinematic){
        return BodyType::KINEMATIC;
    }else if (type == JPH::EMotionType::Dynamic){
        return BodyType::DYNAMIC;
    }

    return BodyType::STATIC;
}

static int appendShape3D(Body3DComponent& body){
    if (!body.shapes.validIndex(body.numShapes)){
        Log::error("Cannot add more shapes in this body");
        return -1;
    }

    size_t index = body.numShapes;
    body.shapes[index] = Shape3D();
    body.numShapes++;
    body.needUpdateShapes = true;

    return static_cast<int>(index);
}

Body3D::Body3D(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

Body3D::~Body3D(){
}

Body3D::Body3D(const Body3D& rhs): EntityHandle(rhs){
}

Body3D& Body3D::operator=(const Body3D& rhs){
    EntityHandle::operator = (rhs);

    return *this;
}

void Body3D::checkBody(const Body3DComponent& body) const{
    if (body.body.IsInvalid()){
        Log::error("Body3D is not loaded");
        throw std::runtime_error("Body3D is not loaded");
    }
}

const JPH::BodyLockInterface& Body3D::getBodyLockInterface() const{
    std::shared_ptr<PhysicsSystem> physicsSystem = scene->getSystem<PhysicsSystem>();
    JPH::PhysicsSystem* world = physicsSystem->getWorld3D();
    return physicsSystem->isLock3DBodies()? static_cast<const JPH::BodyLockInterface &>(world->GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world->GetBodyLockInterfaceNoLock());
}

JPH::BodyInterface& Body3D::getBodyInterface() const{
    std::shared_ptr<PhysicsSystem> physicsSystem = scene->getSystem<PhysicsSystem>();
    JPH::PhysicsSystem* world = physicsSystem->getWorld3D();
    return physicsSystem->isLock3DBodies()? world->GetBodyInterface() : world->GetBodyInterfaceNoLock();
}

const JPH::Body& Body3D::getJoltBody() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);

    JPH::BodyLockRead lock(getBodyLockInterface(), body.body);
    if (!lock.Succeeded())
        throw std::runtime_error("Body3D is locked");

    return lock.GetBody();
}

JPH::Body& Body3D::getJoltBodyWrite() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);

    JPH::BodyLockWrite lock(getBodyLockInterface(), body.body);
    if (!lock.Succeeded())
        throw std::runtime_error("Body3D is locked");

    return lock.GetBody();
}

JPH::Body* Body3D::getJoltBodyNoLock() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    return getBodyLockInterface().TryGetBody(body.body);
}

Object Body3D::getAttachedObject(){
    return Object(scene, entity);
}

void Body3D::load(){
    Body3DComponent& body = getComponent<Body3DComponent>();
    body.needReloadBody = true;
    body.needUpdateShapes = true;

    scene->getSystem<PhysicsSystem>()->loadBody3D(entity);
}

int Body3D::createBoxShape(float width, float height, float depth){
    return createBoxShape(Vector3::ZERO, Quaternion::IDENTITY, width, height, depth);
}

int Body3D::createBoxShape(Vector3 position, Quaternion rotation, float width, float height, float depth){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::BOX;
    shape.position = position;
    shape.rotation = rotation;
    shape.width = width;
    shape.height = height;
    shape.depth = depth;

    return index;
}

int Body3D::createSphereShape(float radius){
    return createSphereShape(Vector3::ZERO, Quaternion::IDENTITY, radius);
}

int Body3D::createSphereShape(Vector3 position, Quaternion rotation, float radius){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::SPHERE;
    shape.position = position;
    shape.rotation = rotation;
    shape.radius = radius;

    return index;
}

int Body3D::createCapsuleShape(float halfHeight, float radius){
    return createCapsuleShape(Vector3::ZERO, Quaternion::IDENTITY, halfHeight, radius);
}

int Body3D::createCapsuleShape(Vector3 position, Quaternion rotation, float halfHeight, float radius){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::CAPSULE;
    shape.position = position;
    shape.rotation = rotation;
    shape.halfHeight = halfHeight;
    shape.radius = radius;

    return index;
}

int Body3D::createTaperedCapsuleShape(float halfHeight, float topRadius, float bottomRadius){
    return createTaperedCapsuleShape(Vector3::ZERO, Quaternion::IDENTITY, halfHeight, topRadius, bottomRadius);
}

int Body3D::createTaperedCapsuleShape(Vector3 position, Quaternion rotation, float halfHeight, float topRadius, float bottomRadius){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::TAPERED_CAPSULE;
    shape.position = position;
    shape.rotation = rotation;
    shape.halfHeight = halfHeight;
    shape.topRadius = topRadius;
    shape.bottomRadius = bottomRadius;

    return index;
}

int Body3D::createCylinderShape(float halfHeight, float radius){
    return createCylinderShape(Vector3::ZERO, Quaternion::IDENTITY, halfHeight, radius);
}

int Body3D::createCylinderShape(Vector3 position, Quaternion rotation, float halfHeight, float radius){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::CYLINDER;
    shape.position = position;
    shape.rotation = rotation;
    shape.halfHeight = halfHeight;
    shape.radius = radius;

    return index;
}

int Body3D::createConvexHullShape(){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::CONVEX_HULL;
    shape.source = Shape3DSource::ENTITY_MESH;
    shape.sourceEntity = entity;

    return index;
}

int Body3D::createConvexHullShape(std::vector<Vector3> vertices){
    return createConvexHullShape(Vector3::ZERO, Quaternion::IDENTITY, vertices);
}

int Body3D::createConvexHullShape(Vector3 position, Quaternion rotation, std::vector<Vector3> vertices){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::CONVEX_HULL;
    shape.position = position;
    shape.rotation = rotation;
    shape.source = Shape3DSource::RAW_VERTICES;
    shape.numVertices = vertices.size();
    for (size_t i = 0; i < shape.numVertices; i++){
        if (!shape.vertices.validIndex(i)){
            Log::warn("Convex hull vertices truncated from %zu to %zu", shape.numVertices, i);
            shape.numVertices = i;
            break;
        }
        shape.vertices[i] = vertices[i];
    }

    return index;
}

int Body3D::createMeshShape(){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::MESH;
    shape.source = Shape3DSource::ENTITY_MESH;
    shape.sourceEntity = entity;

    return index;
}

int Body3D::createMeshShape(std::vector<Vector3> vertices, std::vector<uint16_t> indices){
    return createMeshShape(Vector3::ZERO, Quaternion::IDENTITY, vertices, indices);
}

int Body3D::createMeshShape(Vector3 position, Quaternion rotation, std::vector<Vector3> vertices, std::vector<uint16_t> indices){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::MESH;
    shape.position = position;
    shape.rotation = rotation;
    shape.source = Shape3DSource::RAW_MESH;
    shape.numVertices = vertices.size();
    for (size_t i = 0; i < shape.numVertices; i++){
        if (!shape.vertices.validIndex(i)){
            Log::warn("Mesh shape vertices truncated from %zu to %zu", shape.numVertices, i);
            shape.numVertices = i;
            break;
        }
        shape.vertices[i] = vertices[i];
    }
    shape.numIndices = indices.size();
    for (size_t i = 0; i < shape.numIndices; i++){
        if (!shape.indices.validIndex(i)){
            Log::warn("Mesh shape indices truncated from %zu to %zu", shape.numIndices, i);
            shape.numIndices = i;
            break;
        }
        shape.indices[i] = indices[i];
    }

    return index;
}

int Body3D::createHeightFieldShape(){
    return createHeightFieldShape(0);
}

int Body3D::createHeightFieldShape(unsigned int samplesSize){
    Body3DComponent& body = getComponent<Body3DComponent>();
    int index = appendShape3D(body);
    if (index < 0) return index;

    Shape3D& shape = body.shapes[index];
    shape.type = Shape3DType::HEIGHTFIELD;
    shape.source = Shape3DSource::ENTITY_HEIGHTFIELD;
    shape.sourceEntity = entity;
    shape.samplesSize = samplesSize;

    return index;
}

size_t Body3D::getNumShapes() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    return body.numShapes;
}

Shape3DType Body3D::getShapeType(size_t index) const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].type;
    }else{
        Log::error("Cannot find shape %i of body", index);
        throw std::runtime_error("Cannot find shape");
    }
}

void Body3D::setShapeDensity(float density){
    setShapeDensity(0, density);
}

void Body3D::setShapeDensity(size_t index, float density){
    Body3DComponent& body = getComponent<Body3DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].density = density;
        if (body.shapes[index].shape && body.shapes[index].shape->GetType() == JPH::EShapeType::Convex){
            JPH::ConvexShape* convexShape = (JPH::ConvexShape*)body.shapes[index].shape.GetPtr();
            convexShape->SetDensity(density);
        }else{
            Log::warn("The shape %i does not have density", index);
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

float Body3D::getShapeDensity() const{
    return getShapeDensity(0);
}

float Body3D::getShapeDensity(size_t index) const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].density;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return 0;
}

void Body3D::setType(BodyType type){
    Body3DComponent& body = getComponent<Body3DComponent>();

    body.type = type;

    if (!body.body.IsInvalid()){
        JPH::EActivation activation = JPH::EActivation::DontActivate;

        if (type != BodyType::STATIC){
            activation = JPH::EActivation::Activate;
        }

        getBodyInterface().SetMotionType(body.body, getBodyTypeToJolt(type), activation);
    }
}

BodyType Body3D::getType() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    return getJoltToBodyType(getBodyInterface().GetMotionType(body.body));
}

bool Body3D::canBeKinematicOrDynamic() const{
    return getJoltBody().CanBeKinematicOrDynamic();
}

void Body3D::activate(){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);

    getBodyInterface().ActivateBody(body.body);
}

void Body3D::deactivate(){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);

    getBodyInterface().DeactivateBody(body.body);
}

bool Body3D::isSensor() const{
    return getJoltBody().IsSensor();
}

void Body3D::setIsSensor(bool sensor){
    getJoltBodyWrite().SetIsSensor(sensor);
}

bool Body3D::isCollideKinematicVsNonDynamic() const{
    return getJoltBody().GetCollideKinematicVsNonDynamic();
}

void Body3D::setCollideKinematicVsNonDynamic(bool collideKinematicVsNonDynamic){
    getJoltBodyWrite().SetCollideKinematicVsNonDynamic(collideKinematicVsNonDynamic);
}

void Body3D::setAllowedDOFsAll(){
    JPH::Body& jbody = getJoltBodyWrite();
    jbody.GetMotionProperties()->SetMassProperties(JPH::EAllowedDOFs::All, jbody.GetShape()->GetMassProperties());
}

void Body3D::setAllowedDOFs2DPlane(){
    JPH::Body& jbody = getJoltBodyWrite();
    jbody.GetMotionProperties()->SetMassProperties(JPH::EAllowedDOFs::Plane2D, jbody.GetShape()->GetMassProperties());
}

void Body3D::setAllowedDOFs(bool translationX, bool translationY, bool translationZ, bool rotationX, bool rotationY, bool rotationZ){
    JPH::EAllowedDOFs allowedDOFs;

    if (translationX){
        allowedDOFs |= JPH::EAllowedDOFs::TranslationX;
    }
    if (translationY){
        allowedDOFs |= JPH::EAllowedDOFs::TranslationY;
    }
    if (translationZ){
        allowedDOFs |= JPH::EAllowedDOFs::TranslationZ;
    }
    if (rotationX){
        allowedDOFs |= JPH::EAllowedDOFs::RotationX;
    }
    if (rotationY){
        allowedDOFs |= JPH::EAllowedDOFs::RotationY;
    }
    if (rotationZ){
        allowedDOFs |= JPH::EAllowedDOFs::RotationZ;
    }

    JPH::Body& jbody = getJoltBodyWrite();
    jbody.GetMotionProperties()->SetMassProperties(allowedDOFs, jbody.GetShape()->GetMassProperties());
}

float Body3D::getMass() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    return getBodyInterface().GetShape(body.body)->GetMassProperties().mMass;
}

void Body3D::setMass(float mass){
    JPH::Body& jbody = getJoltBodyWrite();
    JPH::MassProperties massProperties = jbody.GetShape()->GetMassProperties();
    massProperties.mMass = mass;
    jbody.GetMotionProperties()->SetMassProperties(jbody.GetMotionProperties()->GetAllowedDOFs(), massProperties);
}

void Body3D::setOverrideMassAndInertia(Vector3 solidBoxSize, float solidBoxDensity){
    Body3DComponent& body = getComponent<Body3DComponent>();

    if (!body.body.IsInvalid()){
        body.solidBoxSize = solidBoxSize;
        body.solidBoxDensity = solidBoxDensity;
        body.overrideMassProperties = true;
    }else{
        Log::error("Cannot override mass and inertia of loaded body");
    }
}

float Body3D::getGravityFactor() const{
    return getJoltBody().GetMotionProperties()->GetGravityFactor();
}

void Body3D::setGravityFactor(float gravityFactor){
    getJoltBodyWrite().GetMotionProperties()->SetGravityFactor(gravityFactor);
}

void Body3D::setBitsFilter(uint16_t category, uint16_t mask){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().SetObjectLayer(body.body, JPH::ObjectLayerPairFilterMask::sGetObjectLayer(category, mask));
}

uint16_t Body3D::getCategoryBitsFilter() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::ObjectLayer objectLayer = getBodyInterface().GetObjectLayer(body.body);

    return JPH::ObjectLayerPairFilterMask::sGetGroup(objectLayer);
}

void Body3D::setCategoryBitsFilter(uint16_t category){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::BodyInterface &body_interface = getBodyInterface();

    JPH::ObjectLayer objectLayer = body_interface.GetObjectLayer(body.body);
    uint32_t mask = JPH::ObjectLayerPairFilterMask::sGetMask(objectLayer);

    body_interface.SetObjectLayer(body.body, JPH::ObjectLayerPairFilterMask::sGetObjectLayer(category, mask));
}

uint16_t Body3D::getMaskBitsFilter() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::ObjectLayer objectLayer = getBodyInterface().GetObjectLayer(body.body);

    return JPH::ObjectLayerPairFilterMask::sGetMask(objectLayer);
}

void Body3D::setMaskBitsFilter(uint16_t mask){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::BodyInterface &body_interface = getBodyInterface();

    JPH::ObjectLayer objectLayer = body_interface.GetObjectLayer(body.body);
    uint32_t group = JPH::ObjectLayerPairFilterMask::sGetGroup(objectLayer);

    body_interface.SetObjectLayer(body.body, JPH::ObjectLayerPairFilterMask::sGetObjectLayer(group, mask));
}

uint32_t Body3D::getCollisionGroupID() const{
    return getJoltBody().GetCollisionGroup().GetGroupID();
}

void Body3D::setCollisionGroupID(uint32_t group){
    JPH::Ref<JPH::GroupFilterTable> group_filter = new JPH::GroupFilterTable(2);
    group_filter->DisableCollision(0, 0);

    getJoltBodyWrite().SetCollisionGroup(JPH::CollisionGroup(group_filter, group, 0));
}

bool Body3D::isAllowSleeping() const{
    return getJoltBody().GetAllowSleeping();
}

void Body3D::setAllowSleeping(bool allowSleeping){
    getJoltBodyWrite().SetAllowSleeping(allowSleeping);
}

float Body3D::getFriction() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    return getBodyInterface().GetFriction(body.body);
}

void Body3D::setFriction(float friction){
    Body3DComponent& body = getComponent<Body3DComponent>();

    if (friction < 0 || friction > 1){
        Log::warn("Friction should be 0 and 1, 0 = no friction");
    }

    checkBody(body);
    getBodyInterface().SetFriction(body.body, friction);
}

float Body3D::getRestitution() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    return getBodyInterface().GetRestitution(body.body);
}

void Body3D::setRestitution(float restitution){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().SetRestitution(body.body, restitution);
}

Vector3 Body3D::getLinearVelocity() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::Vec3 vec = getBodyInterface().GetLinearVelocity(body.body);

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

void Body3D::setLinearVelocity(Vector3 linearVelocity){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().SetLinearVelocity(body.body, JPH::Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z));
}

void Body3D::setLinearVelocityClamped(Vector3 linearVelocity){
    getJoltBodyWrite().SetLinearVelocityClamped(JPH::Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z));
}

Vector3 Body3D::getAngularVelocity() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::Vec3 vec = getBodyInterface().GetAngularVelocity(body.body);

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

void Body3D::setAngularVelocity(Vector3 angularVelocity){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().SetAngularVelocity(body.body, JPH::Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
}

void Body3D::setAngularVelocityClamped(Vector3 angularVelocity){
    getJoltBodyWrite().SetAngularVelocityClamped(JPH::Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
}

Vector3 Body3D::getPointVelocityCOM(Vector3 pointRelativeToCOM) const{
    JPH::Vec3 vec = getJoltBody().GetPointVelocityCOM(JPH::Vec3(pointRelativeToCOM.x, pointRelativeToCOM.y, pointRelativeToCOM.z));

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Vector3 Body3D::getPointVelocity(Vector3 point) const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::Vec3 vec = getBodyInterface().GetPointVelocity(body.body, JPH::Vec3(point.x, point.y, point.z));

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Vector3 Body3D::getAccumulatedForce() const{
    JPH::Vec3 vec = getJoltBody().GetAccumulatedForce();

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Vector3 Body3D::getAccumulatedTorque() const{
    JPH::Vec3 vec = getJoltBody().GetAccumulatedTorque();

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}

Matrix4 Body3D::getInverseInertia() const{
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::Mat44 mat = getBodyInterface().GetInverseInertia(body.body);

    Matrix4 retMat;
    for (int i = 0; i < 4; i++){
        retMat.setColumn(i, Vector4(mat.GetColumn4(i).GetX(), mat.GetColumn4(i).GetY(), mat.GetColumn4(i).GetZ(), mat.GetColumn4(i).GetW()));
    }

    return retMat;
}

void Body3D::applyForce(const Vector3& force){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddForce(body.body, JPH::Vec3(force.x, force.y, force.z));
}

void Body3D::applyForce(const Vector3& force, const Vector3& point){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddForce(body.body, JPH::Vec3(force.x, force.y, force.z), JPH::Vec3(point.x, point.y, point.z));
}

void Body3D::applyTorque(const Vector3& torque){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddTorque(body.body, JPH::Vec3(torque.x, torque.y, torque.z));
}

void Body3D::applyImpulse(const Vector3& impulse){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddImpulse(body.body, JPH::Vec3(impulse.x, impulse.y, impulse.z));
}

void Body3D::applyImpulse(const Vector3& impulse, const Vector3& point){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddImpulse(body.body, JPH::Vec3(impulse.x, impulse.y, impulse.z), JPH::Vec3(point.x, point.y, point.z));
}

void Body3D::applyAngularImpulse(const Vector3& angularImpulse){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    getBodyInterface().AddAngularImpulse(body.body, JPH::Vec3(angularImpulse.x, angularImpulse.y, angularImpulse.z));
}

bool Body3D::applyBuoyancyImpulse(const Vector3& surfacePosition, const Vector3& surfaceNormal, const float buoyancy, const float linearDrag, const float angularDrag, const Vector3& fluidVelocity, const Vector3& gravity, const float deltaTime){
    Body3DComponent& body = getComponent<Body3DComponent>();

    JPH::Vec3 surPos = JPH::Vec3(surfacePosition.x, surfacePosition.y, surfacePosition.z);
    JPH::Vec3 surNor = JPH::Vec3(surfaceNormal.x, surfaceNormal.y, surfaceNormal.z);
    JPH::Vec3 fluVel = JPH::Vec3(fluidVelocity.x, fluidVelocity.y, fluidVelocity.z);
    JPH::Vec3 grav = JPH::Vec3(gravity.x, gravity.y, gravity.z);

    return getJoltBodyWrite().ApplyBuoyancyImpulse(surPos, surNor, buoyancy, linearDrag, angularDrag, fluVel, grav, deltaTime);
}

Vector3 Body3D::getCenterOfMassPosition(){
    Body3DComponent& body = getComponent<Body3DComponent>();

    checkBody(body);
    JPH::Vec3 vec = getBodyInterface().GetCenterOfMassPosition(body.body);

    return Vector3(vec.GetX(), vec.GetY(), vec.GetZ());
}