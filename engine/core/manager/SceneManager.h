//
// (c) 2025 Eduardo Doria.
//

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include "Export.h"
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <cstdint>

namespace Supernova {

    class Scene;

    // SceneManager allows registering named scene stacks and switching between them at runtime.
    // A "scene stack" corresponds to an editor SceneProject: one main Scene plus zero or more
    // layer Scenes. The factory function is responsible for calling Engine::setScene() and
    // Engine::addSceneLayer() for each scene in the stack.
    //
    // Usage from C++ (standalone generated code):
    //   SceneManager::registerScene("Level1", load_Level1);
    //   SceneManager::loadScene("Level1");     // by name
    //   SceneManager::loadScene(0);            // by index (registration order)
    //
    // Usage from Lua script:
    //   SceneManager.loadScene("Level1")
    //   SceneManager.loadScene(0)

    class SUPERNOVA_API SceneManager {
    private:
        struct SceneEntry {
            uint32_t id;
            std::string name;
            std::function<void()> factory;
        };

        static std::vector<SceneEntry> entries;
        static uint32_t currentId;
        static std::map<uint32_t, Scene*> scenePtrs;

    public:
        // Register a named scene stack.
        // The factory must call Engine::setScene() / Engine::addSceneLayer() to set up
        // the scene hierarchy. It should also call Engine::removeAllScenes() first if
        // a scene transition is desired (this is done automatically by loadScene()).
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> factory);

        // Load a scene stack by name. Calls Engine::removeAllScenes() then invokes the
        // registered factory. Returns false if the name is not found.
        static bool loadScene(const std::string& name);

        // Load a scene stack by id.
        // Returns false if the id is not found.
        static bool loadScene(uint32_t id);

        // Return the id of a scene by name, or 0 if not found.
        static uint32_t getSceneId(const std::string& name);

        // Return the name of the scene at the given id, or "" if not found.
        static std::string getSceneName(uint32_t id);

        // Return all registered scene names in registration order.
        static std::vector<std::string> getSceneNames();

        // Return the total number of registered scenes.
        static int getSceneCount();

        // Return the id of the scene most recently loaded via loadScene(), or 0 if none.
        static uint32_t getCurrentSceneId();

        // Return the name of the scene most recently loaded via loadScene(), or "" if none.
        static std::string getCurrentSceneName();

        // Remove all registered scenes. Intended for editor use when resetting state.
        static void clearAll();

        // Store a scene pointer associated with a scene ID for cross-scene entity resolution.
        static void setScenePtr(uint32_t id, Scene* scene);

        // Retrieve a stored scene pointer by ID. Returns nullptr if not found.
        static Scene* getScenePtr(uint32_t id);

        // Remove a stored scene pointer.
        static void removeScenePtr(uint32_t id);
    };

} // namespace Supernova

#endif // SCENEMANAGER_H
