//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix3.h"
#include "Matrix4.h"
#include "Quaternion.h"
#include "Rect.h"
#include "Ray.h"
#include "Sphere.h"
#include "Plane.h"
#include "AABB.h"
#include "OBB.h"

using namespace doriax;

namespace luabridge
{
    template<> struct Stack<Plane::Side> : EnumWrapper<Plane::Side>{};
    template<> struct Stack<AABB::BoxType> : EnumWrapper<AABB::BoxType>{};
    template<> struct Stack<AABB::CornerEnum> : EnumWrapper<AABB::CornerEnum>{};
    template<> struct Stack<OBB::BoxType> : EnumWrapper<OBB::BoxType>{};
    template<> struct Stack<OBB::CornerEnum> : EnumWrapper<OBB::CornerEnum>{};
    
    template<> struct Stack<RayFilter> : EnumWrapper<RayFilter>{};
}

void LuaBinding::registerMathClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("RotationOrder")
        .addVariable("XYZ", RotationOrder::XYZ)
        .addVariable("XZY", RotationOrder::XZY)
        .addVariable("YXZ", RotationOrder::YXZ)
        .addVariable("YZX", RotationOrder::YZX)
        .addVariable("ZXY", RotationOrder::ZXY)
        .addVariable("ZYX", RotationOrder::ZYX)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<Vector2>("Vector2")
        .addConstructor<
            void(), 
            void(const float, const float), 
            void(Vector2),
            void(Vector3), 
            void(Vector4)>()
        .addStaticProperty("ZERO", &Vector2::ZERO)
        .addStaticProperty("UNIT_X", &Vector2::UNIT_X)
        .addStaticProperty("UNIT_Y", &Vector2::UNIT_Y)
        .addStaticProperty("NEGATIVE_UNIT_X", &Vector2::NEGATIVE_UNIT_X)
        .addStaticProperty("NEGATIVE_UNIT_Y", &Vector2::NEGATIVE_UNIT_Y)
        .addStaticProperty("UNIT_SCALE", &Vector2::UNIT_SCALE)
        .addProperty("x", &Vector2::x)
        .addProperty("y", &Vector2::y)
        .addFunction("__tostring", &Vector2::toString)
        .addFunction("__eq", &Vector2::operator==)
        .addFunction("__lt", &Vector2::operator<)
        .addFunction("__sub", (Vector2 (Vector2::*)(const Vector2&) const)&Vector2::operator-)
        .addFunction("__add", (Vector2 (Vector2::*)(const Vector2&) const)&Vector2::operator+)
        .addFunction("__div", 
            luabridge::overload<float>(&Vector2::operator/),
            luabridge::overload<const Vector2&>(&Vector2::operator/))
        .addFunction("__mul", 
            luabridge::overload<float>(&Vector2::operator*),
            luabridge::overload<const Vector2&>(&Vector2::operator*))
        .addFunction("__unm", (Vector2 (Vector2::*)() const)&Vector2::operator-)
        .addFunction("isValid", &Vector2::isValid)
        .addFunction("swap", &Vector2::swap)
        .addFunction("length", &Vector2::length)
        .addFunction("squaredLength", &Vector2::squaredLength)
        .addFunction("distance", &Vector2::distance)
        .addFunction("squaredDistance", &Vector2::squaredDistance)
        .addFunction("dotProduct", &Vector2::dotProduct)
        .addFunction("normalize", &Vector2::normalize)
        .addFunction("normalized", &Vector2::normalized)
        .addFunction("normalizeL", &Vector2::normalizeL)
        .addFunction("midPoint", &Vector2::midPoint)
        .addFunction("makeFloor", &Vector2::makeFloor)
        .addFunction("makeCeil", &Vector2::makeCeil)
        .addFunction("perpendicular", &Vector2::perpendicular)
        .addFunction("crossProduct", &Vector2::crossProduct)
        .addFunction("normalizedCopy", &Vector2::normalizedCopy)
        .addFunction("reflect", &Vector2::reflect)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Vector3>("Vector3")
        .addConstructor<
            void(), 
            void(const float, const float, const float), 
            void(Vector2, const float),
            void(Vector3), 
            void(Vector4)>()
        .addStaticProperty("ZERO", &Vector3::ZERO)
        .addStaticProperty("UNIT_X", &Vector3::UNIT_X)
        .addStaticProperty("UNIT_Y", &Vector3::UNIT_Y)
        .addStaticProperty("UNIT_Z", &Vector3::UNIT_Z)
        .addStaticProperty("UNIT_SCALE", &Vector3::UNIT_SCALE)
        .addProperty("x", &Vector3::x)
        .addProperty("y", &Vector3::y)
        .addProperty("z", &Vector3::z)
        .addFunction("__tostring", &Vector3::toString)
        .addFunction("__eq", &Vector3::operator==)
        .addFunction("__lt", &Vector3::operator<)
        .addFunction("__sub", (Vector3 (Vector3::*)(const Vector3&) const)&Vector3::operator-)
        .addFunction("__add", (Vector3 (Vector3::*)(const Vector3&) const)&Vector3::operator+)
        .addFunction("__div", (Vector3 (Vector3::*)(const Vector3&) const)&Vector3::operator/)
        .addFunction("__mul", 
            luabridge::overload<float>(&Vector3::operator*), // need float operator first to fix estrange error in Emscripten
            luabridge::overload<const Vector3&>(&Vector3::operator*))
        .addFunction("__unm", (Vector3 (Vector3::*)() const)&Vector3::operator-)
        .addFunction("isValid", &Vector3::isValid)
        .addFunction("length", &Vector3::length)
        .addFunction("squaredLength", &Vector3::squaredLength)
        .addFunction("dotProduct", &Vector3::dotProduct)
        .addFunction("absDotProduct", &Vector3::absDotProduct)
        .addFunction("distance", &Vector3::distance)
        .addFunction("squaredDistance", &Vector3::squaredDistance)
        .addFunction("normalize", &Vector3::normalize)
        .addFunction("normalized", &Vector3::normalized)
        .addFunction("normalizeL", &Vector3::normalizeL)
        .addFunction("crossProduct", &Vector3::crossProduct)
        .addFunction("midPoint", &Vector3::midPoint)
        .addFunction("makeFloor", &Vector3::makeFloor)
        .addFunction("makeCeil", &Vector3::makeCeil)
        .addFunction("perpendicular", &Vector3::perpendicular)
        .addFunction("reflect", &Vector3::reflect)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Vector4>("Vector4")
        .addConstructor<
            void(), 
            void(const float, const float, const float, const float), 
            void(Vector2, const float, const float),
            void(Vector3, const float), 
            void(Vector4)>()
        .addStaticProperty("ZERO", &Vector4::ZERO)
        .addStaticProperty("UNIT_X", &Vector4::UNIT_X)
        .addStaticProperty("UNIT_Y", &Vector4::UNIT_Y)
        .addStaticProperty("UNIT_Z", &Vector4::UNIT_Z)
        .addStaticProperty("UNIT_W", &Vector4::UNIT_W)
        .addStaticProperty("UNIT_SCALE", &Vector4::UNIT_SCALE)
        .addProperty("x", &Vector4::x)
        .addProperty("y", &Vector4::y)
        .addProperty("z", &Vector4::z)
        .addProperty("w", &Vector4::w)
        .addFunction("__tostring", &Vector4::toString)
        .addFunction("__eq", &Vector4::operator==)
        .addFunction("__lt", &Vector4::operator<)
        .addFunction("__sub", (Vector4 (Vector4::*)(const Vector4&) const)&Vector4::operator-)
        .addFunction("__add", (Vector4 (Vector4::*)(const Vector4&) const)&Vector4::operator+)
        .addFunction("__div", 
            luabridge::overload<float>(&Vector4::operator/),
            luabridge::overload<const Vector4&>(&Vector4::operator/))
        .addFunction("__mul", 
            luabridge::overload<float>(&Vector4::operator*),
            luabridge::overload<const Vector4&>(&Vector4::operator*))
        .addFunction("__unm", (Vector4 (Vector4::*)() const)&Vector4::operator-)
        .addFunction("isValid", &Vector4::isValid)
        .addFunction("swap", &Vector4::swap)
        .addFunction("divideByW", &Vector4::divideByW)
        .addFunction("dotProduct", &Vector4::dotProduct)
        .addFunction("isNaN", &Vector4::isNaN)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Rect>("Rect")
        .addConstructor<void(), void(float, float, float, float)>()
        .addProperty("x", &Rect::getX, &Rect::setX)
        .addProperty("y", &Rect::getY, &Rect::setY)
        .addProperty("width", &Rect::getWidth, &Rect::setWidth)
        .addProperty("height", &Rect::getHeight, &Rect::setHeight)
        .addFunction("__tostring", &Rect::toString)
        .addFunction("__eq", &Rect::operator==)
        .addFunction("getVector", &Rect::getVector)
        .addFunction("setRect", 
            luabridge::overload<Rect>(&Rect::setRect),
            luabridge::overload<float, float, float, float>(&Rect::setRect))
        .addFunction("fitOnRect", &Rect::fitOnRect)
        .addFunction("contains", &Rect::contains)
        .addFunction("isNormalized", &Rect::isNormalized)
        .addFunction("isZero", &Rect::isZero)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Sphere>("Sphere")
        .addConstructor<void(), void(Vector3, float)>()
        .addProperty("center", &Sphere::center)
        .addProperty("radius", &Sphere::radius)
        .addFunction("toString", &Sphere::toString)
        .addFunction("contains", &Sphere::contains)
        .addFunction("intersects", 
            luabridge::overload<const Sphere&>(&Sphere::intersects),
            luabridge::overload<const AABB&>(&Sphere::intersects),
            luabridge::overload<const OBB&>(&Sphere::intersects),
            luabridge::overload<const Plane&>(&Sphere::intersects),
            luabridge::overload<const Vector3&>(&Sphere::intersects))
        .addFunction("merge", &Sphere::merge)
        .addFunction("surfaceArea", &Sphere::surfaceArea)
        .addFunction("volume", &Sphere::volume)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Matrix3>("Matrix3")
        .addConstructor<
            void(), 
            void(
                float, float, float, 
                float, float, float, 
                float, float, float)>()
        .addFunction("__tostring", &Matrix3::toString)
        .addFunction("__eq", &Matrix3::operator==)
        .addFunction("__sub", (Matrix3 (Matrix3::*)(const Matrix3&) const)&Matrix3::operator-)
        .addFunction("__add", (Matrix3 (Matrix3::*)(const Matrix3&) const)&Matrix3::operator+)
        .addFunction("__mul", (Matrix3 (Matrix3::*)(const Matrix3&) const)&Matrix3::operator*)
        .addFunction("row", &Matrix3::row)
        .addFunction("column", &Matrix3::column)
        .addFunction("set", &Matrix3::set)
        .addFunction("get", &Matrix3::get)
        .addFunction("setRow", &Matrix3::setRow)
        .addFunction("setColumn", &Matrix3::setColumn)
        .addFunction("isValid", &Matrix3::isValid)
        .addFunction("identity", &Matrix3::identity)
        .addFunction("calcInverse", &Matrix3::calcInverse)
        .addFunction("inverse", &Matrix3::inverse)
        .addFunction("transpose", &Matrix3::transpose)
        .addStaticFunction("rotateMatrix", 
            luabridge::overload<const float, const Vector3&>(&Matrix3::rotateMatrix),
            luabridge::overload<const float, const float>(&Matrix3::rotateMatrix))
        .addStaticFunction("rotateXMatrix", &Matrix3::rotateXMatrix)
        .addStaticFunction("rotateYMatrix", &Matrix3::rotateYMatrix)
        .addStaticFunction("rotateZMatrix", &Matrix3::rotateZMatrix)
        .addStaticFunction("scaleMatrix", 
            luabridge::overload<const Vector3&>(&Matrix3::scaleMatrix),
            luabridge::overload<const float>(&Matrix3::scaleMatrix))
        .addFunction("decomposeQDU", &Matrix3::decomposeQDU)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Matrix4>("Matrix4")
        .addConstructor<
            void(), 
            void(
                float, float, float, float,
                float, float, float, float,
                float, float, float, float,
                float, float, float, float)>()
        .addFunction("__tostring", &Matrix4::toString)
        .addFunction("__eq", &Matrix4::operator==)
        .addFunction("__sub", (Matrix4 (Matrix4::*)(const Matrix4&) const)&Matrix4::operator-)
        .addFunction("__add", (Matrix4 (Matrix4::*)(const Matrix4&) const)&Matrix4::operator+)
        .addFunction("__mul", (Matrix4 (Matrix4::*)(const Matrix4&) const)&Matrix4::operator*)
        .addFunction("row", &Matrix4::row)
        .addFunction("column", &Matrix4::column)
        .addFunction("set", &Matrix4::set)
        .addFunction("get", &Matrix4::get)
        .addFunction("setRow", &Matrix4::setRow)
        .addFunction("setColumn", &Matrix4::setColumn)
        .addFunction("isValid", &Matrix4::isValid)
        .addFunction("identity", &Matrix4::identity)
        .addFunction("translateInPlace", &Matrix4::translateInPlace)
        .addFunction("linear", &Matrix4::linear)
        .addFunction("inverse", &Matrix4::inverse)
        .addFunction("transpose", &Matrix4::transpose)
        .addFunction("determinant", &Matrix4::determinant)
        .addStaticFunction("translateMatrix", 
            luabridge::overload<const Vector3&>(&Matrix4::translateMatrix),
            luabridge::overload<const float, const float, const float>(&Matrix4::translateMatrix))
        .addStaticFunction("rotateMatrix", 
            luabridge::overload<const float, const Vector3&>(&Matrix4::rotateMatrix),
            luabridge::overload<const float, const float>(&Matrix4::rotateMatrix))
        .addStaticFunction("rotateXMatrix", &Matrix4::rotateXMatrix)
        .addStaticFunction("rotateYMatrix", &Matrix4::rotateYMatrix)
        .addStaticFunction("rotateZMatrix", &Matrix4::rotateZMatrix)
        .addStaticFunction("scaleMatrix", 
            luabridge::overload<const Vector3&>(&Matrix4::scaleMatrix),
            luabridge::overload<const float>(&Matrix4::scaleMatrix))
        .addStaticFunction("lookAtMatrix", &Matrix4::lookAtMatrix)
        .addStaticFunction("frustumMatrix", &Matrix4::frustumMatrix)
        .addStaticFunction("orthoMatrix", &Matrix4::orthoMatrix)
        .addStaticFunction("perspectiveMatrix", &Matrix4::perspectiveMatrix)
        .addFunction("decomposeStandard", &Matrix4::decomposeStandard)
        .addFunction("decomposeQDU", &Matrix4::decomposeQDU)
        .addFunction("decompose", &Matrix4::decompose)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Quaternion>("Quaternion")
        .addConstructor<
            void(), 
            void(const float, const float, const float, const float), 
            void(const float, const float, const float),
            void(const float, const float, const float, const RotationOrder&),
            void(const Vector3*),
            void(const Vector3&, const Vector3&, const Vector3&),
            void(const float, const Vector3&),
            void(const Matrix3&),
            void(const Matrix4&)>()
        .addStaticProperty("IDENTITY", &Quaternion::IDENTITY)
        .addFunction("__tostring", &Quaternion::toString)
        .addFunction("__eq", &Quaternion::operator==)
        .addFunction("__sub", (Quaternion (Quaternion::*)(const Quaternion&) const)&Quaternion::operator-)
        .addFunction("__add", (Quaternion (Quaternion::*)(const Quaternion&) const)&Quaternion::operator+)
        .addFunction("__mul", (Quaternion (Quaternion::*)(const Quaternion&) const)&Quaternion::operator*)
        .addFunction("__unm", (Quaternion (Quaternion::*)() const)&Quaternion::operator-)
        .addFunction("fromEulerAngles", &Quaternion::fromEulerAngles)
        .addFunction("fromAxes", 
            luabridge::overload<const Vector3*>(&Quaternion::fromAxes),
            luabridge::overload<const Vector3&, const Vector3&, const Vector3&>(&Quaternion::fromAxes))
        .addFunction("fromRotationMatrix", 
            luabridge::overload<const Matrix3&>(&Quaternion::fromRotationMatrix),
            luabridge::overload<const Matrix4&>(&Quaternion::fromRotationMatrix))
        .addFunction("getRotationMatrix", &Quaternion::getRotationMatrix)
        .addFunction("fromAngle", &Quaternion::fromAngle)
        .addFunction("fromAngleAxis", &Quaternion::fromAngleAxis)
        .addFunction("getEulerAngles", &Quaternion::getEulerAngles)
        .addFunction("xAxis", &Quaternion::xAxis)
        .addFunction("yAxis", &Quaternion::yAxis)
        .addFunction("zAxis", &Quaternion::zAxis)
        .addFunction("dot", &Quaternion::dot)
        .addFunction("norm", &Quaternion::norm)
        .addFunction("inverse", &Quaternion::inverse)
        .addFunction("unitInverse", &Quaternion::unitInverse)
        .addFunction("exp", &Quaternion::exp)
        .addFunction("log", &Quaternion::log)
        .addStaticFunction("slerp", 
            luabridge::overload<float, const Quaternion&, const Quaternion&>(&Quaternion::slerp),
            luabridge::overload<float, const Quaternion&, const Quaternion&, bool>(&Quaternion::slerp))
        .addStaticFunction("slerpExtraSpins", &Quaternion::slerpExtraSpins)
        .addStaticFunction("nlerp", 
            luabridge::overload<float, const Quaternion&, const Quaternion&>(&Quaternion::nlerp),
            luabridge::overload<float, const Quaternion&, const Quaternion&, bool>(&Quaternion::nlerp))
        .addStaticFunction("squad", &Quaternion::squad)
        .addFunction("normalize", &Quaternion::normalize)
        .addFunction("normalized", &Quaternion::normalized)
        .addFunction("normalizeL", &Quaternion::normalizeL)
        .addFunction("getRoll", &Quaternion::getRoll)
        .addFunction("getPitch", &Quaternion::getPitch)
        .addFunction("getYaw", &Quaternion::getYaw)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Plane>("Plane")
        .addConstructor<
            void(), 
            void(const Vector3&, float), 
            void(float, float, float, float), 
            void(const Vector3&, const Vector3&), 
            void(const Vector3&, const Vector3&, const Vector3&)>()
        .addFunction("__unm", (Plane (Plane::*)() const)&Plane::operator-)
        .addFunction("__eq", &Plane::operator==)
        .addFunction("getSide", 
            luabridge::overload<const Vector3&>(&Plane::getSide),
            luabridge::overload<const Vector3&, const Vector3&>(&Plane::getSide),
            luabridge::overload<const AABB&>(&Plane::getSide),
            luabridge::overload<const OBB&>(&Plane::getSide))
        .addFunction("getDistance", &Plane::getDistance)
        .addFunction("redefine", 
            luabridge::overload<const Vector3&, const Vector3&, const Vector3&>(&Plane::redefine),
            luabridge::overload<const Vector3&, const Vector3&>(&Plane::redefine))
        .addFunction("projectVector", &Plane::projectVector)
        .addFunction("normalize", &Plane::normalize)
        .addFunction("normalized", &Plane::normalized)
        .addStaticProperty("Side", [](lua_State* L) {
            auto table = luabridge::newTable(L);
            table.push(L);
            luabridge::getNamespaceFromStack(L)
                .addVariable("NO_SIDE", Plane::Side::NO_SIDE)
                .addVariable("POSITIVE_SIDE", Plane::Side::POSITIVE_SIDE)
                .addVariable("NEGATIVE_SIDE", Plane::Side::NEGATIVE_SIDE)
                .addVariable("BOTH_SIDE", Plane::Side::BOTH_SIDE);
            table.pop();
            return table;
        })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AABB>("AABB")
        .addConstructor<
            void(), 
            void(AABB::BoxType), 
            void(const Vector3&, const Vector3&), 
            void(float, float, float, float, float, float)>()
        .addStaticProperty("ZERO", &AABB::ZERO)
        .addProperty("minimum", (const Vector3&(AABB::*)() const)&AABB::getMinimum, (void(AABB::*)(const Vector3&))&AABB::setMinimum)
        .addFunction("setMinimum", (void(AABB::*)(float, float, float))&AABB::setMinimum)
        .addFunction("setMinimumX", &AABB::setMinimumX)
        .addFunction("setMinimumY", &AABB::setMinimumY)
        .addFunction("setMinimumZ", &AABB::setMinimumZ)
        .addProperty("maximum", (const Vector3&(AABB::*)() const)&AABB::getMaximum, (void(AABB::*)(const Vector3&))&AABB::setMaximum)
        .addFunction("setMaximum", (void(AABB::*)(float, float, float))&AABB::setMaximum)
        .addFunction("setMaximumX", &AABB::setMaximumX)
        .addFunction("setMaximumY", &AABB::setMaximumY)
        .addFunction("setMaximumZ", &AABB::setMaximumZ)
        .addFunction("setExtents", 
            luabridge::overload<const Vector3&, const Vector3&>(&AABB::setExtents),
            luabridge::overload<float, float, float, float, float, float>(&AABB::setExtents))
        .addFunction("getCorner", &AABB::getCorner)
        .addFunction("getCorners", &AABB::getCorners)
        .addFunction("merge", 
            luabridge::overload<const AABB&>(&AABB::merge),
            luabridge::overload<const Vector3&>(&AABB::merge))
        .addFunction("transform", &AABB::transform)
        .addFunction("setNull", &AABB::setNull)
        .addFunction("isNull", &AABB::isNull)
        .addFunction("setInfinite", &AABB::setInfinite)
        .addFunction("isInfinite", &AABB::isInfinite)
        .addFunction("setFinite", &AABB::setFinite)
        .addFunction("isFinite", &AABB::isFinite)
        .addFunction("intersects", 
            luabridge::overload<const AABB&>(&AABB::intersects),
            luabridge::overload<const OBB&>(&AABB::intersects),
            luabridge::overload<const Plane&>(&AABB::intersects),
            luabridge::overload<const Sphere&>(&AABB::intersects),
            luabridge::overload<const Vector3&>(&AABB::intersects))
        .addFunction("intersection", &AABB::intersection)
        .addFunction("volume", &AABB::volume)
        .addFunction("scale", &AABB::scale)
        .addFunction("getCenter", &AABB::getCenter)
        .addFunction("getSize", &AABB::getSize)
        .addFunction("getHalfSize", &AABB::getHalfSize)
        .addFunction("contains", 
            luabridge::overload<const AABB&>(&AABB::contains),
            luabridge::overload<const Vector3&>(&AABB::contains))
        .addFunction("squaredDistance", &AABB::squaredDistance)
        .addFunction("distance", &AABB::distance)
        .addStaticProperty("BoxType", [](lua_State* L) {
            auto table = luabridge::newTable(L);
            table.push(L);
            luabridge::getNamespaceFromStack(L)
                .addVariable("BOXTYPE_NULL", AABB::BoxType::BOXTYPE_NULL)
                .addVariable("BOXTYPE_FINITE", AABB::BoxType::BOXTYPE_FINITE)
                .addVariable("BOXTYPE_INFINITE", AABB::BoxType::BOXTYPE_INFINITE);
            table.pop();
            return table;
            })
        .addStaticProperty("CornerEnum", [](lua_State* L) {
            auto table = luabridge::newTable(L);
            table.push(L);
            luabridge::getNamespaceFromStack(L)
                .addVariable("FAR_LEFT_BOTTOM", AABB::CornerEnum::FAR_LEFT_BOTTOM)
                .addVariable("FAR_LEFT_TOP", AABB::CornerEnum::FAR_LEFT_TOP)
                .addVariable("FAR_RIGHT_TOP", AABB::CornerEnum::FAR_RIGHT_TOP)
                .addVariable("FAR_RIGHT_BOTTOM", AABB::CornerEnum::FAR_RIGHT_BOTTOM)
                .addVariable("NEAR_RIGHT_BOTTOM", AABB::CornerEnum::NEAR_RIGHT_BOTTOM)
                .addVariable("NEAR_LEFT_BOTTOM", AABB::CornerEnum::NEAR_LEFT_BOTTOM)
                .addVariable("NEAR_LEFT_TOP", AABB::CornerEnum::NEAR_LEFT_TOP)
                .addVariable("NEAR_RIGHT_TOP", AABB::CornerEnum::NEAR_RIGHT_TOP);
            table.pop();
            return table;
        })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<OBB>("OBB")
        .addConstructor<
            void(), 
            void(OBB::BoxType), 
            void(const Vector3&, const Vector3&),
            void(const Vector3&, const Vector3&, const Vector3&, const Vector3&, const Vector3&)>()
        .addFunction("toString", &OBB::toString)
        .addFunction("setNull", &OBB::setNull)
        .addFunction("isNull", &OBB::isNull)
        .addFunction("setInfinite", &OBB::setInfinite)
        .addFunction("isInfinite", &OBB::isInfinite)
        .addFunction("setFinite", &OBB::setFinite)
        .addFunction("isFinite", &OBB::isFinite)
        .addProperty("center", &OBB::getCenter, &OBB::setCenter)
        .addProperty("halfExtents", &OBB::getHalfExtents, &OBB::setHalfExtents)
        .addFunction("getAxisX", &OBB::getAxisX)
        .addFunction("getAxisY", &OBB::getAxisY)
        .addFunction("getAxisZ", &OBB::getAxisZ)
        .addFunction("setAxes", 
            luabridge::overload<const Quaternion&>(&OBB::setAxes),
            luabridge::overload<const Vector3&, const Vector3&, const Vector3&>(&OBB::setAxes))
        .addProperty("orientation", &OBB::getOrientation, &OBB::setOrientation)
        .addFunction("transform", 
            luabridge::overload<const Matrix4&>(&OBB::transform),
            luabridge::overload<const Vector3&, const Quaternion&, const Vector3&>(&OBB::transform))
        .addFunction("toAABB", &OBB::toAABB)
        .addFunction("toMatrix", &OBB::toMatrix)
        .addFunction("getCorner", &OBB::getCorner)
        .addFunction("getCorners", &OBB::getCorners)
        .addFunction("enclose", 
            luabridge::overload<const Vector3&>(&OBB::enclose),
            luabridge::overload<const OBB&>(&OBB::enclose))
        .addFunction("intersects", 
            luabridge::overload<const OBB&>(&OBB::intersects),
            luabridge::overload<const AABB&>(&OBB::intersects),
            luabridge::overload<const Sphere&>(&OBB::intersects),
            luabridge::overload<const Plane&>(&OBB::intersects))
        .addFunction("contains", 
            luabridge::overload<const Vector3&>(&OBB::contains),
            luabridge::overload<const OBB&>(&OBB::contains))
        .addFunction("squaredDistance", &OBB::squaredDistance)
        .addFunction("distance", &OBB::distance)
        .addFunction("volume", &OBB::volume)
        .addFunction("closestPoint", &OBB::closestPoint)
        .addStaticProperty("BoxType", [](lua_State* L) {
            auto table = luabridge::newTable(L);
            table.push(L);
            luabridge::getNamespaceFromStack(L)
                .addVariable("BOXTYPE_NULL", OBB::BoxType::BOXTYPE_NULL)
                .addVariable("BOXTYPE_FINITE", OBB::BoxType::BOXTYPE_FINITE)
                .addVariable("BOXTYPE_INFINITE", OBB::BoxType::BOXTYPE_INFINITE);
            table.pop();
            return table;
            })
        .addStaticProperty("CornerEnum", [](lua_State* L) {
            auto table = luabridge::newTable(L);
            table.push(L);
            luabridge::getNamespaceFromStack(L)
                .addVariable("FAR_LEFT_BOTTOM", OBB::CornerEnum::FAR_LEFT_BOTTOM)
                .addVariable("FAR_LEFT_TOP", OBB::CornerEnum::FAR_LEFT_TOP)
                .addVariable("FAR_RIGHT_TOP", OBB::CornerEnum::FAR_RIGHT_TOP)
                .addVariable("FAR_RIGHT_BOTTOM", OBB::CornerEnum::FAR_RIGHT_BOTTOM)
                .addVariable("NEAR_RIGHT_BOTTOM", OBB::CornerEnum::NEAR_RIGHT_BOTTOM)
                .addVariable("NEAR_LEFT_BOTTOM", OBB::CornerEnum::NEAR_LEFT_BOTTOM)
                .addVariable("NEAR_LEFT_TOP", OBB::CornerEnum::NEAR_LEFT_TOP)
                .addVariable("NEAR_RIGHT_TOP", OBB::CornerEnum::NEAR_RIGHT_TOP);
            table.pop();
            return table;
        })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("RayFilter")
        .addVariable("BODY_2D", RayFilter::BODY_2D)
        .addVariable("BODY_3D", RayFilter::BODY_3D)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<RayReturn>("RayReturn")
        .addProperty("hit", &RayReturn::hit)
        .addProperty("distance", &RayReturn::distance)
        .addProperty("point", &RayReturn::point)
        .addProperty("normal", &RayReturn::normal)
        .addProperty("body", &RayReturn::body)
        .addProperty("shapeIndex", &RayReturn::shapeIndex)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Ray>("Ray")
        .addConstructor<void(), void(Vector3, Vector3)>()
        .addStaticProperty("NO_HIT", &Ray::NO_HIT)
        .addProperty("origin", &Ray::getOrigin, &Ray::setOrigin)
        .addProperty("direction", &Ray::getDirection, &Ray::setDirection)
        .addFunction("getPoint", &Ray::getPoint)
        .addFunction("intersects", 
            luabridge::overload<const AABB&>(&Ray::intersects),
            luabridge::overload<const OBB&>(&Ray::intersects),
            luabridge::overload<const Plane&>(&Ray::intersects),
            luabridge::overload<const Body2D&>(&Ray::intersects),
            luabridge::overload<const Body2D&, size_t>(&Ray::intersects),
            luabridge::overload<const Body3D&>(&Ray::intersects),
            luabridge::overload<const Body3D&, size_t>(&Ray::intersects),
            luabridge::overload<Scene*, RayFilter>(&Ray::intersects),
            luabridge::overload<Scene*, RayFilter, bool>(&Ray::intersects),
            luabridge::overload<Scene*, RayFilter, uint16_t, uint16_t>(&Ray::intersects),
            luabridge::overload<Scene*, RayFilter, bool, uint16_t, uint16_t>(&Ray::intersects),
            luabridge::overload<Scene*, uint8_t>(&Ray::intersects),
            luabridge::overload<Scene*, uint8_t, uint16_t, uint16_t>(&Ray::intersects))
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}