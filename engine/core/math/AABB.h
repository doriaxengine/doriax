#ifndef AABB_h
#define AABB_h

#include "Vector3.h"
#include "Matrix4.h"
#include "Sphere.h"

namespace doriax{

    class Plane;
    class OBB;
    
    class DORIAX_API AABB{

    public:

        enum BoxType {
            BOXTYPE_NULL,
            BOXTYPE_FINITE,
            BOXTYPE_INFINITE
        };

        enum CornerEnum {
            FAR_LEFT_BOTTOM = 0,
            FAR_LEFT_TOP = 1,
            FAR_RIGHT_TOP = 2,
            FAR_RIGHT_BOTTOM = 3,
            NEAR_RIGHT_BOTTOM = 7,
            NEAR_LEFT_BOTTOM = 6,
            NEAR_LEFT_TOP = 5,
            NEAR_RIGHT_TOP = 4
        };

    private:

        Vector3 mMinimum;
        Vector3 mMaximum;
        BoxType mBoxType;
        mutable Vector3* mCorners;

    public:

        static const AABB ZERO; // not null, but all zeros

        AABB();
        AABB(BoxType e);
        AABB(const AABB & rkBox);
        AABB(const Vector3& min, const Vector3& max);
        AABB(float mx, float my, float mz, float Mx, float My, float Mz);

        ~AABB();

        AABB& operator= (const AABB& rhs);
        bool operator== (const AABB& rhs) const;
        bool operator!= (const AABB& rhs) const;

        OBB getOBB() const;

        const Vector3& getMinimum() const;
        Vector3& getMinimum();

        const Vector3& getMaximum() const;
        Vector3& getMaximum();

        void setMinimum( const Vector3& vec );
        void setMinimum( float x, float y, float z );
        void setMinimumX(float x);
        void setMinimumY(float y);
        void setMinimumZ(float z);

        void setMaximum( const Vector3& vec );
        void setMaximum( float x, float y, float z );
        void setMaximumX( float x );
        void setMaximumY( float y );
        void setMaximumZ( float z );

        void setExtents( const Vector3& min, const Vector3& max );

        void setExtents(float mx, float my, float mz, float Mx, float My, float Mz );

        Vector3 getCorner(CornerEnum cornerToGet) const;
        const Vector3* getCorners(void) const;

        AABB& merge( const AABB& rhs );
        AABB& merge( const Vector3& point );

        AABB& transform( const Matrix4& matrix );

        void setNull();
        bool isNull(void) const;
        void setInfinite();
        bool isInfinite(void) const;
        void setFinite();
        bool isFinite(void) const;

        bool intersects(const AABB& b2) const;
        bool intersects(const OBB& obb) const;
        bool intersects(const Plane& p) const;
        bool intersects(const Sphere& sp) const;
        bool intersects(const Vector3& v) const;

        AABB intersection(const AABB& b2) const;

        float volume(void) const;

        void scale(const Vector3& s);

        Vector3 getCenter(void) const;
        Vector3 getSize(void) const;
        Vector3 getHalfSize(void) const;

        bool contains(const Vector3& v) const;
        bool contains(const AABB& other) const;

        float squaredDistance(const Vector3& v) const;
        float distance (const Vector3& v) const;
        
    };
    
}

#endif /* AABB_h */
