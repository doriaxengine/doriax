// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Body2D.h"

#include "object/Object.h"
#include "component/Body2DComponent.h"
#include "subsystem/PhysicsSystem.h"
#include "box2d/box2d.h"
#include "object/physics/Contact2D.h"
#include "util/Angle.h"

#include <algorithm>
#include <cmath>

using namespace doriax;

b2BodyType getBodyTypeToB2(BodyType type){
    if (type == BodyType::STATIC){
        return b2_staticBody;
    }else if (type == BodyType::KINEMATIC){
        return b2_kinematicBody;
    }else if (type == BodyType::DYNAMIC){
        return b2_dynamicBody;
    }

    return b2_dynamicBody;
}

BodyType getB2ToBodyType(b2BodyType type){
    if (type == b2_staticBody){
        return BodyType::STATIC;
    }else if (type == b2_kinematicBody){
        return BodyType::KINEMATIC;
    }else if (type == b2_dynamicBody){
        return BodyType::DYNAMIC;
    }

    return BodyType::DYNAMIC;
}

Vector2 getB2ToVector2(b2Vec2 vec2){
    return Vector2(vec2.x, vec2.y);
}

// calls fn for a shape's Box2D shapes: its own, or one per box of a tilemap source
template<typename F>
static void forEachBox2DShape(const Shape2D& shape, F fn){
    if (b2Shape_IsValid(shape.shape)){
        fn(shape.shape);
    }
    for (b2ShapeId tileShape : shape.tileShapes){
        if (b2Shape_IsValid(tileShape)){
            fn(tileShape);
        }
    }
}

// a chain has one material for all its segments
template<typename F>
static void changeSurfaceMaterial(const Shape2D& shape, F change){
    forEachBox2DShape(shape, [&](b2ShapeId shapeId){
        b2SurfaceMaterial material = b2Shape_GetSurfaceMaterial(shapeId);
        change(material);
        b2Shape_SetSurfaceMaterial(shapeId, &material);
    });
    if (b2Chain_IsValid(shape.chain)){
        b2SurfaceMaterial material = b2Chain_GetSurfaceMaterial(shape.chain, 0);
        change(material);
        b2Chain_SetAllSurfaceMaterials(shape.chain, &material);
    }
}

static int appendShape2D(Body2DComponent& body){
    if (!body.shapes.validIndex(body.numShapes)){
        Log::error("Cannot add more shapes in this body");
        return -1;
    }

    size_t index = body.numShapes;
    body.shapes[index] = Shape2D();
    body.numShapes++;
    body.needUpdateShapes = true;

    return static_cast<int>(index);
}

Body2D::Body2D(Scene* scene, Entity entity): EntityHandle(scene, entity){

}

Body2D::~Body2D(){

}

Body2D::Body2D(const Body2D& rhs): EntityHandle(rhs){
    
}

Body2D& Body2D::operator=(const Body2D& rhs){
    EntityHandle::operator =(rhs);

    return *this;
}

void Body2D::checkBody(const Body2DComponent& body) const{
    if (!b2Body_IsValid(body.body)){
        // a script's init runs before the scene's first load
        scene->getSystem<PhysicsSystem>()->loadAndPlaceBody2D(entity);

        if (!b2Body_IsValid(body.body)){
            Log::error("Body2D is not loaded");
            throw std::runtime_error("Body2D is not loaded");
        }
    }
}

// Box2D locks the world during a step, so a body in it can only be read
bool Body2D::refuseBodyChange() const{
    if (scene->getSystem<PhysicsSystem>()->isSteppingWorld2D()){
        Log::error("Cannot change a 2D body while the physics world is stepping");
        return true;
    }
    return false;
}

// The component already has the new value, the shapes' reload after the step applies it
bool Body2D::deferShapeChange(Body2DComponent& body) const{
    if (scene->getSystem<PhysicsSystem>()->isSteppingWorld2D()){
        body.needUpdateShapes = true;
        return true;
    }
    return false;
}

b2BodyId Body2D::getBox2DBody() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return body.body;
}

b2ShapeId Body2D::getBox2DShape(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (body.shapes.validIndex(index)){
        return body.shapes[index].shape;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return b2_nullShapeId;
}

b2ChainId Body2D::getBox2DChain(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (body.shapes.validIndex(index)){
        return body.shapes[index].chain;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return b2_nullChainId;
}

Object Body2D::getAttachedObject(){
    return Object(scene, entity);
}

float Body2D::getPointsToMeterScale() const{
    return scene->getSystem<PhysicsSystem>()->getPointsToMeterScale2D();
}

void Body2D::load(){
    Body2DComponent& body = getComponent<Body2DComponent>();
    body.needReloadBody = true;
    body.needUpdateShapes = true;

    scene->getSystem<PhysicsSystem>()->loadBody2D(entity);
}

int Body2D::createBoxShape(float width, float height){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::POLYGON;
    shape.numVertices = 4;
    shape.vertices[0] = Vector2(0, 0);
    shape.vertices[1] = Vector2(width, 0);
    shape.vertices[2] = Vector2(width, height);
    shape.vertices[3] = Vector2(0, height);

    return index;
}

int Body2D::createCenteredBoxShape(float width, float height){
    return createCenteredBoxShape(width, height, Vector2(0, 0), 0);
}

int Body2D::createCenteredBoxShape(float width, float height, Vector2 center, float angle){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::POLYGON;
    shape.numVertices = 4;

    float halfW = width / 2.0f;
    float halfH = height / 2.0f;

    Vector2 local[4] = {
        Vector2(-halfW, -halfH),
        Vector2(halfW, -halfH),
        Vector2(halfW, halfH),
        Vector2(-halfW, halfH)
    };

    float rad = Angle::defaultToRad(angle);
    float c = cosf(rad);
    float s = sinf(rad);

    for (int i = 0; i < 4; i++){
        float x = local[i].x;
        float y = local[i].y;
        shape.vertices[i] = Vector2((x * c) - (y * s), (x * s) + (y * c)) + center;
    }

    return index;
}

int Body2D::createRoundedBoxShape(float width, float height, float radius){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::POLYGON;
    shape.radius = radius;
    shape.numVertices = 4;

    float halfW = width / 2.0f;
    float halfH = height / 2.0f;
    shape.vertices[0] = Vector2(-halfW, -halfH);
    shape.vertices[1] = Vector2(halfW, -halfH);
    shape.vertices[2] = Vector2(halfW, halfH);
    shape.vertices[3] = Vector2(-halfW, halfH);

    return index;
}

int Body2D::createPolygonShape(std::vector<Vector2> vertices){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::POLYGON;
    shape.numVertices = vertices.size();
    for (size_t i = 0; i < shape.numVertices; i++){
        if (!shape.vertices.validIndex(i)){
            Log::warn("Polygon shape vertices truncated from %zu to %zu", shape.numVertices, i);
            shape.numVertices = i;
            break;
        }
        shape.vertices[i] = vertices[i];
    }

    return index;
}

int Body2D::createCircleShape(Vector2 center, float radius){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::CIRCLE;
    shape.pointA = center;
    shape.radius = radius;

    return index;
}

int Body2D::createCapsuleShape(Vector2 center1, Vector2 center2, float radius){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::CAPSULE;
    shape.pointA = center1;
    shape.pointB = center2;
    shape.radius = radius;

    return index;
}

int Body2D::createSegmentShape(Vector2 point1, Vector2 point2){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::SEGMENT;
    shape.pointA = point1;
    shape.pointB = point2;

    return index;
}

int Body2D::createChainShape(std::vector<Vector2> vertices, bool loop){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::CHAIN;
    shape.loop = loop;
    shape.numVertices = vertices.size();
    for (size_t i = 0; i < shape.numVertices; i++){
        if (!shape.vertices.validIndex(i)){
            Log::warn("Chain shape vertices truncated from %zu to %zu", shape.numVertices, i);
            shape.numVertices = i;
            break;
        }
        shape.vertices[i] = vertices[i];
    }

    return index;
}

int Body2D::createTilemapShape(){
    Body2DComponent& body = getComponent<Body2DComponent>();
    int index = appendShape2D(body);
    if (index < 0) return index;

    Shape2D& shape = body.shapes[index];
    shape.type = Shape2DType::POLYGON;
    shape.source = Shape2DSource::ENTITY_TILEMAP;

    return index;
}

void Body2D::removeAllShapes(){
    Body2DComponent& body = getComponent<Body2DComponent>();
    body.numShapes = 0;
    body.needUpdateShapes = true;
}

std::vector<Contact2D> Body2D::getBodyContacts(){
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);

    std::vector<Contact2D> contacts;

    if (scene->getSystem<PhysicsSystem>()->isSteppingWorld2D()){
        Log::error("Cannot read 2D contacts while the physics world is stepping");
        return contacts;
    }

    int bodyContactCapacity = b2Body_GetContactCapacity(body.body);
    if (bodyContactCapacity == 0) return contacts;
    std::vector<b2ContactData> contactData(bodyContactCapacity);
    int bodyContactCount = b2Body_GetContactData(body.body, &contactData.front(), bodyContactCapacity);

    for (int i = 0; i < bodyContactCount; ++i){
        contacts.push_back(Contact2D(scene, contactData[i]));
    }

    return contacts;
}

std::vector<Contact2D> Body2D::getShapeContacts(size_t index){
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);

    std::vector<Contact2D> contacts;

    if (scene->getSystem<PhysicsSystem>()->isSteppingWorld2D()){
        Log::error("Cannot read 2D contacts while the physics world is stepping");
        return contacts;
    }

    if (index >= body.numShapes || body.shapes[index].type == Shape2DType::CHAIN){
        Log::error("Cannot find shape %i of body", index);
        return contacts;
    }

    forEachBox2DShape(body.shapes[index], [&](b2ShapeId shapeId){
        int shapeContactCapacity = b2Shape_GetContactCapacity(shapeId);
        if (shapeContactCapacity == 0) return;
        std::vector<b2ContactData> contactData(shapeContactCapacity);
        int shapeContactCount = b2Shape_GetContactData(shapeId, &contactData.front(), shapeContactCapacity);

        for (int i = 0; i < shapeContactCount; ++i){
            contacts.push_back(Contact2D(scene, contactData[i]));
        }
    });

    return contacts;
}

size_t Body2D::getNumShapes() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    return body.numShapes;
}

Shape2DType Body2D::getShapeType(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].type;
    }else{
        Log::error("Cannot find shape %i of body", index);
        throw std::runtime_error("Cannot find shape");
    }
}

void Body2D::setShapeDensity(float density){
    setShapeDensity(0, density);
}

void Body2D::setShapeFriction(float friction){
    setShapeFriction(0, friction);
}

void Body2D::setShapeRestitution(float restitution){
    setShapeRestitution(0, restitution);
}

void Body2D::setShapeRollingResistance(float rollingResistance){
    setShapeRollingResistance(0, rollingResistance);
}

void Body2D::setShapeTangentSpeed(float tangentSpeed){
    setShapeTangentSpeed(0, tangentSpeed);
}

void Body2D::setShapeDensity(size_t index, float density){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].density = density;
        if (deferShapeChange(body)) return;

        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set density of chain shape %i", index);
        }else{
            forEachBox2DShape(body.shapes[index], [&](b2ShapeId shapeId){
                b2Shape_SetDensity(shapeId, density, true);
            });
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeFriction(size_t index, float friction){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].friction = friction;
        if (deferShapeChange(body)) return;

        changeSurfaceMaterial(body.shapes[index], [&](b2SurfaceMaterial& material){
            material.friction = friction;
        });
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeRestitution(size_t index, float restitution){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].restitution = restitution;
        if (deferShapeChange(body)) return;

        changeSurfaceMaterial(body.shapes[index], [&](b2SurfaceMaterial& material){
            material.restitution = restitution;
        });
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeRollingResistance(size_t index, float rollingResistance){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].rollingResistance = rollingResistance;
        if (deferShapeChange(body)) return;

        changeSurfaceMaterial(body.shapes[index], [&](b2SurfaceMaterial& material){
            material.rollingResistance = rollingResistance;
        });
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeTangentSpeed(size_t index, float tangentSpeed){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].tangentSpeed = tangentSpeed;
        if (deferShapeChange(body)) return;

        float pointsToMeterScale = getPointsToMeterScale();
        changeSurfaceMaterial(body.shapes[index], [&](b2SurfaceMaterial& material){
            material.tangentSpeed = tangentSpeed / pointsToMeterScale;
        });
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeSensor(bool sensor){
    setShapeSensor(0, sensor);
}

void Body2D::setShapeSensor(size_t index, bool sensor){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set sensor of chain shape %i", index);
        }else if (body.shapes[index].sensor != sensor){
            body.shapes[index].sensor = sensor;
            body.needUpdateShapes = true;
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

bool Body2D::isShapeSensor() const{
    return isShapeSensor(0);
}

bool Body2D::isShapeSensor(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].sensor;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return false;
}

void Body2D::setShapeEnableHitEvents(bool hitEvents){
    setShapeEnableHitEvents(0, hitEvents);
}

void Body2D::setShapeContactEvents(bool contactEvents){
    setShapeContactEvents(0, contactEvents);
}

void Body2D::setShapePreSolveEvents(bool preSolveEvent){
    setShapePreSolveEvents(0, preSolveEvent);
}

void Body2D::setShapeSensorEvents(bool sensorEvents){
    setShapeSensorEvents(0, sensorEvents);
}


void Body2D::setShapeEnableHitEvents(size_t index, bool hitEvents){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].enableHitEvents = hitEvents;
        if (deferShapeChange(body)) return;

        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set hit events of chain shape %i", index);
        }else{
            forEachBox2DShape(body.shapes[index], [&](b2ShapeId shapeId){
                b2Shape_EnableHitEvents(shapeId, hitEvents);
            });
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeContactEvents(size_t index, bool contactEvents){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].contactEvents = contactEvents;
        if (deferShapeChange(body)) return;

        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set shape contact events of chain shape %i", index);
        }else{
            forEachBox2DShape(body.shapes[index], [&](b2ShapeId shapeId){
                b2Shape_EnableContactEvents(shapeId, contactEvents);
            });
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapePreSolveEvents(size_t index, bool preSolveEvent){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set presolve events of chain shape %i", index);
        }else if (body.shapes[index].preSolveEvents != preSolveEvent){
            // contact recycling follows it, so contacts are made again
            body.shapes[index].preSolveEvents = preSolveEvent;
            body.needUpdateShapes = true;
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}

void Body2D::setShapeSensorEvents(size_t index, bool sensorEvents){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        body.shapes[index].sensorEvents = sensorEvents;
        if (deferShapeChange(body)) return;

        if (body.shapes[index].type == Shape2DType::CHAIN){
            Log::error("Cannot set sensor events of chain shape %i", index);
        }else{
            forEachBox2DShape(body.shapes[index], [&](b2ShapeId shapeId){
                b2Shape_EnableSensorEvents(shapeId, sensorEvents || body.shapes[index].sensor);
            });
        }
    }else{
        Log::error("Cannot find shape %i of body", index);
    }
}


float Body2D::getShapeDensity() const{
    return getShapeDensity(0);
}

float Body2D::getShapeFriction() const{
    return getShapeFriction(0);
}

float Body2D::getShapeRestitution() const{
    return getShapeRestitution(0);
}

float Body2D::getShapeRollingResistance() const{
    return getShapeRollingResistance(0);
}

float Body2D::getShapeTangentSpeed() const{
    return getShapeTangentSpeed(0);
}


float Body2D::getShapeDensity(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].density;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return -1;
}

float Body2D::getShapeFriction(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].friction;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return -1;
}

float Body2D::getShapeRestitution(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].restitution;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return -1;
}

float Body2D::getShapeRollingResistance(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].rollingResistance;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return -1;
}

float Body2D::getShapeTangentSpeed(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].tangentSpeed;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return 0;
}

bool Body2D::isShapeEnableHitEvents() const{
    return isShapeEnableHitEvents(0);
}

bool Body2D::isShapeContactEvents() const{
    return isShapeContactEvents(0);
}

bool Body2D::isShapePreSolveEvents() const{
    return isShapePreSolveEvents(0);
}

bool Body2D::isShapeSensorEvents() const{
    return isShapeSensorEvents(0);
}

bool Body2D::isShapeEnableHitEvents(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].enableHitEvents;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return false;
}

bool Body2D::isShapeContactEvents(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].contactEvents;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return false;
}

bool Body2D::isShapePreSolveEvents(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].preSolveEvents;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return false;
}

bool Body2D::isShapeSensorEvents(size_t index) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (index >= 0 && index < body.numShapes){
        return body.shapes[index].sensorEvents || body.shapes[index].sensor;
    }else{
        Log::error("Cannot find shape %i of body", index);
    }

    return false;
}

Vector2 Body2D::getPosition() const{
    Body2DComponent& body = getComponent<Body2DComponent>();
    float pointsToMeterScale = getPointsToMeterScale();

    checkBody(body);
    b2Vec2 position = b2Body_GetPosition(body.body);

    return Vector2(position.x * pointsToMeterScale, position.y * pointsToMeterScale);
}

void Body2D::setPosition(Vector2 position){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();
    float pointsToMeterScale = getPointsToMeterScale();

    checkBody(body);
    b2Transform bTransform = b2Body_GetTransform(body.body);
    b2Body_SetTransform(body.body, {position.x / pointsToMeterScale, position.y / pointsToMeterScale}, bTransform.q);
    b2Body_SetAwake(body.body, true);
    // jumps, not interpolated
    body.previousTransform.p = b2Body_GetPosition(body.body);

    scene->getSystem<PhysicsSystem>()->updateTransformFromBody2D(entity, body, Engine::getInterpolationAlpha());
}

float Body2D::getAngle() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return Angle::radToDefault(b2Rot_GetAngle(b2Body_GetRotation(body.body)));
}

void Body2D::setAngle(float angle){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Transform bTransform = b2Body_GetTransform(body.body);
    b2Body_SetTransform(body.body, bTransform.p, b2MakeRot(Angle::defaultToRad(angle)));
    b2Body_SetAwake(body.body, true);
    body.previousTransform.q = b2Body_GetRotation(body.body);

    scene->getSystem<PhysicsSystem>()->updateTransformFromBody2D(entity, body, Engine::getInterpolationAlpha());
}

void Body2D::setLinearVelocity(Vector2 linearVelocity){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();
    float pointsToMeterScale = getPointsToMeterScale();

    checkBody(body);
    // points per second, like positions
    b2Body_SetLinearVelocity(body.body, {linearVelocity.x / pointsToMeterScale, linearVelocity.y / pointsToMeterScale});
}

void Body2D::setAngularVelocity(float angularVelocity){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetAngularVelocity(body.body, angularVelocity);
}

void Body2D::setLinearDamping(float linearDamping){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetLinearDamping(body.body, linearDamping);
}

void Body2D::setAngularDamping(float angularDamping){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetAngularDamping(body.body, angularDamping);
}

void Body2D::setEnableSleep(bool enableSleep){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_EnableSleeping(body.body, enableSleep);
}

void Body2D::setAwake(bool awake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetAwake(body.body, awake);
}

void Body2D::setFixedRotation(bool fixedRotation){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2MotionLocks locks = b2Body_GetMotionLocks(body.body);
    locks.angularZ = fixedRotation;
    b2Body_SetMotionLocks(body.body, locks);
}

void Body2D::setMotionLocks(bool linearX, bool linearY, bool angularZ){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetMotionLocks(body.body, {linearX, linearY, angularZ});
}

void Body2D::setBullet(bool bullet){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetBullet(body.body, bullet);
}

void Body2D::setType(BodyType type){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (b2Body_IsValid(body.body)){
        if (refuseBodyChange()) return;

        b2Body_SetType(body.body, getBodyTypeToB2(type));
    }
    body.type = type;
}

void Body2D::setEnabled(bool enabled){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    if (enabled){
        b2Body_Enable(body.body);
    }else{
        b2Body_Disable(body.body);
    }
}

void Body2D::setGravityScale(float gravityScale){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_SetGravityScale(body.body, gravityScale);
}

Vector2 Body2D::getLinearVelocity() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    float pointsToMeterScale = getPointsToMeterScale();
    b2Vec2 vec = b2Body_GetLinearVelocity(body.body);

    return Vector2(vec.x * pointsToMeterScale, vec.y * pointsToMeterScale);
}

float Body2D::getAngularVelocity() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetAngularVelocity(body.body);
}

float Body2D::getLinearDamping() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetLinearDamping(body.body);
}

float Body2D::getAngularDamping() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetAngularDamping(body.body);
}

bool Body2D::isEnableSleep() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_IsSleepingEnabled(body.body);
}

bool Body2D::isAwake() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_IsAwake(body.body);
}

bool Body2D::isFixedRotation() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetMotionLocks(body.body).angularZ;
}

bool Body2D::isLinearXLocked() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetMotionLocks(body.body).linearX;
}

bool Body2D::isLinearYLocked() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetMotionLocks(body.body).linearY;
}

bool Body2D::isBullet() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_IsBullet(body.body);
}

BodyType Body2D::getType() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (b2Body_IsValid(body.body)){
        return getB2ToBodyType(b2Body_GetType(body.body));
    }
    return body.type;
}

bool Body2D::isEnabled() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_IsEnabled(body.body);
}

float Body2D::getGravityScale() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetGravityScale(body.body);
}

void Body2D::setBitsFilter(uint16_t categoryBits, uint16_t maskBits){
    setBitsFilter(0, categoryBits, maskBits);
}

void Body2D::setBitsFilter(size_t shapeIndex, uint16_t categoryBits, uint16_t maskBits){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (body.shapes.validIndex(shapeIndex)){
        body.shapes[shapeIndex].categoryBits = categoryBits;
        body.shapes[shapeIndex].maskBits = maskBits;
        if (deferShapeChange(body)) return;

        forEachBox2DShape(body.shapes[shapeIndex], [&](b2ShapeId shapeId){
            b2Filter filter = b2Shape_GetFilter(shapeId);
            filter.categoryBits = categoryBits;
            filter.maskBits = maskBits;

            b2Shape_SetFilter(shapeId, filter);
        });
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }
}

void Body2D::setCategoryBitsFilter(uint16_t categoryBits){
    setCategoryBitsFilter(0, categoryBits);
}

void Body2D::setMaskBitsFilter(uint16_t maskBits){
    setMaskBitsFilter(0, maskBits);
}

void Body2D::setGroupIndexFilter(int16_t groupIndex){
    setGroupIndexFilter(0, groupIndex);
}

void Body2D::setCategoryBitsFilter(size_t shapeIndex, uint16_t categoryBits){
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (body.shapes.validIndex(shapeIndex)){
        body.shapes[shapeIndex].categoryBits = categoryBits;
        if (deferShapeChange(body)) return;

        forEachBox2DShape(body.shapes[shapeIndex], [&](b2ShapeId shapeId){
            b2Filter filter = b2Shape_GetFilter(shapeId);

            filter.categoryBits = categoryBits;

            b2Shape_SetFilter(shapeId, filter);
        });
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }
}

void Body2D::setMaskBitsFilter(size_t shapeIndex, uint16_t maskBits){
    Body2DComponent& body = getComponent<Body2DComponent>();
    
    if (body.shapes.validIndex(shapeIndex)){
        body.shapes[shapeIndex].maskBits = maskBits;
        if (deferShapeChange(body)) return;

        forEachBox2DShape(body.shapes[shapeIndex], [&](b2ShapeId shapeId){
            b2Filter filter = b2Shape_GetFilter(shapeId);

            filter.maskBits = maskBits;

            b2Shape_SetFilter(shapeId, filter);
        });
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }
}

void Body2D::setGroupIndexFilter(size_t shapeIndex, int16_t groupIndex){
    Body2DComponent& body = getComponent<Body2DComponent>();
    
    if (body.shapes.validIndex(shapeIndex)){
        body.shapes[shapeIndex].groupIndex = groupIndex;
        if (deferShapeChange(body)) return;

        forEachBox2DShape(body.shapes[shapeIndex], [&](b2ShapeId shapeId){
            b2Filter filter = b2Shape_GetFilter(shapeId);

            filter.groupIndex = groupIndex;

            b2Shape_SetFilter(shapeId, filter);
        });
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }
}

uint16_t Body2D::getCategoryBitsFilter() const{
    return getCategoryBitsFilter(0);
}

uint16_t Body2D::getMaskBitsFilter() const{
    return getMaskBitsFilter(0);
}

int16_t Body2D::getGroupIndexFilter() const{
    return getGroupIndexFilter(0);
}

uint16_t Body2D::getCategoryBitsFilter(size_t shapeIndex) const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    if (body.shapes.validIndex(shapeIndex)){
        return body.shapes[shapeIndex].categoryBits;
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }

    return 0;
}

uint16_t Body2D::getMaskBitsFilter(size_t shapeIndex) const{
    Body2DComponent& body = getComponent<Body2DComponent>();
    
    if (body.shapes.validIndex(shapeIndex)){
        return body.shapes[shapeIndex].maskBits;
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }

    return 0;
}

int16_t Body2D::getGroupIndexFilter(size_t shapeIndex) const{
    Body2DComponent& body = getComponent<Body2DComponent>();
    
    if (body.shapes.validIndex(shapeIndex)){
        return body.shapes[shapeIndex].groupIndex;
    }else{
        Log::error("Cannot find shape %i of body", shapeIndex);
    }

    return -1;
}

float Body2D::getMass() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetMass(body.body);
}

float Body2D::getRotationalInertia() const{
    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    return b2Body_GetRotationalInertia(body.body);
}

void Body2D::applyMassFromShapes(){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_UpdateMassFromShapes(body.body);
}

void Body2D::applyForce(const Vector2& force, const Vector2& point, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();
    float pointsToMeterScale = getPointsToMeterScale();

    checkBody(body);
    b2Body_ApplyForce(body.body, {force.x, force.y}, {point.x / pointsToMeterScale, point.y / pointsToMeterScale}, wake);
}

void Body2D::applyForceToCenter(const Vector2& force, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_ApplyForceToCenter(body.body, {force.x, force.y}, wake);
}

void Body2D::applyTorque(float torque, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_ApplyTorque(body.body, torque, wake);
}

void Body2D::applyLinearImpulse(const Vector2& impulse, const Vector2& point, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();
    float pointsToMeterScale = getPointsToMeterScale();

    checkBody(body);
    b2Body_ApplyLinearImpulse(body.body, {impulse.x, impulse.y}, {point.x / pointsToMeterScale, point.y / pointsToMeterScale}, wake);
}

void Body2D::applyLinearImpulseToCenter(const Vector2& impulse, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_ApplyLinearImpulseToCenter(body.body, {impulse.x, impulse.y}, wake);
}

void Body2D::applyAngularImpulse(float impulse, bool wake){
    if (refuseBodyChange()) return;

    Body2DComponent& body = getComponent<Body2DComponent>();

    checkBody(body);
    b2Body_ApplyAngularImpulse(body.body, impulse, wake);
}

