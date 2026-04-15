#include "Matrix4.h"

#include "util/Angle.h"
#include <cmath>
#include <cassert>
#include <string.h>
#include <iostream>
#include <iomanip>
#include "Quaternion.h"
#include "AABB.h"
#include "OBB.h"

using namespace doriax;

Matrix4::Matrix4(){
    identity();
}

Matrix4::Matrix4(const Matrix4 &matrix){
    (*this) = matrix;
}

Matrix4::Matrix4 (float fEntry00, float fEntry10, float fEntry20, float fEntry30,
                float fEntry01, float fEntry11, float fEntry21, float fEntry31,
                float fEntry02, float fEntry12, float fEntry22, float fEntry32,
                float fEntry03, float fEntry13, float fEntry23, float fEntry33){
    matrix[0][0] = fEntry00;
    matrix[0][1] = fEntry01;
    matrix[0][2] = fEntry02;
    matrix[0][3] = fEntry03;
    matrix[1][0] = fEntry10;
    matrix[1][1] = fEntry11;
    matrix[1][2] = fEntry12;
    matrix[1][3] = fEntry13;
    matrix[2][0] = fEntry20;
    matrix[2][1] = fEntry21;
    matrix[2][2] = fEntry22;
    matrix[2][3] = fEntry23;
    matrix[3][0] = fEntry30;
    matrix[3][1] = fEntry31;
    matrix[3][2] = fEntry32;
    matrix[3][3] = fEntry33;
}

Matrix4::Matrix4(const float **matrix){
    std::copy(&matrix[0][0], &matrix[0][0]+16,&this->matrix[0][0]);
}


Matrix4& Matrix4::operator=(const Matrix4 &m){
    memcpy(this->matrix,m.matrix,sizeof(matrix));

    return *this;
}

Matrix4 Matrix4::operator *(const Matrix4 &m) const{
    Matrix4 prod;

    for (int c=0;c<4;c++)
        for (int r=0;r<4;r++)
            prod.set(c,r,
                m.get(c,0)*get(0,r) +
                m.get(c,1)*get(1,r) +
                m.get(c,2)*get(2,r) +
                m.get(c,3)*get(3,r));

    return prod;
}

Matrix4 Matrix4::operator*(float scalar) const {
    Matrix4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.matrix[i][j] = matrix[i][j] * scalar;
        }
    }
    return result;
}

std::string Matrix4::toString() const{
    return "Matrix4("+
        std::to_string(matrix[0][0]) + ", " + std::to_string(matrix[0][1]) + ", " + std::to_string(matrix[0][2]) + ", " + std::to_string(matrix[0][3]) + ", " +
        std::to_string(matrix[1][0]) + ", " + std::to_string(matrix[1][1]) + ", " + std::to_string(matrix[1][2]) + ", " + std::to_string(matrix[1][3]) + ", " +
        std::to_string(matrix[2][0]) + ", " + std::to_string(matrix[2][1]) + ", " + std::to_string(matrix[2][2]) + ", " + std::to_string(matrix[2][3]) + ", " +
        std::to_string(matrix[3][0]) + ", " + std::to_string(matrix[3][1]) + ", " + std::to_string(matrix[3][2]) + ", " + std::to_string(matrix[3][3]) +
        ")"; 
}

Matrix4 Matrix4::operator +(const Matrix4 &m) const{
    Matrix4 prod;
    Vector4 resul;
    int i;
    for(i=0; i<4; ++i){
        resul = row(i) + m.row(i);
        prod.set(0, i, resul.x);
        prod.set(1, i, resul.y);
        prod.set(2, i, resul.z);
        prod.set(3, i, resul.w);
    }
    return prod;
}

Matrix4 Matrix4::operator -(const Matrix4 &m) const{
    Matrix4 prod;
    Vector4 resul;
    int i;
    for(i=0; i<4; ++i){
        resul = row(i) - m.row(i);
        prod.set(0, i, resul.x);
        prod.set(1, i, resul.y);
        prod.set(2, i, resul.z);
        prod.set(3, i, resul.w);
    }
    return prod;
}

Matrix4& Matrix4::operator*=(const Matrix4 &m){

    return (*this) = (*this)*m;
}

Matrix4& Matrix4::operator*=(float scalar) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            matrix[i][j] *= scalar;
        }
    }
    return *this;
}

Vector3 Matrix4::operator*(const Vector3 &v) const{
    float prod[4] = { 0,0,0,0 };

    for (int r=0;r<4;r++)
    {
        for (int c=0;c<3;c++)
            prod[r] += v[c]*get(c,r);

        prod[r] += get(3,r);
    }

    float div = 1.0 / prod[3];

    return Vector3(prod[0]*div,prod[1]*div,prod[2]*div);
}

Vector4 Matrix4::operator*(const Vector4 &v) const{

    float prod[4] = { 0,0,0,0 };

    int i, j;
    for(j=0; j<4; ++j) {
        prod[j] = 0.f;
        for(i=0; i<4; ++i)
            prod[j] += get(i,j) * v[i];
    }

    return Vector4(prod[0] ,prod[1] ,prod[2], prod[3]);
}

AABB Matrix4::operator*(const AABB &aabb) const{
    AABB resul = aabb;

    resul.transform(*this);

    return resul;
}

OBB Matrix4::operator*(const OBB &obb) const {
    OBB result = obb;

    result.transform(*this);

    return result;
}

const float* Matrix4::operator[](int iCol) const{
    assert( iCol < 4 );
    return matrix[iCol];
}

float* Matrix4::operator[](int iCol){
    assert( iCol < 4 );
    return matrix[iCol];
}

bool Matrix4::operator==(const Matrix4 &m) const{
    return !memcmp(matrix,m.matrix,sizeof(matrix));
}

bool Matrix4::operator!=(const Matrix4 &m) const{
    return memcmp(matrix,m.matrix,sizeof(matrix))!=0;
}

Matrix4::operator float *(){
    return (float*)matrix;
}

Matrix4::operator const float *() const{
     return (float*)matrix;
}

void Matrix4::set(const int col, const int row, const float val){
    matrix[col][row] = val;
}

float Matrix4::get(const int col, const int row) const{
    return matrix[col][row];
}


Vector4 Matrix4::row(const unsigned int row) const{
    return Vector4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
}

Vector4 Matrix4::column(const unsigned int column) const{

    return Vector4(matrix[column][0], matrix[column][1], matrix[column][2], matrix[column][3]);
}

void Matrix4::setRow(const unsigned int row, const Vector4& vec){
    assert(row < 4);
    matrix[0][row] = vec.x;
    matrix[1][row] = vec.y;
    matrix[2][row] = vec.z;
    matrix[3][row] = vec.w;
}

void Matrix4::setColumn(const unsigned int column, const Vector4& vec){
    assert(column < 4);
    matrix[column][0] = vec.x;
    matrix[column][1] = vec.y;
    matrix[column][2] = vec.z;
    matrix[column][3] = vec.w;
}

bool Matrix4::isValid() const {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!std::isfinite(matrix[i][j])) {
                return false;
            }
        }
    }
    return true;
}

Matrix4& Matrix4::identity(){
    int i, j;
    for(i=0; i<4; ++i)
        for(j=0; j<4; ++j)
            set(i,j,(i==j ? 1.f : 0.f));

    return *this;
}

Matrix4& Matrix4::translateInPlace(float x, float y, float z){
    Vector4 t = Vector4(x, y, z, 0);
    Vector4 r = Vector4();
    int i;
    for (i = 0; i < 4; ++i) {
        r = row(i);
        set(3, i, get(3, i) + r.dotProduct(t));
    }

    return *this;
}

Matrix3 Matrix4::linear() const{
    return Matrix3(
        matrix[0][0], matrix[1][0], matrix[2][0],
        matrix[0][1], matrix[1][1], matrix[2][1],
        matrix[0][2], matrix[1][2], matrix[2][2]);
}

Matrix4 Matrix4::inverse() const{
    float s[6];
    float c[6];
    s[0] = get(0,0)*get(1,1) - get(1,0)*get(0,1);
    s[1] = get(0,0)*get(1,2) - get(1,0)*get(0,2);
    s[2] = get(0,0)*get(1,3) - get(1,0)*get(0,3);
    s[3] = get(0,1)*get(1,2) - get(1,1)*get(0,2);
    s[4] = get(0,1)*get(1,3) - get(1,1)*get(0,3);
    s[5] = get(0,2)*get(1,3) - get(1,2)*get(0,3);

    c[0] = get(2,0)*get(3,1) - get(3,0)*get(2,1);
    c[1] = get(2,0)*get(3,2) - get(3,0)*get(2,2);
    c[2] = get(2,0)*get(3,3) - get(3,0)*get(2,3);
    c[3] = get(2,1)*get(3,2) - get(3,1)*get(2,2);
    c[4] = get(2,1)*get(3,3) - get(3,1)*get(2,3);
    c[5] = get(2,2)*get(3,3) - get(3,2)*get(2,3);

    float idet = 1.0f/( s[0]*c[5]-s[1]*c[4]+s[2]*c[3]+s[3]*c[2]-s[4]*c[1]+s[5]*c[0] );

    Matrix4 t;

    t.set(0,0, ( get(1,1) * c[5] - get(1,2) * c[4] + get(1,3) * c[3]) * idet);
    t.set(0,1, (-get(0,1) * c[5] + get(0,2) * c[4] - get(0,3) * c[3]) * idet);
    t.set(0,2, ( get(3,1) * s[5] - get(3,2) * s[4] + get(3,3) * s[3]) * idet);
    t.set(0,3, (-get(2,1) * s[5] + get(2,2) * s[4] - get(2,3) * s[3]) * idet);

    t.set(1,0, (-get(1,0) * c[5] + get(1,2) * c[2] - get(1,3) * c[1]) * idet);
    t.set(1,1, ( get(0,0) * c[5] - get(0,2) * c[2] + get(0,3) * c[1]) * idet);
    t.set(1,2, (-get(3,0) * s[5] + get(3,2) * s[2] - get(3,3) * s[1]) * idet);
    t.set(1,3, ( get(2,0) * s[5] - get(2,2) * s[2] + get(2,3) * s[1]) * idet);

    t.set(2,0, ( get(1,0) * c[4] - get(1,1) * c[2] + get(1,3) * c[0]) * idet);
    t.set(2,1, (-get(0,0) * c[4] + get(0,1) * c[2] - get(0,3) * c[0]) * idet);
    t.set(2,2, ( get(3,0) * s[4] - get(3,1) * s[2] + get(3,3) * s[0]) * idet);
    t.set(2,3, (-get(2,0) * s[4] + get(2,1) * s[2] - get(2,3) * s[0]) * idet);

    t.set(3,0, (-get(1,0) * c[3] + get(1,1) * c[1] - get(1,2) * c[0]) * idet);
    t.set(3,1, ( get(0,0) * c[3] - get(0,1) * c[1] + get(0,2) * c[0]) * idet);
    t.set(3,2, (-get(3,0) * s[3] + get(3,1) * s[1] - get(3,2) * s[0]) * idet);
    t.set(3,3, ( get(2,0) * s[3] - get(2,1) * s[1] + get(2,2) * s[0]) * idet);

    return t;
}

Matrix4 Matrix4::transpose() const{
    Matrix4 tmp;

    for (int i=0;i<4;i++)
        for (int j=0;j<4;j++)
            tmp.set(j,i,get(i,j));

    return tmp;
}

float Matrix4::determinant() const{
    return
         matrix[0][3] * matrix[1][2] * matrix[2][1] * matrix[3][0] - matrix[0][2] * matrix[1][3] * matrix[2][1] * matrix[3][0] -
         matrix[0][3] * matrix[1][1] * matrix[2][2] * matrix[3][0] + matrix[0][1] * matrix[1][3] * matrix[2][2] * matrix[3][0] +
         matrix[0][2] * matrix[1][1] * matrix[2][3] * matrix[3][0] - matrix[0][1] * matrix[1][2] * matrix[2][3] * matrix[3][0] -
         matrix[0][3] * matrix[1][2] * matrix[2][0] * matrix[3][1] + matrix[0][2] * matrix[1][3] * matrix[2][0] * matrix[3][1] +
         matrix[0][3] * matrix[1][0] * matrix[2][2] * matrix[3][1] - matrix[0][0] * matrix[1][3] * matrix[2][2] * matrix[3][1] -
         matrix[0][2] * matrix[1][0] * matrix[2][3] * matrix[3][1] + matrix[0][0] * matrix[1][2] * matrix[2][3] * matrix[3][1] +
         matrix[0][3] * matrix[1][1] * matrix[2][0] * matrix[3][2] - matrix[0][1] * matrix[1][3] * matrix[2][0] * matrix[3][2] -
         matrix[0][3] * matrix[1][0] * matrix[2][1] * matrix[3][2] + matrix[0][0] * matrix[1][3] * matrix[2][1] * matrix[3][2] +
         matrix[0][1] * matrix[1][0] * matrix[2][3] * matrix[3][2] - matrix[0][0] * matrix[1][1] * matrix[2][3] * matrix[3][2] -
         matrix[0][2] * matrix[1][1] * matrix[2][0] * matrix[3][3] + matrix[0][1] * matrix[1][2] * matrix[2][0] * matrix[3][3] +
         matrix[0][2] * matrix[1][0] * matrix[2][1] * matrix[3][3] - matrix[0][0] * matrix[1][2] * matrix[2][1] * matrix[3][3] -
         matrix[0][1] * matrix[1][0] * matrix[2][2] * matrix[3][3] + matrix[0][0] * matrix[1][1] * matrix[2][2] * matrix[3][3];
}

Matrix4 Matrix4::translateMatrix(const Vector3& position){
    Matrix4 r;

    r.set(3, 0, position.x);
    r.set(3, 1, position.y);
    r.set(3, 2, position.z);

    return r;
}

Matrix4 Matrix4::translateMatrix(const float x, const float y, const float z){
    Matrix4 r;

    r.set(3, 0, x);
    r.set(3, 1, y);
    r.set(3, 2, z);

    return r;
}

Matrix4 Matrix4::rotateMatrix(const float angle, const Vector3 &axis){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);
    float t = 1 - c;

    Vector3 ax = axis / axis.length();

    float x = ax[0];
    float y = ax[1];
    float z = ax[2];

    Matrix4 r;

    r.set(0, 0, t*x*x+c);
    r.set(0, 1, t*x*y+s*z);
    r.set(0, 2, t*x*z-s*y);
    r.set(0, 3, 0.f);

    r.set(1, 0, t*y*x-s*z);
    r.set(1, 1, t*y*y+c);
    r.set(1, 2, t*y*z+s*x);
    r.set(1, 3, 0.f);

    r.set(2, 0, t*z*x+s*y);
    r.set(2, 1, t*z*y-s*x);
    r.set(2, 2, t*z*z+c);
    r.set(2, 3, 0.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, 0.f);
    r.set(3, 3, 1.f);

    return r;
}

Matrix4 Matrix4::rotateMatrix(const float azimuth, const float elevation){

    float ca = cos(azimuth);
    float sa = sin(azimuth);
    float cb = cos(elevation);
    float sb = sin(elevation);

    Matrix4 r;

    r.set(0,0,cb);
    r.set(0,1,-sa*sb);
    r.set(0,2,ca*sb);
    r.set(0,3, 0.f);

    r.set(1,0,0);
    r.set(1,1,ca);
    r.set(1,2,sa);
    r.set(1,3, 0.f);

    r.set(2,0,-sb);
    r.set(2,1,-sa*cb);
    r.set(2,2,ca*cb);
    r.set(2,3, 0.f);

    r.set(3,0, 0.f);
    r.set(3,1, 0.f);
    r.set(3,2, 0.f);
    r.set(3,3, 1.f);

    return r;
}

Matrix4 Matrix4::rotateXMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix4 r;

    r.set(0, 0, 1.f);
    r.set(0, 1, 0.f);
    r.set(0, 2, 0.f);
    r.set(0, 3, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, c);
    r.set(1, 2, -s);
    r.set(1, 3, 0.f);

    r.set(2, 0, 0.f);
    r.set(2, 1, s);
    r.set(2, 2, c);
    r.set(2, 3, 0.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, 0.f);
    r.set(3, 3, 1.f);

    return r;
}

Matrix4 Matrix4::rotateYMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix4 r;

    r.set(0, 0, c);
    r.set(0, 1, 0.f);
    r.set(0, 2, -s);
    r.set(0, 3, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, 1.f);
    r.set(1, 2, 0.f);
    r.set(1, 3, 0.f);

    r.set(2, 0, s);
    r.set(2, 1, 0.f);
    r.set(2, 2, c);
    r.set(2, 3, 0.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, 0.f);
    r.set(3, 3, 1.f);

    return r;
}

Matrix4 Matrix4::rotateZMatrix(const float angle){

    float defAngle = Angle::defaultToRad(angle);

    float s = sin(defAngle);
    float c = cos(defAngle);

    Matrix4 r;

    r.set(0, 0, c);
    r.set(0, 1, -s);
    r.set(0, 2, 0.f);
    r.set(0, 3, 0.f);

    r.set(1, 0, s);
    r.set(1, 1, c);
    r.set(1, 2, 0.f);
    r.set(1, 3, 0.f);

    r.set(2, 0, 0.f);
    r.set(2, 1, 0.f);
    r.set(2, 2, 1.f);
    r.set(2, 3, 0.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, 0.f);
    r.set(3, 3, 1.f);

    return r;
}

Matrix4 Matrix4::scaleMatrix(const float sf){

    Matrix4 r;

    r.set(0,0,sf);
    r.set(1,1,sf);
    r.set(2,2,sf);

    return r;
}

Matrix4 Matrix4::scaleMatrix(const Vector3& sf){

    Matrix4 r;

    r.set(0,0,sf[0]);
    r.set(1,1,sf[1]);
    r.set(2,2,sf[2]);

    return r;
 }

Matrix4 Matrix4::lookAtMatrix(Vector3 eye, Vector3 center, Vector3 up){
    Vector3 f;
    f = center - eye;
    f = f.normalize();

    Vector3 s;
    s = f.crossProduct(up);
    s = s.normalize();

    Vector3 t;
    t = s.crossProduct(f);

    Matrix4 r;

    r.set(0, 0, s.x);
    r.set(0, 1, t.x);
    r.set(0, 2, -f.x);
    r.set(0, 3, 0.f);

    r.set(1, 0, s.y);
    r.set(1, 1, t.y);
    r.set(1, 2, -f.y);
    r.set(1, 3, 0.f);

    r.set(2, 0, s.z);
    r.set(2, 1, t.z);
    r.set(2, 2, -f.z);
    r.set(2, 3, 0.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, 0.f);
    r.set(3, 3, 1.f);

    r.translateInPlace(-eye.x, -eye.y, -eye.z);

    return r;
}

Matrix4 Matrix4::frustumMatrix(float left, float right, float bottom, float top, float near, float far){

    Matrix4 r;

    r.set(0, 0, 2.f*near/(right-left));
    r.set(0, 1, 0.f);
    r.set(0, 2, 0.f);
    r.set(0, 3, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, 2.*near/(top-bottom));
    r.set(1, 2, 0.f);
    r.set(1, 3, 0.f);

    r.set(2, 0, (right+left)/(right-left));
    r.set(2, 1, (top+bottom)/(top-bottom));
    r.set(2, 2, -(far+near)/(far-near));
    r.set(2, 3, -1.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, -2.f*(far*near)/(far-near));
    r.set(3, 3, 0.f);

    return r;
}

Matrix4 Matrix4::orthoMatrix(float left, float right, float bottom, float top, float near, float far){

    Matrix4 r;

    r.set(0, 0, 2.f/(right-left));
    r.set(0, 1, 0.f);
    r.set(0, 2, 0.f);
    r.set(0, 3, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, 2.f/(top-bottom));
    r.set(1, 2, 0.f);
    r.set(1, 3, 0.f);

    r.set(2, 0, 0.f);
    r.set(2, 1, 0.f);
    r.set(2, 2, -2.f/(far-near));
    r.set(2, 3, 0.f);

    r.set(3, 0, -(right+left)/(right-left));
    r.set(3, 1, -(top+bottom)/(top-bottom));
    r.set(3, 2, -(far+near)/(far-near));
    r.set(3, 3, 1.f);

    return r;
}

Matrix4 Matrix4::perspectiveMatrix(float yfov, float aspect, float near, float far){

    float const a = 1.f / tan(yfov / 2.f);

    Matrix4 r;

    r.set(0, 0, a / aspect);
    r.set(0, 1, 0.f);
    r.set(0, 2, 0.f);
    r.set(0, 3, 0.f);

    r.set(1, 0, 0.f);
    r.set(1, 1, a);
    r.set(1, 2, 0.f);
    r.set(1, 3, 0.f);

    r.set(2, 0, 0.f);
    r.set(2, 1, 0.f);
    r.set(2, 2, -((far + near) / (far - near)));
    r.set(2, 3, -1.f);

    r.set(3, 0, 0.f);
    r.set(3, 1, 0.f);
    r.set(3, 2, -((2.f * far * near) / (far - near)));
    r.set(3, 3, 0.f);

    return r;
}

void Matrix4::decomposeStandard(Vector3& position, Vector3& scale, Quaternion& rotation) const {
    // Extract position
    position.x = matrix[3][0];
    position.y = matrix[3][1];
    position.z = matrix[3][2];

    // Extract scale (column lengths)
    scale.x = Vector3(matrix[0][0], matrix[0][1], matrix[0][2]).length();
    scale.y = Vector3(matrix[1][0], matrix[1][1], matrix[1][2]).length();
    scale.z = Vector3(matrix[2][0], matrix[2][1], matrix[2][2]).length();

    // Handle negative determinant (reflection) - only negate one axis
    if (determinant() < 0) {
        scale.x = -scale.x;
    }

    // Create rotation matrix by normalizing the columns
    Matrix4 rotationM;
    rotationM.identity();

    const float EPSILON = 1e-6f;

    // Only proceed if we have valid scales
    if (scale.x > EPSILON && scale.y > EPSILON && scale.z > EPSILON) {
        // Extract and normalize rotation columns
        Vector3 col0(matrix[0][0] / scale.x, matrix[0][1] / scale.x, matrix[0][2] / scale.x);
        Vector3 col1(matrix[1][0] / scale.y, matrix[1][1] / scale.y, matrix[1][2] / scale.y);
        Vector3 col2(matrix[2][0] / scale.z, matrix[2][1] / scale.z, matrix[2][2] / scale.z);

        // Ensure orthogonality using Gram-Schmidt process
        col0 = col0.normalize();
        col1 = (col1 - col0 * col0.dotProduct(col1)).normalize();
        col2 = col0.crossProduct(col1).normalize();

        rotationM.set(0, 0, col0.x); rotationM.set(0, 1, col0.y); rotationM.set(0, 2, col0.z);
        rotationM.set(1, 0, col1.x); rotationM.set(1, 1, col1.y); rotationM.set(1, 2, col1.z);
        rotationM.set(2, 0, col2.x); rotationM.set(2, 1, col2.y); rotationM.set(2, 2, col2.z);
    }

    // Convert to quaternion
    rotation = Quaternion().fromRotationMatrix(rotationM).normalize();
}

void Matrix4::decomposeQDU(Vector3& position, Vector3& scale, Quaternion& rotation) const{
    position.x = matrix[3][0];
	position.y = matrix[3][1];
	position.z = matrix[3][2];

    Matrix3 matQ;
    Vector3 vecU;
    linear().decomposeQDU( matQ, scale, vecU );

    rotation = Quaternion(matQ);
}

void Matrix4::decompose(Vector3& position, Vector3& scale, Quaternion& rotation) const{
    decomposeQDU(position, scale, rotation);
}