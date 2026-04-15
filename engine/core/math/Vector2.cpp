#include "Vector2.h"

#include "Vector3.h"
#include "Vector4.h"

#include <cmath>

using namespace doriax;

const Vector2 Vector2::ZERO( 0, 0);

const Vector2 Vector2::UNIT_X( 1, 0);
const Vector2 Vector2::UNIT_Y( 0, 1);
const Vector2 Vector2::NEGATIVE_UNIT_X( -1,  0);
const Vector2 Vector2::NEGATIVE_UNIT_Y(  0, -1);
const Vector2 Vector2::UNIT_SCALE(1, 1);

Vector2::Vector2() : x(0), y(0){ }

Vector2::Vector2(const float fX, const float fY ): x( fX ), y( fY ){ }

Vector2::Vector2( const Vector2& v ): x( v.x ), y( v.y ){ }

Vector2::Vector2( const float scaler ): x( scaler), y( scaler ){ }

Vector2::Vector2( const float afCoordinate[2] ): x( afCoordinate[0] ),y( afCoordinate[1] ){ }

Vector2::Vector2( const int afCoordinate[2] ){
    x = (float)afCoordinate[0];
    y = (float)afCoordinate[1];
}

Vector2::Vector2( float* const r ): x( r[0] ), y( r[1] ){ }

Vector2::Vector2( const Vector3& vec3 ){
    x = vec3.x;
    y = vec3.y;
}

Vector2::Vector2( const Vector4& vec4 ){
    x = vec4.x;
    y = vec4.y;
}

std::string Vector2::toString() const{
    return "Vector2(" + std::to_string(x) + ", " + std::to_string(y) + ")"; 
}

float Vector2::operator [] ( const size_t i ) const{
    assert( i < 2 );
    
    return *(&x+i);
}

float& Vector2::operator [] ( const size_t i ){
    assert( i < 2 );
    
    return *(&x+i);
}

Vector2& Vector2::operator = ( const Vector2& rkVector ){
    x = rkVector.x;
    y = rkVector.y;
    
    return *this;
}

Vector2& Vector2::operator = ( const float fScalar){
    x = fScalar;
    y = fScalar;
    
    return *this;
}

bool Vector2::operator == ( const Vector2& rkVector ) const{
    return ( x == rkVector.x && y == rkVector.y );
}

bool Vector2::operator != ( const Vector2& rkVector ) const{
    return ( x != rkVector.x || y != rkVector.y  );
}

Vector2 Vector2::operator + ( const Vector2& rkVector ) const{
    return Vector2(
                   x + rkVector.x,
                   y + rkVector.y);
}

Vector2 Vector2::operator - ( const Vector2& rkVector ) const{
    return Vector2(
                   x - rkVector.x,
                   y - rkVector.y);
}

Vector2 Vector2::operator * ( const float fScalar ) const{
    return Vector2(
                   x * fScalar,
                   y * fScalar);
}

Vector2 Vector2::operator * ( const Vector2& rhs) const{
    return Vector2(
                   x * rhs.x,
                   y * rhs.y);
}

Vector2 Vector2::operator / ( const float fScalar ) const{
    assert( fScalar != 0.0 );
    
    float fInv = 1.0f / fScalar;
    
    return Vector2(
                   x * fInv,
                   y * fInv);
}

Vector2 Vector2::operator / ( const Vector2& rhs) const{
    return Vector2(
                   x / rhs.x,
                   y / rhs.y);
}

const Vector2& Vector2::operator + () const{
    return *this;
}

Vector2 Vector2::operator - () const{
    return Vector2(-x, -y);
}


Vector2 operator * ( const float fScalar, const Vector2& rkVector ){
    return doriax::Vector2(
                   fScalar * rkVector.x,
                   fScalar * rkVector.y);
}


Vector2 operator / ( const float fScalar, const Vector2& rkVector ){
    return Vector2(
                   fScalar / rkVector.x,
                   fScalar / rkVector.y);
}

Vector2 operator + (const Vector2& lhs, const float rhs){
    return Vector2(
                   lhs.x + rhs,
                   lhs.y + rhs);
}

Vector2 operator + (const float lhs, const Vector2& rhs){
    return Vector2(
                   lhs + rhs.x,
                   lhs + rhs.y);
}

Vector2 operator - (const Vector2& lhs, const float rhs){
    return Vector2(
                   lhs.x - rhs,
                   lhs.y - rhs);
}

Vector2 operator - (const float lhs, const Vector2& rhs){
    return Vector2(
                   lhs - rhs.x,
                   lhs - rhs.y);
}

Vector2& Vector2::operator += ( const Vector2& rkVector ){
    x += rkVector.x;
    y += rkVector.y;
    
    return *this;
}

Vector2& Vector2::operator += ( const float fScaler ){
    x += fScaler;
    y += fScaler;
    
    return *this;
}

Vector2& Vector2::operator -= ( const Vector2& rkVector ){
    x -= rkVector.x;
    y -= rkVector.y;
    
    return *this;
}

Vector2& Vector2::operator -= ( const float fScaler ){
    x -= fScaler;
    y -= fScaler;
    
    return *this;
}

Vector2& Vector2::operator *= ( const float fScalar ){
    x *= fScalar;
    y *= fScalar;
    
    return *this;
}

Vector2& Vector2::operator *= ( const Vector2& rkVector ){
    x *= rkVector.x;
    y *= rkVector.y;
    
    return *this;
}

bool Vector2::operator < ( const Vector2& rhs ) const{
    if( x < rhs.x && y < rhs.y )
        return true;
    return false;
}


bool Vector2::operator > ( const Vector2& rhs ) const{
    if( x > rhs.x && y > rhs.y )
        return true;
    return false;
}

bool Vector2::isValid() const {
    return std::isfinite(x) && std::isfinite(y);
}

void Vector2::swap(Vector2& other){
    std::swap(x, other.x);
    std::swap(y, other.y);
}

float Vector2::length () const{
    return sqrt( x * x + y * y );
}


float Vector2::squaredLength () const{
    return x * x + y * y;
}


float Vector2::distance(const Vector2& rhs) const{
    return (*this - rhs).length();
}


float Vector2::squaredDistance(const Vector2& rhs) const{
    return (*this - rhs).squaredLength();
}


float Vector2::dotProduct(const Vector2& vec) const{
    return x * vec.x + y * vec.y;
}

Vector2& Vector2::normalize(){
    float fLength = sqrt( x * x + y * y);
    
    if ( fLength > float(0.0f) )
    {
        float fInvLength = 1.0f / fLength;
        x *= fInvLength;
        y *= fInvLength;
    }
    
    return *this;
}

Vector2 Vector2::normalized() const {
    Vector2 result = *this;
    result.normalize();
    return result;
}

float Vector2::normalizeL(){
    float fLength = sqrt( x * x + y * y);
    
    if ( fLength > float(0.0f) )
    {
        float fInvLength = 1.0f / fLength;
        x *= fInvLength;
        y *= fInvLength;
    }
    
    return fLength;
}


Vector2 Vector2::midPoint( const Vector2& vec ) const{
    return Vector2(
                   ( x + vec.x ) * 0.5f,
                   ( y + vec.y ) * 0.5f );
}

void Vector2::makeFloor( const Vector2& cmp ){
    if( cmp.x < x ) x = cmp.x;
    if( cmp.y < y ) y = cmp.y;
}

void Vector2::makeCeil( const Vector2& cmp ){
    if( cmp.x > x ) x = cmp.x;
    if( cmp.y > y ) y = cmp.y;
}

Vector2 Vector2::perpendicular(void) const{
    return Vector2 (-y, x);
}

float Vector2::crossProduct( const Vector2& rkVector ) const{
    return x * rkVector.y - y * rkVector.x;
}

Vector2 Vector2::normalizedCopy(void) const{
    Vector2 ret = *this;
    ret.normalize();
    return ret;
}


Vector2 Vector2::reflect(const Vector2& normal) const{
    return Vector2( *this - ( 2 * this->dotProduct(normal) * normal ) );
}


