//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "EntityHandle.h"
#include "Fog.h"
#include "SkyBox.h"
#include "Object.h"
#include "Camera.h"
#include "Polygon.h"
#include "Terrain.h"
#include "Light.h"
#include "Mesh.h"
#include "Shape.h"
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
#include "TextEdit.h"
#include "Container.h"
#include "Audio.h"
#include "Body2D.h"
#include "Joint2D.h"
#include "Contact2D.h"
#include "Manifold2D.h"
#include "Body3D.h"
#include "Joint3D.h"
#include "Contact3D.h"
#include "CollideShapeResult3D.h"

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

    luabridge::getGlobalNamespace(L)
        .beginNamespace("Shape2DType")
        .addVariable("POLYGON", Shape2DType::POLYGON)
        .addVariable("CIRCLE", Shape2DType::CIRCLE)
        .addVariable("CAPSULE", Shape2DType::CAPSULE)
        .addVariable("SEGMENT", Shape2DType::SEGMENT)
        .addVariable("CHAIN", Shape2DType::CHAIN)
        .endNamespace();

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

    luabridge::getGlobalNamespace(L)
        .beginNamespace("BodyType")
        .addVariable("STATIC", BodyType::STATIC)
        .addVariable("KINEMATIC", BodyType::KINEMATIC)
        .addVariable("DYNAMIC", BodyType::DYNAMIC)
        .endNamespace();

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
        .addFunction("getBody2D", &Object::getBody2D)
        .addFunction("removeBody2D", &Object::removeBody2D)
        .addFunction("getBody3D", &Object::getBody3D)
        .addFunction("removeBody3D", &Object::removeBody3D)
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
        .deriveClass<Mesh, Object>("Mesh")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setTexture", 
            luabridge::overload<const std::string&>(&Mesh::setTexture),
            luabridge::overload<const std::string&,TextureData>(&Mesh::setTexture),
            luabridge::overload<Framebuffer*>(&Mesh::setTexture))
        .addProperty("color", &Mesh::getColor, (void(Mesh::*)(Vector4))&Mesh::setColor)
        .addFunction("setColor", 
            luabridge::overload<const float, const float, const float>(&Mesh::setColor),
            luabridge::overload<const float, const float, const float, const float>(&Mesh::setColor))
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
        .addProperty("castShadows", &Mesh::isCastShadows, &Mesh::setCastShadows)
        .addProperty("receiveShadows", &Mesh::isReceiveShadows, &Mesh::setReceiveShadows)
        .addProperty("shadowsBillboard", &Mesh::isShadowsBillboard, &Mesh::setShadowsBillboard)
        .addProperty("transparent", &Mesh::isTransparent, &Mesh::setTransparent)
        .addProperty("autoTransparency", &Mesh::isAutoTransparency, &Mesh::setAutoTransparency)
        .addFunction("createInstancedMesh", &Mesh::createInstancedMesh)
        .addFunction("removeInstancedMesh", &Mesh::removeInstancedMesh)
        .addFunction("hasInstancedMesh", &Mesh::hasInstancedMesh)
        .addFunction("setInstancedBillboard", (void(Mesh::*)(bool, bool))&Mesh::setInstancedBillboard)
        .addProperty("instancedBillboard", &Mesh::isInstancedBillboard, (void(Mesh::*)(bool))&Mesh::setInstancedBillboard)
        .addProperty("instancedCylindricalBillboard", &Mesh::isInstancedCylindricalBillboard, &Mesh::setInstancedCylindricalBillboard)
        .addProperty("maxInstances", &Mesh::getMaxInstances, &Mesh::setMaxInstances)
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
        .deriveClass<Terrain, Mesh>("Terrain")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setHeightMap", (void(Terrain::*)(const std::string&))&Terrain::setHeightMap)
        .addFunction("setBlendMap", (void(Terrain::*)(const std::string&))&Terrain::setBlendMap)
        .addFunction("setTextureDetailRed", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailRed)
        .addFunction("setTextureDetailGreen", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailGreen)
        .addFunction("setTextureDetailBlue", (void(Terrain::*)(const std::string&))&Terrain::setTextureDetailBlue)
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
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Sprite, Mesh>("Sprite")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
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
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Text, UILayout>("Text")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setFixedSize", &Text::setFixedSize)
        .addProperty("fixedWidth", &Text::isFixedWidth, &Text::setFixedWidth)
        .addProperty("fixedHeight", &Text::isFixedHeight, &Text::setFixedHeight)
        .addProperty("maxTextSize", &Text::getMaxTextSize, &Text::setMaxTextSize)
        .addProperty("text", &Text::getText, &Text::setText)
        .addProperty("font", &Text::getFont, &Text::setFont)
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
        .addFunction("setTexturePressed", (void(Button::*)(const std::string&))&Button::setTexturePressed)
        .addFunction("setTextureDisabled", (void(Button::*)(const std::string&))&Button::setTextureDisabled)
        .addProperty("colorNormal", &Button::getColorNormal, (void(Button::*)(Vector4))&Button::setColorNormal)
        .addFunction("setColorNormal", 
            luabridge::overload<const float, const float, const float>(&Button::setColorNormal),
            luabridge::overload<const float, const float, const float, const float>(&Button::setColorNormal))
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
        .addFunction("getScrollbarComponent", &Scrollbar::getComponent<ScrollbarComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TextEdit, Image>("TextEdit")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("getTextObject", &TextEdit::getTextObject)
        .addProperty("disabled", &TextEdit::getDisabled, &TextEdit::setDisabled)
        .addProperty("text", &TextEdit::getText, &TextEdit::setText)
        .addProperty("textColor", &TextEdit::getTextColor, (void(TextEdit::*)(Vector4))&TextEdit::setTextColor)
        .addFunction("setTextColor", 
            luabridge::overload<const float, const float, const float>(&TextEdit::setTextColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setTextColor))
        .addProperty("textFont", &TextEdit::getTextFont, &TextEdit::setTextFont)
        .addProperty("fontSize", &TextEdit::getFontSize, &TextEdit::setFontSize)
        .addProperty("maxTextSize", &TextEdit::getMaxTextSize, &TextEdit::setMaxTextSize)
        .addProperty("cursorColor", &TextEdit::getCursorColor, (void(TextEdit::*)(Vector4))&TextEdit::setCursorColor)
        .addFunction("setCursorColor", 
            luabridge::overload<const float, const float, const float>(&TextEdit::setCursorColor),
            luabridge::overload<const float, const float, const float, const float>(&TextEdit::setCursorColor))
        .addProperty("cursorBlink", &TextEdit::getCursorBlink, &TextEdit::setCursorBlink)
        .addProperty("cursorWidth", &TextEdit::getCursorWidth, &TextEdit::setCursorWidth)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Audio, Object>("Audio")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("loadAudio", &Audio::loadAudio)
        .addFunction("destroyAudio", &Audio::destroyAudio)
        .addFunction("play", &Audio::play)
        .addFunction("pause", &Audio::pause)
        .addFunction("stop", &Audio::stop)
        .addFunction("seek", &Audio::seek)
        .addFunction("getLength", &Audio::getLength)
        .addFunction("getPlayingTime", &Audio::getPlayingTime)
        .addFunction("isPlaying", &Audio::isPlaying)
        .addFunction("isPaused", &Audio::isPaused)
        .addFunction("isStopped", &Audio::isStopped)
        .addProperty("sound3D", &Audio::isSound3D, &Audio::setSound3D)
        .addProperty("clockedSound", &Audio::isClockedSound, &Audio::setClockedSound)
        .addProperty("volume", &Audio::getVolume, &Audio::setVolume)
        .addProperty("speed", &Audio::getSpeed, &Audio::setSpeed)
        .addProperty("pan", &Audio::getPan, &Audio::setPan)
        .addProperty("lopping", &Audio::isLopping, &Audio::setLopping)
        .addProperty("loopingPoint", &Audio::getLoopingPoint, &Audio::setLoopingPoint)
        .addProperty("protectVoice", &Audio::isProtectVoice, &Audio::setProtectVoice)
        .addFunction("setInaudibleBehavior", &Audio::setInaudibleBehavior)
        .addFunction("setMinMaxDistance", &Audio::setMinMaxDistance)
        .addProperty("minDistance", &Audio::getMinDistance, &Audio::setMinDistance)
        .addProperty("maxDistance", &Audio::getMaxDistance, &Audio::setMaxDistance)
        .addProperty("attenuationModel", &Audio::getAttenuationModel, &Audio::setAttenuationModel)
        .addProperty("attenuationRolloffFactor", &Audio::getAttenuationRolloffFactor, &Audio::setAttenuationRolloffFactor)
        .addProperty("dopplerFactor", &Audio::getDopplerFactor, &Audio::setDopplerFactor)
        .addFunction("getAudioComponent", &Image::getComponent<AudioComponent>)
        .endClass();

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
        .addProperty("sllowSleeping", &Body3D::isAllowSleeping, &Body3D::setAllowSleeping)
        .addProperty("friction", &Body3D::getFriction, &Body3D::setFriction)
        .addProperty("restitution", &Body3D::getRestitution, &Body3D::setRestitution)
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

#endif //DISABLE_LUA_BINDINGS
}
