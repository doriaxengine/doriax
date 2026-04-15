//
// (c) 2026 Eduardo Doria.
//

#include "Joint2D.h"

#include "Scene.h"
#include "subsystem/PhysicsSystem.h"
#include "component/Joint2DComponent.h"

using namespace doriax;

Joint2D::Joint2D(Scene* scene): EntityHandle(scene){
    addComponent<Joint2DComponent>();
}

Joint2D::Joint2D(Scene* scene, Entity entity): EntityHandle(scene, entity){

}

Joint2D::~Joint2D(){

}

Joint2D::Joint2D(const Joint2D& rhs): EntityHandle(rhs){
    
}

Joint2D& Joint2D::operator=(const Joint2D& rhs){
    EntityHandle::operator =(rhs);

    return *this;
}

b2JointId Joint2D::getBox2DJoint() const{
    Joint2DComponent& joint = getComponent<Joint2DComponent>();

    return joint.joint;
}

void Joint2D::setDistanceJoint(Entity bodyA, Entity bodyB){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.autoAnchors = true;

    setDistanceJoint(bodyA, bodyB, Vector2::ZERO, Vector2::ZERO, false);
}

void Joint2D::setDistanceJoint(Entity bodyA, Entity bodyB, Vector2 worldAnchorOnBodyA, Vector2 worldAnchorOnBodyB, bool rope){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchorOnBodyA;
    joint.anchorB = worldAnchorOnBodyB;
    joint.autoAnchors = false;
    joint.rope = rope;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadDistanceJoint2D(entity, joint, bodyA, bodyB, worldAnchorOnBodyA, worldAnchorOnBodyB, false, rope);
}

void Joint2D::setRevoluteJoint(Entity bodyA, Entity bodyB, Vector2 worldAnchor){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchor;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadRevoluteJoint2D(entity, joint, bodyA, bodyB, worldAnchor);
}

void Joint2D::setPrismaticJoint(Entity bodyA, Entity bodyB, Vector2 worldAnchor, Vector2 worldAxis){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchor;
    joint.axis = worldAxis;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadPrismaticJoint2D(entity, joint, bodyA, bodyB, worldAnchor, worldAxis);
}

void Joint2D::setMouseJoint(Entity bodyA, Entity bodyB, Vector2 target){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.target = target;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadMouseJoint2D(entity, joint, bodyA, bodyB, target);
}

void Joint2D::setWheelJoint(Entity bodyA, Entity bodyB, Vector2 worldAnchor, Vector2 worldAxis){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchor;
    joint.axis = worldAxis;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadWheelJoint2D(entity, joint, bodyA, bodyB, worldAnchor, worldAxis);
}

void Joint2D::setWeldJoint(Entity bodyA, Entity bodyB, Vector2 worldAnchor){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchor;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadWeldJoint2D(entity, joint, bodyA, bodyB, worldAnchor);
}

void Joint2D::setMotorJoint(Entity bodyA, Entity bodyB){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadMotorJoint2D(entity, joint, bodyA, bodyB);
}

Joint2DType Joint2D::getType(){
    Joint2DComponent& joint = getComponent<Joint2DComponent>();

    return joint.type;
}