//
// (c) 2026 Eduardo Doria.
//

#include "Contact2D.h"

#include "box2d/box2d.h"

using namespace doriax;

Contact2D::Contact2D(Scene* scene, b2ContactData contact){
    this->scene = scene;
    this->contact = contact;
}

Contact2D::~Contact2D(){

}

Contact2D::Contact2D(const Contact2D& rhs){
    scene = rhs.scene;
    contact = rhs.contact;

}

Contact2D& Contact2D::operator=(const Contact2D& rhs){
    scene = rhs.scene;
    contact = rhs.contact;
    
    return *this;
}

b2ContactData Contact2D::getBox2DContact() const{
    return contact;
}

Manifold2D Contact2D::getManifold() const{
    return Manifold2D(scene, &contact.manifold);
}

Entity Contact2D::getBodyEntityA() const{
    return reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(contact.shapeIdA)));
}

Body2D Contact2D::getBodyA() const{
    return Body2D(scene, getBodyEntityA());
}

size_t Contact2D::getShapeIndexA() const{
    return reinterpret_cast<size_t>(b2Shape_GetUserData(contact.shapeIdA));
}

Entity Contact2D::getBodyEntityB() const{
    return reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(contact.shapeIdB)));
}

Body2D Contact2D::getBodyB() const{
    return Body2D(scene, getBodyEntityB());
}

size_t Contact2D::getShapeIndexB() const{
    return reinterpret_cast<size_t>(b2Shape_GetUserData(contact.shapeIdB));
}