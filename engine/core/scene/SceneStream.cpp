//
// (c) 2026 Eduardo Doria.
//

#include "scene/SceneStream.h"
#include "Log.h"

namespace doriax {

    SceneStream& SceneStream::get() {
        static SceneStream instance;
        return instance;
    }

    void SceneStream::registerScene(const SceneConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        scenes_[config.name] = config;
    }

    void SceneStream::unregisterScene(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        scenes_.erase(name);
        loaders_.erase(name);
        unloaders_.erase(name);
    }

    void SceneStream::setLoader(const std::string& sceneName, SceneLoader loader) {
        std::lock_guard<std::mutex> lock(mutex_);
        loaders_[sceneName] = std::move(loader);
    }

    void SceneStream::setUnloadHandler(const std::string& sceneName, SceneUnload unloadHandler) {
        std::lock_guard<std::mutex> lock(mutex_);
        unloaders_[sceneName] = std::move(unloadHandler);
    }

    bool SceneStream::isRegistered(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return scenes_.find(name) != scenes_.end();
    }

    const SceneConfig* SceneStream::find(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = scenes_.find(name);
        return it == scenes_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> SceneStream::listScenes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(scenes_.size());
        for (const auto& [name, config] : scenes_) {
            (void)config;
            names.push_back(name);
        }
        return names;
    }

    bool SceneStream::loadScene(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto loaderIt = loaders_.find(name);
        if (loaderIt == loaders_.end()) {
            Log::warn("SceneStream: no loader registered for '%s'", name.c_str());
            return false;
        }

        if (loaderIt->second) {
            loaderIt->second(name);
            return true;
        }

        return false;
    }

    bool SceneStream::unloadScene(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto unloadIt = unloaders_.find(name);
        if (unloadIt == unloaders_.end()) {
            Log::warn("SceneStream: no unload handler registered for '%s'", name.c_str());
            return false;
        }

        if (unloadIt->second) {
            unloadIt->second(name);
            return true;
        }

        return false;
    }

    void SceneStream::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        scenes_.clear();
        loaders_.clear();
        unloaders_.clear();
    }

}
