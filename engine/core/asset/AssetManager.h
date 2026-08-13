//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_ASSET_MANAGER_H
#define DORIAX_ASSET_MANAGER_H

#include "Export.h"
#include "asset/AssetTypes.h"
#include "asset/AssetRegistry.h"
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    class DORIAX_API AssetManager {
    public:
        using AssetCallback = std::function<void(AssetId, bool)>;

        static AssetManager& get();

        AssetId load(const std::string& path, AssetType type, const std::string& name = "");
        AssetId loadAsync(const std::string& path, AssetType type, const std::string& name = "");

        bool isLoaded(AssetId id) const;
        bool isLoaded(const std::string& path) const;

        void registerCallback(AssetId id, AssetCallback callback);
        void flushCallbacks(AssetId id);
        void clear();

        std::vector<std::string> listAssets() const;

    private:
        AssetManager();
        std::unordered_map<AssetId, std::vector<AssetCallback>> callbacks_;
    };

}

#endif // DORIAX_ASSET_MANAGER_H
