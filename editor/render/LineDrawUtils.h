#pragma once

#include "Doriax.h"
#include <cmath>
#include <tuple>

namespace doriax::editor::LineDrawUtils{

    inline void addCircle2D(Lines* lines, const Vector3& center, float radius, const Vector4& color, int segments = 20){
        if (radius <= 0.0f) return;
        for (int i = 0; i < segments; i++){
            float a0 = (2.0f * M_PI * i) / segments;
            float a1 = (2.0f * M_PI * (i + 1)) / segments;
            Vector3 p0(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, center.z);
            Vector3 p1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, center.z);
            lines->addLine(p0, p1, color);
        }
    }

    inline void addArc2D(Lines* lines, const Vector3& center, float radius, float startAngle, float endAngle, const Vector4& color, int segments = 12){
        if (radius <= 0.0f) return;
        for (int i = 0; i < segments; i++){
            float t0 = (float)i / (float)segments;
            float t1 = (float)(i + 1) / (float)segments;
            float a0 = startAngle + (endAngle - startAngle) * t0;
            float a1 = startAngle + (endAngle - startAngle) * t1;
            Vector3 p0(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, center.z);
            Vector3 p1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, center.z);
            lines->addLine(p0, p1, color);
        }
    }

    inline std::tuple<Vector3, Vector3, Vector3> makeBasis(const Vector3& axis){
        Vector3 n = axis.length() > 0.0001f ? axis.normalized() : Vector3(1.0f, 0.0f, 0.0f);
        Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
        if (std::abs(n.dotProduct(up)) > 0.9f){
            up = Vector3(1.0f, 0.0f, 0.0f);
        }
        Vector3 right = n.crossProduct(up).normalized();
        Vector3 forward = right.crossProduct(n).normalized();
        return std::make_tuple(n, right, forward);
    }

    inline void addRing3D(Lines* lines, const Vector3& center, const Vector3& axis, float radius, const Vector4& color, int segments = 24){
        if (radius <= 0.0f) return;
        auto [n, right, forward] = makeBasis(axis);
        (void)n;
        for (int i = 0; i < segments; i++){
            float a0 = (2.0f * M_PI * i) / segments;
            float a1 = (2.0f * M_PI * (i + 1)) / segments;
            Vector3 p0 = center + right * std::cos(a0) * radius + forward * std::sin(a0) * radius;
            Vector3 p1 = center + right * std::cos(a1) * radius + forward * std::sin(a1) * radius;
            lines->addLine(p0, p1, color);
        }
    }

    inline void addArc3D(Lines* lines, const Vector3& center, const Vector3& axis, const Vector3& fromDir, float angleStart, float angleEnd, float radius, const Vector4& color, int segments = 14){
        if (radius <= 0.0f) return;
        Vector3 base = fromDir.length() > 0.0001f ? fromDir.normalized() : Vector3(1.0f, 0.0f, 0.0f);
        Vector3 n = axis.length() > 0.0001f ? axis.normalized() : Vector3(0.0f, 1.0f, 0.0f);
        if (std::abs(base.dotProduct(n)) > 0.95f){
            auto [axisN, right, forward] = makeBasis(n);
            (void)axisN;
            base = right;
        }
        Vector3 tangent = n.crossProduct(base).normalized();
        for (int i = 0; i < segments; i++){
            float t0 = (float)i / (float)segments;
            float t1 = (float)(i + 1) / (float)segments;
            float a0 = angleStart + (angleEnd - angleStart) * t0;
            float a1 = angleStart + (angleEnd - angleStart) * t1;
            Vector3 p0 = center + (base * std::cos(a0) + tangent * std::sin(a0)) * radius;
            Vector3 p1 = center + (base * std::cos(a1) + tangent * std::sin(a1)) * radius;
            lines->addLine(p0, p1, color);
        }
    }

}
