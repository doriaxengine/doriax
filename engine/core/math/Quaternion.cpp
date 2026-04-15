#include "Quaternion.h"

#include "util/Angle.h"
#include <algorithm>
#include <assert.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>
#include <cstdlib>
#include <float.h>
#include "Log.h"

using namespace doriax;

const Quaternion Quaternion::IDENTITY( 1, 0, 0, 0 );

Quaternion::Quaternion()
    : w(1), x(0), y(0), z(0){
}

Quaternion::Quaternion( const Quaternion& rhs ) 
    : w(rhs.w), x(rhs.x), y(rhs.y), z(rhs.z){

}

Quaternion::Quaternion( const float fW, const float fX, const float fY, const float fZ)
    : w( fW), x( fX ), y( fY ), z( fZ ){
}

Quaternion::Quaternion( float* const r )
    : w( r[0] ), x( r[1] ), y( r[2] ), z( r[3] ){
}

Quaternion::Quaternion(const float xAngle, const float yAngle, const float zAngle){
    this->fromEulerAngles(xAngle, yAngle, zAngle, RotationOrder::ZYX);
}

Quaternion::Quaternion(const float xAngle, const float yAngle, const float zAngle, const RotationOrder& order){
    this->fromEulerAngles(xAngle, yAngle, zAngle, order);
}

Quaternion::Quaternion(const Vector3* akAxis){
    this->fromAxes(akAxis);
}

Quaternion::Quaternion(const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis){
    this->fromAxes(xaxis, yaxis, zaxis);
}

Quaternion::Quaternion(const float angle, const Vector3& rkAxis){
    this->fromAngleAxis(angle, rkAxis);
}

Quaternion::Quaternion(const Matrix3& kRot){
    this->fromRotationMatrix(kRot);
}

Quaternion::Quaternion(const Matrix4& kRot){
    this->fromRotationMatrix(kRot);
}

std::string Quaternion::toString() const{
    return "Quaternion(" + std::to_string(w) + ", " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")"; 
}

float Quaternion::operator [] ( const size_t i ) const{
    assert( i < 4 );

    return *(&w+i);
}

float& Quaternion::operator [] ( const size_t i ){
    assert( i < 4 );

    return *(&w+i);
}


float* Quaternion::ptr(){
    return &w;
}


const float* Quaternion::ptr() const{
    return &w;
}

Quaternion& Quaternion::operator = ( const Quaternion& rkQ ){
    w = rkQ.w;
    x = rkQ.x;
    y = rkQ.y;
    z = rkQ.z;

    return *this;
}

bool Quaternion::operator == ( const Quaternion& rhs ) const{
    return ( w == rhs.w &&
            x == rhs.x &&
            y == rhs.y &&
            z == rhs.z );
}

bool Quaternion::operator != ( const Quaternion& rhs ) const{
    return ( w != rhs.w ||
            x != rhs.x ||
            y != rhs.y ||
            z != rhs.z );
}


Quaternion Quaternion::operator + ( const Quaternion& rhs ) const{
    return Quaternion(
                      w + rhs.w,
                      x + rhs.x,
                      y + rhs.y,
                      z + rhs.z);
}

Quaternion Quaternion::operator - ( const Quaternion& rhs ) const{
    return Quaternion(
                      w - rhs.w,
                      x - rhs.x,
                      y - rhs.y,
                      z - rhs.z);
}


Quaternion Quaternion::operator * ( float fScalar ) const{
    return Quaternion(w * fScalar, x * fScalar, y * fScalar, z * fScalar);
}

Quaternion operator * (float fScalar, const Quaternion& rkQ){
    return Quaternion(fScalar * rkQ.w, fScalar * rkQ.x, fScalar * rkQ.y, fScalar * rkQ.z);
}


Quaternion Quaternion::operator * ( const Quaternion& rhs) const{
    return Quaternion
    (
     w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
     w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
     w * rhs.y + y * rhs.w + z * rhs.x - x * rhs.z,
     w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x
     );
}

Vector3 Quaternion::operator* (const Vector3& v) const{
    // nVidia SDK implementation
    Vector3 uv, uuv;
    Vector3 qvec(x, y, z);
    uv = qvec.crossProduct(v);
    uuv = qvec.crossProduct(uv);
    uv *= (2.0f * w);
    uuv *= 2.0f;

    return v + uv + uuv;

}
const Quaternion& Quaternion::operator + () const{
    return *this;
}

Quaternion Quaternion::operator - () const{
    return Quaternion(-w, -x, -y, -z);
}

void Quaternion::swap(Quaternion& other){
    std::swap(w, other.w);
    std::swap(x, other.x);
    std::swap(y, other.y);
    std::swap(z, other.z);
}

void Quaternion::fromEulerAngles(const float xAngle, const float yAngle, const float zAngle, const RotationOrder& order){
    Quaternion qx, qy, qz;

    qx.fromAngleAxis(xAngle, Vector3(1,0,0));
    qy.fromAngleAxis(yAngle, Vector3(0,1,0));
    qz.fromAngleAxis(zAngle, Vector3(0,0,1));

    switch (order) {
        case RotationOrder::XYZ:
            *this = qx * (qy * qz);
            break;
        case RotationOrder::XZY:
            *this = qx * (qz * qy);
            break;
        case RotationOrder::YXZ:
            *this = qy * (qx * qz);
            break;
        case RotationOrder::YZX:
            *this = qy * (qz * qx);
            break;
        case RotationOrder::ZXY:
            *this = qz * (qx * qy);
            break;
        case RotationOrder::ZYX:
            *this = qz * (qy * qx);
            break;
    }
}

void Quaternion::fromAxes (const Vector3* akAxis){
    Matrix4 kRot;

    for (int iCol = 0; iCol < 3; iCol++)
    {
        kRot.set(iCol, 0, akAxis[iCol].x);
        kRot.set(iCol, 1, akAxis[iCol].y);
        kRot.set(iCol, 2, akAxis[iCol].z);
        kRot.set(iCol, 3, 0.0f);
    }

    kRot.set(3,0, 0.0f);
    kRot.set(3,1, 0.0f);
    kRot.set(3,2, 0.0f);
    kRot.set(3,3, 1.0f);

    fromRotationMatrix(kRot);
}

void Quaternion::fromAxes (const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis){
    Matrix4 kRot;

    kRot.set(0,0, xaxis.x);
    kRot.set(0,1, xaxis.y);
    kRot.set(0,2, xaxis.z);
    kRot.set(0,3, 0.0f);

    kRot.set(1,0, yaxis.x);
    kRot.set(1,1, yaxis.y);
    kRot.set(1,2, yaxis.z);
    kRot.set(1,3, 0.0f);

    kRot.set(2,0, zaxis.x);
    kRot.set(2,1, zaxis.y);
    kRot.set(2,2, zaxis.z);
    kRot.set(2,3, 0.0f);

    kRot.set(3,0, 0.0f);
    kRot.set(3,1, 0.0f);
    kRot.set(3,2, 0.0f);
    kRot.set(3,3, 1.0f);

    fromRotationMatrix(kRot);

}

Quaternion& Quaternion::fromRotationMatrix (const Matrix3& kRot){
    float trace = kRot[0][0] + kRot[1][1] + kRot[2][2];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        w = 0.25f / s;
        x = (kRot[1][2] - kRot[2][1]) * s;
        y = (kRot[2][0] - kRot[0][2]) * s;
        z = (kRot[0][1] - kRot[1][0]) * s;
    } else {
        if (kRot[0][0] > kRot[1][1] && kRot[0][0] > kRot[2][2]) {
            float s = 2.0f * sqrtf(1.0f + kRot[0][0] - kRot[1][1] - kRot[2][2]);
            w = (kRot[1][2] - kRot[2][1]) / s;
            x = 0.25f * s;
            y = (kRot[1][0] + kRot[0][1]) / s;
            z = (kRot[2][0] + kRot[0][2]) / s;
        } else if (kRot[1][1] > kRot[2][2]) {
            float s = 2.0f * sqrtf(1.0f + kRot[1][1] - kRot[0][0] - kRot[2][2]);
            w = (kRot[2][0] - kRot[0][2]) / s;
            x = (kRot[1][0] + kRot[0][1]) / s;
            y = 0.25f * s;
            z = (kRot[2][1] + kRot[1][2]) / s;
        } else {
            float s = 2.0f * sqrtf(1.0f + kRot[2][2] - kRot[0][0] - kRot[1][1]);
            w = (kRot[0][1] - kRot[1][0]) / s;
            x = (kRot[2][0] + kRot[0][2]) / s;
            y = (kRot[2][1] + kRot[1][2]) / s;
            z = 0.25f * s;
        }
    }

    return *this;
}

Quaternion& Quaternion::fromRotationMatrix (const Matrix4& kRot){
    return fromRotationMatrix(kRot.linear());
}

Matrix4 Quaternion::getRotationMatrix() const{

    float xx      = x * x;
    float xy      = x * y;
    float xz      = x * z;
    float xw      = x * w;

    float yy      = y * y;
    float yz      = y * z;
    float yw      = y * w;

    float zz      = z * z;
    float zw      = z * w;

    Matrix4 mat;

    mat[0][0] = 1 - 2 * ( yy + zz );
    mat[0][1] =     2 * ( xy + zw );
    mat[0][2] =     2 * ( xz - yw );
    mat[0][3] = 0.0f;

    mat[1][0] =     2 * ( xy - zw );
    mat[1][1] = 1 - 2 * ( xx + zz );
    mat[1][2] =     2 * ( yz + xw );
    mat[1][3] = 0.0f;

    mat[2][0] =     2 * ( xz + yw );
    mat[2][1] =     2 * ( yz - xw );
    mat[2][2] = 1 - 2 * ( xx + yy );
    mat[2][3] = 0.0f;

    mat[3][0] = 0.0f;
    mat[3][1] = 0.0f;
    mat[3][2] = 0.0f;
    mat[3][3] = 1.0f;

    return mat;
}

void Quaternion::fromAngle (const float angle) {

    fromAngleAxis(angle, Vector3(0, 0, 1));
}

void Quaternion::fromAngleAxis (const float angle, const Vector3& rkAxis){

    float defAngle = Angle::defaultToRad(angle);

    float fHalfAngle = 0.5*defAngle;
    float fSin = sin(fHalfAngle);
    w = cos(fHalfAngle);
    x = fSin*rkAxis.x;
    y = fSin*rkAxis.y;
    z = fSin*rkAxis.z;
}

Vector3 Quaternion::getEulerAngles(const RotationOrder& order) const {
    Matrix4 rotMatrix = getRotationMatrix();

    float x, y, z;

    switch (order) {
        case RotationOrder::XYZ: {
            y = asin(std::clamp(rotMatrix[2][0], -1.0f, 1.0f));
            if (y < M_PI_2){
                if (y > -M_PI_2){
                    x = atan2(-rotMatrix[2][1], rotMatrix[2][2]);
                    z = atan2(-rotMatrix[1][0], rotMatrix[0][0]);
                }else{ // Gimbal lock
                    float mY = atan2(rotMatrix[0][1], rotMatrix[1][1]);
                    z = 0.0f; // any angle
                    x = z - mY;
                }

            }else{ // Gimbal lock
                float pY = atan2(rotMatrix[0][1], rotMatrix[1][1]);
                z = 0.0f; // any angle
                x = pY - z;
            }
            break;
        }
        case RotationOrder::XZY: {
            z = asin(std::clamp(-rotMatrix[1][0], -1.0f, 1.0f));
            if (z < M_PI_2){
                if (z > -M_PI_2){
                    x = atan2(rotMatrix[1][2], rotMatrix[1][1]);
                    y = atan2(rotMatrix[2][0], rotMatrix[0][0]);
                }else{ // Gimbal lock
                    float mY = atan2(-rotMatrix[0][2], rotMatrix[2][2]);
                    y = 0.0f; // any angle
                    x = y - mY;
                }
            }else{ // Gimbal lock
                float pY = atan2(-rotMatrix[0][2], rotMatrix[2][2]);
                y = 0.0f; // any angle
                x = pY - y;
            }
            break;
        }
        case RotationOrder::YXZ: {
            x = asin(std::clamp(-rotMatrix[2][1], -1.0f, 1.0f));
            if (x < M_PI_2){
                if (x > -M_PI_2){
                    y = atan2(rotMatrix[2][0], rotMatrix[2][2]);
                    z = atan2(rotMatrix[0][1], rotMatrix[1][1]);
                }else{ // Gimbal lock
                    float mY = atan2(-rotMatrix[1][0], rotMatrix[0][0]);
                    z = 0.0f; // any angle
                    y = z - mY;
                }
            }else{ // Gimbal lock
                float pY = atan2(-rotMatrix[1][0], rotMatrix[0][0]);
                z = 0.0f; // any angle
                y = pY - z;
            }
            break;
        }
        case RotationOrder::YZX: {
            z = asin(std::clamp(rotMatrix[0][1], -1.0f, 1.0f));
            if (z < M_PI_2){
                if (z > -M_PI_2){
                    y = atan2(-rotMatrix[0][2], rotMatrix[0][0]);
                    x = atan2(-rotMatrix[2][1], rotMatrix[1][1]);
                }else{ // Gimbal lock
                    float mY = atan2(rotMatrix[1][2], rotMatrix[2][2]);
                    x = 0.0f; // any angle
                    y = x - mY;
                }
            }else{ // Gimbal lock
                float pY = atan2(rotMatrix[1][2], rotMatrix[2][2]);
                x = 0.0f; // any angle
                y = pY - x;
            }
            break;
        }
        case RotationOrder::ZXY: {
            x = asin(std::clamp(rotMatrix[1][2], -1.0f, 1.0f));
            if (x < M_PI_2){
                if (x > -M_PI_2){
                    z = atan2(-rotMatrix[1][0], rotMatrix[1][1]);
                    y = atan2(-rotMatrix[0][2], rotMatrix[2][2]);
                }else{ // Gimbal lock
                    float mY = atan2(rotMatrix[2][0], rotMatrix[0][0]);
                    y = 0.0f; // any angle
                    z = y - mY;
                }
            }else{ // Gimbal lock
                float pY = atan2(rotMatrix[2][0], rotMatrix[0][0]);
                y = 0.0f; // any angle
                z = pY - y;
            }
            break;
        }
        case RotationOrder::ZYX: {
            y = asin(std::clamp(-rotMatrix[0][2], -1.0f, 1.0f));
            if (y < M_PI_2){
                if (y > -M_PI_2){
                    z = atan2(rotMatrix[0][1], rotMatrix[0][0]);
                    x = atan2(rotMatrix[1][2], rotMatrix[2][2]);
                }else{ // Gimbal lock
                    float mY = atan2(-rotMatrix[1][0], rotMatrix[2][0]);
                    x = 0.0f; // any angle
                    z = x - mY;
                }
            }else{ // Gimbal lock
                float pY = atan2(-rotMatrix[1][0], rotMatrix[2][0]);
                x = 0.0f; // any angle
                z = pY - x;
            }
            break;
        }
        default:
            // Handle unsupported rotation orders or throw an exception
            x = y = z = 0.0f;
            break;
    }

    return Vector3(
        Angle::radToDefault(x),
        Angle::radToDefault(y),
        Angle::radToDefault(z)
    );
}

Vector3 Quaternion::xAxis(void) const{
    float fTy  = 2.0f*y;
    float fTz  = 2.0f*z;
    float fTwy = fTy*w;
    float fTwz = fTz*w;
    float fTxy = fTy*x;
    float fTxz = fTz*x;
    float fTyy = fTy*y;
    float fTzz = fTz*z;

    return Vector3(1.0f-(fTyy+fTzz), fTxy+fTwz, fTxz-fTwy);
}

Vector3 Quaternion::yAxis(void) const{
    float fTx  = 2.0f*x;
    float fTy  = 2.0f*y;
    float fTz  = 2.0f*z;
    float fTwx = fTx*w;
    float fTwz = fTz*w;
    float fTxx = fTx*x;
    float fTxy = fTy*x;
    float fTyz = fTz*y;
    float fTzz = fTz*z;

    return Vector3(fTxy-fTwz, 1.0f-(fTxx+fTzz), fTyz+fTwx);
}

Vector3 Quaternion::zAxis(void) const{
    float fTx  = 2.0f*x;
    float fTy  = 2.0f*y;
    float fTz  = 2.0f*z;
    float fTwx = fTx*w;
    float fTwy = fTy*w;
    float fTxx = fTx*x;
    float fTxz = fTz*x;
    float fTyy = fTy*y;
    float fTyz = fTz*y;

    return Vector3(fTxz+fTwy, fTyz-fTwx, 1.0f-(fTxx+fTyy));
}

float Quaternion::dot (const Quaternion& rkQ) const{
    return w*rkQ.w+x*rkQ.x+y*rkQ.y+z*rkQ.z;
}

float Quaternion::norm () const{
    return w*w+x*x+y*y+z*z;
}

Quaternion Quaternion::inverse () const{
    float fNorm = w*w+x*x+y*y+z*z;
    if ( fNorm > 0.0 )
    {
        float fInvNorm = 1.0f/fNorm;
        return Quaternion(w*fInvNorm,-x*fInvNorm,-y*fInvNorm,-z*fInvNorm);
    }
    else
    {
        return NULL;
    }
}

Quaternion Quaternion::unitInverse () const{
    return Quaternion(w,-x,-y,-z);
}

Quaternion Quaternion::exp () const{

    float fAngle = sqrt(x*x+y*y+z*z);
    float fSin = sin(fAngle);

    Quaternion kResult;
    kResult.w = cos(fAngle);

    if ( fabs(fSin) >= FLT_EPSILON )
    {
        float fCoeff = fSin/(fAngle);
        kResult.x = fCoeff*x;
        kResult.y = fCoeff*y;
        kResult.z = fCoeff*z;
    }
    else
    {
        kResult.x = x;
        kResult.y = y;
        kResult.z = z;
    }

    return kResult;
}

Quaternion Quaternion::log () const{

    Quaternion kResult;
    kResult.w = 0.0;

    if ( fabs(w) < 1.0 )
    {
        float fAngle = acos(w);
        float fSin = sin(fAngle);
        if ( fabs(fSin) >= FLT_EPSILON )
        {
            float fCoeff = fAngle/fSin;
            kResult.x = fCoeff*x;
            kResult.y = fCoeff*y;
            kResult.z = fCoeff*z;
            return kResult;
        }
    }

    kResult.x = x;
    kResult.y = y;
    kResult.z = z;

    return kResult;
}

bool Quaternion::equals(const Quaternion& rhs) const{
    float matching = dot(rhs);

    return ( fabs(matching-1.0) < 0.001 );
}

Quaternion Quaternion::slerp(float t, const Quaternion& q1, const Quaternion& q2){
    return slerp(t, q1, q2, true);
}

Quaternion Quaternion::slerp(float t, const Quaternion& q1, const Quaternion& q2, bool shortestPath){
    float w1, x1, y1, z1, w2, x2, y2, z2, w3, x3, y3, z3;

    float theta, mult1, mult2;

    w1 = q1.w; x1 = q1.x; y1 = q1.y; z1 = q1.z;
    w2 = q2.w; x2 = q2.x; y2 = q2.y; z2 = q2.z;

    float dot = w1*w2 + x1*x2 + y1*y2 + z1*z2;

    if (dot < 0 && shortestPath) {
        w2 = -w2; x2 = -x2; y2 = -y2; z2 = -z2; dot = -dot;
    }

    theta = acos(dot);

    if (theta > FLT_EPSILON) {
        mult1 = sin( (1-t)*theta ) / sin( theta );
        mult2 = sin( t*theta ) / sin( theta );
    } else {
        mult1 = 1 - t;
        mult2 = t;
    }

    w3 =  mult1*w1 + mult2*w2;
    x3 =  mult1*x1 + mult2*x2;
    y3 =  mult1*y1 + mult2*y2;
    z3 =  mult1*z1 + mult2*z2;

    return Quaternion(w3, x3, y3, z3);
}

Quaternion Quaternion::slerpExtraSpins (float fT, const Quaternion& rkP, const Quaternion& rkQ, int iExtraSpins){
    float fCos = rkP.dot(rkQ);
    float fAngle = acos(fCos);

    if ( fabs(fAngle) < FLT_EPSILON )
        return rkP;

    float fSin = sin(fAngle);
    float fPhase ( M_PI*iExtraSpins*fT );
    float fInvSin = 1.0f/fSin;
    float fCoeff0 = sin((1.0f-fT)*fAngle - fPhase)*fInvSin;
    float fCoeff1 = sin(fT*fAngle + fPhase)*fInvSin;
    return fCoeff0*rkP + fCoeff1*rkQ;
}

Quaternion Quaternion::nlerp(float fT, const Quaternion& rkP, const Quaternion& rkQ){
    return nlerp(fT, rkP, rkQ, false);
}

Quaternion Quaternion::nlerp(float fT, const Quaternion& rkP, const Quaternion& rkQ, bool shortestPath){
    Quaternion result;
    float fCos = rkP.dot(rkQ);
    if (fCos < 0.0f && shortestPath)
    {
        result = rkP + fT * ((-rkQ) - rkP);
    }
    else
    {
        result = rkP + fT * (rkQ - rkP);
    }
    result.normalize();
    return result;
}

Quaternion Quaternion::squad (float fT, const Quaternion& rkP, const Quaternion& rkA, const Quaternion& rkB, const Quaternion& rkQ){
    float fSlerpT = 2.0f*fT*(1.0f-fT);
    Quaternion kSlerpP = slerp(fT, rkP, rkQ);
    Quaternion kSlerpQ = slerp(fT, rkA, rkB);
    return slerp(fSlerpT, kSlerpP ,kSlerpQ);
}

Quaternion& Quaternion::normalize(void){
    float len = norm();
    float factor = 1.0f / sqrt(len);
    *this = *this * factor;
    return *this;
}

Quaternion Quaternion::normalized() const {
    Quaternion result = *this;
    result.normalize();
    return result;
}

float Quaternion::normalizeL(void){
    float len = norm();
    float factor = 1.0f / sqrt(len);
    *this = *this * factor;
    return len;
}

float Quaternion::getRoll() const{
    float fTy = 2.0f * y;
    float fTz = 2.0f * z;
    float fTwz = fTz * w;
    float fTxy = fTy * x;
    float fTyy = fTy * y;
    float fTzz = fTz * z;

    return Angle::radToDefault(atan2(fTxy + fTwz, 1.0f - (fTyy + fTzz)));
}

float Quaternion::getPitch() const{
    float fTx = 2.0f * x;
    float fTz = 2.0f * z;
    float fTwx = fTx * w;
    float fTxx = fTx * x;
    float fTyz = fTz * y;
    float fTzz = fTz * z;

    return Angle::radToDefault(atan2(fTyz + fTwx, 1.0f - (fTxx + fTzz)));
}

float Quaternion::getYaw() const{
    float fTx = 2.0f * x;
    float fTy = 2.0f * y;
    float fTz = 2.0f * z;
    float fTwy = fTy * w;
    float fTxx = fTx * x;
    float fTxz = fTz * x;
    float fTyy = fTy * y;

    return Angle::radToDefault(atan2(fTxz + fTwy, 1.0f - (fTxx + fTyy)));
}
