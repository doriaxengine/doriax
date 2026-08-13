//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_ASSET_TYPES_H
#define DORIAX_ASSET_TYPES_H

#include "Export.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    using AssetId = std::uint64_t;

    enum class AssetType {
        Texture,
        Mesh,
        Material,
        Shader,
        Audio,
        Animation,
        Scene,
        Prefab,
        Script,
        Font,
        Unknown
    };

    struct DORIAX_API AssetMetadata {
        AssetId id = 0;
        AssetType type = AssetType::Unknown;
        std::string path;
        std::string name;
        std::string source;
        std::string importHash;
        std::string version;
        bool isLoaded = false;
        bool isDirty = false;
    };

    struct DORIAX_API AssetDependency {
        AssetId assetId = 0;
        std::vector<AssetId> dependencies;
    };

    struct DORIAX_API AssetLoadRequest {
        AssetId id = 0;
        AssetType type = AssetType::Unknown;
        std::string path;
        std::string name;
        bool async = true;
    };

}

#endif // DORIAX_ASSET_TYPES_H
