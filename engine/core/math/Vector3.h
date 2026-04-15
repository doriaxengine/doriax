#ifndef vector3_h
#define vector3_h

#include "Export.h"
#include <math.h>
#include <string>

namespace doriax { class Vector3; }
doriax::Vector3 operator * ( float f, const doriax::Vector3& v );


namespace doriax {

    class Vector2;
    class Vector4;

    class DORIAX_API Vector3
    {
    public:
        float x, y, z;

        static const Vector3 ZERO;
        static const Vector3 UNIT_X;
        static const Vector3 UNIT_Y;
        static const Vector3 UNIT_Z;
        static const Vector3 UNIT_SCALE;

        Vector3();
        Vector3( const float nx, const float ny, const float nz );
        Vector3( const Vector3& v );
        explicit Vector3( const float v[3] );
        explicit Vector3( const int v[3] );
        explicit Vector3( float* const v );
        explicit Vector3( const float scaler );
        explicit Vector3( const Vector2& vec2, const float nz );
        explicit Vector3( const Vector4& vec4 );

        std::string toString() const;

        float operator [] ( unsigned i ) const;
        float& operator [] ( unsigned i );
        Vector3& operator = ( const Vector3& v );
        bool operator == ( const Vector3& v ) const;
        bool operator != ( const Vector3& v ) const;

        Vector3 operator + ( const Vector3& v ) const;
        Vector3 operator - ( const Vector3& v ) const;
        Vector3 operator * ( float f ) const;
        Vector3 operator * ( const Vector3& v) const;
        Vector3 operator / ( float f ) const;
        Vector3 operator - () const;
        friend Vector3 (::operator *) ( float f, const Vector3& v );

        Vector3& operator += ( const Vector3& v );
        Vector3& operator -= ( const Vector3& v );
        Vector3& operator *= ( float f );
        Vector3& operator /= ( float f );

        bool operator < ( const Vector3& v ) const;
        bool operator > ( const Vector3& v ) const;

        bool isValid() const;

        float length () const;
        float squaredLength () const;
        float dotProduct(const Vector3& v) const;
        float absDotProduct(const Vector3& v) const;
        float distance(const Vector3& rhs) const;
        float squaredDistance(const Vector3& rhs) const;

        Vector3& normalize();
        Vector3 normalized() const;
        float normalizeL();

        Vector3 crossProduct( const Vector3& v ) const;
        Vector3 midPoint( const Vector3& v ) const;

        void makeFloor( const Vector3& v );
        void makeCeil( const Vector3& v );

        Vector3 perpendicular() const;

        Vector3 reflect(const Vector3& normal) const;

    };
    
}

#endif
