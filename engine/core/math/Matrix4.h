
#ifndef matrix4_h
#define matrix4_h

#include "math/Vector3.h"
#include "math/Matrix3.h"
#include "math/Vector4.h"

namespace doriax {

    class Quaternion;
    class AABB;
    class OBB;

    class DORIAX_API Matrix4{

    private:

        float matrix[4][4]; //[col][row]

    public:

        Matrix4();
        Matrix4(const Matrix4 &matrix);
        Matrix4 (float fEntry00, float fEntry10, float fEntry20, float fEntry30,
                float fEntry01, float fEntry11, float fEntry21, float fEntry31,
                float fEntry02, float fEntry12, float fEntry22, float fEntry32,
                float fEntry03, float fEntry13, float fEntry23, float fEntry33);
        Matrix4(const float **matrix);

        std::string toString() const;

        Matrix4 &operator=(const Matrix4 &);
        Matrix4 operator*(const Matrix4 &) const;
        Matrix4 operator*(float scalar) const;
        Matrix4 operator+(const Matrix4 &) const;
        Matrix4 operator-(const Matrix4 &) const;
        Matrix4 &operator*=(const Matrix4 &);
        Matrix4 &operator*=(float scalar);
        Vector3 operator*(const Vector3 &) const;
        Vector4 operator*(const Vector4 &) const;
        AABB operator*(const AABB &) const;
        OBB operator*(const OBB &) const;
        const float *operator[](int iCol) const;
        float *operator[](int iCol);
        bool operator==(const Matrix4 &) const;
        bool operator!=(const Matrix4 &) const;
        operator float * ();
        operator const float * () const;

        Vector4 row(const unsigned int row) const;
        Vector4 column(const unsigned int column) const;
        void set(const int col, const int row, const float val);
        float get(const int col, const int row) const;

        void setRow(const unsigned int row, const Vector4& vec);
        void setColumn(const unsigned int column, const Vector4& vec);

        bool isValid() const;

        Matrix4& identity();
        Matrix4& translateInPlace(float x, float y, float z);

        Matrix3 linear() const;

        Matrix4 inverse() const;
        Matrix4 transpose() const;
        float determinant() const;

        static Matrix4 translateMatrix(const float x, const float y, const float z);
        static Matrix4 translateMatrix(const Vector3& position);

        static Matrix4 rotateMatrix(const float angle, const Vector3 &axis);
        static Matrix4 rotateMatrix(const float azimuth, const float elevation);
        static Matrix4 rotateXMatrix(const float angle);
        static Matrix4 rotateYMatrix(const float angle);
        static Matrix4 rotateZMatrix(const float angle);

        static Matrix4 scaleMatrix(const float sf);
        static Matrix4 scaleMatrix(const Vector3& sf);

        static Matrix4 lookAtMatrix(Vector3 eye, Vector3 center, Vector3 up);
        static Matrix4 frustumMatrix(float left, float right, float bottom, float top, float near, float far);
        static Matrix4 orthoMatrix(float l, float r, float b, float t, float n, float f);
        static Matrix4 perspectiveMatrix(float yfov, float aspect, float near, float far);

        void decomposeStandard(Vector3& position, Vector3& scale, Quaternion& rotation) const;
        void decomposeQDU(Vector3& position, Vector3& scale, Quaternion& rotation) const;
        void decompose(Vector3& position, Vector3& scale, Quaternion& rotation) const;
    };
    
}

#endif
