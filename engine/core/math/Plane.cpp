#include "Plane.h"

#include "Matrix3.h"
#include "OBB.h"

using namespace doriax;

Plane::Plane () {
    normal = Vector3::ZERO;
    d = 0.0;
}

Plane::Plane (const Plane& rhs) {
    normal = rhs.normal;
    d = rhs.d;
}

Plane::Plane (const Vector3& rkNormal, float fConstant) {
    normal = rkNormal;
    d = -fConstant;
}

Plane::Plane (float a, float b, float c, float _d)
        : normal(a, b, c), d(_d) {
}

Plane::Plane (const Vector3& rkNormal, const Vector3& rkPoint) {
    redefine(rkNormal, rkPoint);
}

Plane::Plane (const Vector3& rkPoint0, const Vector3& rkPoint1, const Vector3& rkPoint2) {
    redefine(rkPoint0, rkPoint1, rkPoint2);
}

Plane Plane::operator - () const {
    return Plane(-(normal.x), -(normal.y), -(normal.z), -d);
}

bool Plane::operator==(const Plane& rhs) const {
    return (rhs.d == d && rhs.normal == normal);
}

bool Plane::operator!=(const Plane& rhs) const {
    return (rhs.d != d || rhs.normal != normal);
}

float Plane::getDistance (const Vector3& rkPoint) const {
    return normal.dotProduct(rkPoint) + d;
}

Plane::Side Plane::getSide (const Vector3& rkPoint) const {
    float fDistance = getDistance(rkPoint);

    if ( fDistance < 0.0 )
        return Plane::NEGATIVE_SIDE;

    if ( fDistance > 0.0 )
        return Plane::POSITIVE_SIDE;

    return Plane::NO_SIDE;
}

Plane::Side Plane::getSide (const Vector3& centre, const Vector3& halfSize) const {
    float dist = getDistance(centre);
    float maxAbsDist = normal.absDotProduct(halfSize);

    if (dist < -maxAbsDist)
        return Plane::NEGATIVE_SIDE;

    if (dist > +maxAbsDist)
        return Plane::POSITIVE_SIDE;

    return Plane::BOTH_SIDE;
}

Plane::Side Plane::getSide (const AABB& box) const {
    if (box.isNull())
        return Plane::NO_SIDE;
    if (box.isInfinite())
        return Plane::BOTH_SIDE;

    return getSide(box.getCenter(), box.getHalfSize());
}

Plane::Side Plane::getSide (const OBB& obb) const {
    // First, find the extent of the OBB along the plane's normal
    float r = obb.getHalfExtents().x * std::abs(normal.dotProduct(obb.getAxisX())) +
              obb.getHalfExtents().y * std::abs(normal.dotProduct(obb.getAxisY())) +
              obb.getHalfExtents().z * std::abs(normal.dotProduct(obb.getAxisZ()));

    // Now find the distance of the center from the plane
    float distance = getDistance(obb.getCenter());

    // Determine which side the OBB is on
    if (distance < -r)
        return Plane::NEGATIVE_SIDE;

    if (distance > r)
        return Plane::POSITIVE_SIDE;

    return Plane::BOTH_SIDE;
}

void Plane::redefine(const Vector3& rkPoint0, const Vector3& rkPoint1, const Vector3& rkPoint2) {
    Vector3 kEdge1 = rkPoint1 - rkPoint0;
    Vector3 kEdge2 = rkPoint2 - rkPoint0;
    normal = kEdge1.crossProduct(kEdge2);
    normal.normalize();
    d = -normal.dotProduct(rkPoint0);
}

void Plane::redefine(const Vector3& rkNormal, const Vector3& rkPoint) {
    normal = rkNormal;
    d = -rkNormal.dotProduct(rkPoint);
}

Vector3 Plane::projectVector(const Vector3& p) const {
    Matrix3 xform;
    xform[0][0] = 1.0f - normal.x * normal.x;
    xform[1][0] = -normal.x * normal.y;
    xform[2][0] = -normal.x * normal.z;
    xform[0][1] = -normal.y * normal.x;
    xform[1][1] = 1.0f - normal.y * normal.y;
    xform[2][1] = -normal.y * normal.z;
    xform[0][2] = -normal.z * normal.x;
    xform[1][2] = -normal.z * normal.y;
    xform[2][2] = 1.0f - normal.z * normal.z;
    return xform * p;
}

Plane& Plane::normalize(void) {
    float fLength = normal.length();

    if ( fLength > 0.0 ) {
        float fInvLength = 1.0f / fLength;
        normal *= fInvLength;
        d *= fInvLength;
    }

    return *this;
}

Plane Plane::normalized() const{
    Plane result = *this;
    result.normalize();
    return result;
}