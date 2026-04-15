
#ifndef vector2_h
#define vector2_h

#include "Export.h"
#include <algorithm>
#include <assert.h>
#include <math.h>
#include <string>

namespace doriax { class Vector2; }
doriax::Vector2 operator * ( const float fScalar, const doriax::Vector2& rkVector );
doriax::Vector2 operator / ( const float fScalar, const doriax::Vector2& rkVector );
doriax::Vector2 operator + (const doriax::Vector2& lhs, const float rhs);
doriax::Vector2 operator + (const float lhs, const doriax::Vector2& rhs);
doriax::Vector2 operator - (const doriax::Vector2& lhs, const float rhs);
doriax::Vector2 operator - (const float lhs, const doriax::Vector2& rhs);

namespace doriax {

    class Vector3;
    class Vector4;

    class DORIAX_API Vector2
    {
    public:
        float x, y;

        static const Vector2 ZERO;
        static const Vector2 UNIT_X;
        static const Vector2 UNIT_Y;
        static const Vector2 NEGATIVE_UNIT_X;
        static const Vector2 NEGATIVE_UNIT_Y;
        static const Vector2 UNIT_SCALE;

        Vector2();
        Vector2( const float fX, const float fY );
        Vector2( const Vector2& v );
        explicit Vector2( const float scaler );
        explicit Vector2( const float afCoordinate[2] );
        explicit Vector2( const int afCoordinate[2] );
        explicit Vector2( float* const r );
        explicit Vector2( const Vector3& vec3 );
        explicit Vector2( const Vector4& vec4 );

        std::string toString() const;

        float operator [] ( const size_t i ) const;
        float& operator [] ( const size_t i );
        Vector2& operator = ( const Vector2& rkVector );
        Vector2& operator = ( const float fScalar);
        bool operator == ( const Vector2& rkVector ) const;
        bool operator != ( const Vector2& rkVector ) const;

        Vector2 operator + ( const Vector2& rkVector ) const;
        Vector2 operator - ( const Vector2& rkVector ) const;
        Vector2 operator * ( const float fScalar ) const;
        Vector2 operator * ( const Vector2& rhs) const;
        Vector2 operator / ( const float fScalar ) const;
        Vector2 operator / ( const Vector2& rhs) const;
        const Vector2& operator + () const;
        Vector2 operator - () const;

        friend Vector2 (::operator *) ( const float fScalar, const Vector2& rkVector );
        friend Vector2 (::operator /) ( const float fScalar, const Vector2& rkVector );
        friend Vector2 (::operator +) (const Vector2& lhs, const float rhs);
        friend Vector2 (::operator +) (const float lhs, const Vector2& rhs);
        friend Vector2 (::operator -) (const Vector2& lhs, const float rhs);
        friend Vector2 (::operator -) (const float lhs, const Vector2& rhs);

        Vector2& operator += ( const Vector2& rkVector );
        Vector2& operator += ( const float fScaler );
        Vector2& operator -= ( const Vector2& rkVector );
        Vector2& operator -= ( const float fScaler );
        Vector2& operator *= ( const float fScalar );
        Vector2& operator *= ( const Vector2& rkVector );
        Vector2& operator /= ( const float fScalar );
        Vector2& operator /= ( const Vector2& rkVector );

        bool operator < ( const Vector2& rhs ) const;
        bool operator > ( const Vector2& rhs ) const;

        bool isValid() const;

        void swap(Vector2& other);
        float length () const;
        float squaredLength () const;
        float distance(const Vector2& rhs) const;
        float squaredDistance(const Vector2& rhs) const;
        float dotProduct(const Vector2& vec) const;

        Vector2& normalize();
        Vector2 normalized() const;
        float normalizeL();

        Vector2 midPoint( const Vector2& vec ) const;

        void makeFloor( const Vector2& cmp );
        void makeCeil( const Vector2& cmp );

        Vector2 perpendicular(void) const;
        float crossProduct( const Vector2& rkVector ) const;

        Vector2 normalizedCopy(void) const;

        Vector2 reflect(const Vector2& normal) const;

    };

}

#endif
