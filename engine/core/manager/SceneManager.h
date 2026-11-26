// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include "Export.h"
#include "texture/Texture.h"
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
#include <initializer_list>

namespace doriax {

    class Scene;

    // Files a scene loads, known before it is built so preloadScene() can load them
    struct SceneResources {
        std::vector<Texture> textures;
        std::vector<std::string> sounds;
        std::vector<std::string> models;
    };

    // SceneManager allows registering named scene stacks and switching between them at runtime.
    // A "scene stack" corresponds to an editor SceneProject: one main Scene plus zero or more
    // layer Scenes. The load factory replaces the running scenes with the stack; the add
    // factory only creates them, for a stack shown on top of the running one.
    //
    // Usage from C++ (standalone generated code):
    //   SceneManager::registerScene(1, "Level1", load_Level1, add_Level1);
    //   SceneManager::loadScene("Level1");     // by name
    //   SceneManager::loadScene(1);            // by id
    //   SceneManager::addChildScene("Hud");    // on top of Level1
    //
    // Usage from Lua script:
    //   SceneManager.loadScene("Level1")
    //   SceneManager.addChildScene("Hud")

    class DORIAX_API SceneManager {
    private:
        struct SceneEntry {
            uint32_t id;
            std::string name;
            std::function<void()> loadFactory;
            std::function<void()> addFactory;
            std::vector<uint32_t> sceneIds;
            std::function<SceneResources()> resources;
        };

        // Files of a preloadScene() still loading, and the loaded ones held in their pools
        struct Preload {
            uint32_t id;
            bool started = false;
            bool cancelled = false; // kept until the loads in flight end
            size_t total = 0;
            SceneResources pending;
            std::vector<std::shared_ptr<void>> held;
            // removes a loaded file from its pool when no scene uses it
            std::vector<std::function<void()>> releases;
        };

        enum class LoadingState {
            None,
            Covering,   // loading scene over the old stack, switch pending
            Loading,    // new stack loading its resources
            Loaded      // new stack loaded, holds keep the loading scene up
        };

        static std::vector<SceneEntry> entries;
        static uint32_t currentId;
        static std::optional<uint32_t> pendingId;
        static std::map<uint32_t, Scene*> scenePtrs;

        static uint32_t loadingSceneId;
        static float loadingDelay;
        static float loadingTimeout;
        static LoadingState loadingState;
        static bool loadingSceneShown;
        static bool loadingSceneReady;
        static double loadingStateTime;
        static size_t loadingCount;
        static double loadingProgressTime;
        static float loadingProgress;
        static int loadingHolds;

        static std::vector<Preload> preloads;

        static std::vector<uint32_t> buildSceneStackIds(uint32_t id, const std::vector<uint32_t>& sceneIds);
        static SceneEntry* findEntry(uint32_t id);
        static void runFactory(uint32_t id);

        static std::vector<Scene*> getRunningScenes(uint32_t id);
        static bool canShowLoadingScene(uint32_t id);
        static void raiseLoadingScene();
        static void startLoading(uint32_t id);
        static void resetLoadProgress();
        static bool isLoadProgressing(size_t loaded, double now);
        static void getLoadCount(uint32_t id, size_t& loaded, size_t& total);

        static Preload* findPreload(uint32_t id);
        static void startPreload(uint32_t id);
        static void releasePreload(Preload& preload);
        static void updatePreloads();

    public:
        // Register a named scene stack.
        // The load factory must call Engine::setScene() / Engine::addSceneLayer() to set up
        // the scene hierarchy. It should also call Engine::removeAllScenes() first if
        // a scene transition is desired (this is done automatically by loadScene()).
        // The add factory creates the scenes and registers them with setScenePtr();
        // addChildScene() runs it and puts them on screen, so it must be safe to call more
        // than once. A stack registered without one can only be added once something else
        // has created its scenes.
        // The initializer list overload is what keeps a braced fourth argument unambiguous
        // between the scene ids and the add factory.
        // The resources function lists the files a scene loads, for preloadScene().
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory);
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, const std::vector<uint32_t>& sceneIds);
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::initializer_list<uint32_t> sceneIds);
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory);
        static void registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory, const std::vector<uint32_t>& sceneIds, std::function<SceneResources()> resources = nullptr);

        // Load a scene stack by name. Calls Engine::removeAllScenes() then invokes the
        // registered factory. Returns false if the name is not found.
        // While a scene is running (script callback, contact, input) the transition is
        // deferred to the next frame, as the factory tears down the running scenes and
        // scripts; the current scene only changes then.
        static bool loadScene(const std::string& name);

        // Load a scene stack by id.
        // Returns false if the id is not found.
        static bool loadScene(uint32_t id);

        // True between a deferred loadScene() and the frame that applies it.
        static bool isLoadPending();

        // Runs the deferred transition. Called by Engine before the scenes update.
        static void applyPendingLoad();

        // Scene stack shown on top while loadScene() switches stacks, until the new stack
        // has loaded. An empty name or 0 disables it.
        static bool setLoadingScene(const std::string& name);
        static bool setLoadingScene(uint32_t id);
        static uint32_t getLoadingSceneId();

        // Seconds the loading scene covers the old scenes before they are replaced.
        static void setLoadingDelay(float seconds);
        static float getLoadingDelay();

        // Seconds without load progress before a load stops waiting.
        static void setLoadingTimeout(float seconds);
        static float getLoadingTimeout();

        // True from loadScene() until the new stack has loaded and every hold is released.
        static bool isLoading();

        // Loaded share of the new stack's drawables and sounds, from 0 to 1. Never goes back.
        static float getLoadingProgress();

        // Keeps the loading scene up after the new stack has loaded, until released.
        // Returns false when nothing is loading.
        static bool holdLoading();
        static void releaseLoading();

        // True for a scene below the shown loading scene, which gets no UI input.
        static bool isCoveredByLoading(Scene* scene);

        // Tracks the loading of a stack started without loadScene(), as the editor's Play does.
        static void setCurrentScene(uint32_t id);

        // Loads the files of a scene stack in the background while the game runs, so a later
        // loadScene() finds them ready. They are held until that load or cancelPreload().
        // Without async loading one file loads per frame, and models are skipped.
        static bool preloadScene(const std::string& name);
        static bool preloadScene(uint32_t id);

        // Loaded share of a preloadScene(), from 0 to 1; 0 when it was not requested.
        static float getPreloadProgress(const std::string& name);
        static float getPreloadProgress(uint32_t id);

        static void cancelPreload(const std::string& name);
        static void cancelPreload(uint32_t id);

        // Called by Engine after the scenes draw.
        static void updateLoading();

        // Add a child scene stack on top of the running scenes, keeping the main scene.
        // Runs the stack's add factory, which creates the scenes that are missing and restores
        // the scripts of any that were torn down, then adds them in stack order. Unlike
        // loadScene() this takes effect at once, including inside a frame, as nothing is
        // destroyed: the scenes added run and draw from the same frame on.
        static bool addChildScene(uint32_t id);
        static bool addChildScene(const std::string& name);

        // Remove a child scene stack from the active scene list without deleting Scene objects. The main scene is kept.
        static bool removeChildScene(uint32_t id);
        static bool removeChildScene(const std::string& name);

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
        // the id a scene was set with, or 0
        static uint32_t getScenePtrId(Scene* scene);

        // Remove a stored scene pointer.
        static void removeScenePtr(uint32_t id);
    };

} // namespace doriax

#endif // SCENEMANAGER_H
