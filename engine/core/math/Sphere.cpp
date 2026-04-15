#include "Sphere.h"

#define _USE_MATH_DEFINES
#include <cmath>

#include "Plane.h"
#include "AABB.h"
#include "OBB.h"

using namespace doriax;


Sphere::Sphere() : center(Vector3(0, 0, 0)), radius(1.0f){ 

}

Sphere::Sphere(Vector3 c, float r) : center(c), radius(r){ 

}

Sphere::Sphere(const Sphere& s){
    this->center = s.center;
    this->radius = s.radius;
}

Sphere& Sphere::operator = (const Sphere& s){
    this->center = s.center;
    this->radius = s.radius;
    
    return *this;
}

bool Sphere::operator == (const Sphere& s){
    return ((this->center == s.center) && (this->radius == s.radius));
}

bool Sphere::operator != (const Sphere& s){
    return ((this->center != s.center) || (this->radius != s.radius));
}

std::string Sphere::toString() const {
    return std::string("Sphere(center: ") + center.toString() + ", radius: " + std::to_string(radius) + ")";
}

bool Sphere::contains(const Vector3& point) const {
    return (point - center).squaredLength() <= (radius * radius);
}

bool Sphere::intersects(const Sphere& other) const {
    float distanceSquared = (center - other.center).squaredLength();
    float radiusSum = radius + other.radius;
    return distanceSquared <= (radiusSum * radiusSum);
}

bool Sphere::intersects(const AABB& aabb) const{
    return aabb.intersects(*this);
}

bool Sphere::intersects(const OBB& obb) const {
    Vector3 closestPt = obb.closestPoint(center);
    float distanceSquared = closestPt.squaredDistance(center);
    return distanceSquared <= (radius * radius);
}

bool Sphere::intersects(const Plane& plane) const{
    return abs(plane.getDistance(center)) <= radius;
}

bool Sphere::intersects(const Vector3& v) const{
    return ((v - center).squaredLength() <= (radius*radius));
}

void Sphere::merge(const Sphere& other){
    Vector3 diff = other.center - center;
    float lengthSq = diff.squaredLength();
    float radiusDiff = other.radius - radius;

    if ((radiusDiff*radiusDiff) >= lengthSq) {
        if (radiusDiff <= 0.0f){
            return;
        }else{
            center = other.center;
            radius = other.radius;
            return;
        }
    }

    float length = sqrt(lengthSq);
    float t = (length + radiusDiff) / (2.0f * length);
    center = center + diff * t;
    radius = 0.5f * (length + radius + other.radius);
}

float Sphere::surfaceArea() const {
    return 4 * M_PI * radius * radius;
}

float Sphere::volume() const {
    return (4.0f / 3.0f) * M_PI * radius * radius * radius;
}