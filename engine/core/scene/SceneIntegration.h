//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_SCENE_INTEGRATION_H
#define DORIAX_SCENE_INTEGRATION_H

#include "Export.h"
#include "Entity.h"
#include "scene/Prefab.h"
#include "scene/SceneStream.h"
#include <string>
#include <functional>

namespace doriax {

    class Scene;

    // SceneIntegration provides runtime bridge functions between the new scene/prefab
    // systems (SceneStream, Prefab, PrefabRegistry) and the existing engine lifecycle
    // (SceneManager, Scene, BundleManager).
    //
    // Usage:
    //   SceneIntegration::instantiatePrefab("player", scene);
    //   SceneIntegration::loadSceneAdditive("level_2");
    //   SceneIntegration::registerPrefabFactory("enemies/Goblin", factory_func);

    class DORIAX_API SceneIntegration {
    public:
        // Register a prefab factory into the global PrefabRegistry.
        // This is the primary entry point for game code to define prefabs.
        static void registerPrefabFactory(
            const std::string& name,
            Prefab::Factory factory,
            Prefab::Destroyer destroyer = {}
        );

        // Instantiate a registered prefab into the given scene.
        // Returns NULL_ENTITY if the prefab is not found or factory fails.
        static Entity instantiatePrefab(const std::string& prefabName, Scene* scene);

        // Instantiate a prefab under an existing root entity.
        // Returns NULL_ENTITY on failure.
        static Entity instantiatePrefabAt(
            const std::string& prefabName,
            Scene* scene,
            Entity rootEntity
        );

        // Destroy a prefab instance by its root entity.
        static bool destroyPrefabInstance(Scene* scene, Entity rootEntity);

        // Configure and register a scene in the SceneStream registry.
        // Optionally provide loader and unloader callbacks.
        static void registerScene(
            const SceneConfig& config,
            SceneStream::SceneLoader loader = {},
            SceneStream::SceneUnload unloader = {}
        );

        // Load a scene by name via SceneStream.
        // This invokes the registered loader callback.
        static bool loadScene(const std::string& sceneName);

        // Unload a scene by name via SceneStream.
        static bool unloadScene(const std::string& sceneName);

        // Load a scene additively (layer) without clearing the current scene.
        // Requires an active scene context via Engine::scene().
        static bool loadSceneAdditive(const std::string& sceneName);

        // Create a default scene loader that uses SceneManager's factory system.
        // Pass this to registerScene() as the loader callback.
        static SceneStream::SceneLoader makeSceneManagerLoader(
            uint32_t sceneId,
            std::function<void()> factory
        );

        // Check if a prefab is registered in the global PrefabRegistry.
        static bool isPrefabRegistered(const std::string& name);

        // List all registered prefab names.
        static std::vector<std::string> listRegisteredPrefabs();

        // List all registered scene names in SceneStream.
        static std::vector<std::string> listRegisteredScenes();
    };

}

#endif // DORIAX_SCENE_INTEGRATION_H
