#include "Vector4.h"

#include "Vector2.h"
#include "Vector3.h"

#include <cmath>

using namespace doriax;

const Vector4 Vector4::ZERO( 0, 0, 0, 0 );
const Vector4 Vector4::UNIT_X( 1, 0, 0, 0 );
const Vector4 Vector4::UNIT_Y( 0, 1, 0, 0 );
const Vector4 Vector4::UNIT_Z( 0, 0, 1, 0 );
const Vector4 Vector4::UNIT_W( 0, 0, 0, 1 );
const Vector4 Vector4::UNIT_SCALE(1, 1, 1, 1);


Vector4::Vector4() : x(0), y(0), z(0), w(0){ }

Vector4::Vector4( const float fX, const float fY, const float fZ, const float fW ): x( fX ), y( fY ), z( fZ ), w( fW){ }

Vector4::Vector4( const Vector4& rhs ): x(rhs.x), y(rhs.y), z(rhs.z), w(rhs.w){ }

Vector4::Vector4( const float afCoordinate[4] ): x( afCoordinate[0] ),y( afCoordinate[1] ),z( afCoordinate[2] ),w( afCoordinate[3] ){ }

Vector4::Vector4( const int afCoordinate[4] ){
    x = (float)afCoordinate[0];
    y = (float)afCoordinate[1];
    z = (float)afCoordinate[2];
    w = (float)afCoordinate[3];
}

Vector4::Vector4( float* const r ): x( r[0] ), y( r[1] ), z( r[2] ), w( r[3] ){ }

Vector4::Vector4( const float scaler ): x( scaler ), y( scaler ), z( scaler ), w( scaler ){ }

Vector4::Vector4( const Vector2& rhs, const float fZ, const float fW ): x(rhs.x), y(rhs.y), z(fZ), w(fW){ }

Vector4::Vector4( const Vector3& rhs, const float fW ): x(rhs.x), y(rhs.y), z(rhs.z), w(fW){ }

std::string Vector4::toString() const{
    return "Vector4(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ", " + std::to_string(w) + ")"; 
}

float Vector4::operator [] ( const size_t i ) const{
    assert( i < 4 );
    
    return *(&x+i);
}

float& Vector4::operator [] ( const size_t i ){
    assert( i < 4 );
    
    return *(&x+i);
}

Vector4& Vector4::operator = ( const Vector4& rkVector ){
    x = rkVector.x;
    y = rkVector.y;
    z = rkVector.z;
    w = rkVector.w;
    
    return *this;
}

Vector4& Vector4::operator = ( const float fScalar){
    x = fScalar;
    y = fScalar;
    z = fScalar;
    w = fScalar;
    return *this;
}

bool Vector4::operator == ( const Vector4& rkVector ) const{
    return ( x == rkVector.x &&
            y == rkVector.y &&
            z == rkVector.z &&
            w == rkVector.w );
}

bool Vector4::operator != ( const Vector4& rkVector ) const{
    return ( x != rkVector.x ||
            y != rkVector.y ||
            z != rkVector.z ||
            w != rkVector.w );
}

Vector4& Vector4::operator = (const Vector3& rhs){
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
}


Vector4 Vector4::operator + ( const Vector4& rkVector ) const{
    return Vector4(
                   x + rkVector.x,
                   y + rkVector.y,
                   z + rkVector.z,
                   w + rkVector.w);
}

Vector4 Vector4::operator - ( const Vector4& rkVector ) const{
    return Vector4(
                   x - rkVector.x,
                   y - rkVector.y,
                   z - rkVector.z,
                   w - rkVector.w);
}

Vector4 Vector4::operator * ( const float fScalar ) const{
    return Vector4(
                   x * fScalar,
                   y * fScalar,
                   z * fScalar,
                   w * fScalar);
}

Vector4 Vector4::operator * ( const Vector4& rhs) const{
    return Vector4(
                   rhs.x * x,
                   rhs.y * y,
                   rhs.z * z,
                   rhs.w * w);
}

Vector4 Vector4::operator / ( const float fScalar ) const{
    assert( fScalar != 0.0 );
    
    float fInv = 1.0f / fScalar;
    
    return Vector4(
                   x * fInv,
                   y * fInv,
                   z * fInv,
                   w * fInv);
}

Vector4 Vector4::operator / ( const Vector4& rhs) const{
    return Vector4(
                   x / rhs.x,
                   y / rhs.y,
                   z / rhs.z,
                   w / rhs.w);
}

const Vector4& Vector4::operator + () const{
    return *this;
}

Vector4 Vector4::operator - () const{
    return Vector4(-x, -y, -z, -w);
}

Vector4 operator * ( const float fScalar, const Vector4& rkVector ){
    return Vector4(
                   fScalar * rkVector.x,
                   fScalar * rkVector.y,
                   fScalar * rkVector.z,
                   fScalar * rkVector.w);
}

Vector4 operator / ( const float fScalar, const Vector4& rkVector ){
    return Vector4(
                   fScalar / rkVector.x,
                   fScalar / rkVector.y,
                   fScalar / rkVector.z,
                   fScalar / rkVector.w);
}

Vector4 operator + (const Vector4& lhs, const float rhs){
    return Vector4(
                   lhs.x + rhs,
                   lhs.y + rhs,
                   lhs.z + rhs,
                   lhs.w + rhs);
}

Vector4 operator + (const float lhs, const Vector4& rhs){
    return Vector4(
                   lhs + rhs.x,
                   lhs + rhs.y,
                   lhs + rhs.z,
                   lhs + rhs.w);
}

Vector4 operator - (const Vector4& lhs, float rhs){
    return Vector4(
                   lhs.x - rhs,
                   lhs.y - rhs,
                   lhs.z - rhs,
                   lhs.w - rhs);
}

Vector4 operator - (const float lhs, const Vector4& rhs){
    return Vector4(
                   lhs - rhs.x,
                   lhs - rhs.y,
                   lhs - rhs.z,
                   lhs - rhs.w);
}

Vector4& Vector4::operator += ( const Vector4& rkVector ){
    x += rkVector.x;
    y += rkVector.y;
    z += rkVector.z;
    w += rkVector.w;
    
    return *this;
}

Vector4& Vector4::operator -= ( const Vector4& rkVector ){
    x -= rkVector.x;
    y -= rkVector.y;
    z -= rkVector.z;
    w -= rkVector.w;
    
    return *this;
}

Vector4& Vector4::operator *= ( const float fScalar ){
    x *= fScalar;
    y *= fScalar;
    z *= fScalar;
    w *= fScalar;
    return *this;
}

Vector4& Vector4::operator += ( const float fScalar ){
    x += fScalar;
    y += fScalar;
    z += fScalar;
    w += fScalar;
    return *this;
}

Vector4& Vector4::operator -= ( const float fScalar ){
    x -= fScalar;
    y -= fScalar;
    z -= fScalar;
    w -= fScalar;
    return *this;
}

Vector4& Vector4::operator *= ( const Vector4& rkVector ){
    x *= rkVector.x;
    y *= rkVector.y;
    z *= rkVector.z;
    w *= rkVector.w;
    
    return *this;
}

Vector4& Vector4::operator /= ( const float fScalar ){
    assert( fScalar != 0.0 );
    
    float fInv = 1.0f / fScalar;
    
    x *= fInv;
    y *= fInv;
    z *= fInv;
    w *= fInv;
    
    return *this;
}

Vector4& Vector4::operator /= ( const Vector4& rkVector ){
    x /= rkVector.x;
    y /= rkVector.y;
    z /= rkVector.z;
    w /= rkVector.w;
    
    return *this;
}

bool Vector4::operator < ( const Vector4& v ) const{
    return ( x < v.x && y < v.y && z < v.z && w < v.w );
}

bool Vector4::operator > ( const Vector4& v ) const{
    return ( x > v.x && y > v.y && z > v.z  && w > v.w );
}

bool Vector4::isValid() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}

void Vector4::swap(Vector4& other){
    std::swap(x, other.x);
    std::swap(y, other.y);
    std::swap(z, other.z);
    std::swap(w, other.w);
}

void Vector4::divideByW(){
    x /= w;
    y /= w;
    z /= w;
}

float Vector4::dotProduct(const Vector4& vec) const{
    return x * vec.x + y * vec.y + z * vec.z + w * vec.w;
}

bool Vector4::isNaN() const{
    return isnan(x) || isnan(y) || isnan(z) || isnan(w);
}


