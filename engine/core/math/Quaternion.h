#ifndef quaternion_h
#define quaternion_h

#include "Vector3.h"
#include "Matrix4.h"

namespace doriax { class Quaternion; }
doriax::Quaternion operator * (float fScalar, const doriax::Quaternion& rkQ);

namespace doriax {

    enum class RotationOrder{
        XYZ,
        XZY,
        YXZ,
        YZX,
        ZXY,
        ZYX
    };

    class DORIAX_API Quaternion {
    public:

        float w, x, y, z;

        static const Quaternion IDENTITY;

        Quaternion();
        Quaternion (const Quaternion& rhs);
        Quaternion( const float fW, const float fX, const float fY, const float fZ );
        explicit Quaternion( float* const r );
        Quaternion(const float xAngle, const float yAngle, const float zAngle);
        Quaternion(const float xAngle, const float yAngle, const float zAngle, const RotationOrder& order);
        Quaternion(const Vector3* akAxis);
        Quaternion(const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis);
        Quaternion(const float angle, const Vector3& rkAxis);
        Quaternion(const Matrix3& kRot);
        Quaternion(const Matrix4& kRot);

        std::string toString() const;

        float operator [] ( const size_t i ) const;
        float& operator [] ( const size_t i );
        float* ptr();
        const float* ptr() const;

        Quaternion& operator = ( const Quaternion& rhs );
        bool operator == ( const Quaternion& rhs ) const;
        bool operator != ( const Quaternion& rhs ) const;
        bool equals(const Quaternion& rhs) const;

        Quaternion operator + ( const Quaternion& rhs ) const;
        Quaternion operator - ( const Quaternion& rhs ) const;
        Quaternion operator * ( float fScalar ) const;

        friend Quaternion (::operator *) (float fScalar, const Quaternion& rkQ);

        Quaternion operator * ( const Quaternion& rhs) const;
        Vector3 operator * (const Vector3& v) const;
        const Quaternion& operator + () const;
        Quaternion operator - () const;

        void swap(Quaternion& other);

        void fromEulerAngles(const float xAngle, const float yAngle, const float zAngle, const RotationOrder& order);
        void fromAxes (const Vector3* akAxis);
        void fromAxes (const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis);
        Quaternion& fromRotationMatrix (const Matrix3& kRot);
        Quaternion& fromRotationMatrix (const Matrix4& kRot);
        Matrix4 getRotationMatrix() const;
        void fromAngle (const float angle);
        void fromAngleAxis (const float angle, const Vector3& rkAxis);

        Vector3 getEulerAngles(const RotationOrder& order) const;
        Vector3 xAxis(void) const;
        Vector3 yAxis(void) const;
        Vector3 zAxis(void) const;

        float dot (const Quaternion& rkQ) const;
        float norm () const;
        Quaternion inverse () const;
        Quaternion unitInverse () const;
        Quaternion exp () const;
        Quaternion log () const;

        static Quaternion slerp(float t, const Quaternion& q1, const Quaternion& q2);
        static Quaternion slerp(float t,const  Quaternion& q1, const Quaternion& q2, bool shortestPath);
        static Quaternion slerpExtraSpins (float fT, const Quaternion& rkP, const Quaternion& rkQ, int iExtraSpins);
        static Quaternion nlerp(float fT, const Quaternion& rkP, const Quaternion& rkQ);
        static Quaternion nlerp(float fT, const Quaternion& rkP, const Quaternion& rkQ, bool shortestPath);
        static Quaternion squad (float fT, const Quaternion& rkP, const Quaternion& rkA, const Quaternion& rkB, const Quaternion& rkQ);

        Quaternion& normalize(void);
        Quaternion normalized() const;
        float normalizeL(void);
        float getRoll() const;
        float getPitch() const;
        float getYaw() const;

    };
    
}

#endif /* quaternion_h */
