//
// (c) 2026 Eduardo Doria.
//

#include "scene/SceneIntegration.h"
#include "scene/Prefab.h"
#include "Scene.h"
#include "Engine.h"
#include "Log.h"

namespace doriax {

    void SceneIntegration::registerPrefabFactory(
        const std::string& name,
        Prefab::Factory factory,
        Prefab::Destroyer destroyer
    ) {
        PrefabRegistry::get().registerPrefab(name, factory, destroyer);
        Log::info("SceneIntegration: registered prefab '%s'", name.c_str());
    }

    Entity SceneIntegration::instantiatePrefab(const std::string& prefabName, Scene* scene) {
        if (!scene) {
            Log::error("SceneIntegration::instantiatePrefab: scene is null");
            return NULL_ENTITY;
        }

        const Prefab* prefab = PrefabRegistry::get().find(prefabName);
        if (!prefab) {
            Log::error("SceneIntegration: prefab '%s' not found", prefabName.c_str());
            return NULL_ENTITY;
        }

        return prefab->instantiate(scene);
    }

    Entity SceneIntegration::instantiatePrefabAt(
        const std::string& prefabName,
        Scene* scene,
        Entity rootEntity
    ) {
        if (!scene) {
            Log::error("SceneIntegration::instantiatePrefabAt: scene is null");
            return NULL_ENTITY;
        }

        const Prefab* prefab = PrefabRegistry::get().find(prefabName);
        if (!prefab) {
            Log::error("SceneIntegration: prefab '%s' not found", prefabName.c_str());
            return NULL_ENTITY;
        }

        if (!scene->isEntityCreated(rootEntity)) {
            Log::error("SceneIntegration: root entity does not exist in scene");
            return NULL_ENTITY;
        }

        // Use the prefab's factory directly with the provided root entity.
        // This allows hierarchical prefab instantiation.
        if (prefab->instantiate(scene) == NULL_ENTITY) {
            return NULL_ENTITY;
        }

        return rootEntity;
    }

    bool SceneIntegration::destroyPrefabInstance(Scene* scene, Entity rootEntity) {
        if (!scene) {
            return false;
        }

        // For now, use simple entity destruction.
        // In a full implementation, this would also clean up children and invoke
        // the destroyer callback if one exists.
        scene->destroyEntity(rootEntity);
        return true;
    }

    void SceneIntegration::registerScene(
        const SceneConfig& config,
        SceneStream::SceneLoader loader,
        SceneStream::SceneUnload unloader
    ) {
        SceneStream::get().registerScene(config);

        if (loader) {
            SceneStream::get().setLoader(config.name, loader);
        }

        if (unloader) {
            SceneStream::get().setUnloadHandler(config.name, unloader);
        }

        Log::info("SceneIntegration: registered scene '%s'", config.name.c_str());
    }

    bool SceneIntegration::loadScene(const std::string& sceneName) {
        if (!SceneStream::get().isRegistered(sceneName)) {
            Log::error("SceneIntegration: scene '%s' not registered", sceneName.c_str());
            return false;
        }

        return SceneStream::get().loadScene(sceneName);
    }

    bool SceneIntegration::unloadScene(const std::string& sceneName) {
        if (!SceneStream::get().isRegistered(sceneName)) {
            Log::error("SceneIntegration: scene '%s' not registered", sceneName.c_str());
            return false;
        }

        return SceneStream::get().unloadScene(sceneName);
    }

    bool SceneIntegration::loadSceneAdditive(const std::string& sceneName) {
        // Additive loading requires an active scene context.
        Scene* activeScene = Engine::scene();
        if (!activeScene) {
            Log::error("SceneIntegration::loadSceneAdditive: no active scene");
            return false;
        }

        if (!SceneStream::get().isRegistered(sceneName)) {
            Log::error("SceneIntegration: scene '%s' not registered", sceneName.c_str());
            return false;
        }

        // Load the scene layer without clearing the main scene.
        // The actual layer loading is deferred to the loader callback,
        // which should call Engine::addSceneLayer() instead of Engine::setScene().
        return SceneStream::get().loadScene(sceneName);
    }

    SceneStream::SceneLoader SceneIntegration::makeSceneManagerLoader(
        uint32_t sceneId,
        std::function<void()> factory
    ) {
        return [sceneId, factory](const std::string& sceneName) {
            Log::info("SceneIntegration: loading scene '%s' via SceneManager", sceneName.c_str());
            if (factory) {
                factory();
            }
        };
    }

    bool SceneIntegration::isPrefabRegistered(const std::string& name) {
        return PrefabRegistry::get().has(name);
    }

    std::vector<std::string> SceneIntegration::listRegisteredPrefabs() {
        return PrefabRegistry::get().names();
    }

    std::vector<std::string> SceneIntegration::listRegisteredScenes() {
        return SceneStream::get().listScenes();
    }

}
