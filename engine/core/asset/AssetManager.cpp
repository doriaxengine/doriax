//
// (c) 2026 Eduardo Doria.
//

#include "asset/AssetManager.h"
#include "thread/JobSystem.h"
#include "Log.h"
#include <future>

namespace doriax {

    AssetManager::AssetManager() = default;

    AssetManager& AssetManager::get() {
        static AssetManager instance;
        return instance;
    }

    AssetId AssetManager::load(const std::string& path, AssetType type, const std::string& name) {
        AssetId id = AssetRegistry::get().registerAsset(path, type, name);
        AssetRegistry::get().setLoaded(id, true);
        return id;
    }

    AssetId AssetManager::loadAsync(const std::string& path, AssetType type, const std::string& name) {
        AssetId id = AssetRegistry::get().registerAsset(path, type, name);

        JobSystem::get().schedule([id, path]() {
            AssetRegistry::get().setLoaded(id, true);
            Log::debug("Asset async load completed for %s", path.c_str());
        });

        return id;
    }

    bool AssetManager::isLoaded(AssetId id) const {
        const AssetMetadata* metadata = AssetRegistry::get().find(id);
        return metadata && metadata->isLoaded;
    }

    bool AssetManager::isLoaded(const std::string& path) const {
        const AssetMetadata* metadata = AssetRegistry::get().findByPath(path);
        return metadata && metadata->isLoaded;
    }

    void AssetManager::registerCallback(AssetId id, AssetCallback callback) {
        if (callback) {
            callbacks_[id].push_back(std::move(callback));
        }
    }

    void AssetManager::flushCallbacks(AssetId id) {
        auto it = callbacks_.find(id);
        if (it == callbacks_.end()) {
            return;
        }

        for (auto& callback : it->second) {
            if (callback) {
                callback(id, isLoaded(id));
            }
        }
        callbacks_.erase(it);
    }

    void AssetManager::clear() {
        callbacks_.clear();
        AssetRegistry::get().clear();
    }

    std::vector<std::string> AssetManager::listAssets() const {
        return AssetRegistry::get().listPaths();
    }

}
