#include "Matrix3.h"

#include "util/Angle.h"
#include <string.h>
#include <algorithm>
#include <assert.h>
#include <cmath>

using namespace doriax;

Matrix3::Matrix3() {
    identity();
}

Matrix3::Matrix3(const Matrix3 &matrix) {
    (*this) = matrix;
}

Matrix3::Matrix3 (float fEntry00, float fEntry10, float fEntry20,
                  float fEntry01, float fEntry11, float fEntry21,
                  float fEntry02, float fEntry12, float fEntry22){
    matrix[0][0] = fEntry00;
    matrix[0][1] = fEntry01;
    matrix[0][2] = fEntry02;
    matrix[1][0] = fEntry10;
    matrix[1][1] = fEntry11;
    matrix[1][2] = fEntry12;
    matrix[2][0] = fEntry20;
    matrix[2][1] = fEntry21;
    matrix[2][2] = fEntry22;
}

Matrix3::Matrix3(const float **matrix) {
    std::copy(&matrix[0][0], &matrix[0][0]+16, &this->matrix[0][0]);
}

std::string Matrix3::toString() const{
    return "Matrix3("+
        std::to_string(matrix[0][0]) + ", " + std::to_string(matrix[0][1]) + ", " + std::to_string(matrix[0][2]) + ", " +
        std::to_string(matrix[1][0]) + ", " + std::to_string(matrix[1][1]) + ", " + std::to_string(matrix[1][2]) + ", " +
        std::to_string(matrix[2][0]) + ", " + std::to_string(matrix[2][1]) + ", " + std::to_string(matrix[2][2]) +
        ")"; 
}

Matrix3& Matrix3::operator=(const Matrix3 &m) {
    memcpy(this->matrix, m.matrix, sizeof(matrix));

    return *this;
}

Matrix3 Matrix3::operator *(const Matrix3 &m) const {
    Matrix3 prod;

    for (int c=0;c<3;c++)
        for (int r=0;r<3;r++)
            prod.set(c,r,
                     m.get(c,0)*get(0,r) +
                     m.get(c,1)*get(1,r) +
                     m.get(c,2)*get(2,r));

    return prod;
}

Matrix3 Matrix3::operator +(const Matrix3 &m) const {
    Matrix3 prod;
    Vector3 resul;
    int i;
    for(i=0; i<3; ++i){
        resul = row(i) + m.row(i);
        prod.set(0, i, resul.x);
        prod.set(1, i, resul.y);
        prod.set(2, i, resul.z);
    }
    return prod;
}

Matrix3 Matrix3::operator -(const Matrix3 &m) const {
    Matrix3 prod;
    Vector3 resul;
    int i;
    for(i=0; i<3; ++i){
        resul = row(i) - m.row(i);
        prod.set(0, i, resul.x);
        prod.set(1, i, resul.y);
        prod.set(2, i, resul.z);
    }
    return prod;
}

Matrix3& Matrix3::operator*=(const Matrix3 &m) {
    return (*this) = (*this)*m;
}

Matrix3& Matrix3::operator*=(float scalar) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            matrix[i][j] *= scalar;
        }
    }
    return *this;
}

Vector3 Matrix3::operator*(const Vector3 &v) const {
    float prod[3] = { 0,0,0 };

    int i, j;
    for(j=0; j<3; ++j) {
        prod[j] = 0.f;
        for(i=0; i<3; ++i)
            prod[j] += get(i,j) * v[i];
    }

    return Vector3(prod[0] ,prod[1] ,prod[2]);
}

Matrix3 Matrix3::operator*(float scalar) const {
    Matrix3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.matrix[i][j] = matrix[i][j] * scalar;
        }
    }
    return result;
}

const float* Matrix3::operator[](int iCol) const {
    assert( iCol < 3 );
    return matrix[iCol];
}

float* Matrix3::operator[](int iCol) {
    assert( iCol < 3 );
    return matrix[iCol];
}

bool Matrix3::operator==(const Matrix3 &m) const {
    return !memcmp(matrix,m.matrix,sizeof(matrix));
}

bool Matrix3::operator!=(const Matrix3 &m) const {
    return memcmp(matrix,m.matrix,sizeof(matrix))!=0;
}

Matrix3::operator float *() {
    return (float*)matrix;
}


Matrix3::operator const float *() const {
    return (float*)matrix;
}

void Matrix3::set(const int col, const int row, const float val) {
    matrix[col][row] = val;
}

float Matrix3::get(const int col, const int row) const {
    return matrix[col][row];
}


Vector3 Matrix3::row(const unsigned int row) const {
    return Vector3(matrix[0][row], matrix[1][row], matrix[2][row]);
}

Vector3 Matrix3::column(const unsigned int column) const {

    return Vector3(matrix[column][0], matrix[column][1], matrix[column][2]);
}

void Matrix3::setRow(const unsigned int row, const Vector3& vec){
    assert(row < 3);
    matrix[0][row] = vec.x;
    matrix[1][row] = vec.y;
    matrix[2][row] = vec.z;
}

void Matrix3::setColumn(const unsigned int column, const Vector3& vec){
    assert(column < 3);
    matrix[column][0] = vec.x;
    matrix[column][1] = vec.y;
    matrix[column][2] = vec.z;
}

bool Matrix3::isValid() const {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(matrix[i][j])) {
                return false;
            }
        }
    }
    return true;
}

Matrix3& Matrix3::identity() {
    int i, j;
    for(i=0; i<3; ++i)
        for(j=0; j<3; ++j)
            set(i,j,(i==j ? 1.f : 0.f));

    return *this;
}

Matrix3 Matrix3::inverse(float fTolerance) const{
    Matrix3 kInverse(0,0,0,0,0,0,0,0,0);
    calcInverse(kInverse,fTolerance);
    return kInverse;
}

Matrix3 Matrix3::transpose() const{
    Matrix3 tmp;

    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++)
            tmp.set(j,i,get(i,j));

    return tmp;
}

float Matrix3::determinant() const{
    float fCofactor00 = matrix[1][1]*matrix[2][2] - matrix[2][1]*matrix[1][2];
    float fCofactor01 = matrix[2][1]*matrix[0][2] - matrix[0][1]*matrix[2][2];
    float fCofactor02 = matrix[0][1]*matrix[1][2] - matrix[1][1]*matrix[0][2];

    float fDet =
            matrix[0][0]*fCofactor00 +
            matrix[1][0]*fCofactor01 +
            matrix[2][0]*fCofactor02;

    return fDet;
}

bool Matrix3::calcInverse(Matrix3& rkInverse, float fTolerance) const {
    rkInverse[0][0] = matrix[1][1]*matrix[2][2] - matrix[2][1]*matrix[1][2];
    rkInverse[1][0] = matrix[2][0]*matrix[1][2] - matrix[1][0]*matrix[2][2];
    rkInverse[2][0] = matrix[1][0]*matrix[2][1] - matrix[2][0]*matrix[1][1];
    rkInverse[0][1] = matrix[2][1]*matrix[0][2] - matrix[0][1]*matrix[2][2];
    rkInverse[1][1] = matrix[0][0]*matrix[2][2] - matrix[2][0]*matrix[0][2];
    rkInverse[2][1] = matrix[2][0]*matrix[0][1] - matrix[0][0]*matrix[2][1];
    rkInverse[0][2] = matrix[0][1]*matrix[1][2] - matrix[1][1]*matrix[0][2];
    rkInverse[1][2] = matrix[1][0]*matrix[0][2] - matrix[0][0]*matrix[1][2];
    rkInverse[2][2] = matrix[0][0]*matrix[1][1] - matrix[1][0]*matrix[0][1];

    float fDet =
            matrix[0][0]*rkInverse[0][0] +
            matrix[1][0]*rkInverse[0][1] +
            matrix[2][0]*rkInverse[0][2];

    if ( abs(fDet) <= fTolerance )
        return false;

    float fInvDet = 1.0f/fDet;
    for (size_t iRow = 0; iRow < 3; iRow++)
    {
        for (size_t iCol = 0; iCol < 3; iCol++)
            rkInverse[iCol][iRow] *= fInvDet;
    }

    return true;
}

Matrix3 Matrix3::rotateMatrix(const float angle, const Vector3 &axis){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);
    float t = 1 - c;

    Vector3 ax = axis / axis.length();

    float x = ax[0];
    float y = ax[1];
    float z = ax[2];

    Matrix3 r;

    r.set(0, 0, t*x*x+c);
    r.set(0, 1, t*x*y+s*z);
    r.set(0, 2, t*x*z-s*y);

    r.set(1, 0, t*y*x-s*z);
    r.set(1, 1, t*y*y+c);
    r.set(1, 2, t*y*z+s*x);

    r.set(2, 0, t*z*x+s*y);
    r.set(2, 1, t*z*y-s*x);
    r.set(2, 2, t*z*z+c);

    return r;
}

Matrix3 Matrix3::rotateMatrix(const float azimuth, const float elevation){

    float ca = cos(azimuth);
    float sa = sin(azimuth);
    float cb = cos(elevation);
    float sb = sin(elevation);

    Matrix3 r;

    r.set(0,0,cb);
    r.set(0,1,-sa*sb);
    r.set(0,2,ca*sb);

    r.set(1,0,0);
    r.set(1,1,ca);
    r.set(1,2,sa);

    r.set(2,0,-sb);
    r.set(2,1,-sa*cb);
    r.set(2,2,ca*cb);

    return r;
}

Matrix3 Matrix3::rotateXMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix3 r;

    r.set(0, 0, 1.f);
    r.set(0, 1, 0.f);
    r.set(0, 2, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, c);
    r.set(1, 2, -s);

    r.set(2, 0, 0.f);
    r.set(2, 1, s);
    r.set(2, 2, c);

    return r;
}

Matrix3 Matrix3::rotateYMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix3 r;

    r.set(0, 0, c);
    r.set(0, 1, 0.f);
    r.set(0, 2, -s);

    r.set(1, 0, 0.f);
    r.set(1, 1, 1.f);
    r.set(1, 2, 0.f);

    r.set(2, 0, s);
    r.set(2, 1, 0.f);
    r.set(2, 2, c);

    return r;
}

Matrix3 Matrix3::rotateZMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix3 r;

    r.set(0, 0, c);
    r.set(0, 1, -s);
    r.set(0, 2, 0.f);

    r.set(1, 0, s);
    r.set(1, 1, c);
    r.set(1, 2, 0.f);

    r.set(2, 0, 0.f);
    r.set(2, 1, 0.f);
    r.set(2, 2, 1.f);

    return r;
}

Matrix3 Matrix3::scaleMatrix(const float sf){

    Matrix3 r;

    r.set(0,0,sf);
    r.set(1,1,sf);
    r.set(2,2,sf);

    return r;
}

Matrix3 Matrix3::scaleMatrix(const Vector3& sf){

    Matrix3 r;

    r.set(0,0,sf[0]);
    r.set(1,1,sf[1]);
    r.set(2,2,sf[2]);

    return r;
}


void Matrix3::decomposeQDU(Matrix3& kQ, Vector3& kD, Vector3& kU) const{
    // orthogonal matrix Q
    float fInvLength = matrix[0][0]*matrix[0][0] + matrix[0][1]*matrix[0][1] + matrix[0][2]*matrix[0][2];
    if (fInvLength != 0) fInvLength = 1.0 / sqrtf(fInvLength);

    kQ[0][0] = matrix[0][0]*fInvLength;
    kQ[0][1] = matrix[0][1]*fInvLength;
    kQ[0][2] = matrix[0][2]*fInvLength;

    float fDot = kQ[0][0]*matrix[1][0] + kQ[0][1]*matrix[1][1] + kQ[0][2]*matrix[1][2];
    kQ[1][0] = matrix[1][0]-fDot*kQ[0][0];
    kQ[1][1] = matrix[1][1]-fDot*kQ[0][1];
    kQ[1][2] = matrix[1][2]-fDot*kQ[0][2];
    fInvLength = kQ[1][0]*kQ[1][0] + kQ[1][1]*kQ[1][1] + kQ[1][2]*kQ[1][2];
    if (fInvLength != 0) fInvLength = 1.0 / sqrtf(fInvLength);

    kQ[1][0] *= fInvLength;
    kQ[1][1] *= fInvLength;
    kQ[1][2] *= fInvLength;

    fDot = kQ[0][0]*matrix[2][0] + kQ[0][1]*matrix[2][1] + kQ[0][2]*matrix[2][2];
    kQ[2][0] = matrix[2][0]-fDot*kQ[0][0];
    kQ[2][1] = matrix[2][1]-fDot*kQ[0][1];
    kQ[2][2] = matrix[2][2]-fDot*kQ[0][2];
    fDot = kQ[1][0]*matrix[2][0] + kQ[1][1]*matrix[2][1] + kQ[1][2]*matrix[2][2];
    kQ[2][0] -= fDot*kQ[1][0];
    kQ[2][1] -= fDot*kQ[1][1];
    kQ[2][2] -= fDot*kQ[1][2];
    fInvLength = kQ[2][0]*kQ[2][0] + kQ[2][1]*kQ[2][1] + kQ[2][2]*kQ[2][2];
    if (fInvLength != 0) fInvLength = 1.0 / sqrtf(fInvLength);

    kQ[2][0] *= fInvLength;
    kQ[2][1] *= fInvLength;
    kQ[2][2] *= fInvLength;

    // check that orthogonal matrix has determinant 1 (no reflections)
    float fDet = kQ[0][0]*kQ[1][1]*kQ[2][2] + kQ[1][0]*kQ[2][1]*kQ[0][2] +
        kQ[2][0]*kQ[0][1]*kQ[1][2] - kQ[2][0]*kQ[1][1]*kQ[0][2] -
        kQ[1][0]*kQ[0][1]*kQ[2][2] - kQ[0][0]*kQ[2][1]*kQ[1][2];

    if ( fDet < 0.0 ){
        for (size_t iCol = 0; iCol < 3; iCol++)
            for (size_t iRow = 0; iRow < 3; iRow++)
                kQ[iCol][iRow] = -kQ[iCol][iRow];
    }

    // right matrix R
    Matrix3 kR;
    kR[0][0] = kQ[0][0]*matrix[0][0] + kQ[0][1]*matrix[0][1] + kQ[0][2]*matrix[0][2];
    kR[1][0] = kQ[0][0]*matrix[1][0] + kQ[0][1]*matrix[1][1] + kQ[0][2]*matrix[1][2];
    kR[1][1] = kQ[1][0]*matrix[1][0] + kQ[1][1]*matrix[1][1] + kQ[1][2]*matrix[1][2];
    kR[2][0] = kQ[0][0]*matrix[2][0] + kQ[0][1]*matrix[2][1] + kQ[0][2]*matrix[2][2];
    kR[2][1] = kQ[1][0]*matrix[2][0] + kQ[1][1]*matrix[2][1] + kQ[1][2]*matrix[2][2];
    kR[2][2] = kQ[2][0]*matrix[2][0] + kQ[2][1]*matrix[2][1] + kQ[2][2]*matrix[2][2];

    // scaling component
    kD[0] = kR[0][0];
    kD[1] = kR[1][1];
    kD[2] = kR[2][2];

    // shear component
    float fInvD0 = 1.0f/kD[0];
    kU[0] = kR[1][0]*fInvD0;
    kU[1] = kR[2][0]*fInvD0;
    kU[2] = kR[2][1]/kD[1];
}