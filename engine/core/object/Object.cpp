//
// (c) 2026 Eduardo Doria.
//

#include "Object.h"

#include "subsystem/RenderSystem.h"
#include "subsystem/PhysicsSystem.h"

using namespace doriax;

Object::Object(Scene* scene): EntityHandle(scene){
    addComponent<Transform>();
}

Object::Object(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

Object::~Object(){
}

void Object::setPosition(Vector3 position){
    Transform& transform = getComponent<Transform>();

    if (transform.position != position){
        transform.position = position;

        transform.needUpdate = true;
    }
}

void Object::setPosition(const float x, const float y, const float z){
    setPosition(Vector3(x,y,z));
}

void Object::setPosition(const float x, const float y){
    setPosition(Vector3(x,y,0));
}

Vector3 Object::getPosition() const{
    Transform& transform = getComponent<Transform>();
    return transform.position;
}

Vector3 Object::getWorldPosition() const{
    Transform& transform = getComponent<Transform>();
    return transform.worldPosition;
}

void Object::setRotation(Quaternion rotation){
    Transform& transform = getComponent<Transform>();

    if (transform.rotation != rotation){
        transform.rotation = rotation;

        transform.needUpdate = true;
    }
}

void Object::setRotation(const float xAngle, const float yAngle, const float zAngle){
    setRotation(Quaternion(xAngle, yAngle, zAngle));
}

Quaternion Object::getRotation() const{
    Transform& transform = getComponent<Transform>();
    return transform.rotation;
}

Quaternion Object::getWorldRotation() const{
    Transform& transform = getComponent<Transform>();
    return transform.worldRotation;
}

void Object::setScale(const float factor){
    setScale(Vector3(factor,factor,factor));
}

void Object::setScale(Vector3 scale){
    Transform& transform = getComponent<Transform>();

    if (transform.scale != scale){
        transform.scale = scale;

        transform.needUpdate = true;
    }
}

Vector3 Object::getScale() const{
    Transform& transform = getComponent<Transform>();
    return transform.scale;
}

Vector3 Object::getWorldScale() const{
    Transform& transform = getComponent<Transform>();
    return transform.worldScale;
}

void Object::setVisible(bool visible){
    Transform& transform = getComponent<Transform>();
    if (transform.visible != visible){
        transform.visible = visible;
        transform.needUpdateChildVisibility = true;
    }
}

bool Object::isVisible() const{
    Transform& transform = getComponent<Transform>();
    return transform.visible;
}

void Object::setVisibleOnly(bool visible){
    Transform& transform = getComponent<Transform>();
    transform.visible = visible;
}

void Object::setBillboard(bool billboard, bool fake, bool cylindrical){
    Transform& transform = getComponent<Transform>();

    transform.billboard = billboard;
    transform.fakeBillboard = fake;
    transform.cylindricalBillboard = cylindrical;
}

void Object::setBillboard(bool billboard){
    Transform& transform = getComponent<Transform>();

    transform.billboard = billboard;
}

bool Object::isBillboard() const{
    Transform& transform = getComponent<Transform>();

    return transform.billboard;
}

void Object::setBillboardRotation(Quaternion rotation){
    Transform& transform = getComponent<Transform>();

    if (transform.billboardRotation != rotation){
        transform.billboardRotation = rotation;
        transform.needUpdate = true;
    }
}

void Object::setBillboardRotation(const float xAngle, const float yAngle, const float zAngle){
    setBillboardRotation(Quaternion(xAngle, yAngle, zAngle));
}

Quaternion Object::getBillboardRotation() const{
    Transform& transform = getComponent<Transform>();

    return transform.billboardRotation;
}

void Object::setFakeBillboard(bool fakeBillboard){
    Transform& transform = getComponent<Transform>();

    transform.fakeBillboard = fakeBillboard;
}

bool Object::isFakeBillboard() const{
    Transform& transform = getComponent<Transform>();

    return transform.fakeBillboard;
}

void Object::setCylindricalBillboard(bool cylindricalBillboard){
    Transform& transform = getComponent<Transform>();

    transform.cylindricalBillboard = cylindricalBillboard;
}

bool Object::isCylindricalBillboard() const{
    Transform& transform = getComponent<Transform>();

    return transform.cylindricalBillboard;
}

void Object::setLocalMatrix(Matrix4 localMatrix){
    Transform& transform = getComponent<Transform>();

    if (transform.localMatrix != localMatrix){
        transform.localMatrix = localMatrix;

        localMatrix.decompose(transform.position, transform.scale, transform.rotation);

        transform.needUpdate = true;
    }
}

Matrix4 Object::getLocalMatrix() const{
    Transform& transform = getComponent<Transform>();
    return transform.localMatrix;
}

Matrix4 Object::getModelMatrix() const{
    Transform& transform = getComponent<Transform>();
    return transform.modelMatrix;
}

Matrix4 Object::getNormalMatrix() const{
    Transform& transform = getComponent<Transform>();
    return transform.normalMatrix;
}

void Object::addChild(Object* child){
    addChild(child->entity);
}

void Object::addChild(Entity child){
    scene->addEntityChild(this->entity, child, false);
}

void Object::removeChild(Object* child){
    removeChild(child->entity);
}

void Object::removeChild(Entity child){
    Transform* childTransform = scene->findComponent<Transform>(child);
    if (childTransform && childTransform->parent == this->entity){
        scene->addEntityChild(NULL_ENTITY, child, false);
    }
}

void Object::removeParent(){
    scene->addEntityChild(NULL_ENTITY, this->entity, false);
}

void Object::moveToTop(){
    scene->moveChildToTop(this->entity);
}

void Object::moveUp(){
    scene->moveChildUp(this->entity);
}

void Object::moveDown(){
    scene->moveChildDown(this->entity);
}

void Object::moveToBottom(){
    scene->moveChildToBottom(this->entity);
}

void Object::updateTransform(){
    Transform& transform = getComponent<Transform>();

    scene->getSystem<RenderSystem>()->updateTransform(transform);
}

Body2D Object::getBody2D(){
    scene->getSystem<PhysicsSystem>()->createBody2D(entity);
    return Body2D(scene, entity);
}

void Object::removeBody2D(){
    scene->getSystem<PhysicsSystem>()->removeBody2D(entity);
}

Body3D Object::getBody3D(){
    scene->getSystem<PhysicsSystem>()->createBody3D(entity);
    return Body3D(scene, entity);
}

void Object::removeBody3D(){
    scene->getSystem<PhysicsSystem>()->removeBody3D(entity);
}

Ray Object::getRay(Vector3 direction){
    return Ray(getWorldPosition(), direction);
}