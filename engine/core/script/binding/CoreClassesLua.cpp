// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "Engine.h"
#include "Log.h"
#include "Scene.h"
#include "SceneManager.h"
#include "BundleManager.h"
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
        .beginNamespace("MaterialAlphaMode")
        .addVariable("AUTO", MaterialAlphaMode::AUTO)
        .addVariable("ALPHA_OPAQUE", MaterialAlphaMode::ALPHA_OPAQUE)
        .addVariable("MASK", MaterialAlphaMode::MASK)
        .addVariable("BLEND", MaterialAlphaMode::BLEND)
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
        .beginNamespace("MouseMode")
        .addVariable("NORMAL", MouseMode::NORMAL)
        .addVariable("HIDDEN", MouseMode::HIDDEN)
        .addVariable("CAPTURED", MouseMode::CAPTURED)
        .addVariable("CONFINED", MouseMode::CONFINED)
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
        .beginNamespace("ShadowQuality")
        .addVariable("NONE", ShadowQuality::NONE)
        .addVariable("LOW", ShadowQuality::LOW)
        .addVariable("MEDIUM", ShadowQuality::MEDIUM)
        .addVariable("HIGH", ShadowQuality::HIGH)
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

    // The "no entity" sentinel, mirroring the C++ NULL_ENTITY macro. Scene:findEntity
    // returns this when no match is found.
    luabridge::getGlobalNamespace(L)
        .addVariable("NULL_ENTITY", static_cast<Entity>(NULL_ENTITY));

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
        .addStaticFunction("hasScenesToExecuteOnce", &Engine::hasScenesToExecuteOnce)
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
        .addStaticProperty("updateTime", &Engine::getUpdateTime, &Engine::setUpdateTime)
        .addStaticFunction("setUpdateTimeMS", &Engine::setUpdateTimeMS)
        .addStaticProperty("interpolationAlpha", &Engine::getInterpolationAlpha)
        .addStaticProperty("mouseCursor", &Engine::getMouseCursor, &Engine::setMouseCursor)
        .addStaticProperty("mouseMode", &Engine::getMouseMode, &Engine::setMouseMode)
        .addStaticFunction("setMousePosition", &Engine::setMousePosition)
        .addStaticProperty("platform", &Engine::getPlatform)
        .addStaticProperty("graphicBackend", &Engine::getGraphicBackend)
        .addStaticProperty("openGL", &Engine::isOpenGL)
        .addStaticProperty("framerate", &Engine::getFramerate)
        .addStaticProperty("deltatime", &Engine::getDeltatime)
        .addStaticProperty("systemTime", &Engine::getSystemTime)
        .addStaticProperty("asyncLoading", &Engine::isAsyncLoading, &Engine::setAsyncLoading)
        .addStaticFunction("startAsyncThread", &Engine::startAsyncThread)
        .addStaticFunction("commitThreadQueue", &Engine::commitThreadQueue)
        .addStaticFunction("endAsyncThread", &Engine::endAsyncThread)
        .addStaticFunction("isAsyncThread", &Engine::isAsyncThread)
        .addStaticFunction("isViewLoaded", &Engine::isViewLoaded)
        .addStaticFunction("setMaxResourceLoadingThreads", &Engine::setMaxResourceLoadingThreads)
        .addStaticFunction("getQueuedResourceCount", &Engine::getQueuedResourceCount)
        .addStaticProperty("framebuffer", &Engine::getFramebuffer, &Engine::setFramebuffer)
        .addStaticFunction("clearAllSubscriptions", &Engine::clearAllSubscriptions)
        .addStaticFunction("clearComponentSubscriptions", &Engine::clearComponentSubscriptions)

        .addStaticProperty("onViewLoaded", [] () { return &Engine::onViewLoaded; }, [] (lua_State* L) { Engine::onViewLoaded = L; })
        .addStaticProperty("onCanvasChanged", [] () { return &Engine::onViewChanged; }, [] (lua_State* L) { Engine::onViewChanged = L; })
        .addStaticProperty("onViewDestroyed", [] () { return &Engine::onViewDestroyed; }, [] (lua_State* L) { Engine::onViewDestroyed = L; })
        .addStaticProperty("onDraw", [] () { return &Engine::onDraw; }, [] (lua_State* L) { Engine::onDraw = L; })
        .addStaticProperty("onUpdate", [] () { return &Engine::onUpdate; }, [] (lua_State* L) { Engine::onUpdate = L; })
        .addStaticProperty("onFixedUpdate", [] () { return &Engine::onFixedUpdate; }, [] (lua_State* L) { Engine::onFixedUpdate = L; })
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
        .addStaticProperty("onGamepadConnect", [] () { return &Engine::onGamepadConnect; }, [] (lua_State* L) { Engine::onGamepadConnect = L; })
        .addStaticProperty("onGamepadDisconnect", [] () { return &Engine::onGamepadDisconnect; }, [] (lua_State* L) { Engine::onGamepadDisconnect = L; })
        .addStaticProperty("onGamepadButtonDown", [] () { return &Engine::onGamepadButtonDown; }, [] (lua_State* L) { Engine::onGamepadButtonDown = L; })
        .addStaticProperty("onGamepadButtonUp", [] () { return &Engine::onGamepadButtonUp; }, [] (lua_State* L) { Engine::onGamepadButtonUp = L; })
        .addStaticProperty("onGamepadAxisMove", [] () { return &Engine::onGamepadAxisMove; }, [] (lua_State* L) { Engine::onGamepadAxisMove = L; })

        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PostProcessPass>("PostProcessPass")
        .addConstructor<void (*) (void)>()
        .addProperty("shader", &PostProcessPass::shader, &PostProcessPass::shader)
        .addProperty("enabled", &PostProcessPass::enabled, &PostProcessPass::enabled)
        .addFunction("setUniform",
            luabridge::overload<const std::string&, const Vector4&>(&PostProcessPass::setUniform),
            luabridge::overload<const std::string&, const Vector3&>(&PostProcessPass::setUniform),
            luabridge::overload<const std::string&, const Vector2&>(&PostProcessPass::setUniform),
            luabridge::overload<const std::string&, float>(&PostProcessPass::setUniform))
        .addFunction("getUniform", &PostProcessPass::getUniform)
        .addFunction("removeUniform", &PostProcessPass::removeUniform)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SceneManager>("SceneManager")
        .addStaticFunction("registerScene", luabridge::overload<uint32_t, const std::string&, std::function<void()>, std::function<void()>>(&SceneManager::registerScene))
        .addStaticFunction("loadScene", 
            luabridge::overload<uint32_t>(&SceneManager::loadScene),
            luabridge::overload<const std::string&>(&SceneManager::loadScene))
        .addStaticFunction("addChildScene",
            luabridge::overload<uint32_t>(&SceneManager::addChildScene),
            luabridge::overload<const std::string&>(&SceneManager::addChildScene))
        .addStaticFunction("removeChildScene",
            luabridge::overload<uint32_t>(&SceneManager::removeChildScene),
            luabridge::overload<const std::string&>(&SceneManager::removeChildScene))
        .addStaticFunction("getSceneId", &SceneManager::getSceneId)
        .addStaticFunction("getSceneName", &SceneManager::getSceneName)
        .addStaticFunction("getSceneNames", &SceneManager::getSceneNames)
        .addStaticProperty("sceneCount", &SceneManager::getSceneCount)
        .addStaticProperty("currentSceneId", &SceneManager::getCurrentSceneId)
        .addStaticProperty("currentSceneName", &SceneManager::getCurrentSceneName)
        .addStaticProperty("loadPending", &SceneManager::isLoadPending)
        .addStaticFunction("clearAll", &SceneManager::clearAll)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<BundleManager>("BundleManager")
        // Lua reads a number as a string too, so the id and entity overloads have to be
        // offered before the ones taking a name
        .addStaticFunction("createBundle",
            luabridge::overload<uint32_t, Scene*>(&BundleManager::createBundle),
            luabridge::overload<uint32_t, Scene*, Entity>(&BundleManager::createBundle),
            luabridge::overload<uint32_t, Scene*, const std::string&>(&BundleManager::createBundle),
            luabridge::overload<uint32_t, const EntityHandle&>(&BundleManager::createBundle),
            luabridge::overload<const std::string&, Scene*>(&BundleManager::createBundle),
            luabridge::overload<const std::string&, Scene*, Entity>(&BundleManager::createBundle),
            luabridge::overload<const std::string&, Scene*, const std::string&>(&BundleManager::createBundle),
            luabridge::overload<const std::string&, const EntityHandle&>(&BundleManager::createBundle))
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
        .beginClass<FunctionSubscribe<void(float,float,int)>>("FunctionSubscribe_V_FFI")
        .addFunction("__call", &FunctionSubscribe<void(float,float,int)>::call)
        .addFunction("call", &FunctionSubscribe<void(float,float,int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(float,float,int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(float,float,int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int)>>("FunctionSubscribe_V_I")
        .addFunction("__call", &FunctionSubscribe<void(int)>::call)
        .addFunction("call", &FunctionSubscribe<void(int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,int)>>("FunctionSubscribe_V_II")
        .addFunction("__call", &FunctionSubscribe<void(int,int)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,int)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,int)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,int)>::add)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FunctionSubscribe<void(int,int,float)>>("FunctionSubscribe_V_IIF")
        .addFunction("__call", &FunctionSubscribe<void(int,int,float)>::call)
        .addFunction("call", &FunctionSubscribe<void(int,int,float)>::call)
        .addFunction("add", (bool (FunctionSubscribe<void(int,int,float)>::*)(const std::string&, lua_State*))&FunctionSubscribe<void(int,int,float)>::add)
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
        .beginClass<FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)>>("FunctionSubscribe_B_B3B3V3CR3")
        .addFunction("__call", &FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)>::call)
        .addFunction("call", &FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)>::call)
        .addFunction("add", (bool (FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)>::*)(const std::string&, lua_State*))&FunctionSubscribe<bool(Body3D, Body3D, Vector3, CollideShapeResult3D)>::add)
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
        .addProperty("svgScale", &Texture::getSvgScale, &Texture::setSvgScale)
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
        .addProperty("svgScale", &TextureData::getSVGScale, &TextureData::setSVGScale)
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
        // A load result, read-only on purpose
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
        // Mesh::getMaterial hands back a copy, so these stay read-only: a write would look
        // like it worked and change nothing. Use Mesh::setMaterial.
        .beginClass<Material>("Material")
        .addProperty("baseColorFactor", &Material::baseColorFactor)
        .addProperty("metallicFactor", &Material::metallicFactor)
        .addProperty("roughnessFactor", &Material::roughnessFactor)
        .addProperty("alphaMode", &Material::alphaMode)
        .addProperty("alphaCutoff", &Material::alphaCutoff)
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
        .addFunction("findEntity",
            luabridge::constOverload<const std::string&>(&EntityRegistry::findEntity),
            luabridge::constOverload<const std::string&, Entity>(&EntityRegistry::findEntity))
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
        .addProperty("shadowQuality", &Scene::getShadowQuality, &Scene::setShadowQuality)
        .addProperty("lightState", &Scene::getLightState, &Scene::setLightState)
        .addProperty("globalIlluminationColor", &Scene::getGlobalIlluminationColor, (void (Scene::*)(Vector3))&Scene::setGlobalIllumination)
        .addProperty("globalIlluminationIntensity", &Scene::getGlobalIlluminationIntensity, (void (Scene::*)(float))&Scene::setGlobalIllumination)
        .addFunction("getGlobalIlluminationColorLinear", &Scene::getGlobalIlluminationColorLinear)
        .addFunction("setGlobalIllumination", (void (Scene::*)(float, Vector3))&Scene::setGlobalIllumination)
        .addProperty("ambientLight2DColor", &Scene::getAmbientLight2DColor, (void (Scene::*)(Vector3))&Scene::setAmbientLight2D)
        .addProperty("ambientLight2DIntensity", &Scene::getAmbientLight2DIntensity, (void (Scene::*)(float))&Scene::setAmbientLight2D)
        .addFunction("getAmbientLight2DColorLinear", &Scene::getAmbientLight2DColorLinear)
        .addFunction("setAmbientLight2D", (void (Scene::*)(float, Vector3))&Scene::setAmbientLight2D)
        .addProperty("shadow2DQuality", &Scene::getShadow2DQuality, &Scene::setShadow2DQuality)
        .addProperty("gravity2D", &Scene::getGravity2D, (void (Scene::*)(Vector2))&Scene::setGravity2D)
        .addFunction("setGravity2D",
            luabridge::overload<Vector2>(&Scene::setGravity2D),
            luabridge::overload<float, float>(&Scene::setGravity2D))
        .addProperty("gravity3D", &Scene::getGravity3D, (void (Scene::*)(Vector3))&Scene::setGravity3D)
        .addFunction("setGravity3D",
            luabridge::overload<Vector3>(&Scene::setGravity3D),
            luabridge::overload<float, float, float>(&Scene::setGravity3D))
        .addProperty("ssaoEnabled", &Scene::isSSAOEnabled, &Scene::setSSAOEnabled)
        .addProperty("ssaoRadius", &Scene::getSSAORadius, &Scene::setSSAORadius)
        .addProperty("ssaoIntensity", &Scene::getSSAOIntensity, &Scene::setSSAOIntensity)
        .addProperty("ssaoBias", &Scene::getSSAOBias, &Scene::setSSAOBias)
        .addProperty("ssaoDebug", &Scene::isSSAODebug, &Scene::setSSAODebug)
        .addProperty("ssrEnabled", &Scene::isSSREnabled, &Scene::setSSREnabled)
        .addProperty("ssrMaxDistance", &Scene::getSSRMaxDistance, &Scene::setSSRMaxDistance)
        .addProperty("ssrThickness", &Scene::getSSRThickness, &Scene::setSSRThickness)
        .addProperty("ssrMaxSteps", &Scene::getSSRMaxSteps, &Scene::setSSRMaxSteps)
        .addProperty("ssrIntensity", &Scene::getSSRIntensity, &Scene::setSSRIntensity)
        .addProperty("ssrBlur", &Scene::getSSRBlur, &Scene::setSSRBlur)
        .addProperty("ssrDebugMode", &Scene::getSSRDebugMode, &Scene::setSSRDebugMode)
        .addProperty("fixedResolutionEnabled", &Scene::isFixedResolutionEnabled, &Scene::setFixedResolutionEnabled)
        .addProperty("fixedResolutionWidth", &Scene::getFixedResolutionWidth, &Scene::setFixedResolutionWidth)
        .addProperty("fixedResolutionHeight", &Scene::getFixedResolutionHeight, &Scene::setFixedResolutionHeight)
        .addProperty("fixedResolutionFilter", &Scene::getFixedResolutionFilter, &Scene::setFixedResolutionFilter)
        .addFunction("setFixedResolutionSize", &Scene::setFixedResolutionSize)
        .addProperty("defaultMeshShader", &Scene::getDefaultMeshShader, &Scene::setDefaultMeshShader)
        .addProperty("defaultUIShader", &Scene::getDefaultUIShader, &Scene::setDefaultUIShader)
        .addProperty("defaultSkyShader", &Scene::getDefaultSkyShader, &Scene::setDefaultSkyShader)
        .addProperty("defaultPointsShader", &Scene::getDefaultPointsShader, &Scene::setDefaultPointsShader)
        .addProperty("defaultLinesShader", &Scene::getDefaultLinesShader, &Scene::setDefaultLinesShader)
        .addProperty("postProcessPasses", &Scene::getPostProcessPasses, &Scene::setPostProcessPasses)
        .addFunction("setPostProcessUniform",
            luabridge::overload<unsigned int, const std::string&, const Vector4&>(&Scene::setPostProcessUniform),
            luabridge::overload<unsigned int, const std::string&, const Vector3&>(&Scene::setPostProcessUniform),
            luabridge::overload<unsigned int, const std::string&, const Vector2&>(&Scene::setPostProcessUniform),
            luabridge::overload<unsigned int, const std::string&, float>(&Scene::setPostProcessUniform))
        .addFunction("getPostProcessUniform", &Scene::getPostProcessUniform)
        .addFunction("removePostProcessUniform", &Scene::removePostProcessUniform)
        .addFunction("setPostProcessPassEnabled", &Scene::setPostProcessPassEnabled)
        .addFunction("isPostProcessPassEnabled", &Scene::isPostProcessPassEnabled)
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
        .addStaticFunction("print", [] (const char* text) { Log::print("%s", text ? text : "(nil)"); })
        .addStaticFunction("verbose", [] (const char* text) { Log::verbose("%s", text ? text : "(nil)"); })
        .addStaticFunction("debug", [] (const char* text) { Log::debug("%s", text ? text : "(nil)"); })
        .addStaticFunction("warn", [] (const char* text) { Log::warn("%s", text ? text : "(nil)"); })
        .addStaticFunction("error", [] (const char* text) { Log::error("%s", text ? text : "(nil)"); })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Input>("Input")
        .addStaticProperty("MODIFIER_SHIFT", [] () -> int { return D_MODIFIER_SHIFT; })
        .addStaticProperty("MODIFIER_CONTROL", [] () -> int { return D_MODIFIER_CONTROL; })
        .addStaticProperty("MODIFIER_ALT", [] () -> int { return D_MODIFIER_ALT; })
        .addStaticProperty("MODIFIER_SUPER", [] () -> int { return D_MODIFIER_SUPER; })
        .addStaticProperty("MODIFIER_CAPS_LOCK", [] () -> int { return D_MODIFIER_CAPS_LOCK; })
        .addStaticProperty("MODIFIER_NUM_LOCK", [] () -> int { return D_MODIFIER_NUM_LOCK; })
        .addStaticProperty("KEY_UNKNOWN", [] () -> int { return D_KEY_UNKNOWN; })
        .addStaticProperty("KEY_SPACE", [] () -> int { return D_KEY_SPACE; })
        .addStaticProperty("KEY_APOSTROPHE", [] () -> int { return D_KEY_APOSTROPHE; })
        .addStaticProperty("KEY_COMMA", [] () -> int { return D_KEY_COMMA; })
        .addStaticProperty("KEY_MINUS", [] () -> int { return D_KEY_MINUS; })
        .addStaticProperty("KEY_PERIOD", [] () -> int { return D_KEY_PERIOD; })
        .addStaticProperty("KEY_SLASH", [] () -> int { return D_KEY_SLASH; })
        .addStaticProperty("KEY_0", [] () -> int { return D_KEY_0; })
        .addStaticProperty("KEY_1", [] () -> int { return D_KEY_1; })
        .addStaticProperty("KEY_2", [] () -> int { return D_KEY_2; })
        .addStaticProperty("KEY_3", [] () -> int { return D_KEY_3; })
        .addStaticProperty("KEY_4", [] () -> int { return D_KEY_4; })
        .addStaticProperty("KEY_5", [] () -> int { return D_KEY_5; })
        .addStaticProperty("KEY_6", [] () -> int { return D_KEY_6; })
        .addStaticProperty("KEY_7", [] () -> int { return D_KEY_7; })
        .addStaticProperty("KEY_8", [] () -> int { return D_KEY_8; })
        .addStaticProperty("KEY_9", [] () -> int { return D_KEY_9; })
        .addStaticProperty("KEY_SEMICOLON", [] () -> int { return D_KEY_SEMICOLON; })
        .addStaticProperty("KEY_EQUAL", [] () -> int { return D_KEY_EQUAL; })
        .addStaticProperty("KEY_A", [] () -> int { return D_KEY_A; })
        .addStaticProperty("KEY_B", [] () -> int { return D_KEY_B; })
        .addStaticProperty("KEY_C", [] () -> int { return D_KEY_C; })
        .addStaticProperty("KEY_D", [] () -> int { return D_KEY_D; })
        .addStaticProperty("KEY_E", [] () -> int { return D_KEY_E; })
        .addStaticProperty("KEY_F", [] () -> int { return D_KEY_F; })
        .addStaticProperty("KEY_G", [] () -> int { return D_KEY_G; })
        .addStaticProperty("KEY_H", [] () -> int { return D_KEY_H; })
        .addStaticProperty("KEY_I", [] () -> int { return D_KEY_I; })
        .addStaticProperty("KEY_J", [] () -> int { return D_KEY_J; })
        .addStaticProperty("KEY_K", [] () -> int { return D_KEY_K; })
        .addStaticProperty("KEY_L", [] () -> int { return D_KEY_L; })
        .addStaticProperty("KEY_M", [] () -> int { return D_KEY_M; })
        .addStaticProperty("KEY_N", [] () -> int { return D_KEY_N; })
        .addStaticProperty("KEY_O", [] () -> int { return D_KEY_O; })
        .addStaticProperty("KEY_P", [] () -> int { return D_KEY_P; })
        .addStaticProperty("KEY_Q", [] () -> int { return D_KEY_Q; })
        .addStaticProperty("KEY_R", [] () -> int { return D_KEY_R; })
        .addStaticProperty("KEY_S", [] () -> int { return D_KEY_S; })
        .addStaticProperty("KEY_T", [] () -> int { return D_KEY_T; })
        .addStaticProperty("KEY_U", [] () -> int { return D_KEY_U; })
        .addStaticProperty("KEY_V", [] () -> int { return D_KEY_V; })
        .addStaticProperty("KEY_W", [] () -> int { return D_KEY_W; })
        .addStaticProperty("KEY_X", [] () -> int { return D_KEY_X; })
        .addStaticProperty("KEY_Y", [] () -> int { return D_KEY_Y; })
        .addStaticProperty("KEY_Z", [] () -> int { return D_KEY_Z; })
        .addStaticProperty("KEY_LEFT_BRACKET", [] () -> int { return D_KEY_LEFT_BRACKET; })
        .addStaticProperty("KEY_BACKSLASH", [] () -> int { return D_KEY_BACKSLASH; })
        .addStaticProperty("KEY_RIGHT_BRACKET", [] () -> int { return D_KEY_RIGHT_BRACKET; })
        .addStaticProperty("KEY_GRAVE_ACCENT", [] () -> int { return D_KEY_GRAVE_ACCENT; })
        .addStaticProperty("KEY_WORLD_1", [] () -> int { return D_KEY_WORLD_1; })
        .addStaticProperty("KEY_WORLD_2", [] () -> int { return D_KEY_WORLD_2; })
        .addStaticProperty("KEY_ESCAPE", [] () -> int { return D_KEY_ESCAPE; })
        .addStaticProperty("KEY_ENTER", [] () -> int { return D_KEY_ENTER; })
        .addStaticProperty("KEY_TAB", [] () -> int { return D_KEY_TAB; })
        .addStaticProperty("KEY_BACKSPACE", [] () -> int { return D_KEY_BACKSPACE; })
        .addStaticProperty("KEY_INSERT", [] () -> int { return D_KEY_INSERT; })
        .addStaticProperty("KEY_DELETE", [] () -> int { return D_KEY_DELETE; })
        .addStaticProperty("KEY_RIGHT", [] () -> int { return D_KEY_RIGHT; })
        .addStaticProperty("KEY_LEFT", [] () -> int { return D_KEY_LEFT; })
        .addStaticProperty("KEY_DOWN", [] () -> int { return D_KEY_DOWN; })
        .addStaticProperty("KEY_UP", [] () -> int { return D_KEY_UP; })
        .addStaticProperty("KEY_PAGE_UP", [] () -> int { return D_KEY_PAGE_UP; })
        .addStaticProperty("KEY_PAGE_DOWN", [] () -> int { return D_KEY_PAGE_DOWN; })
        .addStaticProperty("KEY_HOME", [] () -> int { return D_KEY_HOME; })
        .addStaticProperty("KEY_END", [] () -> int { return D_KEY_END; })
        .addStaticProperty("KEY_CAPS_LOCK", [] () -> int { return D_KEY_CAPS_LOCK; })
        .addStaticProperty("KEY_SCROLL_LOCK", [] () -> int { return D_KEY_SCROLL_LOCK; })
        .addStaticProperty("KEY_NUM_LOCK", [] () -> int { return D_KEY_NUM_LOCK; })
        .addStaticProperty("KEY_PRINT_SCREEN", [] () -> int { return D_KEY_PRINT_SCREEN; })
        .addStaticProperty("KEY_PAUSE", [] () -> int { return D_KEY_PAUSE; })
        .addStaticProperty("KEY_F1", [] () -> int { return D_KEY_F1; })
        .addStaticProperty("KEY_F2", [] () -> int { return D_KEY_F2; })
        .addStaticProperty("KEY_F3", [] () -> int { return D_KEY_F3; })
        .addStaticProperty("KEY_F4", [] () -> int { return D_KEY_F4; })
        .addStaticProperty("KEY_F5", [] () -> int { return D_KEY_F5; })
        .addStaticProperty("KEY_F6", [] () -> int { return D_KEY_F6; })
        .addStaticProperty("KEY_F7", [] () -> int { return D_KEY_F7; })
        .addStaticProperty("KEY_F8", [] () -> int { return D_KEY_F8; })
        .addStaticProperty("KEY_F9", [] () -> int { return D_KEY_F9; })
        .addStaticProperty("KEY_F10", [] () -> int { return D_KEY_F10; })
        .addStaticProperty("KEY_F11", [] () -> int { return D_KEY_F11; })
        .addStaticProperty("KEY_F12", [] () -> int { return D_KEY_F12; })
        .addStaticProperty("KEY_F13", [] () -> int { return D_KEY_F13; })
        .addStaticProperty("KEY_F14", [] () -> int { return D_KEY_F14; })
        .addStaticProperty("KEY_F15", [] () -> int { return D_KEY_F15; })
        .addStaticProperty("KEY_F16", [] () -> int { return D_KEY_F16; })
        .addStaticProperty("KEY_F17", [] () -> int { return D_KEY_F17; })
        .addStaticProperty("KEY_F18", [] () -> int { return D_KEY_F18; })
        .addStaticProperty("KEY_F19", [] () -> int { return D_KEY_F19; })
        .addStaticProperty("KEY_F20", [] () -> int { return D_KEY_F20; })
        .addStaticProperty("KEY_F21", [] () -> int { return D_KEY_F21; })
        .addStaticProperty("KEY_F22", [] () -> int { return D_KEY_F22; })
        .addStaticProperty("KEY_F23", [] () -> int { return D_KEY_F23; })
        .addStaticProperty("KEY_F24", [] () -> int { return D_KEY_F24; })
        .addStaticProperty("KEY_F25", [] () -> int { return D_KEY_F25; })
        .addStaticProperty("KEY_KP_0", [] () -> int { return D_KEY_KP_0; })
        .addStaticProperty("KEY_KP_1", [] () -> int { return D_KEY_KP_1; })
        .addStaticProperty("KEY_KP_2", [] () -> int { return D_KEY_KP_2; })
        .addStaticProperty("KEY_KP_3", [] () -> int { return D_KEY_KP_3; })
        .addStaticProperty("KEY_KP_4", [] () -> int { return D_KEY_KP_4; })
        .addStaticProperty("KEY_KP_5", [] () -> int { return D_KEY_KP_5; })
        .addStaticProperty("KEY_KP_6", [] () -> int { return D_KEY_KP_6; })
        .addStaticProperty("KEY_KP_7", [] () -> int { return D_KEY_KP_7; })
        .addStaticProperty("KEY_KP_8", [] () -> int { return D_KEY_KP_8; })
        .addStaticProperty("KEY_KP_9", [] () -> int { return D_KEY_KP_9; })
        .addStaticProperty("KEY_KP_DECIMAL", [] () -> int { return D_KEY_KP_DECIMAL; })
        .addStaticProperty("KEY_KP_DIVIDE", [] () -> int { return D_KEY_KP_DIVIDE; })
        .addStaticProperty("KEY_KP_MULTIPLY", [] () -> int { return D_KEY_KP_MULTIPLY; })
        .addStaticProperty("KEY_KP_SUBTRACT", [] () -> int { return D_KEY_KP_SUBTRACT; })
        .addStaticProperty("KEY_KP_ADD", [] () -> int { return D_KEY_KP_ADD; })
        .addStaticProperty("KEY_KP_ENTER", [] () -> int { return D_KEY_KP_ENTER; })
        .addStaticProperty("KEY_KP_EQUAL", [] () -> int { return D_KEY_KP_EQUAL; })
        .addStaticProperty("KEY_LEFT_SHIFT", [] () -> int { return D_KEY_LEFT_SHIFT; })
        .addStaticProperty("KEY_LEFT_CONTROL", [] () -> int { return D_KEY_LEFT_CONTROL; })
        .addStaticProperty("KEY_LEFT_ALT", [] () -> int { return D_KEY_LEFT_ALT; })
        .addStaticProperty("KEY_LEFT_SUPER", [] () -> int { return D_KEY_LEFT_SUPER; })
        .addStaticProperty("KEY_RIGHT_SHIFT", [] () -> int { return D_KEY_RIGHT_SHIFT; })
        .addStaticProperty("KEY_RIGHT_CONTROL", [] () -> int { return D_KEY_RIGHT_CONTROL; })
        .addStaticProperty("KEY_RIGHT_ALT", [] () -> int { return D_KEY_RIGHT_ALT; })
        .addStaticProperty("KEY_RIGHT_SUPER", [] () -> int { return D_KEY_RIGHT_SUPER; })
        .addStaticProperty("KEY_MENU", [] () -> int { return D_KEY_MENU; })
        .addStaticProperty("KEY_LAST", [] () -> int { return D_KEY_LAST; })

        .addStaticProperty("MOUSE_BUTTON_1", [] () -> int { return D_MOUSE_BUTTON_1; })
        .addStaticProperty("MOUSE_BUTTON_2", [] () -> int { return D_MOUSE_BUTTON_2; })
        .addStaticProperty("MOUSE_BUTTON_3", [] () -> int { return D_MOUSE_BUTTON_3; })
        .addStaticProperty("MOUSE_BUTTON_4", [] () -> int { return D_MOUSE_BUTTON_4; })
        .addStaticProperty("MOUSE_BUTTON_5", [] () -> int { return D_MOUSE_BUTTON_5; })
        .addStaticProperty("MOUSE_BUTTON_6", [] () -> int { return D_MOUSE_BUTTON_6; })
        .addStaticProperty("MOUSE_BUTTON_7", [] () -> int { return D_MOUSE_BUTTON_7; })
        .addStaticProperty("MOUSE_BUTTON_8", [] () -> int { return D_MOUSE_BUTTON_8; })
        .addStaticProperty("MOUSE_BUTTON_LAST", [] () -> int { return D_MOUSE_BUTTON_LAST; })
        .addStaticProperty("MOUSE_BUTTON_LEFT", [] () -> int { return D_MOUSE_BUTTON_LEFT; })
        .addStaticProperty("MOUSE_BUTTON_RIGHT", [] () -> int { return D_MOUSE_BUTTON_RIGHT; })
        .addStaticProperty("MOUSE_BUTTON_MIDDLE", [] () -> int { return D_MOUSE_BUTTON_MIDDLE; })

        .addStaticProperty("GAMEPAD_BUTTON_A", [] () -> int { return D_GAMEPAD_BUTTON_A; })
        .addStaticProperty("GAMEPAD_BUTTON_B", [] () -> int { return D_GAMEPAD_BUTTON_B; })
        .addStaticProperty("GAMEPAD_BUTTON_X", [] () -> int { return D_GAMEPAD_BUTTON_X; })
        .addStaticProperty("GAMEPAD_BUTTON_Y", [] () -> int { return D_GAMEPAD_BUTTON_Y; })
        .addStaticProperty("GAMEPAD_BUTTON_LEFT_BUMPER", [] () -> int { return D_GAMEPAD_BUTTON_LEFT_BUMPER; })
        .addStaticProperty("GAMEPAD_BUTTON_RIGHT_BUMPER", [] () -> int { return D_GAMEPAD_BUTTON_RIGHT_BUMPER; })
        .addStaticProperty("GAMEPAD_BUTTON_BACK", [] () -> int { return D_GAMEPAD_BUTTON_BACK; })
        .addStaticProperty("GAMEPAD_BUTTON_START", [] () -> int { return D_GAMEPAD_BUTTON_START; })
        .addStaticProperty("GAMEPAD_BUTTON_GUIDE", [] () -> int { return D_GAMEPAD_BUTTON_GUIDE; })
        .addStaticProperty("GAMEPAD_BUTTON_LEFT_THUMB", [] () -> int { return D_GAMEPAD_BUTTON_LEFT_THUMB; })
        .addStaticProperty("GAMEPAD_BUTTON_RIGHT_THUMB", [] () -> int { return D_GAMEPAD_BUTTON_RIGHT_THUMB; })
        .addStaticProperty("GAMEPAD_BUTTON_DPAD_UP", [] () -> int { return D_GAMEPAD_BUTTON_DPAD_UP; })
        .addStaticProperty("GAMEPAD_BUTTON_DPAD_RIGHT", [] () -> int { return D_GAMEPAD_BUTTON_DPAD_RIGHT; })
        .addStaticProperty("GAMEPAD_BUTTON_DPAD_DOWN", [] () -> int { return D_GAMEPAD_BUTTON_DPAD_DOWN; })
        .addStaticProperty("GAMEPAD_BUTTON_DPAD_LEFT", [] () -> int { return D_GAMEPAD_BUTTON_DPAD_LEFT; })
        .addStaticProperty("GAMEPAD_BUTTON_LAST", [] () -> int { return D_GAMEPAD_BUTTON_LAST; })
        .addStaticProperty("GAMEPAD_BUTTON_CROSS", [] () -> int { return D_GAMEPAD_BUTTON_CROSS; })
        .addStaticProperty("GAMEPAD_BUTTON_CIRCLE", [] () -> int { return D_GAMEPAD_BUTTON_CIRCLE; })
        .addStaticProperty("GAMEPAD_BUTTON_SQUARE", [] () -> int { return D_GAMEPAD_BUTTON_SQUARE; })
        .addStaticProperty("GAMEPAD_BUTTON_TRIANGLE", [] () -> int { return D_GAMEPAD_BUTTON_TRIANGLE; })

        .addStaticProperty("GAMEPAD_AXIS_LEFT_X", [] () -> int { return D_GAMEPAD_AXIS_LEFT_X; })
        .addStaticProperty("GAMEPAD_AXIS_LEFT_Y", [] () -> int { return D_GAMEPAD_AXIS_LEFT_Y; })
        .addStaticProperty("GAMEPAD_AXIS_RIGHT_X", [] () -> int { return D_GAMEPAD_AXIS_RIGHT_X; })
        .addStaticProperty("GAMEPAD_AXIS_RIGHT_Y", [] () -> int { return D_GAMEPAD_AXIS_RIGHT_Y; })
        .addStaticProperty("GAMEPAD_AXIS_LEFT_TRIGGER", [] () -> int { return D_GAMEPAD_AXIS_LEFT_TRIGGER; })
        .addStaticProperty("GAMEPAD_AXIS_RIGHT_TRIGGER", [] () -> int { return D_GAMEPAD_AXIS_RIGHT_TRIGGER; })
        .addStaticProperty("GAMEPAD_AXIS_LAST", [] () -> int { return D_GAMEPAD_AXIS_LAST; })

        .addStaticFunction("isKeyPressed", &Input::isKeyPressed)
        .addStaticFunction("isMousePressed", &Input::isMousePressed)
        .addStaticFunction("isTouch", &Input::isTouch)
        .addStaticFunction("isMouseEntered", &Input::isMouseEntered)
        .addStaticFunction("getMousePosition", &Input::getMousePosition)
        .addStaticFunction("getMouseScroll", &Input::getMouseScroll)
        .addStaticFunction("getTouchPosition", &Input::getTouchPosition)
        .addStaticFunction("getTouches", &Input::getTouches)
        .addStaticFunction("numTouches", &Input::numTouches)
        .addStaticFunction("isGamepadConnected", &Input::isGamepadConnected)
        .addStaticFunction("getGamepadName", &Input::getGamepadName)
        .addStaticFunction("isGamepadButtonPressed", &Input::isGamepadButtonPressed)
        .addStaticFunction("getGamepadAxis", &Input::getGamepadAxis)
        .addStaticFunction("numGamepads", &Input::numGamepads)
        .addStaticFunction("getGamepadId", &Input::getGamepadId)
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
        .addStaticFunction("isWindowMaximized", [] () { return System::instance().isWindowMaximized(); })
        .addStaticFunction("maximizeWindow", [] () { System::instance().maximizeWindow(); })
        .addStaticFunction("restoreWindow", [] () { System::instance().restoreWindow(); })
        .addStaticFunction("setWindowSize", [] (int width, int height) { System::instance().setWindowSize(width, height); })
        .addStaticFunction("isWindowResizable", [] () { return System::instance().isWindowResizable(); })
        .addStaticFunction("setWindowResizable", [] (bool resizable) { System::instance().setWindowResizable(resizable); })
        .addStaticFunction("setWindowTitle", [] (const std::string& title) { System::instance().setWindowTitle(title); })
        .addStaticFunction("quit", [] () { System::instance().quit(); })
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
