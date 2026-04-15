//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "Engine.h"
#include "Log.h"
#include "Scene.h"
#include "SceneManager.h"
#include "BundleManager.h"
#include "Log.h"
#include "Input.h"
#include "System.h"
#include "Body2D.h"
#include "Contact2D.h"
#include "Manifold2D.h"
#include "Body3D.h"
#include "Contact3D.h"
#include "CollideShapeResult3D.h"

#include "subsystem/ActionSystem.h"
#include "subsystem/AudioSystem.h"
#include "subsystem/MeshSystem.h"
#include "subsystem/PhysicsSystem.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"

using namespace doriax;

void LuaBinding::registerCoreClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("Scaling")
        .addVariable("FITWIDTH", Scaling::FITWIDTH)
        .addVariable("FITHEIGHT", Scaling::FITHEIGHT)
        .addVariable("LETTERBOX", Scaling::LETTERBOX)
        .addVariable("CROP", Scaling::CROP)
        .addVariable("STRETCH", Scaling::STRETCH)
        .addVariable("NATIVE", Scaling::NATIVE)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("Platform")
        .addVariable("MacOS", Platform::MacOS)
        .addVariable("iOS", Platform::iOS)
        .addVariable("Web", Platform::Web)
        .addVariable("Android", Platform::Android)
        .addVariable("Linux", Platform::Linux)
        .addVariable("Windows", Platform::Windows)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("GraphicBackend")
        .addVariable("GLCORE", GraphicBackend::GLCORE)
        .addVariable("GLES3", GraphicBackend::GLES3)
        .addVariable("D3D11", GraphicBackend::D3D11)
        .addVariable("METAL", GraphicBackend::METAL)
        .addVariable("WGPU", GraphicBackend::WGPU)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("TextureStrategy")
        .addVariable("FIT", TextureStrategy::FIT)
        .addVariable("RESIZE", TextureStrategy::RESIZE)
        .addVariable("NONE", TextureStrategy::NONE)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("TextureType")
        .addVariable("TEXTURE_2D", TextureType::TEXTURE_2D)
        .addVariable("TEXTURE_3D", TextureType::TEXTURE_3D)
        .addVariable("TEXTURE_CUBE", TextureType::TEXTURE_CUBE)
        .addVariable("TEXTURE_ARRAY", TextureType::TEXTURE_ARRAY)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ColorFormat")
        .addVariable("RED", ColorFormat::RED)
        .addVariable("RGBA", ColorFormat::RGBA)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("PrimitiveType")
        .addVariable("TRIANGLES", PrimitiveType::TRIANGLES)
        .addVariable("TRIANGLE_STRIP", PrimitiveType::TRIANGLE_STRIP)
        .addVariable("POINTS", PrimitiveType::POINTS)
        .addVariable("LINES", PrimitiveType::LINES)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("CullingMode")
        .addVariable("BACK", CullingMode::BACK)
        .addVariable("FRONT", CullingMode::FRONT)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("WindingOrder")
        .addVariable("CCW", WindingOrder::CCW)
        .addVariable("CW", WindingOrder::CW)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("TextureFilter")
        .addVariable("NEAREST", TextureFilter::NEAREST)
        .addVariable("LINEAR", TextureFilter::LINEAR)
        .addVariable("NEAREST_MIPMAP_NEAREST", TextureFilter::NEAREST_MIPMAP_NEAREST)
        .addVariable("NEAREST_MIPMAP_LINEAR", TextureFilter::NEAREST_MIPMAP_LINEAR)
        .addVariable("LINEAR_MIPMAP_NEAREST", TextureFilter::LINEAR_MIPMAP_NEAREST)
        .addVariable("LINEAR_MIPMAP_LINEAR", TextureFilter::LINEAR_MIPMAP_LINEAR)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("TextureWrap")
        .addVariable("REPEAT", TextureWrap::REPEAT)
        .addVariable("MIRRORED_REPEAT", TextureWrap::MIRRORED_REPEAT)
        .addVariable("CLAMP_TO_EDGE", TextureWrap::CLAMP_TO_EDGE)
        .addVariable("CLAMP_TO_BORDER", TextureWrap::CLAMP_TO_BORDER)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("AdMobRating")
        .addVariable("General", AdMobRating::General)
        .addVariable("ParentalGuidance", AdMobRating::ParentalGuidance)
        .addVariable("Teen", AdMobRating::Teen)
        .addVariable("MatureAudience", AdMobRating::MatureAudience)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("CursorType")
        .addVariable("ARROW", CursorType::ARROW)
        .addVariable("IBEAM", CursorType::IBEAM)
        .addVariable("CROSSHAIR", CursorType::CROSSHAIR)
        .addVariable("POINTING_HAND", CursorType::POINTING_HAND)
        .addVariable("RESIZE_EW", CursorType::RESIZE_EW)
        .addVariable("RESIZE_NS", CursorType::RESIZE_NS)
        .addVariable("RESIZE_NWSE", CursorType::RESIZE_NWSE)
        .addVariable("RESIZE_NESW", CursorType::RESIZE_NESW)
        .addVariable("RESIZE_ALL", CursorType::RESIZE_ALL)
        .addVariable("NOT_ALLOWED", CursorType::NOT_ALLOWED)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ResourceLoadState")
        .addVariable("NotStarted", ResourceLoadState::NotStarted)
        .addVariable("Loading", ResourceLoadState::Loading)
        .addVariable("Finished", ResourceLoadState::Finished)
        .addVariable("Failed", ResourceLoadState::Failed)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("LightState")
        .addVariable("OFF", LightState::OFF)
        .addVariable("ON", LightState::ON)
        .addVariable("AUTO", LightState::AUTO)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("UIEventState")
        .addVariable("NOT_SET", UIEventState::NOT_SET)
        .addVariable("ENABLED", UIEventState::ENABLED)
        .addVariable("DISABLED", UIEventState::DISABLED)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("EntityPool")
        .addVariable("User", EntityPool::User)
        .addVariable("System", EntityPool::System)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<Engine>("Engine")

        .addStaticProperty("scene", &Engine::getScene, &Engine::setScene)
        .addStaticFunction("setScene", &Engine::setScene)
        .addStaticFunction("getScene", &Engine::getScene)
        .addStaticFunction("addSceneLayer", &Engine::addSceneLayer)
        .addStaticFunction("executeSceneOnce", &Engine::executeSceneOnce)
        .addStaticFunction("removeScene", &Engine::removeScene)
        .addStaticFunction("removeAllSceneLayers", &Engine::removeAllSceneLayers)
        .addStaticFunction("removeAllScenes", &Engine::removeAllScenes)
        .addStaticFunction("isSceneRunning", &Engine::isSceneRunning)
        .addStaticFunction("getMainScene", &Engine::getMainScene)
        .addStaticFunction("getLastScene", &Engine::getLastScene)
        .addStaticFunction("pauseGameEvents", &Engine::pauseGameEvents)
        .addStaticProperty("canvasWidth", &Engine::getCanvasWidth)
        .addStaticProperty("canvasHeight", &Engine::getCanvasHeight)
        .addStaticFunction("setCanvasSize", &Engine::setCanvasSize)
        .addStaticProperty("preferredCanvasWidth", &Engine::getPreferredCanvasWidth)
        .addStaticProperty("preferredCanvasHeight", &Engine::getPreferredCanvasHeight)
        .addStaticProperty("viewRect", &Engine::getViewRect)
        .addStaticProperty("scalingMode", &Engine::getScalingMode, &Engine::setScalingMode)
        .addStaticProperty("textureStrategy", &Engine::getTextureStrategy, &Engine::setTextureStrategy)
        .addStaticProperty("callMouseInTouchEvent", &Engine::isCallMouseInTouchEvent, &Engine::setCallMouseInTouchEvent)
        .addStaticProperty("callTouchInMouseEvent", &Engine::isCallTouchInMouseEvent, &Engine::setCallTouchInMouseEvent)
        .addStaticFunction("setCallTouchInMouseEvent", &Engine::setCallTouchInMouseEvent)
        .addStaticProperty("useDegrees", &Engine::isUseDegrees, &Engine::setUseDegrees)
        .addStaticProperty("allowEventsOutCanvas", &Engine::isAllowEventsOutCanvas, &Engine::setAllowEventsOutCanvas)
        .addStaticProperty("ignoreEventsHandledByUI", &Engine::isIgnoreEventsHandledByUI, &Engine::setIgnoreEventsHandledByUI)
        .addStaticFunction("isUIEventReceived", &Engine::isUIEventReceived)
        .addStaticProperty("fixedTimeSceneUpdate", &Engine::isFixedTimeSceneUpdate, &Engine::setFixedTimeSceneUpdate)
        .addStaticProperty("updateTime", &Engine::getUpdateTime, &Engine::setUpdateTime)
        .addStaticFunction("setUpdateTimeMS", &Engine::setUpdateTimeMS)
        .addStaticProperty("sceneUpdateTime", &Engine::getSceneUpdateTime)
        .addStaticProperty("mouseCursor", &Engine::getMouseCursor, &Engine::setMouseCursor)
        .addStaticProperty("platform", &Engine::getPlatform)
        .addStaticProperty("graphicBackend", &Engine::getGraphicBackend)
        .addStaticProperty("openGL", &Engine::isOpenGL)
        .addStaticProperty("framerate", &Engine::getFramerate)
        .addStaticProperty("deltatime", &Engine::getDeltatime)
        .addStaticFunction("startAsyncThread", &Engine::startAsyncThread)
        .addStaticFunction("commitThreadQueue", &Engine::commitThreadQueue)
        .addStaticFunction("endAsyncThread", &Engine::endAsyncThread)
        .addStaticFunction("isAsyncThread", &Engine::isAsyncThread)
        .addStaticFunction("isViewLoaded", &Engine::isViewLoaded)
        .addStaticFunction("setMaxResourceLoadingThreads", &Engine::setMaxResourceLoadingThreads)
        .addStaticFunction("getQueuedResourceCount", &Engine::getQueuedResourceCount)
        .addStaticProperty("framebuffer", &Engine::getFramebuffer, &Engine::setFramebuffer)
        .addStaticFunction("clearAllSubscriptions", &Engine::clearAllSubscriptions)

        .addStaticProperty("onViewLoaded", [] () { return &Engine::onViewLoaded; }, [] (lua_State* L) { Engine::onViewLoaded = L; })
        .addStaticProperty("onCanvasChanged", [] () { return &Engine::onViewChanged; }, [] (lua_State* L) { Engine::onViewChanged = L; })
        .addStaticProperty("onViewDestroyed", [] () { return &Engine::onViewDestroyed; }, [] (lua_State* L) { Engine::onViewDestroyed = L; })
        .addStaticProperty("onDraw", [] () { return &Engine::onDraw; }, [] (lua_State* L) { Engine::onDraw = L; })
        .addStaticProperty("onUpdate", [] () { return &Engine::onUpdate; }, [] (lua_State* L) { Engine::onUpdate = L; })
        .addStaticProperty("onPostUpdate", [] () { return &Engine::onPostUpdate; }, [] (lua_State* L) { Engine::onPostUpdate = L; })
        .addStaticProperty("onPause", [] () { return &Engine::onPause; }, [] (lua_State* L) { Engine::onPause = L; })
        .addStaticProperty("onResume", [] () { return &Engine::onResume; }, [] (lua_State* L) { Engine::onResume = L; })
        .addStaticProperty("onShutdown", [] () { return &Engine::onShutdown; }, [] (lua_State* L) { Engine::onShutdown = L; })
        .addStaticProperty("onTouchStart", [] () { return &Engine::onTouchStart; }, [] (lua_State* L) { Engine::onTouchStart = L; })
        .addStaticProperty("onTouchEnd", [] () { return &Engine::onTouchEnd; }, [] (lua_State* L) { Engine::onTouchEnd = L; })
        .addStaticProperty("onTouchMove", [] () { return &Engine::onTouchMove; }, [] (lua_State* L) { Engine::onTouchMove = L; })
        .addStaticProperty("onTouchCancel", [] () { return &Engine::onTouchCancel; }, [] (lua_State* L) { Engine::onTouchCancel = L; })
        .addStaticProperty("onMouseDown", [] () { return &Engine::onMouseDown; }, [] (lua_State* L) { Engine::onMouseDown = L; })
        .addStaticProperty("onMouseUp", [] () { return &Engine::onMouseUp; }, [] (lua_State* L) { Engine::onMouseUp = L; })
        .addStaticProperty("onMouseScroll", [] () { return &Engine::onMouseScroll; }, [] (lua_State* L) { Engine::onMouseScroll = L; })
        .addStaticProperty("onMouseMove", [] () { return &Engine::onMouseMove; }, [] (lua_State* L) { Engine::onMouseMove = L; })
        .addStaticProperty("onMouseEnter", [] () { return &Engine::onMouseEnter; }, [] (lua_State* L) { Engine::onMouseEnter = L; })
        .addStaticProperty("onMouseLeave", [] () { return &Engine::onMouseLeave; }, [] (lua_State* L) { Engine::onMouseLeave = L; })
        .addStaticProperty("onKeyDown", [] () { return &Engine::onKeyDown; }, [] (lua_State* L) { Engine::onKeyDown = L; })
        .addStaticProperty("onKeyUp", [] () { return &Engine::onKeyUp; }, [] (lua_State* L) { Engine::onKeyUp = L; })
        .addStaticProperty("onCharInput", [] () { return &Engine::onCharInput; }, [] (lua_State* L) { Engine::onCharInput = L; })

        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SceneManager>("SceneManager")
        .addStaticFunction("registerScene", &SceneManager::registerScene)
        .addStaticFunction("loadScene", 
            luabridge::overload<uint32_t>(&SceneManager::loadScene),
            luabridge::overload<const std::string&>(&SceneManager::loadScene))
        .addStaticFunction("getSceneId", &SceneManager::getSceneId)
        .addStaticFunction("getSceneName", &SceneManager::getSceneName)
        .addStaticFunction("getSceneNames", &SceneManager::getSceneNames)
        .addStaticProperty("sceneCount", &SceneManager::getSceneCount)
        .addStaticProperty("currentSceneId", &SceneManager::getCurrentSceneId)
        .addStaticProperty("currentSceneName", &SceneManager::getCurrentSceneName)
        .addStaticFunction("clearAll", &SceneManager::clearAll)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<BundleManager>("BundleManager")
        .addStaticFunction("createBundle",
            luabridge::overload<const std::string&, Scene*>(&BundleManager::createBundle),
            luabridge::overload<uint32_t, Scene*>(&BundleManager::createBundle),
            luabridge::overload<const std::string&, Scene*, Entity>(&BundleManager::createBundle),
            luabridge::overload<uint32_t, Scene*, Entity>(&BundleManager::createBundle))
        .addStaticFunction("destroyBundle", &BundleManager::destroyBundle)
        .addStaticFunction("getBundleId", &BundleManager::getBundleId)
        .addStaticFunction("getBundleName", &BundleManager::getBundleName)
        .addStaticFunction("getBundleNames", &BundleManager::getBundleNames)
        .addStaticProperty("bundleCount", &BundleManager::getBundleCount)
        .addStaticFunction("clearAll", &BundleManager::clearAll)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void()>>("FunctionSubscribe_V")
        .addFunction("__call", &FunctionSubscribe<void()>::call)
        .addFunction("call", &FunctionSubscribe<void()>::call)
        .addFunction("add", (bool (FunctionSubscribe<void()>::*)(const std::string&, lua_State*))&FunctionSubscribe<void()>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(float)>>("FunctionSubscribe_V_F")
        .addFunction("__call", &FunctionSubscribe<void(float)>::call)
        .addFunction("call", &FunctionSubscribe<void(float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(float)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(float,float)>>("FunctionSubscribe_V_FF")
        .addFunction("__call", &FunctionSubscribe<void(float,float)>::call)
        .addFunction("call", &FunctionSubscribe<void(float,float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(float,float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(float,float)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,int)>>("FunctionSubscribe_V_II")
        .addFunction("__call", &FunctionSubscribe<void(int,int)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,float,float)>>("FunctionSubscribe_V_IFF")
        .addFunction("__call", &FunctionSubscribe<void(int,float,float)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,float,float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,float,float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,float,float)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,float,float,int)>>("FunctionSubscribe_V_IFFI")
        .addFunction("__call", &FunctionSubscribe<void(int,float,float,int)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,float,float,int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,float,float,int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,float,float,int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,bool,int)>>("FunctionSubscribe_V_IBI")
        .addFunction("__call", &FunctionSubscribe<void(int,bool,int)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,bool,int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,bool,int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,bool,int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(wchar_t)>>("FunctionSubscribe_V_W")
        .addFunction("__call", &FunctionSubscribe<void(wchar_t)>::call)
        .addFunction("call", &FunctionSubscribe<void(wchar_t)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(wchar_t)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(wchar_t)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<float(float)>>("FunctionSubscribe_F_F")
        .addFunction("__call", &FunctionSubscribe<float(float)>::call)
        .addFunction("call", &FunctionSubscribe<float(float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<float(float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<float(float)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)>>("FunctionSubscribe_V_B2ULB2UL")
        .addFunction("__call", &FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)>>("FunctionSubscribe_V_B2ULB2ULV2V2F")
        .addFunction("__call", &FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body2D, unsigned long, Body2D, unsigned long, Vector2, Vector2, float)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)>>("FunctionSubscribe_B_B2ULB2UL")
        .addFunction("__call", &FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)>::call)
        .addFunction("call", &FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)>::call)
        .addFunction("add", (bool (FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)>::*)(const std::string&, lua_State*))&FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)>>("FunctionSubscribe_B_B2ULB2ULM2")
        .addFunction("__call", &FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)>::call)
        .addFunction("call", &FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)>::call)
        .addFunction("add", (bool (FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)>::*)(const std::string&, lua_State*))&FunctionSubscribe<bool(Body2D, unsigned long, Body2D, unsigned long, Manifold2D)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body3D)>>("FunctionSubscribe_V_B3")
        .addFunction("__call", &FunctionSubscribe<void(Body3D)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body3D)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body3D)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body3D)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body3D, Body3D, Contact3D)>>("FunctionSubscribe_V_B3B3C3")
        .addFunction("__call", &FunctionSubscribe<void(Body3D, Body3D, Contact3D)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body3D, Body3D, Contact3D)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body3D, Body3D, Contact3D)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body3D, Body3D, Contact3D)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)>>("FunctionSubscribe_V_B3B3ULUL")
        .addFunction("__call", &FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body3D, Body3D, unsigned long, unsigned long)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(Body3D, Body3D, Vector3, CollideShapeResult3D)>>("FunctionSubscribe_V_B3B3V3CR3")
        .addFunction("__call", &FunctionSubscribe<void(Body3D, Body3D, Vector3, CollideShapeResult3D)>::call)
        .addFunction("call", &FunctionSubscribe<void(Body3D, Body3D, Vector3, CollideShapeResult3D)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(Body3D, Body3D, Vector3, CollideShapeResult3D)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(Body3D, Body3D, Vector3, CollideShapeResult3D)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Texture>("Texture")
        .addConstructor <void (*) (void), void (*) (const std::string&), void (*) (const std::string&, TextureData), void (*) (Framebuffer*)> ()
        .addFunction("setPath", &Texture::setPath)
        .addFunction("setData", &Texture::setData)
        .addFunction("setId", &Texture::setId)
        .addFunction("setCubePath", &Texture::setCubePath)
        .addFunction("setCubePaths", &Texture::setCubePaths)
        .addFunction("setFramebuffer", &Texture::setFramebuffer)
        .addFunction("load", &Texture::load)
        .addFunction("destroy", &Texture::destroy)
        .addFunction("getRender", &Texture::getRender)
        .addFunction("getPath", &Texture::getPath)
        .addFunction("getData", &Texture::getData)
        .addFunction("getId", &Texture::getId)
        .addFunction("getNumFaces", &Texture::getNumFaces)
        .addFunction("getType", &Texture::getType)
        .addFunction("isCubeMap", &Texture::isCubeMap)
        .addFunction("getWidth", &Texture::getWidth)
        .addFunction("getHeight", &Texture::getHeight)
        .addFunction("isTransparent", &Texture::isTransparent)
        .addProperty("releaseDataAfterLoad", &Texture::isReleaseDataAfterLoad, &Texture::setReleaseDataAfterLoad)
        .addFunction("releaseData", &Texture::releaseData)
        .addFunction("empty", &Texture::empty)
        .addFunction("isFramebuffer", &Texture::isFramebuffer)
        .addFunction("isFramebufferOutdated", &Texture::isFramebufferOutdated)
        .addProperty("minFilter", &Texture::getMinFilter, &Texture::setMinFilter)
        .addProperty("magFilter", &Texture::getMagFilter, &Texture::setMagFilter)
        .addProperty("wrapU", &Texture::getWrapU, &Texture::setWrapU)
        .addProperty("wrapV", &Texture::getWrapV, &Texture::setWrapV)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TextureData>("TextureData")
        .addConstructor<
            void(), 
            void(int, int, unsigned int, ColorFormat, int, void*), 
            //void(unsigned char*, unsigned int), // TODO: missing constructor
            void(const char*)>()
        .addFunction("loadTextureFromFile", &TextureData::loadTextureFromFile)
        //.addFunction("loadTextureFromMemory", &TextureData::loadTextureFromMemory)
        .addFunction("releaseImageData", &TextureData::releaseImageData)
        .addFunction("hasAlpha", &TextureData::hasAlpha)
        .addFunction("resizePowerOfTwo", &TextureData::resizePowerOfTwo)
        .addFunction("fitPowerOfTwo", &TextureData::fitPowerOfTwo)
        .addFunction("resize", &TextureData::resize)
        .addFunction("crop", &TextureData::crop)
        .addFunction("fitSize", &TextureData::fitSize)
        .addFunction("flipVertical", &TextureData::flipVertical)
        .addFunction("getColorComponent", &TextureData::getColorComponent)
        .addProperty("dataOwned", &TextureData::getDataOwned, &TextureData::setDataOwned)
        .addFunction("getWidth", &TextureData::getWidth)
        .addFunction("getHeight", &TextureData::getHeight)
        .addFunction("getOriginalWidth", &TextureData::getOriginalWidth)
        .addFunction("getOriginalHeight", &TextureData::getOriginalHeight)
        .addFunction("getSize", &TextureData::getSize)
        .addFunction("getColorFormat", &TextureData::getColorFormat)
        .addFunction("getChannels", &TextureData::getChannels)
        .addFunction("getData", &TextureData::getData)
        .addFunction("isTransparent", &TextureData::isTransparent)
        .addFunction("getMinNearestPowerOfTwo", &TextureData::getMinNearestPowerOfTwo)
        .endClass();

        luabridge::getGlobalNamespace(L)
            .beginClass<TextureLoadResult>("TextureLoadResult")
            .addConstructor<void()>()
            .addProperty("id", &TextureLoadResult::id)
            .addProperty("state", &TextureLoadResult::state)
            .addProperty("errorMessage", &TextureLoadResult::errorMessage)
            .addProperty("data", &TextureLoadResult::data)
            .addFunction("__tostring", [] (const TextureLoadResult& result) {
                return "TextureLoadResult(id: " + result.id + ", state: " + std::to_string(static_cast<int>(result.state)) + ")";
            })
            .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Material>("Material")
        .addProperty("baseColorFactor", &Material::baseColorFactor)
        .addProperty("metallicFactor", &Material::metallicFactor)
        .addProperty("roughnessFactor", &Material::roughnessFactor)
        .addProperty("emissiveFactor", &Material::emissiveFactor)
        .addProperty("baseColorTexture", &Material::baseColorTexture)
        .addProperty("emissiveTexture", &Material::emissiveTexture)
        .addProperty("metallicRoughnessTexture", &Material::metallicRoughnessTexture)
        .addProperty("occlusionTexture", &Material::occlusionTexture)
        .addProperty("normalTexture", &Material::normalTexture)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Framebuffer>("Framebuffer")
        .addConstructor <void (*) (void)> ()
        .addFunction("create", &Framebuffer::create)
        .addFunction("destroy", &Framebuffer::destroy)
        .addFunction("isCreated", &Framebuffer::isCreated)
        .addFunction("getRender", &Framebuffer::getRender)
        .addFunction("getVersion", &Framebuffer::getVersion)
        .addFunction("create", &Framebuffer::create)
        .addFunction("width", &Framebuffer::getWidth, &Framebuffer::setWidth)
        .addFunction("height", &Framebuffer::getHeight, &Framebuffer::setHeight)
        .addProperty("minFilter", &Framebuffer::getMinFilter, &Framebuffer::setMinFilter)
        .addProperty("magFilter", &Framebuffer::getMagFilter, &Framebuffer::setMagFilter)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TextureRender>("TextureRender")
        .addConstructor <void (*) (void)> ()
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FramebufferRender>("FramebufferRender")
        .addConstructor <void (*) (void)> ()
        .addFunction("createFramebuffer", &FramebufferRender::createFramebuffer)
        .addFunction("destroyFramebuffer", &FramebufferRender::destroyFramebuffer)
        .addFunction("isCreated", &FramebufferRender::isCreated)
        .addFunction("getColorTexture", &FramebufferRender::getColorTexture)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<EntityRegistry>("EntityRegistry")
        .addConstructor <void (*) (void), void (*) (EntityPool)> ()
        .addFunction("createEntity", &EntityRegistry::createEntity)
        .addFunction("createUserEntity", &EntityRegistry::createUserEntity)
        .addFunction("createSystemEntity", &EntityRegistry::createSystemEntity)
        .addFunction("recreateEntity", &EntityRegistry::recreateEntity)
        .addFunction("isEntityCreated", &EntityRegistry::isEntityCreated)
        .addFunction("destroyEntity", &EntityRegistry::destroyEntity)
        .addFunction("setEntityName", &EntityRegistry::setEntityName)
        .addFunction("getEntityName", &EntityRegistry::getEntityName)
        .addFunction("getSignature", &EntityRegistry::getSignature)
        .addFunction("addEntityChild", &EntityRegistry::addEntityChild)
        .addFunction("moveChildToIndex", &EntityRegistry::moveChildToIndex)
        .addFunction("moveChildToTop", &EntityRegistry::moveChildToTop)
        .addFunction("moveChildUp", &EntityRegistry::moveChildUp)
        .addFunction("moveChildDown", &EntityRegistry::moveChildDown)
        .addFunction("moveChildToBottom", &EntityRegistry::moveChildToBottom)
        .addFunction("findOldestParent", &EntityRegistry::findOldestParent)
        .addFunction("isParentOf", &EntityRegistry::isParentOf)
        .addFunction("findBranchLastIndex", &EntityRegistry::findBranchLastIndex)
        .addFunction("setDefaultEntityPool", &EntityRegistry::setDefaultEntityPool)
        .addFunction("getDefaultEntityPool", &EntityRegistry::getDefaultEntityPool)
        .addFunction("getLastEntity", &EntityRegistry::getLastEntity)
        .addFunction("getEntityList", &EntityRegistry::getEntityList)
        .addFunction("clear", &EntityRegistry::clear)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Scene, EntityRegistry>("Scene")
        .addConstructor <void (*) (void), void (*) (EntityPool)> ()
        .addFunction("load", &Scene::load)
        .addFunction("destroy", &Scene::destroy)
        .addFunction("draw", &Scene::draw)
        .addFunction("update", &Scene::update)
        .addFunction("updateCameraSize", &Scene::updateCameraSize)
        .addProperty("camera", (Entity(Scene::*)()const)&Scene::getCamera, (void(Scene::*)(Entity))&Scene::setCamera)
        .addFunction("setCamera", 
            luabridge::overload<Camera*>(&Scene::setCamera),
            luabridge::overload<Entity>(&Scene::setCamera))
        .addProperty("backgroundColor", &Scene::getBackgroundColor, (void (Scene::*)(Vector4))&Scene::setBackgroundColor)
        .addFunction("setBackgroundColor", 
            luabridge::overload<float, float, float>(&Scene::setBackgroundColor),
            luabridge::overload<float, float, float, float>(&Scene::setBackgroundColor))
        .addProperty("shadowsPCF", &Scene::isShadowsPCF, &Scene::setShadowsPCF)
        .addProperty("lightState", &Scene::getLightState, &Scene::setLightState)
        .addProperty("globalIlluminationColor", &Scene::getGlobalIlluminationColor, (void (Scene::*)(Vector3))&Scene::setGlobalIllumination)
        .addProperty("globalIlluminationIntensity", &Scene::getGlobalIlluminationIntensity, (void (Scene::*)(float))&Scene::setGlobalIllumination)
        .addFunction("getGlobalIlluminationColorLinear", &Scene::getGlobalIlluminationColorLinear)
        .addFunction("setGlobalIllumination", (void (Scene::*)(float, Vector3))&Scene::setGlobalIllumination)
        .addFunction("canReceiveUIEvents", &Scene::canReceiveUIEvents)
        .addProperty("enableUIEvents", &Scene::getEnableUIEvents, (void (Scene::*)(UIEventState))&Scene::setEnableUIEvents)
        .addFunction("getActionSystem", [] (Scene* self, lua_State* L) { return self->getSystem<ActionSystem>().get(); })
        .addFunction("getAudioSystem", [] (Scene* self, lua_State* L) { return self->getSystem<AudioSystem>().get(); })
        .addFunction("getMeshSystem", [] (Scene* self, lua_State* L) { return self->getSystem<MeshSystem>().get(); })
        .addFunction("getPhysicsSystem", [] (Scene* self, lua_State* L) { return self->getSystem<PhysicsSystem>().get(); })
        .addFunction("getRenderSystem", [] (Scene* self, lua_State* L) { return self->getSystem<RenderSystem>().get(); })
        .addFunction("getUISystem", [] (Scene* self, lua_State* L) { return self->getSystem<UISystem>().get(); })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Log>("Log")
        .addStaticFunction("print", [] (const char* text) { Log::print(text); })
        .addStaticFunction("verbose", [] (const char* text) { Log::verbose(text); })
        .addStaticFunction("debug", [] (const char* text) { Log::debug(text); })
        .addStaticFunction("warn", [] (const char* text) { Log::warn(text); })
        .addStaticFunction("error", [] (const char* text) { Log::error(text); })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Input>("Input")
        .addStaticProperty("MODIFIER_SHIFT", [] () -> int { return S_MODIFIER_SHIFT; })
        .addStaticProperty("MODIFIER_CONTROL", [] () -> int { return S_MODIFIER_CONTROL; })
        .addStaticProperty("MODIFIER_ALT", [] () -> int { return S_MODIFIER_ALT; })
        .addStaticProperty("MODIFIER_SUPER", [] () -> int { return S_MODIFIER_SUPER; })
        .addStaticProperty("MODIFIER_CAPS_LOCK", [] () -> int { return S_MODIFIER_CAPS_LOCK; })
        .addStaticProperty("MODIFIER_NUM_LOCK", [] () -> int { return S_MODIFIER_NUM_LOCK; })
        .addStaticProperty("KEY_UNKNOWN", [] () -> int { return S_KEY_UNKNOWN; })
        .addStaticProperty("KEY_SPACE", [] () -> int { return S_KEY_SPACE; })
        .addStaticProperty("KEY_APOSTROPHE", [] () -> int { return S_KEY_APOSTROPHE; })
        .addStaticProperty("KEY_COMMA", [] () -> int { return S_KEY_COMMA; })
        .addStaticProperty("KEY_MINUS", [] () -> int { return S_KEY_MINUS; })
        .addStaticProperty("KEY_PERIOD", [] () -> int { return S_KEY_PERIOD; })
        .addStaticProperty("KEY_SLASH", [] () -> int { return S_KEY_SLASH; })
        .addStaticProperty("KEY_0", [] () -> int { return S_KEY_0; })
        .addStaticProperty("KEY_1", [] () -> int { return S_KEY_1; })
        .addStaticProperty("KEY_2", [] () -> int { return S_KEY_2; })
        .addStaticProperty("KEY_3", [] () -> int { return S_KEY_3; })
        .addStaticProperty("KEY_4", [] () -> int { return S_KEY_4; })
        .addStaticProperty("KEY_5", [] () -> int { return S_KEY_5; })
        .addStaticProperty("KEY_6", [] () -> int { return S_KEY_6; })
        .addStaticProperty("KEY_7", [] () -> int { return S_KEY_7; })
        .addStaticProperty("KEY_8", [] () -> int { return S_KEY_8; })
        .addStaticProperty("KEY_9", [] () -> int { return S_KEY_9; })
        .addStaticProperty("KEY_SEMICOLON", [] () -> int { return S_KEY_SEMICOLON; })
        .addStaticProperty("KEY_EQUAL", [] () -> int { return S_KEY_EQUAL; })
        .addStaticProperty("KEY_A", [] () -> int { return S_KEY_A; })
        .addStaticProperty("KEY_B", [] () -> int { return S_KEY_B; })
        .addStaticProperty("KEY_C", [] () -> int { return S_KEY_C; })
        .addStaticProperty("KEY_D", [] () -> int { return S_KEY_D; })
        .addStaticProperty("KEY_E", [] () -> int { return S_KEY_E; })
        .addStaticProperty("KEY_F", [] () -> int { return S_KEY_F; })
        .addStaticProperty("KEY_G", [] () -> int { return S_KEY_G; })
        .addStaticProperty("KEY_H", [] () -> int { return S_KEY_H; })
        .addStaticProperty("KEY_I", [] () -> int { return S_KEY_I; })
        .addStaticProperty("KEY_J", [] () -> int { return S_KEY_J; })
        .addStaticProperty("KEY_K", [] () -> int { return S_KEY_K; })
        .addStaticProperty("KEY_L", [] () -> int { return S_KEY_L; })
        .addStaticProperty("KEY_M", [] () -> int { return S_KEY_M; })
        .addStaticProperty("KEY_N", [] () -> int { return S_KEY_N; })
        .addStaticProperty("KEY_O", [] () -> int { return S_KEY_O; })
        .addStaticProperty("KEY_P", [] () -> int { return S_KEY_P; })
        .addStaticProperty("KEY_Q", [] () -> int { return S_KEY_Q; })
        .addStaticProperty("KEY_R", [] () -> int { return S_KEY_R; })
        .addStaticProperty("KEY_S", [] () -> int { return S_KEY_S; })
        .addStaticProperty("KEY_T", [] () -> int { return S_KEY_T; })
        .addStaticProperty("KEY_U", [] () -> int { return S_KEY_U; })
        .addStaticProperty("KEY_V", [] () -> int { return S_KEY_V; })
        .addStaticProperty("KEY_W", [] () -> int { return S_KEY_W; })
        .addStaticProperty("KEY_X", [] () -> int { return S_KEY_X; })
        .addStaticProperty("KEY_Y", [] () -> int { return S_KEY_Y; })
        .addStaticProperty("KEY_Z", [] () -> int { return S_KEY_Z; })
        .addStaticProperty("KEY_LEFT_BRACKET", [] () -> int { return S_KEY_LEFT_BRACKET; })
        .addStaticProperty("KEY_BACKSLASH", [] () -> int { return S_KEY_BACKSLASH; })
        .addStaticProperty("KEY_RIGHT_BRACKET", [] () -> int { return S_KEY_RIGHT_BRACKET; })
        .addStaticProperty("KEY_GRAVE_ACCENT", [] () -> int { return S_KEY_GRAVE_ACCENT; })
        .addStaticProperty("KEY_WORLD_1", [] () -> int { return S_KEY_WORLD_1; })
        .addStaticProperty("KEY_WORLD_2", [] () -> int { return S_KEY_WORLD_2; })
        .addStaticProperty("KEY_ESCAPE", [] () -> int { return S_KEY_ESCAPE; })
        .addStaticProperty("KEY_ENTER", [] () -> int { return S_KEY_ENTER; })
        .addStaticProperty("KEY_TAB", [] () -> int { return S_KEY_TAB; })
        .addStaticProperty("KEY_BACKSPACE", [] () -> int { return S_KEY_BACKSPACE; })
        .addStaticProperty("KEY_INSERT", [] () -> int { return S_KEY_INSERT; })
        .addStaticProperty("KEY_DELETE", [] () -> int { return S_KEY_DELETE; })
        .addStaticProperty("KEY_RIGHT", [] () -> int { return S_KEY_RIGHT; })
        .addStaticProperty("KEY_LEFT", [] () -> int { return S_KEY_LEFT; })
        .addStaticProperty("KEY_DOWN", [] () -> int { return S_KEY_DOWN; })
        .addStaticProperty("KEY_UP", [] () -> int { return S_KEY_UP; })
        .addStaticProperty("KEY_PAGE_UP", [] () -> int { return S_KEY_PAGE_UP; })
        .addStaticProperty("KEY_PAGE_DOWN", [] () -> int { return S_KEY_PAGE_DOWN; })
        .addStaticProperty("KEY_HOME", [] () -> int { return S_KEY_HOME; })
        .addStaticProperty("KEY_END", [] () -> int { return S_KEY_END; })
        .addStaticProperty("KEY_CAPS_LOCK", [] () -> int { return S_KEY_CAPS_LOCK; })
        .addStaticProperty("KEY_SCROLL_LOCK", [] () -> int { return S_KEY_SCROLL_LOCK; })
        .addStaticProperty("KEY_NUM_LOCK", [] () -> int { return S_KEY_NUM_LOCK; })
        .addStaticProperty("KEY_PRINT_SCREEN", [] () -> int { return S_KEY_PRINT_SCREEN; })
        .addStaticProperty("KEY_PAUSE", [] () -> int { return S_KEY_PAUSE; })
        .addStaticProperty("KEY_F1", [] () -> int { return S_KEY_F1; })
        .addStaticProperty("KEY_F2", [] () -> int { return S_KEY_F2; })
        .addStaticProperty("KEY_F3", [] () -> int { return S_KEY_F3; })
        .addStaticProperty("KEY_F4", [] () -> int { return S_KEY_F4; })
        .addStaticProperty("KEY_F5", [] () -> int { return S_KEY_F5; })
        .addStaticProperty("KEY_F6", [] () -> int { return S_KEY_F6; })
        .addStaticProperty("KEY_F7", [] () -> int { return S_KEY_F7; })
        .addStaticProperty("KEY_F8", [] () -> int { return S_KEY_F8; })
        .addStaticProperty("KEY_F9", [] () -> int { return S_KEY_F9; })
        .addStaticProperty("KEY_F10", [] () -> int { return S_KEY_F10; })
        .addStaticProperty("KEY_F11", [] () -> int { return S_KEY_F11; })
        .addStaticProperty("KEY_F12", [] () -> int { return S_KEY_F12; })
        .addStaticProperty("KEY_F13", [] () -> int { return S_KEY_F13; })
        .addStaticProperty("KEY_F14", [] () -> int { return S_KEY_F14; })
        .addStaticProperty("KEY_F15", [] () -> int { return S_KEY_F15; })
        .addStaticProperty("KEY_F16", [] () -> int { return S_KEY_F16; })
        .addStaticProperty("KEY_F17", [] () -> int { return S_KEY_F17; })
        .addStaticProperty("KEY_F18", [] () -> int { return S_KEY_F18; })
        .addStaticProperty("KEY_F19", [] () -> int { return S_KEY_F19; })
        .addStaticProperty("KEY_F20", [] () -> int { return S_KEY_F20; })
        .addStaticProperty("KEY_F21", [] () -> int { return S_KEY_F21; })
        .addStaticProperty("KEY_F22", [] () -> int { return S_KEY_F22; })
        .addStaticProperty("KEY_F23", [] () -> int { return S_KEY_F23; })
        .addStaticProperty("KEY_F24", [] () -> int { return S_KEY_F24; })
        .addStaticProperty("KEY_F25", [] () -> int { return S_KEY_F25; })
        .addStaticProperty("KEY_KP_0", [] () -> int { return S_KEY_KP_0; })
        .addStaticProperty("KEY_KP_1", [] () -> int { return S_KEY_KP_1; })
        .addStaticProperty("KEY_KP_2", [] () -> int { return S_KEY_KP_2; })
        .addStaticProperty("KEY_KP_3", [] () -> int { return S_KEY_KP_3; })
        .addStaticProperty("KEY_KP_4", [] () -> int { return S_KEY_KP_4; })
        .addStaticProperty("KEY_KP_5", [] () -> int { return S_KEY_KP_5; })
        .addStaticProperty("KEY_KP_6", [] () -> int { return S_KEY_KP_6; })
        .addStaticProperty("KEY_KP_7", [] () -> int { return S_KEY_KP_7; })
        .addStaticProperty("KEY_KP_8", [] () -> int { return S_KEY_KP_8; })
        .addStaticProperty("KEY_KP_9", [] () -> int { return S_KEY_KP_9; })
        .addStaticProperty("KEY_KP_DECIMAL", [] () -> int { return S_KEY_KP_DECIMAL; })
        .addStaticProperty("KEY_KP_DIVIDE", [] () -> int { return S_KEY_KP_DIVIDE; })
        .addStaticProperty("KEY_KP_MULTIPLY", [] () -> int { return S_KEY_KP_MULTIPLY; })
        .addStaticProperty("KEY_KP_SUBTRACT", [] () -> int { return S_KEY_KP_SUBTRACT; })
        .addStaticProperty("KEY_KP_ADD", [] () -> int { return S_KEY_KP_ADD; })
        .addStaticProperty("KEY_KP_ENTER", [] () -> int { return S_KEY_KP_ENTER; })
        .addStaticProperty("KEY_KP_EQUAL", [] () -> int { return S_KEY_KP_EQUAL; })
        .addStaticProperty("KEY_LEFT_SHIFT", [] () -> int { return S_KEY_LEFT_SHIFT; })
        .addStaticProperty("KEY_LEFT_CONTROL", [] () -> int { return S_KEY_LEFT_CONTROL; })
        .addStaticProperty("KEY_LEFT_ALT", [] () -> int { return S_KEY_LEFT_ALT; })
        .addStaticProperty("KEY_LEFT_SUPER", [] () -> int { return S_KEY_LEFT_SUPER; })
        .addStaticProperty("KEY_RIGHT_SHIFT", [] () -> int { return S_KEY_RIGHT_SHIFT; })
        .addStaticProperty("KEY_RIGHT_CONTROL", [] () -> int { return S_KEY_RIGHT_CONTROL; })
        .addStaticProperty("KEY_RIGHT_ALT", [] () -> int { return S_KEY_RIGHT_ALT; })
        .addStaticProperty("KEY_RIGHT_SUPER", [] () -> int { return S_KEY_RIGHT_SUPER; })
        .addStaticProperty("KEY_MENU", [] () -> int { return S_KEY_MENU; })
        .addStaticProperty("KEY_LAST", [] () -> int { return S_KEY_LAST; })

        .addStaticFunction("isKeyPressed", &Input::isKeyPressed)
        .addStaticFunction("isMousePressed", &Input::isMousePressed)
        .addStaticFunction("isTouch", &Input::isTouch)
        .addStaticFunction("isMouseEntered", &Input::isMouseEntered)
        .addStaticFunction("getMousePosition", &Input::getMousePosition)
        .addStaticFunction("getMouseScroll", &Input::getMouseScroll)
        .addStaticFunction("getTouchPosition", &Input::getTouchPosition)
        .addStaticFunction("getTouches", &Input::getTouches)
        .addStaticFunction("numTouches", &Input::numTouches)
        .addStaticFunction("getModifiers", &Input::getModifiers)
        .addStaticFunction("findTouchIndex", &Input::findTouchIndex)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<System>("System")
        .addStaticFunction("getScreenWidth", [] () { return System::instance().getScreenWidth(); })
        .addStaticFunction("getScreenHeight", [] () { return System::instance().getScreenHeight(); })
        .addStaticFunction("showVirtualKeyboard", [] (std::wstring text) { System::instance().showVirtualKeyboard(text); })
        .addStaticFunction("hideVirtualKeyboard", [] () { System::instance().hideVirtualKeyboard(); })
        .addStaticFunction("isFullscreen", [] () { return System::instance().isFullscreen(); })
        .addStaticFunction("requestFullscreen", [] () { System::instance().requestFullscreen(); })
        .addStaticFunction("exitFullscreen", [] () { System::instance().exitFullscreen(); })
        .addStaticFunction("getDirSeparator", [] () { return System::instance().getDirSeparator(); })
        .addStaticFunction("getAssetPath", [] () { return System::instance().getAssetPath(); })
        .addStaticFunction("getUserDataPath", [] () { return System::instance().getUserDataPath(); })
        .addStaticFunction("getLuaPath", [] () { return System::instance().getLuaPath(); })
        .addStaticFunction("getShaderPath", [] () { return System::instance().getShaderPath(); })

        //UserSettings not need here

        .addStaticFunction("initializeAdMob", [] () { return System::instance().initializeAdMob(); })
        .addStaticFunction("setMaxAdContentRating", [] (AdMobRating rating) { return System::instance().setMaxAdContentRating(rating); })
        .addStaticFunction("loadInterstitialAd", [] (const std::string& adUnitID) { return System::instance().loadInterstitialAd(adUnitID); })
        .addStaticFunction("isInterstitialAdLoaded", [] () { return System::instance().isInterstitialAdLoaded(); })
        .addStaticFunction("showInterstitialAd", [] () { return System::instance().showInterstitialAd(); })

        .addStaticFunction("initializeCrazyGamesSDK", [] () { return System::instance().initializeCrazyGamesSDK(); })
        .addStaticFunction("showCrazyGamesAd", [] (std::string type) { return System::instance().showCrazyGamesAd(type); })
        .addStaticFunction("happytimeCrazyGames", [] () { return System::instance().happytimeCrazyGames(); })
        .addStaticFunction("gameplayStartCrazyGames", [] () { return System::instance().gameplayStartCrazyGames(); })
        .addStaticFunction("gameplayStopCrazyGames", [] () { return System::instance().gameplayStopCrazyGames(); })
        .addStaticFunction("loadingStartCrazyGames", [] () { return System::instance().loadingStartCrazyGames(); })
        .addStaticFunction("loadingStopCrazyGames", [] () { return System::instance().loadingStopCrazyGames(); })
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}