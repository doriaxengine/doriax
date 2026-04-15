//
// (c) 2026 Eduardo Doria.
//

#include "Joint3D.h"

#include "Scene.h"
#include "subsystem/PhysicsSystem.h"
#include "component/Joint3DComponent.h"

using namespace doriax;

Joint3D::Joint3D(Scene* scene): EntityHandle(scene){
    addComponent<Joint3DComponent>();
}

Joint3D::Joint3D(Scene* scene, Entity entity): EntityHandle(scene, entity){

}

Joint3D::~Joint3D(){

}

Joint3D::Joint3D(const Joint3D& rhs): EntityHandle(rhs){
    
}

Joint3D& Joint3D::operator=(const Joint3D& rhs){
    EntityHandle::operator =(rhs);

    return *this;
}

JPH::TwoBodyConstraint* Joint3D::getJoltJoint() const{
    Joint3DComponent& joint = getComponent<Joint3DComponent>();

    return joint.joint;
}

void Joint3D::setFixedJoint(Entity bodyA, Entity bodyB){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadFixedJoint3D(joint, bodyA, bodyB);
}

void Joint3D::setDistanceJoint(Entity bodyA, Entity bodyB){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.autoAnchors = true;

    setDistanceJoint(bodyA, bodyB, Vector3::ZERO, Vector3::ZERO);
}

void Joint3D::setDistanceJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchorOnBodyA;
    joint.anchorB = worldAnchorOnBodyB;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadDistanceJoint3D(joint, bodyA, bodyB, worldAnchorOnBodyA, worldAnchorOnBodyB, joint.autoAnchors);
}

void Joint3D::setPointJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchor = worldAnchor;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadPointJoint3D(joint, bodyA, bodyB, worldAnchor);
}

void Joint3D::setHingeJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 axis, Vector3 normal){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchor = worldAnchor;
    joint.axis = axis;
    joint.normal = normal;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadHingeJoint3D(joint, bodyA, bodyB, worldAnchor, axis, normal);
}

void Joint3D::setConeJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 twistAxis){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchor = worldAnchor;
    joint.twistAxis = twistAxis;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadConeJoint3D(joint, bodyA, bodyB, worldAnchor, twistAxis);
}

void Joint3D::setPrismaticJoint(Entity bodyA, Entity bodyB, Vector3 sliderAxis, float limitsMin, float limitsMax){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.axis = sliderAxis;
    joint.limitsMin = limitsMin;
    joint.limitsMax = limitsMax;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadPrismaticJoint3D(joint, bodyA, bodyB, sliderAxis, limitsMin, limitsMax);
}

void Joint3D::setSwingTwistJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchor, Vector3 twistAxis, Vector3 planeAxis, float normalHalfConeAngle, float planeHalfConeAngle, float twistMinAngle, float twistMaxAngle){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchor = worldAnchor;
    joint.twistAxis = twistAxis;
    joint.planeAxis = planeAxis;
    joint.normalHalfConeAngle = normalHalfConeAngle;
    joint.planeHalfConeAngle = planeHalfConeAngle;
    joint.twistMinAngle = twistMinAngle;
    joint.twistMaxAngle = twistMaxAngle;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadSwingTwistJoint3D(joint, bodyA, bodyB, worldAnchor, twistAxis, planeAxis, normalHalfConeAngle, planeHalfConeAngle, twistMinAngle, twistMaxAngle);
}

void Joint3D::setSixDOFJoint(Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB, Vector3 axisX, Vector3 axisY){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.anchorA = worldAnchorOnBodyA;
    joint.anchorB = worldAnchorOnBodyB;
    joint.axisX = axisX;
    joint.axisY = axisY;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadSixDOFJoint3D(joint, bodyA, bodyB, worldAnchorOnBodyA, worldAnchorOnBodyB, axisX, axisY);
}

void Joint3D::setPathJoint(Entity bodyA, Entity bodyB, std::vector<Vector3> positions, std::vector<Vector3> tangents, std::vector<Vector3> normals, Vector3 pathPosition, bool isLooping){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.pathPoints = positions;
    joint.pathPosition = pathPosition;
    joint.isLooping = isLooping;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadPathJoint3D(joint, bodyA, bodyB, positions, tangents, normals, pathPosition, isLooping);
}

void Joint3D::setGearJoint(Entity bodyA, Entity bodyB, Entity hingeA, Entity hingeB, int numTeethGearA, int numTeethGearB){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.hingeA = hingeA;
    joint.hingeB = hingeB;
    joint.numTeethGearA = numTeethGearA;
    joint.numTeethGearB = numTeethGearB;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadGearJoint3D(joint, bodyA, bodyB, hingeA, hingeB, numTeethGearA, numTeethGearB);
}

void Joint3D::setRackAndPinionJoint(Entity bodyA, Entity bodyB, Entity hinge, Entity slider, int numTeethRack, int numTeethGear, int rackLength){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();
    joint.bodyA = bodyA;
    joint.bodyB = bodyB;
    joint.hinge = hinge;
    joint.slider = slider;
    joint.numTeethRack = numTeethRack;
    joint.numTeethGear = numTeethGear;
    joint.rackLength = rackLength;
    joint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadRackAndPinionJoint3D(joint, bodyA, bodyB, hinge, slider, numTeethRack, numTeethGear, rackLength);
}

void Joint3D::setPulleyJoint(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 worldAnchorOnBodyA, Vector3 worldAnchorOnBodyB, Vector3 fixedPointA, Vector3 fixedPointB){
    (void)joint;

    Joint3DComponent& myJoint = getComponent<Joint3DComponent>();
    myJoint.bodyA = bodyA;
    myJoint.bodyB = bodyB;
    myJoint.anchorA = worldAnchorOnBodyA;
    myJoint.anchorB = worldAnchorOnBodyB;
    myJoint.fixedPointA = fixedPointA;
    myJoint.fixedPointB = fixedPointB;
    myJoint.needUpdateJoint = !scene->getSystem<PhysicsSystem>()->loadPulleyJoint3D(myJoint, bodyA, bodyB, worldAnchorOnBodyA, worldAnchorOnBodyB, fixedPointA, fixedPointB);
}

Joint3DType Joint3D::getType(){
    Joint3DComponent& joint = getComponent<Joint3DComponent>();

    return joint.type;
}