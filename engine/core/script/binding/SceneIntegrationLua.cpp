//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "scene/SceneIntegration.h"
#include "scene/Prefab.h"
#include "scene/SceneStream.h"
#include "Log.h"

using namespace doriax;

void LuaBinding::registerSceneIntegrationClasses(lua_State *L) {
#ifndef DISABLE_LUA_BINDINGS
    luabridge::getGlobalNamespace(L)
            .beginNamespace("doriax")
            .beginClass<SceneIntegration>("SceneIntegration")
                .addStaticFunction("instantiatePrefab", &SceneIntegration::instantiatePrefab)
                .addStaticFunction("destroyPrefabInstance", &SceneIntegration::destroyPrefabInstance)
                .addStaticFunction("registerPrefabFactory", +[](const std::string& name, const luabridge::LuaRef& factory) {
                    if (!factory.isFunction()) {
                        Log::error("registerPrefabFactory: factory must be a function");
                        return;
                    }
                    // Wrap the Lua function as a Prefab::Factory
                    Prefab::Factory factoryFunc = [factory](Scene* scene, Entity root) {
                        try {
                            factory(scene, root);
                        } catch (const std::exception& e) {
                            Log::error("Prefab factory error: %s", e.what());
                        }
                    };
                    SceneIntegration::registerPrefabFactory(name, factoryFunc);
                })
                .addStaticFunction("loadScene", &SceneIntegration::loadScene)
                .addStaticFunction("unloadScene", &SceneIntegration::unloadScene)
                .addStaticFunction("loadSceneAdditive", &SceneIntegration::loadSceneAdditive)
                .addStaticFunction("isPrefabRegistered", &SceneIntegration::isPrefabRegistered)
                .addStaticFunction("listRegisteredPrefabs", &SceneIntegration::listRegisteredPrefabs)
                .addStaticFunction("listRegisteredScenes", &SceneIntegration::listRegisteredScenes)
            .endClass()

            .beginClass<SceneConfig>("SceneConfig")
                .addConstructor<void()>()
                .addProperty("name", &SceneConfig::name)
                .addProperty("path", &SceneConfig::path)
                .addProperty("active", &SceneConfig::active)
                .addProperty("persistent", &SceneConfig::persistent)
                .addProperty("additive", &SceneConfig::additive)
                .addProperty("autoLoad", &SceneConfig::autoLoad)
                .addProperty("streamingMode", &SceneConfig::streamingMode)
                .addProperty("dependencies", &SceneConfig::dependencies)
                .addProperty("metadata", &SceneConfig::metadata)
            .endClass()

            .beginClass<Prefab>("Prefab")
                .addConstructor<void()>()
                .addFunction("name", &Prefab::name)
                .addFunction("setName", &Prefab::setName)
            .endClass()

            .beginNamespace("SceneStream")
                .addFunction("registerScene", +[](const SceneConfig& config) {
                    doriax::SceneStream::get().registerScene(config);
                })
                .addFunction("unregisterScene", +[](const std::string& name) {
                    doriax::SceneStream::get().unregisterScene(name);
                })
                .addFunction("isRegistered", +[](const std::string& name) {
                    return doriax::SceneStream::get().isRegistered(name);
                })
                .addFunction("listScenes", +[]() {
                    return doriax::SceneStream::get().listScenes();
                })
            .endNamespace()

            .endNamespace();

    Log::info("Scene integration Lua bindings registered");

#endif // DISABLE_LUA_BINDINGS
}
