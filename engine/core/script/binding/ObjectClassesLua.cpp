// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "EntityHandle.h"
#include "Fog.h"
#include "ReflectionProbe.h"
#include "SkyBox.h"
#include "Object.h"
#include "Camera.h"
#include "Polygon.h"
#include "Terrain.h"
#include "Light.h"
#include "Light2D.h"
#include "Occluder2D.h"
#include "Mesh.h"
#include "Shape.h"
#include "Mirror.h"
#include "Tilemap.h"
#include "Bone.h"
#include "Model.h"
#include "MeshPolygon.h"
#include "Points.h"
#include "Lines.h"
#include "Sprite.h"
#include "Text.h"
#include "Image.h"
#include "Button.h"
#include "Panel.h"
#include "Scrollbar.h"
#include "Progressbar.h"
#include "TextEdit.h"
#include "Container.h"
#include "Sound.h"
#ifdef DORIAX_PHYSICS_2D
#include "Body2D.h"
#include "Joint2D.h"
#include "Contact2D.h"
#include "Manifold2D.h"
#endif
#ifdef DORIAX_PHYSICS_3D
#include "Body3D.h"
#include "Joint3D.h"
#include "Contact3D.h"
#include "CollideShapeResult3D.h"
#endif

using namespace doriax;


void LuaBinding::registerObjectClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("CameraType")
        .addVariable("CAMERA_UI", CameraType::CAMERA_UI)
        .addVariable("CAMERA_ORTHO", CameraType::CAMERA_ORTHO)
        .addVariable("CAMERA_PERSPECTIVE", CameraType::CAMERA_PERSPECTIVE)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("FrustumPlane")
        .addVariable("FRUSTUM_PLANE_NEAR", FrustumPlane::FRUSTUM_PLANE_NEAR)
        .addVariable("FRUSTUM_PLANE_FAR", FrustumPlane::FRUSTUM_PLANE_FAR)
        .addVariable("FRUSTUM_PLANE_LEFT", FrustumPlane::FRUSTUM_PLANE_LEFT)
        .addVariable("FRUSTUM_PLANE_RIGHT", FrustumPlane::FRUSTUM_PLANE_RIGHT)
        .addVariable("FRUSTUM_PLANE_TOP", FrustumPlane::FRUSTUM_PLANE_TOP)
        .addVariable("FRUSTUM_PLANE_BOTTOM", FrustumPlane::FRUSTUM_PLANE_BOTTOM)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("LightType")
        .addVariable("DIRECTIONAL", LightType::DIRECTIONAL)
        .addVariable("POINT", LightType::POINT)
        .addVariable("SPOT", LightType::SPOT)
        .endNamespace();

#ifdef DORIAX_PHYSICS_2D
    luabridge::getGlobalNamespace(L)
        .beginNamespace("Shape2DType")
        .addVariable("POLYGON", Shape2DType::POLYGON)
        .addVariable("CIRCLE", Shape2DType::CIRCLE)
        .addVariable("CAPSULE", Shape2DType::CAPSULE)
        .addVariable("SEGMENT", Shape2DType::SEGMENT)
        .addVariable("CHAIN", Shape2DType::CHAIN)
        .endNamespace();
#endif

#ifdef DORIAX_PHYSICS_3D
    luabridge::getGlobalNamespace(L)
        .beginNamespace("Shape3DType")
        .addVariable("SPHERE", Shape3DType::SPHERE)
        .addVariable("BOX", Shape3DType::BOX)
        .addVariable("CAPSULE", Shape3DType::CAPSULE)
        .addVariable("TAPERED_CAPSULE", Shape3DType::TAPERED_CAPSULE)
        .addVariable("CYLINDER", Shape3DType::CYLINDER)
        .addVariable("CONVEX_HULL", Shape3DType::CONVEX_HULL)
        .addVariable("MESH", Shape3DType::MESH)
        .addVariable("HEIGHTFIELD", Shape3DType::HEIGHTFIELD)
        .endNamespace();
#endif

#if defined(DORIAX_PHYSICS_2D) || defined(DORIAX_PHYSICS_3D)
    luabridge::getGlobalNamespace(L)
        .beginNamespace("BodyType")
        .addVariable("STATIC", BodyType::STATIC)
        .addVariable("KINEMATIC", BodyType::KINEMATIC)
        .addVariable("DYNAMIC", BodyType::DYNAMIC)
        .endNamespace();
#endif

#ifdef DORIAX_PHYSICS_3D
    luabridge::getGlobalNamespace(L)
        .beginNamespace("Body3DMotionQuality")
        .addVariable("DISCRETE", Body3DMotionQuality::DISCRETE)
        .addVariable("LINEAR_CAST", Body3DMotionQuality::LINEAR_CAST)
        .endNamespace();
#endif

#ifdef DORIAX_PHYSICS_2D
    luabridge::getGlobalNamespace(L)
        .beginNamespace("Joint2DType")
        .addVariable("DISTANCE", Joint2DType::DISTANCE)
        .addVariable("REVOLUTE", Joint2DType::REVOLUTE)
        .addVariable("PRISMATIC", Joint2DType::PRISMATIC)
        //.addVariable("PULLEY", Joint2DType::PULLEY)
        //.addVariable("GEAR", Joint2DType::GEAR)
        .addVariable("MOUSE", Joint2DType::MOUSE)
        .addVariable("WHEEL", Joint2DType::WHEEL)
        .addVariable("WELD", Joint2DType::WELD)
        .addVariable("MOTOR", Joint2DType::MOTOR)
        .endNamespace();
#endif

#ifdef DORIAX_PHYSICS_3D
    luabridge::getGlobalNamespace(L)
        .beginNamespace("Joint3DType")
        .addVariable("FIXED", Joint3DType::FIXED)
        .addVariable("DISTANCE", Joint3DType::DISTANCE)
        .addVariable("POINT", Joint3DType::POINT)
        .addVariable("HINGE", Joint3DType::HINGE)
        .addVariable("CONE", Joint3DType::CONE)
        .addVariable("PRISMATIC", Joint3DType::PRISMATIC)
        .addVariable("SWINGTWIST", Joint3DType::SWINGTWIST)
        .addVariable("SIXDOF", Joint3DType::SIXDOF)
        .addVariable("PATH", Joint3DType::PATH)
        .addVariable("GEAR", Joint3DType::GEAR)
        .addVariable("RACKANDPINON", Joint3DType::RACKANDPINON)
        .addVariable("PULLEY", Joint3DType::PULLEY)
        .endNamespace();
#endif

    luabridge::getGlobalNamespace(L)
        .beginClass<EntityHandle>("EntityHandle")
        .addConstructor <void (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("scene", &EntityHandle::getScene)
        .addProperty("entity", &EntityHandle::getEntity)
        .addProperty("name", &EntityHandle::getName, &EntityHandle::setName)
        .addProperty("entityOwned", &EntityHandle::isEntityOwned, &EntityHandle::setEntityOwned)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Fog, EntityHandle>("Fog")
        .addConstructor <void (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("type", &Fog::getType, &Fog::setType)
        .addProperty("color", &Fog::getColor, (void(Fog::*)(Vector3))&Fog::setColor)
        .addFunction("setColor", (void(Fog::*)(const float, const float, const float))&Fog::setColor)
        .addProperty("density", &Fog::getDensity, &Fog::setDensity)
        .addProperty("linearStart", &Fog::getLinearStart, &Fog::setLinearStart)
        .addProperty("linearEnd", &Fog::getLinearEnd, &Fog::setLinearEnd)
        .addFunction("setLinearStartEnd", &Fog::setLinearStartEnd)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<SkyBox, EntityHandle>("SkyBox")
        .addConstructor <void (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setTextures", 
            luabridge::overload<const std::string&,const std::string&,const std::string&,const std::string&,const std::string&,const std::string&>(&SkyBox::setTextures),
            luabridge::overload<const std::string&,TextureData,TextureData,TextureData,TextureData,TextureData,TextureData>(&SkyBox::setTextures))
        .addFunction("setTexture", &SkyBox::setTexture)
        .addFunction("setTexturePositiveX", &SkyBox::setTexturePositiveX)
        .addFunction("setTextureNegativeX", &SkyBox::setTextureNegativeX)
        .addFunction("setTexturePositiveY", &SkyBox::setTexturePositiveY)
        .addFunction("setTextureNegativeY", &SkyBox::setTextureNegativeY)
        .addFunction("setTexturePositiveZ", &SkyBox::setTexturePositiveZ)
        .addFunction("setTextureNegativeZ", &SkyBox::setTextureNegativeZ)
        .addProperty("color", &SkyBox::getColor, (void(SkyBox::*)(Vector4))&SkyBox::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&SkyBox::setColor),
            luabridge::overload<const float, const float, const float, const float>(&SkyBox::setColor))
        .addProperty("alpha", &SkyBox::getAlpha, &SkyBox::setAlpha)
        .addProperty("rotation", &SkyBox::getRotation, &SkyBox::setRotation)
        .addProperty("customShader", &SkyBox::getCustomShader, &SkyBox::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&SkyBox::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&SkyBox::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&SkyBox::setShaderUniform),
            luabridge::overload<const std::string&, float>(&SkyBox::setShaderUniform))
        .addFunction("getShaderUniform", &SkyBox::getShaderUniform)
        .addFunction("removeShaderUniform", &SkyBox::removeShaderUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Object, EntityHandle>("Object")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("addChild", 
            luabridge::overload<Object*>(&Object::addChild),
            luabridge::overload<Entity>(&Object::addChild))
        .addFunction("removeChild", 
            luabridge::overload<Object*>(&Object::removeChild),
            luabridge::overload<Entity>(&Object::removeChild))
        .addFunction("removeParent", &Object::removeParent)
        .addFunction("moveToTop", &Object::moveToTop)
        .addFunction("moveUp", &Object::moveUp)
        .addFunction("moveDown", &Object::moveDown)
        .addFunction("moveToBottom", &Object::moveToBottom)
        .addProperty("position", &Object::getPosition, (void(Object::*)(Vector3))&Object::setPosition)
        .addFunction("setPosition", 
            luabridge::overload<const float, const float, const float>(&Object::setPosition),
            luabridge::overload<const float, const float>(&Object::setPosition))
        .addFunction("getWorldPosition", &Object::getWorldPosition)
        .addProperty("rotation", &Object::getRotation, (void(Object::*)(Quaternion))&Object::setRotation)
        .addFunction("setRotation", (void(Object::*)(const float, const float, const float))&Object::setRotation)
        .addFunction("getWorldRotation", &Object::getWorldRotation)
        .addProperty("scale", &Object::getScale, (void(Object::*)(Vector3))&Object::setScale)
        .addFunction("setScale", (void(Object::*)(const float))&Object::setScale)
        .addFunction("getWorldScale", &Object::getWorldScale)
        .addProperty("visible", &Object::isVisible, &Object::setVisible)
        .addFunction("setVisibleOnly", &Object::setVisibleOnly)
        .addFunction("setBillboard", (void(Object::*)(bool, bool, bool))&Object::setBillboard)
        .addProperty("billboard", &Object::isBillboard, (void(Object::*)(bool))&Object::setBillboard)
        .addProperty("billboardRotation", &Object::getBillboardRotation, (void(Object::*)(Quaternion))&Object::setBillboardRotation)
        .addFunction("setBillboardRotation", (void(Object::*)(const float, const float, const float))&Object::setBillboardRotation)
        .addProperty("fakeBillboard", &Object::isFakeBillboard, &Object::setFakeBillboard)
        .addProperty("cylindricalBillboard", &Object::isCylindricalBillboard, &Object::setCylindricalBillboard)
        .addProperty("localMatrix", &Object::getLocalMatrix, &Object::setLocalMatrix)
        .addProperty("modelMatrix", &Object::getModelMatrix)
        .addProperty("normalMatrix", &Object::getNormalMatrix)
        .addFunction("updateTransform", &Object::updateTransform)
#ifdef DORIAX_PHYSICS_2D
        .addFunction("getBody2D", &Object::getBody2D)
        .addFunction("removeBody2D", &Object::removeBody2D)
#endif
#ifdef DORIAX_PHYSICS_3D
        .addFunction("getBody3D", &Object::getBody3D)
        .addFunction("removeBody3D", &Object::removeBody3D)
#endif
        .addFunction("getRay", &Object::getRay)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Camera, Object>("Camera")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("activate", &Camera::activate)
        .addFunction("setOrtho", &Camera::setOrtho)
        .addFunction("setPerspective", &Camera::setPerspective)
        .addProperty("autoResize",  &Camera::isAutoResize, &Camera::setAutoResize)
        .addProperty("nearClip",  &Camera::getNearClip, &Camera::setNearClip)
        .addProperty("farClip",  &Camera::getFarClip, &Camera::setFarClip)
        .addProperty("leftClip",  &Camera::getLeftClip, &Camera::setLeftClip)
        .addProperty("rightClip",  &Camera::getRightClip, &Camera::setRightClip)
        .addProperty("bottomClip",  &Camera::getBottomClip, &Camera::setBottomClip)
        .addProperty("topClip",  &Camera::getTopClip, &Camera::setTopClip)
        .addProperty("aspect",  &Camera::getAspect, &Camera::setAspect)
        .addProperty("yfov",  &Camera::getYFov, &Camera::setYFov)
        .addProperty("type",  &Camera::getType, &Camera::setType)
        .addFunction("setType", &Camera::setType)
        .addProperty("target",  &Camera::getTarget, (void(Camera::*)(Vector3))&Camera::setTarget)
        .addFunction("setTarget", (void(Camera::*)(const float, const float, const float))&Camera::setTarget)
        .addFunction("disableTarget", &Camera::disableTarget)
        .addFunction("isUsingTarget", &Camera::isUsingTarget)
        .addProperty("up",  &Camera::getUp, (void(Camera::*)(Vector3))&Camera::setUp)
        .addFunction("setUp", (void(Camera::*)(const float, const float, const float))&Camera::setUp)
        .addFunction("getUp", &Camera::getUp)
        .addFunction("getDirection", &Camera::getDirection)
        .addFunction("getRight", &Camera::getRight)
        .addFunction("getWorldTarget", &Camera::getWorldTarget)
        .addFunction("getWorldDirection", &Camera::getWorldDirection)
        .addFunction("getWorldUp", &Camera::getWorldUp)
        .addFunction("getWorldRight", &Camera::getWorldRight)
        .addFunction("getViewMatrix", &Camera::getViewMatrix)
        .addFunction("getProjectionMatrix", &Camera::getProjectionMatrix)
        .addFunction("getViewProjectionMatrix", &Camera::getViewProjectionMatrix)
        .addFunction("rotateView", &Camera::rotateView)
        .addFunction("rotatePosition", &Camera::rotatePosition)
        .addFunction("elevateView", &Camera::elevateView)
        .addFunction("elevatePosition", &Camera::elevatePosition)
        .addFunction("walkForward", &Camera::walkForward)
        .addFunction("zoom", &Camera::zoom)
        .addFunction("slide", &Camera::slide)
        .addFunction("slideForward", &Camera::slideForward)
        .addFunction("slideUp", &Camera::slideUp)
        .addProperty("renderToTexture", &Camera::isRenderToTexture, &Camera::setRenderToTexture)
        .addFunction("getFramebuffer", &Camera::getFramebuffer)
        .addFunction("setFramebufferSize", &Camera::setFramebufferSize)
        .addFunction("setFramebufferFilter", &Camera::setFramebufferFilter)
        .addProperty("transparentSort", &Camera::isTransparentSort, &Camera::setTransparentSort)
        .addProperty("depthTest", &Camera::isDepthTest, &Camera::setDepthTest)
        .addFunction("screenToRay", &Camera::screenToRay)
        .addFunction("getDistanceFromTarget", &Camera::getDistanceFromTarget)
        .addFunction("updateCamera", &Camera::updateCamera)
        .endClass();


    luabridge::getGlobalNamespace(L)
        .deriveClass<Light, Object>("Light")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("type", &Light::getType, &Light::setType)
        .addFunction("setType", &Light::setType)
        .addProperty("direction", &Light::getDirection, (void(Light::*)(Vector3))&Light::setDirection)
        .addFunction("setDirection", (void(Light::*)(const float, const float, const float))&Light::setDirection)
        .addProperty("color", &Light::getColor, (void(Light::*)(Vector3))&Light::setColor)
        .addFunction("setColor", (void(Light::*)(const float, const float, const float))&Light::setColor)
        .addProperty("range", &Light::getRange, &Light::setRange)
        .addFunction("setRange", &Light::setRange)
        .addProperty("intensity", &Light::getIntensity, &Light::setIntensity)
        .addFunction("setIntensity", &Light::setIntensity)
        .addFunction("setConeAngle", &Light::setConeAngle)
        .addProperty("innerConeAngle", &Light::getInnerConeAngle, &Light::setInnerConeAngle)
        .addFunction("setInnerConeAngle", &Light::setInnerConeAngle)
        .addProperty("outerConeAngle", &Light::getOuterConeAngle, &Light::setOuterConeAngle)
        .addFunction("setOuterConeAngle", &Light::setOuterConeAngle)
        .addProperty("spotMask", &Light::getSpotMask)
        .addFunction("setSpotMask",
            luabridge::overload<const std::string&>(&Light::setSpotMask),
            luabridge::overload<const std::string&, TextureData>(&Light::setSpotMask))
        .addFunction("clearSpotMask", &Light::clearSpotMask)
        .addProperty("shadows", &Light::isShadows, &Light::setShadows)
        .addFunction("setShadows", &Light::setShadows)
        .addFunction("setShadowCameraNearFar", &Light::setShadowCameraNearFar)
        .addProperty("cameraNear", &Light::getCameraNear, &Light::setCameraNear)
        .addFunction("setCameraNear", &Light::setCameraNear)
        .addProperty("cameraFar", &Light::getCameraFar, &Light::setCameraFar)
        .addFunction("setCameraFar", &Light::setCameraFar)
        .addProperty("automaticShadowCamera", &Light::isAutomaticShadowCamera, &Light::setAutomaticShadowCamera)
        .addProperty("numCascades", &Light::getNumCascades, &Light::setNumCascades)
        .addFunction("setNumCascades", &Light::setNumCascades)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Light2D, Object>("Light2D")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("color", &Light2D::getColor, (void(Light2D::*)(Vector3))&Light2D::setColor)
        .addFunction("setColor", (void(Light2D::*)(const float, const float, const float))&Light2D::setColor)
        .addProperty("intensity", &Light2D::getIntensity, &Light2D::setIntensity)
        .addFunction("setIntensity", &Light2D::setIntensity)
        .addProperty("range", &Light2D::getRange, &Light2D::setRange)
        .addFunction("setRange", &Light2D::setRange)
        .addProperty("falloff", &Light2D::getFalloff, &Light2D::setFalloff)
        .addFunction("setFalloff", &Light2D::setFalloff)
        .addProperty("height", &Light2D::getHeight, &Light2D::setHeight)
        .addFunction("setHeight", &Light2D::setHeight)
        .addProperty("shadows", &Light2D::isShadows, &Light2D::setShadows)
        .addFunction("setShadows", &Light2D::setShadows)
        .addProperty("shadowBias", &Light2D::getShadowBias, &Light2D::setShadowBias)
        .addProperty("shadowSoftness", &Light2D::getShadowSoftness, &Light2D::setShadowSoftness)
        .addProperty("mapResolution", &Light2D::getMapResolution, &Light2D::setMapResolution)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("Occluder2DShape")
        .addVariable("AUTO_QUAD", Occluder2DShape::AUTO_QUAD)
        .addVariable("POLYGON", Occluder2DShape::POLYGON)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Occluder2D, Object>("Occluder2D")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("shape", &Occluder2D::getShape, &Occluder2D::setShape)
        .addFunction("addVertex", (void(Occluder2D::*)(float, float))&Occluder2D::addVertex)
        .addFunction("clearVertices", &Occluder2D::clearVertices)
        .addFunction("getVertexCount", &Occluder2D::getVertexCount)
        .addProperty("closed", &Occluder2D::isClosed, &Occluder2D::setClosed)
        .addProperty("enabled", &Occluder2D::isEnabled, &Occluder2D::setEnabled)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ReflectionProbeMode")
        .addVariable("STATIC", ReflectionProbeMode::STATIC)
        .addVariable("DYNAMIC", ReflectionProbeMode::DYNAMIC)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ReflectionProbeUpdateMode")
        .addVariable("ON_LOAD", ReflectionProbeUpdateMode::ON_LOAD)
        .addVariable("ON_MOVE", ReflectionProbeUpdateMode::ON_MOVE)
        .addVariable("INTERVAL", ReflectionProbeUpdateMode::INTERVAL)
        .addVariable("MANUAL", ReflectionProbeUpdateMode::MANUAL)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ReflectionProbe, Object>("ReflectionProbe")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("mode", &ReflectionProbe::getMode, &ReflectionProbe::setMode)
        .addProperty("updateMode", &ReflectionProbe::getUpdateMode, &ReflectionProbe::setUpdateMode)
        .addProperty("boxOffset", &ReflectionProbe::getBoxOffset, (void(ReflectionProbe::*)(Vector3))&ReflectionProbe::setBoxOffset)
        .addFunction("setBoxOffset", (void(ReflectionProbe::*)(const float, const float, const float))&ReflectionProbe::setBoxOffset)
        .addProperty("boxSize", &ReflectionProbe::getBoxSize, (void(ReflectionProbe::*)(Vector3))&ReflectionProbe::setBoxSize)
        .addFunction("setBoxSize", (void(ReflectionProbe::*)(const float, const float, const float))&ReflectionProbe::setBoxSize)
        .addProperty("blendDistance", &ReflectionProbe::getBlendDistance, &ReflectionProbe::setBlendDistance)
        .addProperty("intensity", &ReflectionProbe::getIntensity, &ReflectionProbe::setIntensity)
        .addProperty("priority", &ReflectionProbe::getPriority, &ReflectionProbe::setPriority)
        .addProperty("resolution", &ReflectionProbe::getResolution, &ReflectionProbe::setResolution)
        .addFunction("setClipPlanes", &ReflectionProbe::setClipPlanes)
        .addProperty("nearClip", &ReflectionProbe::getNearClip, &ReflectionProbe::setNearClip)
        .addProperty("farClip", &ReflectionProbe::getFarClip, &ReflectionProbe::setFarClip)
        .addProperty("updateInterval", &ReflectionProbe::getUpdateInterval, &ReflectionProbe::setUpdateInterval)
        .addProperty("includeSky", &ReflectionProbe::isIncludeSky, &ReflectionProbe::setIncludeSky)
        .addFunction("setTextures", &ReflectionProbe::setTextures)
        .addFunction("setTexture", &ReflectionProbe::setTexture)
        .addFunction("refresh", &ReflectionProbe::refresh)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Mesh, Object>("Mesh")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setTexture", 
            luabridge::overload<const std::string&>(&Mesh::setTexture),
            luabridge::overload<const std::string&,TextureData>(&Mesh::setTexture),
            luabridge::overload<Framebuffer*>(&Mesh::setTexture))
        .addProperty("color", &Mesh::getColor, (void(Mesh::*)(Vector4))&Mesh::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&Mesh::setColor),
            luabridge::overload<const float, const float, const float, const float>(&Mesh::setColor),
            luabridge::overload<Vector4>(&Mesh::setColor))
        .addProperty("alpha", &Mesh::getAlpha, &Mesh::setAlpha)
        .addProperty("material", (Material(Mesh::*)()const)&Mesh::getMaterial, (void(Mesh::*)(const Material&))&Mesh::setMaterial)
        .addFunction("setMaterial", 
            luabridge::overload<const Material&>(&Mesh::setMaterial),
            luabridge::overload<unsigned int, const Material&>(&Mesh::setMaterial))
        .addFunction("getMaterial", 
            luabridge::overload<>(&Mesh::getMaterial),
            luabridge::overload<unsigned int>(&Mesh::getMaterial))
        .addProperty("primitiveType", (PrimitiveType(Mesh::*)()const)&Mesh::getPrimitiveType, (void(Mesh::*)(PrimitiveType))&Mesh::setPrimitiveType)
        .addFunction("setPrimitiveType", 
            luabridge::overload<PrimitiveType>(&Mesh::setPrimitiveType),
            luabridge::overload<unsigned int, PrimitiveType>(&Mesh::setPrimitiveType))
        .addFunction("getPrimitiveType", 
            luabridge::overload<>(&Mesh::getPrimitiveType),
            luabridge::overload<unsigned int>(&Mesh::getPrimitiveType))
        .addProperty("faceCulling", (bool(Mesh::*)()const)&Mesh::isFaceCulling, (void(Mesh::*)(bool))&Mesh::setFaceCulling)
        .addFunction("setFaceCulling", 
            luabridge::overload<bool>(&Mesh::setFaceCulling),
            luabridge::overload<unsigned int, bool>(&Mesh::setFaceCulling))
        .addFunction("isFaceCulling", 
            luabridge::overload<>(&Mesh::isFaceCulling),
            luabridge::overload<unsigned int>(&Mesh::isFaceCulling))
        .addProperty("castShadowsWithTexture", (bool(Mesh::*)()const)&Mesh::isCastShadowsWithTexture, (void(Mesh::*)(bool))&Mesh::setCastShadowsWithTexture)
        .addFunction("setCastShadowsWithTexture", 
            luabridge::overload<bool>(&Mesh::setCastShadowsWithTexture),
            luabridge::overload<unsigned int, bool>(&Mesh::setCastShadowsWithTexture))
        .addFunction("isCastShadowsWithTexture", 
            luabridge::overload<>(&Mesh::isCastShadowsWithTexture),
            luabridge::overload<unsigned int>(&Mesh::isCastShadowsWithTexture))
        .addProperty("cullingMode", &Mesh::getCullingMode, &Mesh::setCullingMode)
        .addProperty("windingOrder", &Mesh::getWindingOrder, &Mesh::setWindingOrder)
        .addFunction("getAABB", &Mesh::getAABB)
        .addFunction("getVerticesAABB", &Mesh::getVerticesAABB)
        .addFunction("getWorldAABB", &Mesh::getWorldAABB)
        .addFunction("getNumSubmeshes", &Mesh::getNumSubmeshes)
        .addProperty("receiveLights", &Mesh::isReceiveLights, &Mesh::setReceiveLights)
        .addProperty("receiveIBL", &Mesh::isReceiveIBL, &Mesh::setReceiveIBL)
        .addProperty("castShadows", &Mesh::isCastShadows, &Mesh::setCastShadows)
        .addProperty("receiveShadows", &Mesh::isReceiveShadows, &Mesh::setReceiveShadows)
        .addProperty("lodEnabled", &Mesh::isLodEnabled, &Mesh::setLodEnabled)
        .addProperty("lodBias", &Mesh::getLodBias, &Mesh::setLodBias)
        .addProperty("renderInReflectionProbes", &Mesh::isRenderInReflectionProbes, &Mesh::setRenderInReflectionProbes)
        .addProperty("shadowsBillboard", &Mesh::isShadowsBillboard, &Mesh::setShadowsBillboard)
        .addProperty("transparent", &Mesh::isTransparent, &Mesh::setTransparent)
        .addProperty("autoTransparency", &Mesh::isAutoTransparency, &Mesh::setAutoTransparency)
        .addProperty("customShader", &Mesh::getCustomShader, &Mesh::setCustomShader)
        .addProperty("customDepthShader", &Mesh::getCustomDepthShader, &Mesh::setCustomDepthShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Mesh::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Mesh::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Mesh::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Mesh::setShaderUniform))
        .addFunction("getShaderUniform", &Mesh::getShaderUniform)
        .addFunction("removeShaderUniform", &Mesh::removeShaderUniform)
        .addFunction("setAsMirror",
            luabridge::overload<>(&Mesh::setAsMirror),
            luabridge::overload<Vector3>(&Mesh::setAsMirror))
        .addFunction("removeMirror", &Mesh::removeMirror)
        .addFunction("isMirror", &Mesh::isMirror)
        .addFunction("createInstancedMesh", &Mesh::createInstancedMesh)
        .addFunction("removeInstancedMesh", &Mesh::removeInstancedMesh)
        .addFunction("hasInstancedMesh", &Mesh::hasInstancedMesh)
        .addFunction("setInstancedBillboard", (void(Mesh::*)(bool, bool))&Mesh::setInstancedBillboard)
        .addProperty("instancedBillboard", &Mesh::isInstancedBillboard, (void(Mesh::*)(bool))&Mesh::setInstancedBillboard)
        .addProperty("instancedCylindricalBillboard", &Mesh::isInstancedCylindricalBillboard, &Mesh::setInstancedCylindricalBillboard)
        .addProperty("maxInstances", &Mesh::getMaxInstances, &Mesh::setMaxInstances)
        .addProperty("distanceFade", &Mesh::isDistanceFade, &Mesh::setDistanceFade)
        .addFunction("setFadeRange", &Mesh::setFadeRange)
        .addProperty("fadeStart", &Mesh::getFadeStart, &Mesh::setFadeStart)
        .addProperty("fadeEnd", &Mesh::getFadeEnd, &Mesh::setFadeEnd)
        .addProperty("cullDistance", &Mesh::getCullDistance, &Mesh::setCullDistance)
        .addProperty("cullInstances", &Mesh::isCullInstances, &Mesh::setCullInstances)
        .addFunction("addInstance",
            luabridge::overload<InstanceData>(&Mesh::addInstance),
            luabridge::overload<Vector3>(&Mesh::addInstance),
            luabridge::overload<float, float, float>(&Mesh::addInstance),
            luabridge::overload<Vector3, Quaternion, Vector3>(&Mesh::addInstance),
            luabridge::overload<Vector3, Quaternion, Vector3, Vector4>(&Mesh::addInstance),
            luabridge::overload<Vector3, Quaternion, Vector3, Vector4, Rect>(&Mesh::addInstance))
        .addFunction("getInstance", &Mesh::getInstance)
        .addFunction("updateInstance",
            luabridge::overload<size_t, InstanceData>(&Mesh::updateInstance),
            luabridge::overload<size_t, Vector3>(&Mesh::updateInstance),
            luabridge::overload<size_t, float, float, float>(&Mesh::updateInstance),
            luabridge::overload<size_t, Vector3, Quaternion, Vector3>(&Mesh::updateInstance),
            luabridge::overload<size_t, Vector3, Quaternion, Vector3, Vector4>(&Mesh::updateInstance),
            luabridge::overload<size_t, Vector3, Quaternion, Vector3, Vector4, Rect>(&Mesh::updateInstance))
        .addFunction("removeInstance", &Mesh::removeInstance)
        .addFunction("isInstanceVisible", &Mesh::isInstanceVisible)
        .addFunction("setInstanceVisible", &Mesh::setInstanceVisible)
        .addFunction("updateInstances", &Mesh::updateInstances)
        .addFunction("getNumInstances", &Mesh::getNumInstances)
        .addFunction("clearInstances", &Mesh::clearInstances)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Shape, Mesh>("Shape")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("createPlane", 
            luabridge::overload<float, float>(&Shape::createPlane),
            luabridge::overload<float, float, unsigned int>(&Shape::createPlane))
        .addFunction("createWall", 
            luabridge::overload<float, float>(&Shape::createWall),
            luabridge::overload<float, float, unsigned int>(&Shape::createWall))
        .addFunction("createBox", 
            luabridge::overload<float, float, float>(&Shape::createBox),
            luabridge::overload<float, float, float, unsigned int>(&Shape::createBox))
        .addFunction("createSphere", 
            luabridge::overload<float>(&Shape::createSphere),
            luabridge::overload<float, unsigned int, unsigned int>(&Shape::createSphere))
        .addFunction("createCylinder", 
            luabridge::overload<float, float>(&Shape::createCylinder),
            luabridge::overload<float, float, float>(&Shape::createCylinder),
            luabridge::overload<float, float, unsigned int, unsigned int>(&Shape::createCylinder),
            luabridge::overload<float, float, float, unsigned int, unsigned int>(&Shape::createCylinder))
        .addFunction("createCapsule", 
            luabridge::overload<float, float>(&Shape::createCapsule),
            luabridge::overload<float, float, float>(&Shape::createCapsule),
            luabridge::overload<float, float, unsigned int, unsigned int>(&Shape::createCapsule),
            luabridge::overload<float, float, float, unsigned int, unsigned int>(&Shape::createCapsule))
        .addFunction("createTorus", 
            luabridge::overload<float, float>(&Shape::createTorus),
            luabridge::overload<float, float, unsigned int, unsigned int>(&Shape::createTorus))
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Mirror, Shape>("Mirror")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("normal", &Mirror::getNormal, (void(Mirror::*)(Vector3))&Mirror::setNormal)
        .addFunction("setNormal", (void(Mirror::*)(const float, const float, const float))&Mirror::setNormal)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TerrainSurfaceLayer>("TerrainSurfaceLayer")
        .addConstructor <void (*) (void)> ()
        .addProperty("pbr", &TerrainSurfaceLayer::pbr, &TerrainSurfaceLayer::pbr)
        .addProperty("colorTexture", &TerrainSurfaceLayer::colorTexture, &TerrainSurfaceLayer::colorTexture)
        .addProperty("normalTexture", &TerrainSurfaceLayer::normalTexture, &TerrainSurfaceLayer::normalTexture)
        .addProperty("roughnessTexture", &TerrainSurfaceLayer::roughnessTexture, &TerrainSurfaceLayer::roughnessTexture)
        .addProperty("metallicTexture", &TerrainSurfaceLayer::metallicTexture, &TerrainSurfaceLayer::metallicTexture)
        .addProperty("occlusionTexture", &TerrainSurfaceLayer::occlusionTexture, &TerrainSurfaceLayer::occlusionTexture)
        .addProperty("heightTexture", &TerrainSurfaceLayer::heightTexture, &TerrainSurfaceLayer::heightTexture)
        .addProperty("colorFactor", &TerrainSurfaceLayer::colorFactor, &TerrainSurfaceLayer::colorFactor)
        .addProperty("normalStrength", &TerrainSurfaceLayer::normalStrength, &TerrainSurfaceLayer::normalStrength)
        .addProperty("roughnessFactor", &TerrainSurfaceLayer::roughnessFactor, &TerrainSurfaceLayer::roughnessFactor)
        .addProperty("metallicFactor", &TerrainSurfaceLayer::metallicFactor, &TerrainSurfaceLayer::metallicFactor)
        .addProperty("occlusionStrength", &TerrainSurfaceLayer::occlusionStrength, &TerrainSurfaceLayer::occlusionStrength)
        .addProperty("uvScale", &TerrainSurfaceLayer::uvScale, &TerrainSurfaceLayer::uvScale)
        .addProperty("uvOffset", &TerrainSurfaceLayer::uvOffset, &TerrainSurfaceLayer::uvOffset)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Terrain, Mesh>("Terrain")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setHeightMap", (void(Terrain::*)(const std::string&))&Terrain::setHeightMap)
        .addFunction("setBlendMap", (void(Terrain::*)(const std::string&))&Terrain::setBlendMap)
        .addFunction("setBlendMapIndex", (void(Terrain::*)(unsigned int, const std::string&))&Terrain::setBlendMap)
        .addFunction("setTextureLayer", &Terrain::setTextureLayer)
        .addFunction("setTextureDetailRed", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailRed)
        .addFunction("setTextureDetailGreen", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailGreen)
        .addFunction("setTextureDetailBlue", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailBlue)
        .addFunction("setSurfaceLayer", &Terrain::setSurfaceLayer)
        .addFunction("getSurfaceLayer", &Terrain::getSurfaceLayer)
        .addFunction("setLayerFromMaterial", &Terrain::setLayerFromMaterial)
        .addFunction("removeSurfaceLayer", &Terrain::removeSurfaceLayer)
        .addFunction("getNumLayers", &Terrain::getNumLayers)
        .addProperty("size", &Terrain::getSize, &Terrain::setSize)
        .addProperty("maxHeight", &Terrain::getMaxHeight, &Terrain::setMaxHeight)
        .addProperty("resolution", &Terrain::getResolution, &Terrain::setResolution)
        .addProperty("textureBaseTiles", &Terrain::getTextureBaseTiles, &Terrain::setTextureBaseTiles)
        .addProperty("textureDetailTiles", &Terrain::getTextureDetailTiles, &Terrain::setTextureDetailTiles)
        .addProperty("rootGridSize", &Terrain::getRootGridSize, &Terrain::setRootGridSize)
        .addProperty("levels", &Terrain::getLevels, &Terrain::setLevels)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Bone, Object>("Bone")
        .addConstructor <void (*) (Scene*, Entity)> ()
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Tilemap, Mesh>("Tilemap")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("findRectByString", &Tilemap::findRectByString)
        .addFunction("findTileByString", &Tilemap::findTileByString)
        .addProperty("textureScaleFactor", &Tilemap::getTextureScaleFactor, &Tilemap::setTextureScaleFactor)
        .addFunction("addRect", 
            luabridge::overload<int, const std::string&, const std::string&, TextureFilter, Rect>(&Tilemap::addRect),
            luabridge::overload<int, const std::string&, const std::string&, Rect>(&Tilemap::addRect),
            luabridge::overload<int, const std::string&, Rect>(&Tilemap::addRect),
            luabridge::overload<const std::string&, float, float, float, float>(&Tilemap::addRect),
            luabridge::overload<float, float, float, float>(&Tilemap::addRect),
            luabridge::overload<Rect>(&Tilemap::addRect))
        .addFunction("removeRect", 
            luabridge::overload<int>(&Tilemap::removeRect),
            luabridge::overload<const std::string&>(&Tilemap::removeRect))
        .addFunction("clearRects", &Tilemap::clearRects)
        .addFunction("getRect", 
            luabridge::overload<int>(&Tilemap::getRect),
            luabridge::overload<const std::string&>(&Tilemap::getRect))
        .addFunction("addTile", 
            luabridge::overload<int, const std::string&, int, Vector2, float, float>(&Tilemap::addTile),
            luabridge::overload<const std::string&, int, Vector2, float, float>(&Tilemap::addTile),
            luabridge::overload<int, Vector2, float, float>(&Tilemap::addTile),
            luabridge::overload<const std::string&, const std::string&, Vector2, float, float>(&Tilemap::addTile),
            luabridge::overload<const std::string&, Vector2, float, float>(&Tilemap::addTile))
        .addFunction("removeTile", 
            luabridge::overload<int>(&Tilemap::removeTile),
            luabridge::overload<const std::string&>(&Tilemap::removeTile))
        .addFunction("clearTiles", &Tilemap::clearTiles)
        .addFunction("getTile", 
            luabridge::overload<int>(&Tilemap::getTile),
            luabridge::overload<const std::string&>(&Tilemap::getTile))
        .addProperty("reserveTiles", &Tilemap::getReserveTiles, &Tilemap::setReserveTiles)
        .addFunction("getWidth", &Tilemap::getWidth)
        .addFunction("getHeight", &Tilemap::getHeight)
        .addFunction("clearAll", &Tilemap::clearAll)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Model, Mesh>("Model")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("loadOBJ", &Model::loadOBJ)
        .addFunction("loadGLTF", &Model::loadGLTF)
        .addFunction("loadModel", &Model::loadModel)
        .addFunction("getAnimation", &Model::getAnimation)
        .addFunction("findAnimation", &Model::findAnimation)
        .addFunction("playAnimation",
            luabridge::overload<int>(&Model::playAnimation),
            luabridge::overload<int, float>(&Model::playAnimation),
            luabridge::overload<const std::string&>(&Model::playAnimation),
            luabridge::overload<const std::string&, float>(&Model::playAnimation))
        .addFunction("stopAnimations", &Model::stopAnimations)
        .addFunction("getBone",
            luabridge::overload<int>(&Model::getBone),
            luabridge::overload<const std::string&>(&Model::getBone))
        .addFunction("getMorphWeight", 
            luabridge::overload<int>(&Model::getMorphWeight),
            luabridge::overload<const std::string&>(&Model::getMorphWeight))
        .addFunction("setMorphWeight", 
            luabridge::overload<int, float>(&Model::setMorphWeight),
            luabridge::overload<const std::string&, float>(&Model::setMorphWeight))
        .addFunction("resetToBindPose", &Model::resetToBindPose)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<MeshPolygon, Mesh>("MeshPolygon")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("addVertex", 
            luabridge::overload<Vector3>(&MeshPolygon::addVertex),
            luabridge::overload<float, float>(&MeshPolygon::addVertex))
        .addProperty("width", [] (MeshPolygon* self) -> int { return self->getWidth(); })
        .addProperty("height", [] (MeshPolygon* self) -> int { return self->getHeight(); })
        .addProperty("flipY", &MeshPolygon::isFlipY, &MeshPolygon::setFlipY)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Points, Object>("Points")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("maxPoints", &Points::getMaxPoints, &Points::setMaxPoints)
        .addFunction("addPoint", 
            luabridge::overload<PointData>(&Points::addPoint),
            luabridge::overload<Vector3>(&Points::addPoint),
            luabridge::overload<float, float, float>(&Points::addPoint),
            luabridge::overload<Vector3, Vector4>(&Points::addPoint),
            luabridge::overload<Vector3, Vector4, float>(&Points::addPoint),
            luabridge::overload<Vector3, Vector4, float, float>(&Points::addPoint),
            luabridge::overload<Vector3, Vector4, float, float, Rect>(&Points::addPoint))
        .addFunction("getPoint", &Points::getPoint)
        .addFunction("updatePoint", 
            luabridge::overload<size_t, PointData>(&Points::updatePoint),
            luabridge::overload<size_t, Vector3>(&Points::updatePoint),
            luabridge::overload<size_t, float, float, float>(&Points::updatePoint),
            luabridge::overload<size_t, Vector3, Vector4>(&Points::updatePoint),
            luabridge::overload<size_t, Vector3, Vector4, float>(&Points::updatePoint),
            luabridge::overload<size_t, Vector3, Vector4, float, float>(&Points::updatePoint),
            luabridge::overload<size_t, Vector3, Vector4, float, float, Rect>(&Points::updatePoint))
        .addFunction("removePoint", &Points::removePoint)
        .addFunction("isPointVisible", &Points::isPointVisible)
        .addFunction("setPointVisible", &Points::setPointVisible)
        .addFunction("updatePoints", &Points::updatePoints)
        .addFunction("getNumPoints", &Points::getNumPoints)
        .addFunction("clearPoints", &Points::clearPoints)
        .addFunction("addSpriteFrame", 
            luabridge::overload<int, const std::string&, Rect>(&Points::addSpriteFrame),
            luabridge::overload<const std::string&, float, float, float, float>(&Points::addSpriteFrame),
            luabridge::overload<float, float, float, float>(&Points::addSpriteFrame),
            luabridge::overload<Rect>(&Points::addSpriteFrame))
        .addFunction("removeSpriteFrame", 
            luabridge::overload<int>(&Points::removeSpriteFrame),
            luabridge::overload<const std::string&>(&Points::removeSpriteFrame))
        .addFunction("setTexture", 
            luabridge::overload<const std::string&>(&Points::setTexture),
            luabridge::overload<const std::string&,TextureData>(&Points::setTexture),
            luabridge::overload<Framebuffer*>(&Points::setTexture))
        .addProperty("transparent", &Points::isTransparent, &Points::setTransparent)
        .addProperty("autoTransparency", &Points::isAutoTransparency, &Points::setAutoTransparency)
        .addProperty("customShader", &Points::getCustomShader, &Points::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Points::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Points::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Points::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Points::setShaderUniform))
        .addFunction("getShaderUniform", &Points::getShaderUniform)
        .addFunction("removeShaderUniform", &Points::removeShaderUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Lines, Object>("Lines")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("maxLines", &Lines::getMaxLines, &Lines::setMaxLines)
        .addFunction("addLine", 
            luabridge::overload<Vector3, Vector3>(&Lines::addLine),
            luabridge::overload<Vector3, Vector3, Vector3>(&Lines::addLine),
            luabridge::overload<Vector3, Vector3, Vector4>(&Lines::addLine),
            luabridge::overload<Vector3, Vector3, Vector4, Vector4>(&Lines::addLine))
        .addFunction("getLine", &Lines::getLine)
        .addFunction("updateLine", 
            luabridge::overload<size_t, Vector3, Vector3>(&Lines::updateLine),
            luabridge::overload<size_t, Vector3, Vector3, Vector3>(&Lines::updateLine),
            luabridge::overload<size_t, Vector3, Vector3, Vector4>(&Lines::updateLine),
            luabridge::overload<size_t, Vector3, Vector3, Vector4, Vector4>(&Lines::updateLine),
            luabridge::overload<size_t, Vector3>(&Lines::updateLine),
            luabridge::overload<size_t, Vector4>(&Lines::updateLine),
            luabridge::overload<size_t, Vector4, Vector4>(&Lines::updateLine))
        .addFunction("removeLine", &Lines::removeLine)
        .addFunction("updateLines", &Lines::updateLines)
        .addFunction("getNumLines", &Lines::getNumLines)
        .addFunction("clearLines", &Lines::clearLines)
        .addProperty("customShader", &Lines::getCustomShader, &Lines::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Lines::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Lines::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Lines::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Lines::setShaderUniform))
        .addFunction("getShaderUniform", &Lines::getShaderUniform)
        .addFunction("removeShaderUniform", &Lines::removeShaderUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Sprite, Mesh>("Sprite")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getOccluder2D", &Sprite::getOccluder2D)
        .addFunction("removeOccluder2D", &Sprite::removeOccluder2D)
        .addFunction("setSize", &Sprite::setSize)
        .addProperty("width", &Sprite::getWidth, &Sprite::setWidth)
        .addProperty("height", &Sprite::getHeight, &Sprite::setHeight)
        .addProperty("flipY", &Sprite::isFlipY, &Sprite::setFlipY)
        .addProperty("textureScaleFactor", &Sprite::getTextureScaleFactor, &Sprite::setTextureScaleFactor)
        .addProperty("textureRect", &Sprite::getTextureRect, (void(Sprite::*)(Rect))&Sprite::setTextureRect)
        .addFunction("setTextureRect",(void(Sprite::*)(float, float, float, float)) &Sprite::setTextureRect)
        .addProperty("pivotPreset", &Sprite::getPivotPreset, &Sprite::setPivotPreset)
        .addFunction("addFrame", 
            luabridge::overload<int, const std::string&, Rect>(&Sprite::addFrame),
            luabridge::overload<const std::string&, float, float, float, float>(&Sprite::addFrame),
            luabridge::overload<float, float, float, float>(&Sprite::addFrame),
            luabridge::overload<Rect>(&Sprite::addFrame))
        .addFunction("removeFrame", 
            luabridge::overload<int>(&Sprite::removeFrame),
            luabridge::overload<const std::string&>(&Sprite::removeFrame))
        .addFunction("setFrame", 
            luabridge::overload<int>(&Sprite::setFrame),
            luabridge::overload<const std::string&>(&Sprite::setFrame))
        .addFunction("startAnimation", 
            luabridge::overload<std::vector<int>, std::vector<int>, bool>(&Sprite::startAnimation),
            luabridge::overload<int, int, int, bool>(&Sprite::startAnimation),
            luabridge::overload<const std::string&, int, bool>(&Sprite::startAnimation))
        .addFunction("pauseAnimation", &Sprite::pauseAnimation)
        .addFunction("stopAnimation", &Sprite::stopAnimation)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<UILayout, Object>("UILayout")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setSize", &UILayout::setSize)
        .addProperty("width", &UILayout::getWidth, &UILayout::setWidth)
        .addProperty("height", &UILayout::getHeight, &UILayout::setHeight)
        .addFunction("setAnchorPoints", &UILayout::setAnchorPoints)
        .addProperty("anchorPointLeft", &UILayout::getAnchorPointLeft, &UILayout::setAnchorPointLeft)
        .addProperty("anchorPointTop", &UILayout::getAnchorPointTop, &UILayout::setAnchorPointTop)
        .addProperty("anchorPointRight", &UILayout::getAnchorPointRight, &UILayout::setAnchorPointRight)
        .addProperty("anchorPointBottom", &UILayout::getAnchorPointBottom, &UILayout::setAnchorPointBottom)
        .addFunction("setAnchorOffsets", &UILayout::setAnchorOffsets)
        .addProperty("anchorOffsetLeft", &UILayout::getAnchorOffsetLeft, &UILayout::setAnchorOffsetLeft)
        .addProperty("anchorOffsetTop", &UILayout::getAnchorOffsetTop, &UILayout::setAnchorOffsetTop)
        .addProperty("anchorOffsetRight", &UILayout::getAnchorOffsetRight, &UILayout::setAnchorOffsetRight)
        .addProperty("anchorOffsetBottom", &UILayout::getAnchorOffsetBottom, &UILayout::setAnchorOffsetBottom)
        .addProperty("positionXOffset", &UILayout::getPositionXOffset, &UILayout::setPositionXOffset)
        .addProperty("positionYOffset", &UILayout::getPositionYOffset, &UILayout::setPositionYOffset)
        .addProperty("positionOffset", &UILayout::getPositionOffset, &UILayout::setPositionOffset)
        .addProperty("anchorPreset", &UILayout::getAnchorPreset, &UILayout::setAnchorPreset)
        .addProperty("usingAnchors", &UILayout::isUsingAnchors, &UILayout::setUsingAnchors)
        .addProperty("ignoreScissor", &UILayout::isIgnoreScissor, &UILayout::setIgnoreScissor)
        .addFunction("getUILayoutComponent", &UILayout::getComponent<UILayoutComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Container, UILayout>("Container")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addProperty("type", &Container::getType, &Container::setType)
        .addProperty("useAllWrapSpace", &Container::isUseAllWrapSpace, &Container::setUseAllWrapSpace)
        .addProperty("wrapCellWidth", &Container::getWrapCellWidth, &Container::setWrapCellWidth)
        .addProperty("wrapCellHeight", &Container::getWrapCellHeight, &Container::setWrapCellHeight)
        .addProperty("contentWidth", &Container::getContentWidth)
        .addProperty("contentHeight", &Container::getContentHeight)
        .addFunction("resize", &Container::resize)
        .addFunction("setBoxExpand", 
            luabridge::overload<size_t, bool>(&Container::setBoxExpand),
            luabridge::overload<bool>(&Container::setBoxExpand))
        .addFunction("isBoxExpand", &Container::isBoxExpand)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Polygon, UILayout>("Polygon")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("addVertex", 
            luabridge::overload<Vector3>(&Polygon::addVertex),
            luabridge::overload<float, float>(&Polygon::addVertex))
        .addProperty("color", &Polygon::getColor, (void(Polygon::*)(Vector4))&Polygon::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&Polygon::setColor),
            luabridge::overload<const float, const float, const float, const float>(&Polygon::setColor))
        .addProperty("alpha", &Polygon::getAlpha, &Polygon::setAlpha)
        .addFunction("setTexture", 
            luabridge::overload<const std::string&>(&Polygon::setTexture),
            luabridge::overload<const std::string&,TextureData>(&Polygon::setTexture),
            luabridge::overload<Framebuffer*>(&Polygon::setTexture))
        .addProperty("flipY", &Polygon::isFlipY, &Polygon::setFlipY)
        .addFunction("getAABB", &Polygon::getAABB)
        .addFunction("getWorldAABB", &Polygon::getWorldAABB)
        .addProperty("customShader", &Polygon::getCustomShader, &Polygon::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Polygon::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Polygon::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Polygon::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Polygon::setShaderUniform))
        .addFunction("getShaderUniform", &Polygon::getShaderUniform)
        .addFunction("removeShaderUniform", &Polygon::removeShaderUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Text, UILayout>("Text")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setFixedSize", &Text::setFixedSize)
        .addProperty("fixedWidth", &Text::isFixedWidth, &Text::setFixedWidth)
        .addProperty("fixedHeight", &Text::isFixedHeight, &Text::setFixedHeight)
        .addProperty("maxTextSize", &Text::getMaxTextSize, &Text::setMaxTextSize)
        .addProperty("text", &Text::getText, &Text::setText)
        .addProperty("font", luabridge::constOverload<>(&Text::getFont), luabridge::overload<const std::string&>(&Text::setFont))
        .addFunction("setFont",
            luabridge::overload<const std::string&>(&Text::setFont),
            luabridge::overload<unsigned int, const std::string&>(&Text::setFont))
        .addFunction("getFont",
            luabridge::constOverload<>(&Text::getFont),
            luabridge::constOverload<unsigned int>(&Text::getFont))
        .addProperty("fontSize", &Text::getFontSize, &Text::setFontSize)
        .addProperty("multiline", &Text::getMultiline, &Text::setMultiline)
        .addProperty("color", &Text::getColor, (void(Text::*)(Vector4))&Text::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&Text::setColor),
            luabridge::overload<const float, const float, const float, const float>(&Text::setColor))
        .addProperty("alpha", &Text::getAlpha, &Text::setAlpha)
        .addFunction("getAscent", &Text::getAscent)
        .addFunction("getDescent", &Text::getDescent)
        .addFunction("getLineGap", &Text::getLineGap)
        .addFunction("getLineHeight", &Text::getLineHeight)
        .addFunction("getNumChars", &Text::getNumChars)
        .addFunction("getCharPosition", &Text::getCharPosition)
        .addFunction("getCharWidth", &Text::getCharWidth)
        .addProperty("flipY", &Text::isFlipY, &Text::setFlipY)
        .addProperty("customShader", &Text::getCustomShader, &Text::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Text::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Text::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Text::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Text::setShaderUniform))
        .addFunction("getShaderUniform", &Text::getShaderUniform)
        .addFunction("removeShaderUniform", &Text::removeShaderUniform)
        .addProperty("pivotBaseline", &Text::isPivotBaseline, &Text::setPivotBaseline)
        .addProperty("pivotCentered", &Text::isPivotCentered, &Text::setPivotCentered)
        .addFunction("getAABB", &Text::getAABB)
        .addFunction("getWorldAABB", &Text::getWorldAABB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Image, UILayout>("Image")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setPatchMargin", 
            luabridge::overload<unsigned int>(&Image::setPatchMargin),
            luabridge::overload<unsigned int, unsigned int, unsigned int, unsigned int>(&Image::setPatchMargin))
        .addProperty("patchMarginBottom", &Image::getPatchMarginBottom, &Image::setPatchMarginBottom)
        .addProperty("patchMarginLeft", &Image::getPatchMarginLeft, &Image::setPatchMarginLeft)
        .addProperty("patchMarginRight", &Image::getPatchMarginRight, &Image::setPatchMarginRight)
        .addProperty("patchMarginTop", &Image::getPatchMarginTop, &Image::setPatchMarginTop)
        .addFunction("setTexture", 
            luabridge::overload<const std::string&>(&Image::setTexture),
            luabridge::overload<const std::string&,TextureData>(&Image::setTexture),
            luabridge::overload<Framebuffer*>(&Image::setTexture))
        .addProperty("textureScaleFactor", &Image::getTextureScaleFactor, &Image::setTextureScaleFactor)
        .addProperty("color", &Image::getColor, (void(Image::*)(Vector4))&Image::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&Image::setColor),
            luabridge::overload<const float, const float, const float, const float>(&Image::setColor))
        .addProperty("alpha", &Image::getAlpha, &Image::setAlpha)
        .addProperty("flipY", &Image::isFlipY, &Image::setFlipY)
        .addFunction("getAABB", &Image::getAABB)
        .addFunction("getWorldAABB", &Image::getWorldAABB)
        .addFunction("getUIComponent", &Image::getComponent<UIComponent>)
        .addProperty("customShader", &Image::getCustomShader, &Image::setCustomShader)
        .addFunction("setShaderUniform",
            luabridge::overload<const std::string&, const Vector4&>(&Image::setShaderUniform),
            luabridge::overload<const std::string&, const Vector3&>(&Image::setShaderUniform),
            luabridge::overload<const std::string&, const Vector2&>(&Image::setShaderUniform),
            luabridge::overload<const std::string&, float>(&Image::setShaderUniform))
        .addFunction("getShaderUniform", &Image::getShaderUniform)
        .addFunction("removeShaderUniform", &Image::removeShaderUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Button, Image>("Button")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getLabelObject", &Button::getLabelObject)
        .addProperty("label", &Button::getLabel, &Button::setLabel)
        .addProperty("labelColor", &Button::getLabelColor, (void(Button::*)(Vector4))&Button::setLabelColor)
        .addFunction("setLabelColor", (void(Button::*)(const float, const float, const float, const float))&Button::setLabelColor)
        .addProperty("labelFont", &Button::getLabelFont, &Button::setLabelFont)
        .addProperty("labelFontSize", &Button::getLabelFontSize, &Button::setLabelFontSize)
        .addFunction("setTextureNormal", (void(Button::*)(const std::string&))&Button::setTextureNormal)
        .addFunction("setTextureHovered", (void(Button::*)(const std::string&))&Button::setTextureHovered)
        .addFunction("setTexturePressed", (void(Button::*)(const std::string&))&Button::setTexturePressed)
        .addFunction("setTextureDisabled", (void(Button::*)(const std::string&))&Button::setTextureDisabled)
        .addProperty("colorNormal", &Button::getColorNormal, (void(Button::*)(Vector4))&Button::setColorNormal)
        .addFunction("setColorNormal", 
            luabridge::overload<const float, const float, const float>(&Button::setColorNormal),
            luabridge::overload<const float, const float, const float, const float>(&Button::setColorNormal))
        .addProperty("colorHovered", &Button::getColorHovered, (void(Button::*)(Vector4))&Button::setColorHovered)
        .addFunction("setColorHovered", luabridge::overload<const float, const float, const float, const float>(&Button::setColorHovered),
            luabridge::overload<const float, const float, const float>(&Button::setColorHovered))
        .addProperty("colorPressed", &Button::getColorPressed, (void(Button::*)(Vector4))&Button::setColorPressed)
        .addFunction("setColorPressed", 
            luabridge::overload<const float, const float, const float>(&Button::setColorPressed),
            luabridge::overload<const float, const float, const float, const float>(&Button::setColorPressed))
        .addProperty("colorDisabled", &Button::getColorDisabled, (void(Button::*)(Vector4))&Button::setColorDisabled)
        .addFunction("setColorDisabled", 
            luabridge::overload<const float, const float, const float>(&Button::setColorDisabled),
            luabridge::overload<const float, const float, const float, const float>(&Button::setColorDisabled))
        .addProperty("disabled", &Button::getDisabled, &Button::setDisabled)
        .addFunction("getButtonComponent", &Button::getComponent<ButtonComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Panel, Image>("Panel")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getHeaderImageObject", &Panel::getHeaderImageObject)
        .addFunction("getHeaderContainerObject", &Panel::getHeaderContainerObject)
        .addFunction("getHeaderTextObject", &Panel::getHeaderTextObject)
        .addProperty("title", &Panel::getTitle, &Panel::setTitle)
        .addProperty("titleAnchorPreset", &Panel::getTitleAnchorPreset, &Panel::setTitleAnchorPreset)
        .addProperty("titleColor", &Panel::getTitleColor, (void(Panel::*)(Vector4))&Panel::setTitleColor)
        .addFunction("setTitleColor", (void(Panel::*)(const float, const float, const float, const float))&Panel::setTitleColor)
        .addProperty("titleFont", &Panel::getTitleFont, &Panel::setTitleFont)
        .addProperty("titleFontSize", &Panel::getTitleFontSize, &Panel::setTitleFontSize)
        .addProperty("headerColor", &Panel::getHeaderColor, (void(Panel::*)(Vector4))&Panel::setHeaderColor)
        .addFunction("setHeaderColor", (void(Panel::*)(const float, const float, const float, const float))&Panel::setHeaderColor)
        .addFunction("setHeaderPatchMargin", &Panel::setHeaderPatchMargin)
        .addFunction("setHeaderTexture", &Panel::setHeaderTexture)
        .addFunction("setHeaderMargin", &Panel::setHeaderMargin)
        .addProperty("headerMarginLeft", &Panel::getHeaderMarginLeft, &Panel::setHeaderMarginLeft)
        .addProperty("headerMarginTop", &Panel::getHeaderMarginTop, &Panel::setHeaderMarginTop)
        .addProperty("headerMarginRight", &Panel::getHeaderMarginRight, &Panel::setHeaderMarginRight)
        .addProperty("headerMarginBottom", &Panel::getHeaderMarginBottom, &Panel::setHeaderMarginBottom)
        .addFunction("setMinSize", &Panel::setMinSize)
        .addProperty("minWidth", &Panel::getMinWidth, &Panel::setMinWidth)
        .addProperty("minHeight", &Panel::getMinHeight, &Panel::setMinHeight)
        .addProperty("resizeMargin", &Panel::getResizeMargin, &Panel::setResizeMargin)
        .addFunction("setWindowProperties", &Panel::setWindowProperties)
        .addProperty("canMove", &Panel::isCanMove, &Panel::setCanMove)
        .addProperty("canResize", &Panel::isCanResize, &Panel::setCanResize)
        .addProperty("canBringToFront", &Panel::isCanBringToFront, &Panel::setCanBringToFront)
        .addFunction("getPanelComponent", &Panel::getComponent<PanelComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Scrollbar, Image>("Scrollbar")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getBarObject", &Scrollbar::getBarObject)
        .addProperty("type", &Scrollbar::getType, &Scrollbar::setType)
        .addProperty("barSize", &Scrollbar::getBarSize, &Scrollbar::setBarSize)
        .addProperty("step", &Scrollbar::getStep, &Scrollbar::setStep)
        .addFunction("setBarTexture", 
            luabridge::overload<const std::string&>(&Scrollbar::setBarTexture),
            luabridge::overload<Framebuffer*>(&Scrollbar::setBarTexture))
        .addProperty("barColor", &Scrollbar::getBarColor, (void(Scrollbar::*)(Vector4))&Scrollbar::setBarColor)
        .addFunction("setBarColor", 
            luabridge::overload<const float, const float, const float>(&Scrollbar::setBarColor),
            luabridge::overload<const float, const float, const float, const float>(&Scrollbar::setBarColor))
        .addProperty("barAlpha", &Scrollbar::getBarAlpha, &Scrollbar::setBarAlpha)
        .addFunction("setBarPatchMargin", 
            luabridge::overload<int>(&Scrollbar::setBarPatchMargin),
            luabridge::overload<int, int, int, int>(&Scrollbar::setBarPatchMargin))
        .addFunction("setBarMargin",
            luabridge::overload<int>(&Scrollbar::setBarMargin),
            luabridge::overload<int, int, int, int>(&Scrollbar::setBarMargin))
        .addFunction("getScrollbarComponent", &Scrollbar::getComponent<ScrollbarComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Progressbar, Image>("Progressbar")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getFillObject", &Progressbar::getFillObject)
        .addProperty("type", &Progressbar::getType, &Progressbar::setType)
        .addProperty("value", &Progressbar::getValue, &Progressbar::setValue)
        .addFunction("setFillTexture", 
            luabridge::overload<const std::string&>(&Progressbar::setFillTexture),
            luabridge::overload<Framebuffer*>(&Progressbar::setFillTexture))
        .addProperty("fillColor", &Progressbar::getFillColor, (void(Progressbar::*)(Vector4))&Progressbar::setFillColor)
        .addFunction("setFillColor", 
            luabridge::overload<const float, const float, const float>(&Progressbar::setFillColor),
            luabridge::overload<const float, const float, const float, const float>(&Progressbar::setFillColor))
        .addProperty("fillAlpha", &Progressbar::getFillAlpha, &Progressbar::setFillAlpha)
        .addFunction("setFillPatchMargin", 
            luabridge::overload<int>(&Progressbar::setFillPatchMargin),
            luabridge::overload<int, int, int, int>(&Progressbar::setFillPatchMargin))
        .addFunction("setFillMargin",
            luabridge::overload<int>(&Progressbar::setFillMargin),
            luabridge::overload<int, int, int, int>(&Progressbar::setFillMargin))
        .addFunction("getProgressbarComponent", &Progressbar::getComponent<ProgressbarComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TextEdit, Image>("TextEdit")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getTextObject", &TextEdit::getTextObject)
        .addFunction("getSelectionObject", &TextEdit::getSelectionObject)
        .addProperty("disabled", &TextEdit::getDisabled, &TextEdit::setDisabled)
        .addProperty("text", &TextEdit::getText, &TextEdit::setText)
        .addProperty("textColor", &TextEdit::getTextColor, (void(TextEdit::*)(Vector4))&TextEdit::setTextColor)
        .addFunction("setTextColor", 
            luabridge::overload<const float, const float, const float>(&TextEdit::setTextColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setTextColor))
        .addProperty("textFont", &TextEdit::getTextFont, &TextEdit::setTextFont)
        .addProperty("fontSize", &TextEdit::getFontSize, &TextEdit::setFontSize)
        .addProperty("maxTextSize", &TextEdit::getMaxTextSize, &TextEdit::setMaxTextSize)
        .addProperty("placeholder", &TextEdit::getPlaceholder, &TextEdit::setPlaceholder)
        .addProperty("placeholderColor", &TextEdit::getPlaceholderColor, (void(TextEdit::*)(Vector4))&TextEdit::setPlaceholderColor)
        .addFunction("setPlaceholderColor",
            luabridge::overload<const float, const float, const float>(&TextEdit::setPlaceholderColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setPlaceholderColor))
        .addProperty("password", &TextEdit::getPassword, &TextEdit::setPassword)
        .addProperty("passwordChar", &TextEdit::getPasswordChar, &TextEdit::setPasswordChar)
        .addProperty("cursorIndex", &TextEdit::getCursorIndex, &TextEdit::setCursorIndex)
        .addFunction("getSelectionAnchor", &TextEdit::getSelectionAnchor)
        .addFunction("getSelectionFocus", &TextEdit::getSelectionFocus)
        .addFunction("setSelection", &TextEdit::setSelection)
        .addProperty("cursorColor", &TextEdit::getCursorColor, (void(TextEdit::*)(Vector4))&TextEdit::setCursorColor)
        .addFunction("setCursorColor", 
            luabridge::overload<const float, const float, const float>(&TextEdit::setCursorColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setCursorColor))
        .addProperty("selectionColor", &TextEdit::getSelectionColor, (void(TextEdit::*)(Vector4))&TextEdit::setSelectionColor)
        .addFunction("setSelectionColor",
            luabridge::overload<const float, const float, const float>(&TextEdit::setSelectionColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setSelectionColor))
        .addProperty("cursorBlink", &TextEdit::getCursorBlink, &TextEdit::setCursorBlink)
        .addProperty("cursorWidth", &TextEdit::getCursorWidth, &TextEdit::setCursorWidth)
        .addFunction("getTextEditComponent", &TextEdit::getComponent<TextEditComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Sound, EntityHandle>("Sound")
        // Entity before bool: a number must bind the entity overload, not is3D
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity), void (*) (Scene*, bool)> ()
        .addFunction("loadSound", &Sound::loadSound)
        .addFunction("destroySound", &Sound::destroySound)
        .addFunction("getObject", &Sound::getObject)
        .addFunction("play", &Sound::play)
        .addFunction("pause", &Sound::pause)
        .addFunction("stop", &Sound::stop)
        .addFunction("seek", &Sound::seek)
        .addFunction("getLength", &Sound::getLength)
        .addFunction("getPlayingTime", &Sound::getPlayingTime)
        .addFunction("isPlaying", &Sound::isPlaying)
        .addFunction("isPaused", &Sound::isPaused)
        .addFunction("isStopped", &Sound::isStopped)
        .addProperty("sound3D", &Sound::isSound3D, &Sound::setSound3D)
        .addProperty("clockedSound", &Sound::isClockedSound, &Sound::setClockedSound)
        .addProperty("volume", &Sound::getVolume, &Sound::setVolume)
        .addProperty("speed", &Sound::getSpeed, &Sound::setSpeed)
        .addProperty("pan", &Sound::getPan, &Sound::setPan)
        .addProperty("looping", &Sound::isLooping, &Sound::setLooping)
        .addProperty("loopingPoint", &Sound::getLoopingPoint, &Sound::setLoopingPoint)
        .addProperty("protectVoice", &Sound::isProtectVoice, &Sound::setProtectVoice)
        .addFunction("setInaudibleBehavior", &Sound::setInaudibleBehavior)
        .addFunction("setMinMaxDistance", &Sound::setMinMaxDistance)
        .addProperty("minDistance", &Sound::getMinDistance, &Sound::setMinDistance)
        .addProperty("maxDistance", &Sound::getMaxDistance, &Sound::setMaxDistance)
        .addProperty("attenuationModel", &Sound::getAttenuationModel, &Sound::setAttenuationModel)
        .addProperty("attenuationRolloffFactor", &Sound::getAttenuationRolloffFactor, &Sound::setAttenuationRolloffFactor)
        .addProperty("dopplerFactor", &Sound::getDopplerFactor, &Sound::setDopplerFactor)
        .addFunction("getSoundComponent", &Sound::getComponent<SoundComponent>)
        .endClass();

#ifdef DORIAX_PHYSICS_2D
    luabridge::getGlobalNamespace(L)
        .deriveClass<Body2D, EntityHandle>("Body2D")
        .addConstructor <void (Scene*, Entity)> ()
        .addFunction("getAttachedObject", &Body2D::getAttachedObject)
        .addProperty("pointsToMeterScale", &Body2D::getPointsToMeterScale)
        .addFunction("load", &Body2D::load)
        .addFunction("createBoxShape", &Body2D::createBoxShape)
        .addFunction("createCenteredBoxShape", 
            luabridge::overload<float, float>(&Body2D::createCenteredBoxShape),
            luabridge::overload<float, float, Vector2, float>(&Body2D::createCenteredBoxShape))
        .addFunction("createRoundedBoxShape", &Body2D::createRoundedBoxShape)
        .addFunction("createPolygonShape", &Body2D::createPolygonShape)
        .addFunction("createCircleShape", &Body2D::createCircleShape)
        .addFunction("createCapsuleShape", &Body2D::createCapsuleShape)
        .addFunction("createSegmentShape", &Body2D::createSegmentShape)
        .addFunction("createChainShape", &Body2D::createChainShape)
        .addFunction("removeAllShapes", &Body2D::removeAllShapes)
        .addFunction("getBodyContacts", &Body2D::getBodyContacts)
        .addFunction("getShapeContacts", &Body2D::getShapeContacts)
        .addFunction("getNumShapes", &Body2D::getNumShapes)
        .addFunction("getShapeType", &Body2D::getShapeType)
        .addProperty("shapeDensity", (float(Body2D::*)()const)&Body2D::getShapeDensity, (void(Body2D::*)(float))&Body2D::setShapeDensity)
        .addFunction("setShapeDensity", 
            luabridge::overload<float>(&Body2D::setShapeDensity),
            luabridge::overload<size_t, float>(&Body2D::setShapeDensity))
        .addFunction("getShapeDensity", 
            luabridge::overload<>(&Body2D::getShapeDensity),
            luabridge::overload<size_t>(&Body2D::getShapeDensity))
        .addProperty("shapeFriction", (float(Body2D::*)()const)&Body2D::getShapeFriction, (void(Body2D::*)(float))&Body2D::setShapeFriction)
        .addFunction("setShapeFriction", 
            luabridge::overload<float>(&Body2D::setShapeFriction),
            luabridge::overload<size_t, float>(&Body2D::setShapeFriction))
        .addFunction("getShapeFriction", 
            luabridge::overload<>(&Body2D::getShapeFriction),
            luabridge::overload<size_t>(&Body2D::getShapeFriction))
        .addProperty("shapeRestitution", (float(Body2D::*)()const)&Body2D::getShapeRestitution, (void(Body2D::*)(float))&Body2D::setShapeRestitution)
        .addFunction("setShapeRestitution", 
            luabridge::overload<float>(&Body2D::setShapeRestitution),
            luabridge::overload<size_t, float>(&Body2D::setShapeRestitution))
        .addFunction("getShapeRestitution", 
            luabridge::overload<>(&Body2D::getShapeRestitution),
            luabridge::overload<size_t>(&Body2D::getShapeRestitution))
        .addProperty("shapeSensor", (bool(Body2D::*)()const)&Body2D::isShapeSensor, (void(Body2D::*)(bool))&Body2D::setShapeSensor)
        .addFunction("setShapeSensor", 
            luabridge::overload<bool>(&Body2D::setShapeSensor),
            luabridge::overload<size_t, bool>(&Body2D::setShapeSensor))
        .addFunction("isShapeSensor", 
            luabridge::overload<>(&Body2D::isShapeSensor),
            luabridge::overload<size_t>(&Body2D::isShapeSensor))
        .addProperty("shapeEnableHitEvents", (bool(Body2D::*)()const)&Body2D::isShapeEnableHitEvents, (void(Body2D::*)(bool))&Body2D::setShapeEnableHitEvents)
        .addFunction("setShapeEnableHitEvents", 
            luabridge::overload<bool>(&Body2D::setShapeEnableHitEvents),
            luabridge::overload<size_t, bool>(&Body2D::setShapeEnableHitEvents))
        .addFunction("isShapeEnableHitEvents", 
            luabridge::overload<>(&Body2D::isShapeEnableHitEvents),
            luabridge::overload<size_t>(&Body2D::isShapeEnableHitEvents))
        .addProperty("shapeContactEvents", (bool(Body2D::*)()const)&Body2D::isShapeContactEvents, (void(Body2D::*)(bool))&Body2D::setShapeContactEvents)
        .addFunction("setShapeContactEvents", 
            luabridge::overload<bool>(&Body2D::setShapeContactEvents),
            luabridge::overload<size_t, bool>(&Body2D::setShapeContactEvents))
        .addFunction("isShapeContactEvents", 
            luabridge::overload<>(&Body2D::isShapeContactEvents),
            luabridge::overload<size_t>(&Body2D::isShapeContactEvents))
        .addProperty("shapePreSolveEvents", (bool(Body2D::*)()const)&Body2D::isShapePreSolveEvents, (void(Body2D::*)(bool))&Body2D::setShapePreSolveEvents)
        .addFunction("setShapePreSolveEvents", 
            luabridge::overload<bool>(&Body2D::setShapePreSolveEvents),
            luabridge::overload<size_t, bool>(&Body2D::setShapePreSolveEvents))
        .addFunction("isShapePreSolveEvents", 
            luabridge::overload<>(&Body2D::isShapePreSolveEvents),
            luabridge::overload<size_t>(&Body2D::isShapePreSolveEvents))
        .addProperty("shapeSensorEvents", (bool(Body2D::*)() const)&Body2D::isShapeSensorEvents, (void(Body2D::*)(bool))&Body2D::setShapeSensorEvents)
        .addFunction("setShapeSensorEvents", 
            luabridge::overload<bool>(&Body2D::setShapeSensorEvents),
            luabridge::overload<size_t, bool>(&Body2D::setShapeSensorEvents))
        .addFunction("isShapeSensorEvents", 
            luabridge::overload<>(&Body2D::isShapeSensorEvents),
            luabridge::overload<size_t>(&Body2D::isShapeSensorEvents))
        .addProperty("position", &Body2D::getPosition, &Body2D::setPosition)
        .addProperty("angle", &Body2D::getAngle, &Body2D::setAngle)
        .addProperty("linearVelocity", &Body2D::getLinearVelocity, &Body2D::setLinearVelocity)
        .addProperty("angularVelocity", &Body2D::getAngularVelocity, &Body2D::setAngularVelocity)
        .addProperty("linearDamping", &Body2D::getLinearDamping, &Body2D::setLinearDamping)
        .addProperty("angularDamping", &Body2D::getAngularDamping, &Body2D::setAngularDamping)
        .addProperty("enableSleep", &Body2D::isEnableSleep, &Body2D::setEnableSleep)
        .addProperty("awake", &Body2D::isAwake, &Body2D::setAwake)
        .addProperty("fixedRotation", &Body2D::isFixedRotation, &Body2D::setFixedRotation)
        .addProperty("bullet", &Body2D::isBullet, &Body2D::setBullet)
        .addProperty("type", &Body2D::getType, &Body2D::setType)
        .addProperty("enabled", &Body2D::isEnabled, &Body2D::setEnabled)
        .addProperty("gravityScale", &Body2D::getGravityScale, &Body2D::setGravityScale)
        .addFunction("setBitsFilter", 
            luabridge::overload<uint16_t, uint16_t>(&Body2D::setBitsFilter),
            luabridge::overload<size_t, uint16_t, uint16_t>(&Body2D::setBitsFilter))
        .addProperty("categoryBitsFilter", (uint16_t(Body2D::*)()const)&Body2D::getCategoryBitsFilter, (void(Body2D::*)(uint16_t))&Body2D::setCategoryBitsFilter)
        .addFunction("setCategoryBitsFilter", 
            luabridge::overload<uint16_t>(&Body2D::setCategoryBitsFilter),
            luabridge::overload<size_t, uint16_t>(&Body2D::setCategoryBitsFilter))
        .addFunction("getCategoryBitsFilter", 
            luabridge::overload<>(&Body2D::getCategoryBitsFilter),
            luabridge::overload<size_t>(&Body2D::getCategoryBitsFilter))
        .addProperty("maskBitsFilter", (uint16_t(Body2D::*)()const)&Body2D::getMaskBitsFilter, (void(Body2D::*)(uint16_t))&Body2D::setMaskBitsFilter)
        .addFunction("setMaskBitsFilter", 
            luabridge::overload<uint16_t>(&Body2D::setMaskBitsFilter),
            luabridge::overload<size_t, uint16_t>(&Body2D::setMaskBitsFilter))
        .addFunction("getMaskBitsFilter", 
            luabridge::overload<>(&Body2D::getMaskBitsFilter),
            luabridge::overload<size_t>(&Body2D::getMaskBitsFilter))
        .addProperty("groupIndexFilter", (int16_t(Body2D::*)()const)&Body2D::getGroupIndexFilter, (void(Body2D::*)(int16_t))&Body2D::setGroupIndexFilter)
        .addFunction("setGroupIndexFilter", 
            luabridge::overload<int16_t>(&Body2D::setGroupIndexFilter),
            luabridge::overload<size_t, int16_t>(&Body2D::setGroupIndexFilter))
        .addFunction("getGroupIndexFilter", 
            luabridge::overload<>(&Body2D::getGroupIndexFilter),
            luabridge::overload<size_t>(&Body2D::getGroupIndexFilter))
        .addFunction("getMass", &Body2D::getMass)
        .addFunction("getRotationalInertia", &Body2D::getRotationalInertia)
        .addFunction("applyMassFromShapes", &Body2D::applyMassFromShapes)
        .addFunction("applyForce", &Body2D::applyForce)
        .addFunction("applyForceToCenter", &Body2D::applyForceToCenter)
        .addFunction("applyTorque", &Body2D::applyTorque)
        .addFunction("applyLinearImpulse", &Body2D::applyLinearImpulse)
        .addFunction("applyLinearImpulseToCenter", &Body2D::applyLinearImpulseToCenter)
        .addFunction("applyAngularImpulse", &Body2D::applyAngularImpulse)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Joint2D>("Joint2D")
        .addConstructor <void (Scene*), void (Scene*, Entity)> ()
        .addFunction("setDistanceJoint",
            luabridge::overload<Entity, Entity>(&Joint2D::setDistanceJoint),
            luabridge::overload<Entity, Entity, Vector2, Vector2, bool>(&Joint2D::setDistanceJoint))
        .addFunction("setRevoluteJoint", &Joint2D::setRevoluteJoint)
        .addFunction("setPrismaticJoint", &Joint2D::setPrismaticJoint)
        //.addFunction("setPulleyJoint", &Joint2D::setPulleyJoint)
        //.addFunction("setGearJoint", &Joint2D::setGearJoint)
        .addFunction("setMouseJoint", &Joint2D::setMouseJoint)
        .addFunction("setWheelJoint", &Joint2D::setWheelJoint)
        .addFunction("setWeldJoint", &Joint2D::setWeldJoint)
        .addFunction("setMotorJoint", &Joint2D::setMotorJoint)
        .addFunction("getType", &Joint2D::getType)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Manifold2D>("Manifold2D")
        .addFunction("getManifoldPointAnchorA", &Manifold2D::getManifoldPointAnchorA)
        .addFunction("getManifoldPointAnchorB", &Manifold2D::getManifoldPointAnchorB)
        .addFunction("getManifoldPointPosition", &Manifold2D::getManifoldPointPosition)
        .addFunction("getManifoldPointNormalImpulse", &Manifold2D::getManifoldPointNormalImpulse)
        .addFunction("getManifoldPointNormalVelocity", &Manifold2D::getManifoldPointNormalVelocity)
        .addFunction("getManifoldPointTangentImpulse", &Manifold2D::getManifoldPointTangentImpulse)
        .addFunction("getManifoldPointSeparation", &Manifold2D::getManifoldPointSeparation)
        .addFunction("isManifoldPointPersisted", &Manifold2D::isManifoldPointPersisted)
        .addFunction("getNormal", &Manifold2D::getNormal)
        .addFunction("getPointCount", &Manifold2D::getPointCount)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Contact2D>("Contact2D")
        .addFunction("getManifold", &Contact2D::getManifold)
        .addFunction("getBodyEntityA", &Contact2D::getBodyEntityA)
        .addFunction("getBodyA", &Contact2D::getBodyA)
        .addFunction("getShapeIndexA", &Contact2D::getShapeIndexA)
        .addFunction("getBodyEntityB", &Contact2D::getBodyEntityB)
        .addFunction("getBodyB", &Contact2D::getBodyB)
        .addFunction("getShapeIndexB", &Contact2D::getShapeIndexB)
        .endClass();
#endif

#ifdef DORIAX_PHYSICS_3D
    luabridge::getGlobalNamespace(L)
        .deriveClass<Body3D, EntityHandle>("Body3D")
        .addConstructor <void (Scene*, Entity)> ()
        .addFunction("getAttachedObject", &Body3D::getAttachedObject)
        .addFunction("load", &Body3D::load)
        .addFunction("createBoxShape", 
            luabridge::overload<float, float, float>(&Body3D::createBoxShape),
            luabridge::overload<Vector3, Quaternion, float, float, float>(&Body3D::createBoxShape))
        .addFunction("createSphereShape", 
            luabridge::overload<float>(&Body3D::createSphereShape),
            luabridge::overload<Vector3, Quaternion, float>(&Body3D::createSphereShape))
        .addFunction("createCapsuleShape", 
            luabridge::overload<float, float>(&Body3D::createCapsuleShape),
            luabridge::overload<Vector3, Quaternion, float, float>(&Body3D::createCapsuleShape))
        .addFunction("createTaperedCapsuleShape", 
            luabridge::overload<float, float, float>(&Body3D::createTaperedCapsuleShape),
            luabridge::overload<Vector3, Quaternion, float, float, float>(&Body3D::createTaperedCapsuleShape))
        .addFunction("createCylinderShape", 
            luabridge::overload<float, float>(&Body3D::createCylinderShape),
            luabridge::overload<Vector3, Quaternion, float, float>(&Body3D::createCylinderShape))
        .addFunction("createConvexHullShape",
            luabridge::overload<>(&Body3D::createConvexHullShape),
            luabridge::overload<std::vector<Vector3>>(&Body3D::createConvexHullShape),
            luabridge::overload<Vector3, Quaternion, std::vector<Vector3>>(&Body3D::createConvexHullShape))
        .addFunction("createMeshShape", 
            luabridge::overload<>(&Body3D::createMeshShape),
            luabridge::overload<std::vector<Vector3>, std::vector<uint16_t>>(&Body3D::createMeshShape),
            luabridge::overload<Vector3, Quaternion, std::vector<Vector3>, std::vector<uint16_t>>(&Body3D::createMeshShape))
        .addFunction("createHeightFieldShape", 
            luabridge::overload<>(&Body3D::createHeightFieldShape),
            luabridge::overload<unsigned int>(&Body3D::createHeightFieldShape))
        .addFunction("getNumShapes", &Body3D::getNumShapes)
        .addFunction("getShapeType", &Body3D::getShapeType)
        .addProperty("shapeDensity", (float(Body3D::*)()const)&Body3D::getShapeDensity, (void(Body3D::*)(float))&Body3D::setShapeDensity)
        .addFunction("setShapeDensity", 
            luabridge::overload<float>(&Body3D::setShapeDensity),
            luabridge::overload<size_t, float>(&Body3D::setShapeDensity))
        .addFunction("getShapeDensity", 
            luabridge::overload<>(&Body3D::getShapeDensity),
            luabridge::overload<size_t>(&Body3D::getShapeDensity))
        .addProperty("type", &Body3D::getType, &Body3D::setType)
        .addProperty("motionQuality", &Body3D::getMotionQuality, &Body3D::setMotionQuality)
        .addFunction("canBeKinematicOrDynamic", &Body3D::canBeKinematicOrDynamic)
        .addFunction("activate", &Body3D::activate)
        .addFunction("deactivate", &Body3D::deactivate)
        .addProperty("sensor", &Body3D::isSensor, &Body3D::setIsSensor)
        .addProperty("collideKinematicVsNonDynamic", &Body3D::isCollideKinematicVsNonDynamic, &Body3D::setCollideKinematicVsNonDynamic)
        .addFunction("setAllowedDOFsAll", &Body3D::setAllowedDOFsAll)
        .addFunction("setAllowedDOFs2DPlane", &Body3D::setAllowedDOFs2DPlane)
        .addFunction("setAllowedDOFs", &Body3D::setAllowedDOFs)
        .addProperty("mass", &Body3D::getMass, &Body3D::setMass)
        .addFunction("setOverrideMassAndInertia", &Body3D::setOverrideMassAndInertia)
        .addProperty("gravityFactor", &Body3D::getGravityFactor, &Body3D::setGravityFactor)
        .addProperty("collisionGroupID", &Body3D::getCollisionGroupID, &Body3D::setCollisionGroupID)
        .addFunction("setBitsFilter", &Body3D::setBitsFilter)
        .addProperty("categoryBitsFilter", &Body3D::getCategoryBitsFilter, &Body3D::setCategoryBitsFilter)
        .addProperty("maskBitsFilter", &Body3D::getMaskBitsFilter, &Body3D::setMaskBitsFilter)
        .addProperty("allowSleeping", &Body3D::isAllowSleeping, &Body3D::setAllowSleeping)
        .addProperty("friction", &Body3D::getFriction, &Body3D::setFriction)
        .addProperty("restitution", &Body3D::getRestitution, &Body3D::setRestitution)
        .addProperty("position", &Body3D::getPosition, &Body3D::setPosition)
        .addProperty("rotation", &Body3D::getRotation, &Body3D::setRotation)
        .addProperty("linearVelocity", &Body3D::getLinearVelocity, &Body3D::setLinearVelocity)
        .addFunction("setLinearVelocityClamped", &Body3D::setLinearVelocityClamped)
        .addProperty("angularVelocity", &Body3D::getAngularVelocity, &Body3D::setAngularVelocity)
        .addFunction("setAngularVelocityClamped", &Body3D::setAngularVelocityClamped)
        .addFunction("getPointVelocityCOM", &Body3D::getPointVelocityCOM)
        .addFunction("getPointVelocity", &Body3D::getPointVelocity)
        .addFunction("getAccumulatedForce", &Body3D::getAccumulatedForce)
        .addFunction("getAccumulatedTorque", &Body3D::getAccumulatedTorque)
        .addFunction("getInverseInertia", &Body3D::getInverseInertia)
        .addFunction("applyForce", 
            luabridge::overload<const Vector3&>(&Body3D::applyForce),
            luabridge::overload<const Vector3&, const Vector3&>(&Body3D::applyForce))
        .addFunction("applyTorque", &Body3D::applyTorque)
        .addFunction("applyImpulse", 
            luabridge::overload<const Vector3&>(&Body3D::applyImpulse),
            luabridge::overload<const Vector3&, const Vector3&>(&Body3D::applyImpulse))
        .addFunction("applyAngularImpulse", &Body3D::applyAngularImpulse)
        .addFunction("applyBuoyancyImpulse", &Body3D::applyBuoyancyImpulse)
        .addFunction("getCenterOfMassPosition", &Body3D::getCenterOfMassPosition)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Joint3D>("Joint3D")
        .addConstructor <void (Scene*), void (Scene*, Entity)> ()
        .addFunction("setFixedJoint", &Joint3D::setFixedJoint)
        .addFunction("setDistanceJoint",
            luabridge::overload<Entity, Entity>(&Joint3D::setDistanceJoint),
            luabridge::overload<Entity, Entity, Vector3, Vector3>(&Joint3D::setDistanceJoint))
        .addFunction("setPointJoint", &Joint3D::setPointJoint)
        .addFunction("setHingeJoint", &Joint3D::setHingeJoint)
        .addFunction("setConeJoint", &Joint3D::setConeJoint)
        .addFunction("setPrismaticJoint", &Joint3D::setPrismaticJoint)
        .addFunction("setSwingTwistJoint", &Joint3D::setSwingTwistJoint)
        .addFunction("setSixDOFJoint", &Joint3D::setSixDOFJoint)
        .addFunction("setPathJoint", &Joint3D::setPathJoint)
        .addFunction("setGearJoint", &Joint3D::setGearJoint)
        .addFunction("setRackAndPinionJoint", &Joint3D::setRackAndPinionJoint)
        .addFunction("setPulleyJoint", &Joint3D::setPulleyJoint)
        .addFunction("getType", &Joint3D::getType)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Contact3D>("Contact3D")
        .addFunction("getBaseOffset", &Contact3D::getBaseOffset)
        .addFunction("getWorldSpaceNormal", &Contact3D::getWorldSpaceNormal)
        .addFunction("getPenetrationDepth", &Contact3D::getPenetrationDepth)
        .addFunction("getShapeIndex1", &Contact3D::getShapeIndex1)
        .addFunction("getShapeIndex2", &Contact3D::getShapeIndex2)
        .addFunction("getRelativeContactPointsOnA", &Contact3D::getRelativeContactPointsOnA)
        .addFunction("getRelativeContactPointsOnB", &Contact3D::getRelativeContactPointsOnB)
        .addProperty("combinedFriction", &Contact3D::getCombinedFriction, &Contact3D::setCombinedFriction)
        .addProperty("combinedRestitution", &Contact3D::getCombinedRestitution, &Contact3D::setCombinedRestitution)
        .addProperty("sensor", &Contact3D::isSensor, &Contact3D::setIsSensor)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<CollideShapeResult3D>("CollideShapeResult3D")
        .addFunction("getContactPointOnA", &CollideShapeResult3D::getContactPointOnA)
        .addFunction("getContactPointOnB", &CollideShapeResult3D::getContactPointOnB)
        .addFunction("getPenetrationAxis", &CollideShapeResult3D::getPenetrationAxis)
        .addFunction("getShapeIndex1", &CollideShapeResult3D::getShapeIndex1)
        .addFunction("getShapeIndex2", &CollideShapeResult3D::getShapeIndex2)
        .endClass();
#endif

#endif //DISABLE_LUA_BINDINGS
}
