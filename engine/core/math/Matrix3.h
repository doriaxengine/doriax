#ifndef MATRIX3_H
#define MATRIX3_H

#include "math/Vector3.h"

namespace doriax {

    class DORIAX_API Matrix3 {

    private:

        float matrix[3][3]; //[col][row]

    public:

        Matrix3();
        Matrix3(const Matrix3 &matrix);
        Matrix3 (float fEntry00, float fEntry10, float fEntry20,
                 float fEntry01, float fEntry11, float fEntry21,
                 float fEntry02, float fEntry12, float fEntry22);
        Matrix3(const float **matrix);

        std::string toString() const;

        Matrix3 &operator=(const Matrix3 &);
        Matrix3  operator*(const Matrix3 &) const;
        Matrix3  operator+(const Matrix3 &) const;
        Matrix3  operator-(const Matrix3 &) const;
        Matrix3 &operator*=(const Matrix3 &);
        Matrix3 &operator*=(float scalar);
        Vector3 operator*(const Vector3 &) const;
        Matrix3 operator*(float scalar) const;
        const float *operator[](int iCol) const;
        float *operator[](int iCol);
        bool operator==(const Matrix3 &) const;
        bool operator!=(const Matrix3 &) const;
        operator float * ();
        operator const float * () const;

        Vector3 row(const unsigned int row) const;
        Vector3 column(const unsigned int column) const;
        void set(const int col, const int row, const float val);
        float get(const int col, const int row) const;

        void setRow(const unsigned int row, const Vector3& vec);
        void setColumn(const unsigned int column, const Vector3& vec);

        bool isValid() const;

        Matrix3& identity();

        bool calcInverse(Matrix3& rkInverse, float fTolerance) const;

        Matrix3 inverse(float fTolerance) const;
        Matrix3 transpose() const;
        float determinant() const;

        static Matrix3 rotateMatrix(const float angle, const Vector3 &axis);
        static Matrix3 rotateMatrix(const float azimuth, const float elevation);
        static Matrix3 rotateXMatrix(const float angle);
        static Matrix3 rotateYMatrix(const float angle);
        static Matrix3 rotateZMatrix(const float angle);

        static Matrix3 scaleMatrix(const float sf);
        static Matrix3 scaleMatrix(const Vector3& sf);

        void decomposeQDU(Matrix3& kQ, Vector3& kD, Vector3& kU) const;

    };

}


#endif //MATRIX3_H
