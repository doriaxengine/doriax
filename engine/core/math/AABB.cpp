#include "AABB.h"

#include "Plane.h"
#include "Log.h"
#include "OBB.h"

using namespace doriax;

const AABB AABB::ZERO(0, 0, 0, 0, 0, 0);


AABB::AABB()
        : mMinimum(Vector3(0, 0, 0)), mMaximum(Vector3(0, 0, 0)), mCorners(0) {
    mBoxType = BOXTYPE_NULL;
}

AABB::AABB(BoxType bt)
        : mMinimum(Vector3(0, 0, 0)), mMaximum(Vector3(0, 0, 0)), mCorners(0) {
    mBoxType = bt;
}

AABB::AABB(const AABB & rkBox)
        : mMinimum(Vector3(0, 0, 0)), mMaximum(Vector3(0, 0, 0)), mCorners(0) {
    if (rkBox.isNull())
        setNull();
    else if (rkBox.isInfinite())
        setInfinite();
    else
        setExtents( rkBox.mMinimum, rkBox.mMaximum );
}

AABB::AABB( const Vector3& min, const Vector3& max )
        : mMinimum(Vector3(0, 0, 0)), mMaximum(Vector3(0, 0, 0)), mCorners(0) {
    setExtents( min, max );
}

AABB::AABB(float mx, float my, float mz, float Mx, float My, float Mz )
        : mMinimum(Vector3(0, 0, 0)), mMaximum(Vector3(0, 0, 0)), mCorners(0) {
    setExtents( mx, my, mz, Mx, My, Mz );
}

AABB::~AABB() {
    if (mCorners)
        delete mCorners;
}

AABB& AABB::operator= (const AABB& rhs) {
    if (rhs.isNull())
        setNull();
    else if (rhs.isInfinite())
        setInfinite();
    else
        setExtents(rhs.mMinimum, rhs.mMaximum);

    return *this;
}

bool AABB::operator== (const AABB& rhs) const {
    if (this->mBoxType != rhs.mBoxType)
        return false;

    if (!this->isFinite())
        return true;

    return this->mMinimum == rhs.mMinimum &&
           this->mMaximum == rhs.mMaximum;
}

bool AABB::operator!= (const AABB& rhs) const {
    return !(*this == rhs);
}

OBB AABB::getOBB() const {
    if (isNull()) {
        return OBB();
    }

    if (isInfinite()) {
        return OBB(getCenter(), Vector3(std::numeric_limits<float>::max()));
    }

    return OBB(getCenter(), getHalfSize());
}

const Vector3& AABB::getMinimum() const {
    return mMinimum;
}

Vector3& AABB::getMinimum() {
    return mMinimum;
}

const Vector3& AABB::getMaximum() const {
    return mMaximum;
}

Vector3& AABB::getMaximum() {
    return mMaximum;
}

void AABB::setMinimum( const Vector3& vec ) {
    mBoxType = BOXTYPE_FINITE;
    mMinimum = vec;
}

void AABB::setMinimum( float x, float y, float z ) {
    mBoxType = BOXTYPE_FINITE;
    mMinimum.x = x;
    mMinimum.y = y;
    mMinimum.z = z;
}

void AABB::setMinimumX(float x) {
    mMinimum.x = x;
}

void AABB::setMinimumY(float y) {
    mMinimum.y = y;
}

void AABB::setMinimumZ(float z){
    mMinimum.z = z;
}

void AABB::setMaximum( const Vector3& vec ) {
    mBoxType = BOXTYPE_FINITE;
    mMaximum = vec;
}

void AABB::setMaximum( float x, float y, float z ) {
    mBoxType = BOXTYPE_FINITE;
    mMaximum.x = x;
    mMaximum.y = y;
    mMaximum.z = z;
}

void AABB::setMaximumX( float x ) {
    mMaximum.x = x;
}

void AABB::setMaximumY( float y ) {
    mMaximum.y = y;
}

void AABB::setMaximumZ( float z ) {
    mMaximum.z = z;
}

void AABB::setExtents( const Vector3& min, const Vector3& max ) {
    //TODO: Put error
    //The minimum corner of the box must be less than or equal to maximum corner
    assert( min.x <= max.x && min.y <= max.y && min.z <= max.z );

    mBoxType = BOXTYPE_FINITE;
    mMinimum = min;
    mMaximum = max;
}

void AABB::setExtents(float mx, float my, float mz, float Mx, float My, float Mz ) {
    //TODO: Put error
    //The minimum corner of the box must be less than or equal to maximum corner
    assert( mx <= Mx && my <= My && mz <= Mz );

    mBoxType = BOXTYPE_FINITE;

    mMinimum.x = mx;
    mMinimum.y = my;
    mMinimum.z = mz;

    mMaximum.x = Mx;
    mMaximum.y = My;
    mMaximum.z = Mz;
}

Vector3 AABB::getCorner(CornerEnum cornerToGet) const {
    switch(cornerToGet)
    {
        case FAR_LEFT_BOTTOM:
            return mMinimum;
        case FAR_LEFT_TOP:
            return Vector3(mMinimum.x, mMaximum.y, mMinimum.z);
        case FAR_RIGHT_TOP:
            return Vector3(mMaximum.x, mMaximum.y, mMinimum.z);
        case FAR_RIGHT_BOTTOM:
            return Vector3(mMaximum.x, mMinimum.y, mMinimum.z);
        case NEAR_RIGHT_BOTTOM:
            return Vector3(mMaximum.x, mMinimum.y, mMaximum.z);
        case NEAR_LEFT_BOTTOM:
            return Vector3(mMinimum.x, mMinimum.y, mMaximum.z);
        case NEAR_LEFT_TOP:
            return Vector3(mMinimum.x, mMaximum.y, mMaximum.z);
        case NEAR_RIGHT_TOP:
            return mMaximum;
        default:
            return Vector3();
    }
}

const Vector3* AABB::getCorners(void) const {
    //TODO: Put error
    //Can't get corners of a null or infinite AAB
    assert( mBoxType == BOXTYPE_FINITE );

    if (!mCorners)
        mCorners = new Vector3[8];

    mCorners[0] = mMinimum;
    mCorners[1].x = mMinimum.x; mCorners[1].y = mMaximum.y; mCorners[1].z = mMinimum.z;
    mCorners[2].x = mMaximum.x; mCorners[2].y = mMaximum.y; mCorners[2].z = mMinimum.z;
    mCorners[3].x = mMaximum.x; mCorners[3].y = mMinimum.y; mCorners[3].z = mMinimum.z;

    mCorners[4] = mMaximum;
    mCorners[5].x = mMinimum.x; mCorners[5].y = mMaximum.y; mCorners[5].z = mMaximum.z;
    mCorners[6].x = mMinimum.x; mCorners[6].y = mMinimum.y; mCorners[6].z = mMaximum.z;
    mCorners[7].x = mMaximum.x; mCorners[7].y = mMinimum.y; mCorners[7].z = mMaximum.z;

    return mCorners;
}

AABB& AABB::merge( const AABB& rhs ) {
    if ((rhs.mBoxType == BOXTYPE_NULL) || (mBoxType == BOXTYPE_INFINITE)) {
        return *this;
    }else if (rhs.mBoxType == BOXTYPE_INFINITE) {
        mBoxType = BOXTYPE_INFINITE;
    }else if (mBoxType == BOXTYPE_NULL) {
        setExtents(rhs.mMinimum, rhs.mMaximum);
    }else{
        Vector3 min = mMinimum;
        Vector3 max = mMaximum;
        max.makeCeil(rhs.mMaximum);
        min.makeFloor(rhs.mMinimum);

        setExtents(min, max);
    }

    return *this;
}

AABB& AABB::merge( const Vector3& point ) {
    switch (mBoxType)
    {
        case BOXTYPE_NULL:
            setExtents(point, point);
            return *this;

        case BOXTYPE_FINITE:
            mMaximum.makeCeil(point);
            mMinimum.makeFloor(point);
            return *this;

        case BOXTYPE_INFINITE:
            return *this;
    }

    //TODO: Put error
    //Never reached
    assert( false );

    return *this;
}

AABB& AABB::transform( const Matrix4& matrix ) {
    if( mBoxType != BOXTYPE_FINITE )
        return *this;

    Vector3 oldMin, oldMax, currentCorner;

    oldMin = mMinimum;
    oldMax = mMaximum;

    setNull();

    // min min min
    currentCorner = oldMin;
    merge( matrix * currentCorner );

    // min,min,max
    currentCorner.z = oldMax.z;
    merge( matrix * currentCorner );

    // min max max
    currentCorner.y = oldMax.y;
    merge( matrix * currentCorner );

    // min max min
    currentCorner.z = oldMin.z;
    merge( matrix * currentCorner );

    // max max min
    currentCorner.x = oldMax.x;
    merge( matrix * currentCorner );

    // max max max
    currentCorner.z = oldMax.z;
    merge( matrix * currentCorner );

    // max min max
    currentCorner.y = oldMin.y;
    merge( matrix * currentCorner );

    // max min min
    currentCorner.z = oldMin.z;
    merge( matrix * currentCorner );

    return *this;
}

void AABB::setNull() {
    mBoxType = BOXTYPE_NULL;
}

bool AABB::isNull(void) const {
    return (mBoxType == BOXTYPE_NULL);
}

void AABB::setInfinite() {
    mBoxType = BOXTYPE_INFINITE;
}

bool AABB::isInfinite(void) const {
    return (mBoxType == BOXTYPE_INFINITE);
}

void AABB::setFinite() {
    mBoxType = BOXTYPE_FINITE;
}

bool AABB::isFinite(void) const {
    return (mBoxType == BOXTYPE_FINITE);
}

bool AABB::intersects(const AABB& b2) const {
    if (this->isNull() || b2.isNull())
        return false;

    if (this->isInfinite() || b2.isInfinite())
        return true;

    if (mMaximum.x < b2.mMinimum.x)
        return false;
    if (mMaximum.y < b2.mMinimum.y)
        return false;
    if (mMaximum.z < b2.mMinimum.z)
        return false;

    if (mMinimum.x > b2.mMaximum.x)
        return false;
    if (mMinimum.y > b2.mMaximum.y)
        return false;
    if (mMinimum.z > b2.mMaximum.z)
        return false;

    return true;
}

bool AABB::intersects(const OBB& obb) const {
    if (isNull()) return false;
    if (isInfinite()) return true;

    return getOBB().intersects(obb);
}

bool AABB::intersects(const Plane& p) const {
    return (p.getSide(*this) == Plane::Side::BOTH_SIDE);
}

bool AABB::intersects(const Sphere& sp) const{
    if (isNull()) return false;
    if (isInfinite()) return true;

    const Vector3& center = sp.center;
    float radius = sp.radius;
    const Vector3& min = getMinimum();
    const Vector3& max = getMaximum();

    float s, d = 0;
    for (int i = 0; i < 3; ++i){
        if (center[i] < min[i]){
            s = center[i] - min[i];
            d += s * s;
        }else if(center[i] > max[i]){
            s = center[i] - max[i];
            d += s * s;
        }
    }
    return d <= radius * radius;
}

bool AABB::intersects(const Vector3& v) const {
    switch (mBoxType)
    {
        case BOXTYPE_NULL:
            return false;

        case BOXTYPE_FINITE:
            return(v.x >= mMinimum.x  &&  v.x <= mMaximum.x  &&
                   v.y >= mMinimum.y  &&  v.y <= mMaximum.y  &&
                   v.z >= mMinimum.z  &&  v.z <= mMaximum.z);

        case BOXTYPE_INFINITE:
            return true;

        default:
            assert( false );
            return false;
    }
}

AABB AABB::intersection(const AABB& b2) const {
    if (this->isNull() || b2.isNull()) {
        return AABB();
    } else if (this->isInfinite()) {
        return b2;
    } else if (b2.isInfinite()) {
        return *this;
    }

    Vector3 intMin = mMinimum;
    Vector3 intMax = mMaximum;

    intMin.makeCeil(b2.getMinimum());
    intMax.makeFloor(b2.getMaximum());

    if (intMin.x < intMax.x &&
        intMin.y < intMax.y &&
        intMin.z < intMax.z) {
        return AABB(intMin, intMax);
    }

    return AABB();
}

float AABB::volume(void) const {
    switch (mBoxType)
    {
        case BOXTYPE_NULL:
            return 0.0f;

        case BOXTYPE_FINITE:
        {
            Vector3 diff = mMaximum - mMinimum;
            return diff.x * diff.y * diff.z;
        }

        case BOXTYPE_INFINITE:
            return std::numeric_limits<float>::infinity();

        default:
            assert( false );
            return 0.0f;
    }
}

void AABB::scale(const Vector3& s) {
    if (mBoxType != BOXTYPE_FINITE)
        return;

    Vector3 min = mMinimum * s;
    Vector3 max = mMaximum * s;
    setExtents(min, max);
}

Vector3 AABB::getCenter(void) const {
    //TODO: Put error
    //Can't get center of a null or infinite AAB
    assert( mBoxType == BOXTYPE_FINITE );

    return Vector3(
            (mMaximum.x + mMinimum.x) * 0.5f,
            (mMaximum.y + mMinimum.y) * 0.5f,
            (mMaximum.z + mMinimum.z) * 0.5f);
}

Vector3 AABB::getSize(void) const {
    switch (mBoxType)
    {
        case BOXTYPE_NULL:
            return Vector3::ZERO;

        case BOXTYPE_FINITE:
            return mMaximum - mMinimum;

        case BOXTYPE_INFINITE:
            return Vector3(
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity());

        default:
            assert( false );
            return Vector3::ZERO;
    }
}

Vector3 AABB::getHalfSize(void) const {
    switch (mBoxType)
    {
        case BOXTYPE_NULL:
            return Vector3::ZERO;

        case BOXTYPE_FINITE:
            return (mMaximum - mMinimum) * 0.5;

        case BOXTYPE_INFINITE:
            return Vector3(
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity());

        default:
            assert( false );
            return Vector3::ZERO;
    }
}

bool AABB::contains(const Vector3& v) const {
    if (isNull())
        return false;
    if (isInfinite())
        return true;

    return mMinimum.x <= v.x && v.x <= mMaximum.x &&
           mMinimum.y <= v.y && v.y <= mMaximum.y &&
           mMinimum.z <= v.z && v.z <= mMaximum.z;
}

float AABB::squaredDistance(const Vector3& v) const {

    if (this->contains(v))
        return 0;
    else
    {
        Vector3 maxDist(0,0,0);

        if (v.x < mMinimum.x)
            maxDist.x = mMinimum.x - v.x;
        else if (v.x > mMaximum.x)
            maxDist.x = v.x - mMaximum.x;

        if (v.y < mMinimum.y)
            maxDist.y = mMinimum.y - v.y;
        else if (v.y > mMaximum.y)
            maxDist.y = v.y - mMaximum.y;

        if (v.z < mMinimum.z)
            maxDist.z = mMinimum.z - v.z;
        else if (v.z > mMaximum.z)
            maxDist.z = v.z - mMaximum.z;

        return maxDist.squaredLength();
    }
}

float AABB::distance (const Vector3& v) const {
    return sqrt(squaredDistance(v));
}

bool AABB::contains(const AABB& other) const {
    if (other.isNull() || this->isInfinite())
        return true;

    if (this->isNull() || other.isInfinite())
        return false;

    return this->mMinimum.x <= other.mMinimum.x &&
           this->mMinimum.y <= other.mMinimum.y &&
           this->mMinimum.z <= other.mMinimum.z &&
           other.mMaximum.x <= this->mMaximum.x &&
           other.mMaximum.y <= this->mMaximum.y &&
           other.mMaximum.z <= this->mMaximum.z;
}