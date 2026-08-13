//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_SCENE_CONFIG_H
#define DORIAX_SCENE_CONFIG_H

#include "Export.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    enum class SceneStreamingMode {
        Disabled,
        Manual,
        Chunked,
        Additive
    };

    struct DORIAX_API SceneConfig {
        std::string name;
        std::string path;
        bool active = true;
        bool persistent = false;
        bool additive = false;
        bool autoLoad = false;
        SceneStreamingMode streamingMode = SceneStreamingMode::Disabled;
        std::vector<std::string> dependencies;
        std::unordered_map<std::string, std::string> metadata;
    };

}

#endif // DORIAX_SCENE_CONFIG_H
