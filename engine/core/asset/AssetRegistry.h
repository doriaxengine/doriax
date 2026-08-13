//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_ASSET_REGISTRY_H
#define DORIAX_ASSET_REGISTRY_H

#include "Export.h"
#include "asset/AssetTypes.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    class DORIAX_API AssetRegistry {
    public:
        static AssetRegistry& get();

        AssetId registerAsset(const std::string& path, AssetType type, const std::string& name = "");
        bool contains(AssetId id) const;
        bool containsPath(const std::string& path) const;
        AssetMetadata* find(AssetId id);
        const AssetMetadata* find(AssetId id) const;
        AssetMetadata* findByPath(const std::string& path);
        const AssetMetadata* findByPath(const std::string& path) const;

        void setLoaded(AssetId id, bool loaded);
        void markDirty(AssetId id, bool dirty);

        void addDependency(AssetId assetId, AssetId dependencyId);
        const std::vector<AssetId>* getDependencies(AssetId assetId) const;
        std::vector<std::string> listPaths() const;

        void clear();

    private:
        AssetRegistry();
        mutable std::mutex mutex_;
        std::unordered_map<AssetId, AssetMetadata> assets_;
        std::unordered_map<AssetId, std::vector<AssetId>> dependencies_;
        AssetId nextId_ = 1;
    };

}

#endif // DORIAX_ASSET_REGISTRY_H
