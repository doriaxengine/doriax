//
// (c) 2026 Eduardo Doria.
//

#include "asset/AssetRegistry.h"
#include "Log.h"
#include <functional>

namespace doriax {

    AssetRegistry::AssetRegistry() = default;

    AssetRegistry& AssetRegistry::get() {
        static AssetRegistry instance;
        return instance;
    }

    AssetId AssetRegistry::registerAsset(const std::string& path, AssetType type, const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& [id, metadata] : assets_) {
            if (metadata.path == path) {
                return id;
            }
        }

        AssetId id = nextId_++;
        AssetMetadata metadata;
        metadata.id = id;
        metadata.type = type;
        metadata.path = path;
        metadata.name = name.empty() ? path : name;
        metadata.version = "1.0";
        assets_[id] = metadata;
        return id;
    }

    bool AssetRegistry::contains(AssetId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return assets_.find(id) != assets_.end();
    }

    bool AssetRegistry::containsPath(const std::string& path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, metadata] : assets_) {
            if (metadata.path == path) {
                return true;
            }
        }
        return false;
    }

    AssetMetadata* AssetRegistry::find(AssetId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(id);
        return it == assets_.end() ? nullptr : &it->second;
    }

    const AssetMetadata* AssetRegistry::find(AssetId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(id);
        return it == assets_.end() ? nullptr : &it->second;
    }

    AssetMetadata* AssetRegistry::findByPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, metadata] : assets_) {
            if (metadata.path == path) {
                return &metadata;
            }
        }
        return nullptr;
    }

    const AssetMetadata* AssetRegistry::findByPath(const std::string& path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, metadata] : assets_) {
            if (metadata.path == path) {
                return &metadata;
            }
        }
        return nullptr;
    }

    void AssetRegistry::setLoaded(AssetId id, bool loaded) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(id);
        if (it == assets_.end()) {
            return;
        }
        it->second.isLoaded = loaded;
    }

    void AssetRegistry::markDirty(AssetId id, bool dirty) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(id);
        if (it == assets_.end()) {
            return;
        }
        it->second.isDirty = dirty;
    }

    void AssetRegistry::addDependency(AssetId assetId, AssetId dependencyId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (assetId == 0 || dependencyId == 0) {
            return;
        }
        dependencies_[assetId].push_back(dependencyId);
    }

    const std::vector<AssetId>* AssetRegistry::getDependencies(AssetId assetId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = dependencies_.find(assetId);
        if (it == dependencies_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> AssetRegistry::listPaths() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> paths;
        paths.reserve(assets_.size());
        for (const auto& [id, metadata] : assets_) {
            paths.push_back(metadata.path);
        }
        return paths;
    }

    void AssetRegistry::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_.clear();
        dependencies_.clear();
        nextId_ = 1;
    }

}
