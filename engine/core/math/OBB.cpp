#include "OBB.h"

#include "Plane.h"
#include "Sphere.h"
#include "Ray.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace doriax;

const OBB OBB::ZERO(OBB::BOXTYPE_FINITE);

OBB::OBB()
    : mCenter(Vector3::ZERO),
      mHalfExtents(Vector3(0.0f, 0.0f, 0.0f)),
      mAxisX(Vector3(1.0f, 0.0f, 0.0f)),
      mAxisY(Vector3(0.0f, 1.0f, 0.0f)),
      mAxisZ(Vector3(0.0f, 0.0f, 1.0f)),
      mCorners(0),
      mBoxType(BOXTYPE_NULL)
{
}

OBB::OBB(BoxType boxType)
    : mCenter(Vector3::ZERO),
      mHalfExtents(Vector3(0.0f, 0.0f, 0.0f)),
      mAxisX(Vector3(1.0f, 0.0f, 0.0f)),
      mAxisY(Vector3(0.0f, 1.0f, 0.0f)),
      mAxisZ(Vector3(0.0f, 0.0f, 1.0f)),
      mCorners(0),
      mBoxType(boxType)
{
}

OBB::OBB(const OBB& obb)
    : mCenter(obb.mCenter),
      mHalfExtents(obb.mHalfExtents),
      mAxisX(obb.mAxisX),
      mAxisY(obb.mAxisY),
      mAxisZ(obb.mAxisZ),
      mCorners(0),
      mBoxType(obb.mBoxType)
{
}

OBB::OBB(const Vector3& center, const Vector3& halfExtents)
    : mCenter(center),
      mHalfExtents(halfExtents),
      mAxisX(Vector3(1.0f, 0.0f, 0.0f)),
      mAxisY(Vector3(0.0f, 1.0f, 0.0f)),
      mAxisZ(Vector3(0.0f, 0.0f, 1.0f)),
      mCorners(0)
{
    mBoxType = BOXTYPE_FINITE;
}

OBB::OBB(const Vector3& center, const Vector3& halfExtents, 
    const Vector3& axisX, const Vector3& axisY, const Vector3& axisZ)
    : mCenter(center),
      mHalfExtents(halfExtents),
      mAxisX(axisX.normalized()),
      mAxisY(axisY.normalized()),
      mAxisZ(axisZ.normalized()),
      mCorners(0)
{
    mBoxType = BOXTYPE_FINITE;
}

OBB::OBB(const AABB& aabb)
    : mCenter(aabb.getCenter()),
      mHalfExtents(aabb.getHalfSize()),
      mAxisX(Vector3(1.0f, 0.0f, 0.0f)),
      mAxisY(Vector3(0.0f, 1.0f, 0.0f)),
      mAxisZ(Vector3(0.0f, 0.0f, 1.0f)),
      mCorners(0)
{
    if (aabb.isNull())
        mBoxType = BOXTYPE_NULL;
    else if (aabb.isInfinite())
        mBoxType = BOXTYPE_INFINITE;
    else
        mBoxType = BOXTYPE_FINITE;
}

OBB::OBB(const AABB& aabb, const Matrix4& transform) {
    // Extract position, rotation, and scale from transform matrix
    Vector3 position;
    Vector3 scale;
    Quaternion rotation;
    transform.decompose(position, scale, rotation);
    
    // Set center, half extents, and orientation
    mCenter = transform * aabb.getCenter();
    mHalfExtents = aabb.getHalfSize() * scale;
    setOrientation(rotation);

    if (aabb.isNull())
        mBoxType = BOXTYPE_NULL;
    else if (aabb.isInfinite())
        mBoxType = BOXTYPE_INFINITE;
    else
        mBoxType = BOXTYPE_FINITE;
}

OBB::~OBB() {
    if (mCorners)
        delete[] mCorners;
}

OBB& OBB::operator=(const OBB& rhs) {
    if (this == &rhs) return *this;

    mCenter = rhs.mCenter;
    mHalfExtents = rhs.mHalfExtents;
    mAxisX = rhs.mAxisX;
    mAxisY = rhs.mAxisY;
    mAxisZ = rhs.mAxisZ;
    mBoxType = rhs.mBoxType;

    if (mCorners) {
        delete[] mCorners;
        mCorners = 0;
    }

    return *this;
}

bool OBB::operator==(const OBB& rhs) const {
    if (this->mBoxType != rhs.mBoxType)
        return false;

    if (!this->isFinite())
        return true;  // NULL or INFINITE boxes are equal if same type

    return mCenter == rhs.mCenter &&
           mHalfExtents == rhs.mHalfExtents &&
           mAxisX == rhs.mAxisX &&
           mAxisY == rhs.mAxisY &&
           mAxisZ == rhs.mAxisZ;
}

bool OBB::operator!=(const OBB& rhs) const {
    return !(*this == rhs);
}

std::string OBB::toString() const {
    std::string typeStr;
    switch(mBoxType) {
        case BOXTYPE_NULL: typeStr = "NULL"; break;
        case BOXTYPE_FINITE: typeStr = "FINITE"; break;
        case BOXTYPE_INFINITE: typeStr = "INFINITE"; break;
    }

    return "OBB(" + typeStr + ", Center: " + mCenter.toString() + 
           ", HalfExtents: " + mHalfExtents.toString() + 
           ", AxisX: " + mAxisX.toString() + 
           ", AxisY: " + mAxisY.toString() + 
           ", AxisZ: " + mAxisZ.toString() + ")";
}

void OBB::setNull() { 
    mBoxType = BOXTYPE_NULL; 
}

bool OBB::isNull() const { 
    return mBoxType == BOXTYPE_NULL; 
}

void OBB::setInfinite() { 
    mBoxType = BOXTYPE_INFINITE; 
}

bool OBB::isInfinite() const { 
    return mBoxType == BOXTYPE_INFINITE; 
}

void OBB::setFinite() { 
    mBoxType = BOXTYPE_FINITE; 
}

bool OBB::isFinite() const { 
    return mBoxType == BOXTYPE_FINITE; 
}

const Vector3& OBB::getCenter() const {
    return mCenter;
}

void OBB::setCenter(const Vector3& center) {
    mCenter = center;
}

const Vector3& OBB::getHalfExtents() const {
    return mHalfExtents;
}

void OBB::setHalfExtents(const Vector3& halfExtents) {
    mHalfExtents = halfExtents;

    if (halfExtents.x == 0 && halfExtents.y == 0 && halfExtents.z == 0) {
        mBoxType = BOXTYPE_NULL;
    } else if (mBoxType == BOXTYPE_NULL) {
        mBoxType = BOXTYPE_FINITE;
    }
}

const Vector3& OBB::getAxisX() const {
    return mAxisX;
}

const Vector3& OBB::getAxisY() const {
    return mAxisY;
}

const Vector3& OBB::getAxisZ() const {
    return mAxisZ;
}

void OBB::setAxes(const Vector3& axisX, const Vector3& axisY, const Vector3& axisZ) {
    mAxisX = axisX.normalized();
    mAxisY = axisY.normalized();
    mAxisZ = axisZ.normalized();
}

void OBB::setAxes(const Quaternion& orientation) {
    mAxisX = orientation * Vector3::UNIT_X;
    mAxisY = orientation * Vector3::UNIT_Y;
    mAxisZ = orientation * Vector3::UNIT_Z;
}

void OBB::setOrientation(const Quaternion& orientation) {
    setAxes(orientation);
}

Quaternion OBB::getOrientation() const {
    // Create rotation matrix from the axes
    Matrix3 rotMatrix;
    rotMatrix[0][0] = mAxisX.x; rotMatrix[0][1] = mAxisY.x; rotMatrix[0][2] = mAxisZ.x;
    rotMatrix[1][0] = mAxisX.y; rotMatrix[1][1] = mAxisY.y; rotMatrix[1][2] = mAxisZ.y;
    rotMatrix[2][0] = mAxisX.z; rotMatrix[2][1] = mAxisY.z; rotMatrix[2][2] = mAxisZ.z;

    // Convert to quaternion
    return Quaternion(rotMatrix);
}

void OBB::transform(const Matrix4& matrix) {
    // Extract position, rotation, and scale from transform matrix
    Vector3 position;
    Vector3 scale;
    Quaternion rotation;
    matrix.decompose(position, scale, rotation);

    // Apply transformation
    transform(position, rotation, scale);
}

void OBB::transform(const Vector3& translate, const Quaternion& rotate, const Vector3& scale) {
    // Transform center
    mCenter = rotate * (mCenter * scale) + translate;

    // Transform axes
    Quaternion currOrientation = getOrientation();
    Quaternion newOrientation = rotate * currOrientation;
    setOrientation(newOrientation);

    // Scale half extents
    mHalfExtents = mHalfExtents * scale;
}

AABB OBB::toAABB() const {
    if (isNull()) return AABB(AABB::BOXTYPE_NULL);
    if (isInfinite()) return AABB(AABB::BOXTYPE_INFINITE);

    Vector3 min(std::numeric_limits<float>::max());
    Vector3 max(std::numeric_limits<float>::lowest());

    // Get corners and find min/max points
    const Vector3* corners = getCorners();

    for (int i = 0; i < 8; ++i) {
        min.makeFloor(corners[i]);
        max.makeCeil(corners[i]);
    }

    return AABB(min, max);
}

Matrix4 OBB::toMatrix() const {
    // Create rotation matrix from axes
    Matrix4 transform;

    // Set rotation (columns 0-2)
    transform.setColumn(0, Vector4(mAxisX * mHalfExtents.x, 0.0f));
    transform.setColumn(1, Vector4(mAxisY * mHalfExtents.y, 0.0f));
    transform.setColumn(2, Vector4(mAxisZ * mHalfExtents.z, 0.0f));

    // Set translation (column 3)
    transform.setColumn(3, Vector4(mCenter, 1.0f));

    return transform;
}

Vector3 OBB::getCorner(CornerEnum cornerToGet) const {
    if (isNull() || isInfinite()) return Vector3();

    switch(cornerToGet) {
        case FAR_LEFT_BOTTOM:
            return mCenter - mAxisX * mHalfExtents.x - mAxisY * mHalfExtents.y - mAxisZ * mHalfExtents.z;
        case FAR_LEFT_TOP:
            return mCenter - mAxisX * mHalfExtents.x + mAxisY * mHalfExtents.y - mAxisZ * mHalfExtents.z;
        case FAR_RIGHT_TOP:
            return mCenter + mAxisX * mHalfExtents.x + mAxisY * mHalfExtents.y - mAxisZ * mHalfExtents.z;
        case FAR_RIGHT_BOTTOM:
            return mCenter + mAxisX * mHalfExtents.x - mAxisY * mHalfExtents.y - mAxisZ * mHalfExtents.z;
        case NEAR_RIGHT_BOTTOM:
            return mCenter + mAxisX * mHalfExtents.x - mAxisY * mHalfExtents.y + mAxisZ * mHalfExtents.z;
        case NEAR_LEFT_BOTTOM:
            return mCenter - mAxisX * mHalfExtents.x - mAxisY * mHalfExtents.y + mAxisZ * mHalfExtents.z;
        case NEAR_LEFT_TOP:
            return mCenter - mAxisX * mHalfExtents.x + mAxisY * mHalfExtents.y + mAxisZ * mHalfExtents.z;
        case NEAR_RIGHT_TOP:
            return mCenter + mAxisX * mHalfExtents.x + mAxisY * mHalfExtents.y + mAxisZ * mHalfExtents.z;
        default:
            return Vector3();
    }
}

const Vector3* OBB::getCorners() const {
    if (!mCorners)
        mCorners = new Vector3[8];

    mCorners[FAR_LEFT_BOTTOM] = getCorner(FAR_LEFT_BOTTOM);
    mCorners[FAR_LEFT_TOP] = getCorner(FAR_LEFT_TOP);
    mCorners[FAR_RIGHT_TOP] = getCorner(FAR_RIGHT_TOP);
    mCorners[FAR_RIGHT_BOTTOM] = getCorner(FAR_RIGHT_BOTTOM);
    mCorners[NEAR_RIGHT_TOP] = getCorner(NEAR_RIGHT_TOP);
    mCorners[NEAR_LEFT_TOP] = getCorner(NEAR_LEFT_TOP);
    mCorners[NEAR_LEFT_BOTTOM] = getCorner(NEAR_LEFT_BOTTOM);
    mCorners[NEAR_RIGHT_BOTTOM] = getCorner(NEAR_RIGHT_BOTTOM);

    return mCorners;
}

void OBB::enclose(const OBB& other) {
    if (other.isNull()) return;

    if (this->isNull()) {
        *this = other;
        return;
    }

    if (other.isInfinite()) {
        setInfinite();
        return;
    }

    if (this->isInfinite()) {
        return;
    }

    const Vector3* otherCorners = other.getCorners();

    // Expand this OBB to include all corners of the other OBB
    for (int i = 0; i < 8; ++i) {
        enclose(otherCorners[i]);
    }

    if (mCorners) {
        delete[] mCorners;
        mCorners = nullptr;
    }
}

void OBB::enclose(const Vector3& point) {
    if (isInfinite()) return;

    if (isNull()) {
        mCenter = point;
        mHalfExtents = Vector3::ZERO;
        mAxisX = Vector3(1.0f, 0.0f, 0.0f);
        mAxisY = Vector3(0.0f, 1.0f, 0.0f);
        mAxisZ = Vector3(0.0f, 0.0f, 1.0f);
        setFinite();
        return;
    }

    // Transform point to OBB's local space
    Vector3 localPoint = point - mCenter;

    // Project point onto each axis
    float x = localPoint.dotProduct(mAxisX);
    float y = localPoint.dotProduct(mAxisY);
    float z = localPoint.dotProduct(mAxisZ);

    // Find new extents
    Vector3 minExtents = Vector3(-mHalfExtents.x, -mHalfExtents.y, -mHalfExtents.z);
    Vector3 maxExtents = mHalfExtents;

    // Update min/max extents
    if (x < minExtents.x) minExtents.x = x;
    if (x > maxExtents.x) maxExtents.x = x;
    if (y < minExtents.y) minExtents.y = y;
    if (y > maxExtents.y) maxExtents.y = y;
    if (z < minExtents.z) minExtents.z = z;
    if (z > maxExtents.z) maxExtents.z = z;

    // Calculate new center and half extents in local space
    Vector3 localCenter = (minExtents + maxExtents) * 0.5f;
    mHalfExtents = (maxExtents - minExtents) * 0.5f;

    // Transform center back to world space
    mCenter = mCenter + mAxisX * localCenter.x + mAxisY * localCenter.y + mAxisZ * localCenter.z;
}

bool OBB::intersects(const OBB& other) const {
    if (isNull() || other.isNull()) return false;
    if (isInfinite() || other.isInfinite()) return true;

    // Implementation using Separating Axis Theorem (SAT)

    // Get the axes to test
    const Vector3* axes1[3] = { &mAxisX, &mAxisY, &mAxisZ };
    const Vector3* axes2[3] = { &other.mAxisX, &other.mAxisY, &other.mAxisZ };

    // Translation vector between the two OBBs
    Vector3 translation = other.mCenter - mCenter;

    // Matrix to transform from other's local space to this OBB's local space
    float R[3][3];

    // Compute R Matrix (rotation matrix from other to this)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = axes1[i]->dotProduct(*axes2[j]);
        }
    }

    // Translation relative to A's frame
    float t[3];
    t[0] = translation.dotProduct(*axes1[0]);
    t[1] = translation.dotProduct(*axes1[1]);
    t[2] = translation.dotProduct(*axes1[2]);

    // Test axes of this OBB
    for (int i = 0; i < 3; ++i) {
        float ra = mHalfExtents[i];
        float rb = other.mHalfExtents[0] * std::abs(R[i][0]) + 
                   other.mHalfExtents[1] * std::abs(R[i][1]) + 
                   other.mHalfExtents[2] * std::abs(R[i][2]);
        
        if (std::abs(t[i]) > ra + rb) {
            return false;
        }
    }

    // Test axes of other OBB
    for (int i = 0; i < 3; ++i) {
        float ra = mHalfExtents[0] * std::abs(R[0][i]) + 
                   mHalfExtents[1] * std::abs(R[1][i]) + 
                   mHalfExtents[2] * std::abs(R[2][i]);
        float rb = other.mHalfExtents[i];

        float t_i = t[0] * R[0][i] + t[1] * R[1][i] + t[2] * R[2][i];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Test the 9 cross products of the edges
    // X axis of this with X, Y, Z of other
    {
        float ra = mHalfExtents[1] * std::abs(R[2][0]) + mHalfExtents[2] * std::abs(R[1][0]);
        float rb = other.mHalfExtents[1] * std::abs(R[0][2]) + other.mHalfExtents[2] * std::abs(R[0][1]);
        float t_i = t[2] * R[1][0] - t[1] * R[2][0];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // X axis of this with Y axis of other
    {
        float ra = mHalfExtents[1] * std::abs(R[2][1]) + mHalfExtents[2] * std::abs(R[1][1]);
        float rb = other.mHalfExtents[0] * std::abs(R[0][2]) + other.mHalfExtents[2] * std::abs(R[0][0]);
        float t_i = t[2] * R[1][1] - t[1] * R[2][1];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // X axis of this with Z axis of other
    {
        float ra = mHalfExtents[1] * std::abs(R[2][2]) + mHalfExtents[2] * std::abs(R[1][2]);
        float rb = other.mHalfExtents[0] * std::abs(R[0][1]) + other.mHalfExtents[1] * std::abs(R[0][0]);
        float t_i = t[2] * R[1][2] - t[1] * R[2][2];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Y axis of this with X, Y, Z of other
    {
        float ra = mHalfExtents[0] * std::abs(R[2][0]) + mHalfExtents[2] * std::abs(R[0][0]);
        float rb = other.mHalfExtents[1] * std::abs(R[1][2]) + other.mHalfExtents[2] * std::abs(R[1][1]);
        float t_i = t[0] * R[2][0] - t[2] * R[0][0];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Y axis of this with Y axis of other
    {
        float ra = mHalfExtents[0] * std::abs(R[2][1]) + mHalfExtents[2] * std::abs(R[0][1]);
        float rb = other.mHalfExtents[0] * std::abs(R[1][2]) + other.mHalfExtents[2] * std::abs(R[1][0]);
        float t_i = t[0] * R[2][1] - t[2] * R[0][1];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Y axis of this with Z axis of other
    {
        float ra = mHalfExtents[0] * std::abs(R[2][2]) + mHalfExtents[2] * std::abs(R[0][2]);
        float rb = other.mHalfExtents[0] * std::abs(R[1][1]) + other.mHalfExtents[1] * std::abs(R[1][0]);
        float t_i = t[0] * R[2][2] - t[2] * R[0][2];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Z axis of this with X, Y, Z of other
    {
        float ra = mHalfExtents[0] * std::abs(R[1][0]) + mHalfExtents[1] * std::abs(R[0][0]);
        float rb = other.mHalfExtents[1] * std::abs(R[2][2]) + other.mHalfExtents[2] * std::abs(R[2][1]);
        float t_i = t[1] * R[0][0] - t[0] * R[1][0];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Z axis of this with Y axis of other
    {
        float ra = mHalfExtents[0] * std::abs(R[1][1]) + mHalfExtents[1] * std::abs(R[0][1]);
        float rb = other.mHalfExtents[0] * std::abs(R[2][2]) + other.mHalfExtents[2] * std::abs(R[2][0]);
        float t_i = t[1] * R[0][1] - t[0] * R[1][1];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }

    // Z axis of this with Z axis of other
    {
        float ra = mHalfExtents[0] * std::abs(R[1][2]) + mHalfExtents[1] * std::abs(R[0][2]);
        float rb = other.mHalfExtents[0] * std::abs(R[2][1]) + other.mHalfExtents[1] * std::abs(R[2][0]);
        float t_i = t[1] * R[0][2] - t[0] * R[1][2];

        if (std::abs(t_i) > ra + rb) {
            return false;
        }
    }
    
    // No separating axis found, the OBBs intersect
    return true;
}

bool OBB::intersects(const AABB& aabb) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    // Convert AABB to OBB and use OBB-OBB intersection test
    OBB obbFromAABB(aabb);
    return intersects(obbFromAABB);
}

bool OBB::intersects(const Sphere& sphere) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    Vector3 closest = closestPoint(sphere.center);
    float distanceSquared = closest.squaredDistance(sphere.center);
    return distanceSquared <= (sphere.radius * sphere.radius);
}

bool OBB::intersects(const Plane& plane) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    // Get the projection interval radius of the OBB onto the plane normal
    float r = mHalfExtents.x * std::abs(plane.normal.dotProduct(mAxisX)) +
              mHalfExtents.y * std::abs(plane.normal.dotProduct(mAxisY)) +
              mHalfExtents.z * std::abs(plane.normal.dotProduct(mAxisZ));

    // Compute distance of box center from plane
    float distance = plane.normal.dotProduct(mCenter) + plane.d;

    // Intersection occurs when distance falls within [-r, r]
    return std::abs(distance) <= r;
}

bool OBB::intersects(const Vector3& point) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    // Transform the point into the OBB's local space
    Vector3 localPoint = point - mCenter;

    // Project local point onto each axis
    float projX = localPoint.dotProduct(mAxisX);
    float projY = localPoint.dotProduct(mAxisY);
    float projZ = localPoint.dotProduct(mAxisZ);

    // Check if the point is within the OBB
    return std::abs(projX) <= mHalfExtents.x &&
           std::abs(projY) <= mHalfExtents.y &&
           std::abs(projZ) <= mHalfExtents.z;
}

bool OBB::contains(const Vector3& point) const {
    return intersects(point);
}

bool OBB::contains(const OBB& other) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    // Get corners of the other OBB
    const Vector3* corners = other.getCorners();

    // Check if all corners are inside this OBB
    for (int i = 0; i < 8; ++i) {
        if (!contains(corners[i])) {
            return false;
        }
    }

    return true;
}

float OBB::squaredDistance(const Vector3& point) const {
    Vector3 closestPoint = this->closestPoint(point);
    return closestPoint.squaredDistance(point);
}

float OBB::distance(const Vector3& point) const {
    return sqrt(squaredDistance(point));
}

float OBB::volume() const {
    if (isNull()) return 0.0f;
    if (isInfinite()) return std::numeric_limits<float>::infinity();

    return mHalfExtents.x * 2 * mHalfExtents.y * 2 * mHalfExtents.z * 2;
}

Vector3 OBB::closestPoint(const Vector3& point) const {
    // Convert point to OBB's local coordinates
    Vector3 localPoint = point - mCenter;

    // Project point onto each axis
    float x = localPoint.dotProduct(mAxisX);
    float y = localPoint.dotProduct(mAxisY);
    float z = localPoint.dotProduct(mAxisZ);

    // Clamp to box
    x = std::clamp(x, -mHalfExtents.x, mHalfExtents.x);
    y = std::clamp(y, -mHalfExtents.y, mHalfExtents.y);
    z = std::clamp(z, -mHalfExtents.z, mHalfExtents.z);

    // Convert back to world coordinates
    return mCenter + mAxisX * x + mAxisY * y + mAxisZ * z;
}