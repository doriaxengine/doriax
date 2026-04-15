#include "Vector3.h"

#include "Vector2.h"
#include "Vector4.h"

#include <cmath>

using namespace doriax;

const Vector3 Vector3::ZERO( 0, 0, 0 );
const Vector3 Vector3::UNIT_X( 1, 0, 0 );
const Vector3 Vector3::UNIT_Y( 0, 1, 0 );
const Vector3 Vector3::UNIT_Z( 0, 0, 1 );
const Vector3 Vector3::UNIT_SCALE(1, 1, 1);


Vector3::Vector3()
        : x(0), y(0), z(0) {
}

Vector3::Vector3( const float nx, const float ny, const float nz ) : x(nx), y(ny), z(nz) {}

Vector3::Vector3( const Vector3& v ) : x(v.x), y(v.y), z(v.z) {}

Vector3::Vector3( const float v[3] ) : x(v[0]), y(v[1]), z(v[2]) {}

Vector3::Vector3( const int v[3] ): x((float)v[0]), y((float)v[1]), z((float)v[2]) {}

Vector3::Vector3( float* const v ) : x(v[0]), y(v[1]), z(v[2]) {}

Vector3::Vector3( const float scaler ): x( scaler ), y( scaler ), z( scaler ){ }

Vector3::Vector3( const Vector2& vec2, const float nz ) : x(vec2.x), y(vec2.y), z(nz) {}

Vector3::Vector3( const Vector4& vec4 ) : x(vec4.x), y(vec4.y), z(vec4.z) {}

std::string Vector3::toString() const{
    return "Vector3(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")"; 
}

float Vector3::operator [] ( unsigned i ) const{
    return *(&x+i);
}

float& Vector3::operator [] ( unsigned i ){
    return *(&x+i);
}

Vector3& Vector3::operator = ( const Vector3& v ){
    x = v.x;
    y = v.y;
    z = v.z;
    
    return *this;
}

bool Vector3::operator == ( const Vector3& v ) const{
    return ( x == v.x && y == v.y && z == v.z );
}

bool Vector3::operator != ( const Vector3& v ) const{
    return ( x != v.x || y != v.y || z != v.z );
}

Vector3 Vector3::operator + ( const Vector3& v ) const{
    return Vector3(x + v.x, y + v.y, z + v.z);
}

Vector3 Vector3::operator - ( const Vector3& v ) const{
    return Vector3(x - v.x, y - v.y, z - v.z);
}

Vector3 Vector3::operator * ( float f ) const{
    return Vector3(x * f, y * f, z * f);
}

Vector3 Vector3::operator * ( const Vector3& v ) const{
    return Vector3(x * v.x, y * v.y, z * v.z);
}

Vector3 Vector3::operator / ( float f ) const{
    f = 1.0f / f;
    return Vector3(x * f, y * f, z * f);
}

Vector3 Vector3::operator - () const{
    return Vector3( -x, -y, -z);
}

Vector3 operator * ( float f, const Vector3& v ){
    return Vector3(f * v.x, f * v.y, f * v.z);
}


Vector3& Vector3::operator += ( const Vector3& v ){
    x += v.x;
    y += v.y;
    z += v.z;
    
    return *this;
}

Vector3& Vector3::operator -= ( const Vector3& v ){
    x -= v.x;
    y -= v.y;
    z -= v.z;
    
    return *this;
}

Vector3& Vector3::operator *= ( float f ){
    x *= f;
    y *= f;
    z *= f;
    return *this;
}

Vector3& Vector3::operator /= ( float f ){
    f = 1.0f / f;
    
    x *= f;
    y *= f;
    z *= f;
    
    return *this;
}

bool Vector3::operator < ( const Vector3& v ) const{
    return ( x < v.x && y < v.y && z < v.z );
}

bool Vector3::operator > ( const Vector3& v ) const{
    return ( x > v.x && y > v.y && z > v.z );
}

bool Vector3::isValid() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

float Vector3::length () const{
    return (float)sqrt( x * x + y * y + z * z );
}

float Vector3::squaredLength () const{
    return x * x + y * y + z * z;
}

float Vector3::dotProduct(const Vector3& v) const{
    return x * v.x + y * v.y + z * v.z;
}

float Vector3::absDotProduct(const Vector3& v) const{
    return fabsf(x * v.x) + fabsf(y * v.y) + fabsf(z * v.z);
}

float Vector3::distance(const Vector3& rhs) const{
    return (*this - rhs).length();
}


float Vector3::squaredDistance(const Vector3& rhs) const{
    return (*this - rhs).squaredLength();
}

Vector3& Vector3::normalize(){
    float length = (float)sqrt( x * x + y * y + z * z );

    if (length > 0.0){
        float invLength = 1.0f / length;
        x *= invLength;
        y *= invLength;
        z *= invLength;
    }
    
    return *this;
}

Vector3 Vector3::normalized() const {
    Vector3 result = *this;
    result.normalize();
    return result;
}

float Vector3::normalizeL(){
    float length = (float)sqrt( x * x + y * y + z * z );

    if (length > 0.0){
        float invLength = 1.0f / length;
        x *= invLength;
        y *= invLength;
        z *= invLength;
    }

    return length;
}

Vector3 Vector3::crossProduct( const Vector3& v ) const{
    return Vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
}

Vector3 Vector3::midPoint( const Vector3& v ) const{
    return Vector3( ( x + v.x ) * 0.5f, ( y + v.y ) * 0.5f, ( z + v.z ) * 0.5f );
}

void Vector3::makeFloor( const Vector3& v ){
    if( v.x < x ) x = v.x;
    if( v.y < y ) y = v.y;
    if( v.z < z ) z = v.z;
}

void Vector3::makeCeil( const Vector3& v ){
    if( v.x > x ) x = v.x;
    if( v.y > y ) y = v.y;
    if( v.z > z ) z = v.z;
}

Vector3 Vector3::perpendicular() const {
    static const float fSquareZero = 1e-06f * 1e-06f;

    Vector3 perp = this->crossProduct(Vector3::UNIT_X);
    if (perp.squaredLength() < fSquareZero) {
        perp = this->crossProduct(Vector3::UNIT_Y);
    }
    if (perp.squaredLength() < fSquareZero) {
        perp = this->crossProduct(Vector3::UNIT_Z);
    }
    perp.normalize();

    return perp;
}

Vector3 Vector3::reflect(const Vector3& normal) const{
    return Vector3( *this - ( 2 * this->dotProduct(normal) * normal ) );
}