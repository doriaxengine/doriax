//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_SCENE_STREAM_H
#define DORIAX_SCENE_STREAM_H

#include "Export.h"
#include "scene/SceneConfig.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    class DORIAX_API SceneStream {
    public:
        using SceneLoader = std::function<void(const std::string&)>;
        using SceneUnload = std::function<void(const std::string&)>;

        static SceneStream& get();

        void registerScene(const SceneConfig& config);
        void unregisterScene(const std::string& name);
        void setLoader(const std::string& sceneName, SceneLoader loader);
        void setUnloadHandler(const std::string& sceneName, SceneUnload unloadHandler);

        bool isRegistered(const std::string& name) const;
        const SceneConfig* find(const std::string& name) const;
        std::vector<std::string> listScenes() const;

        bool loadScene(const std::string& name);
        bool unloadScene(const std::string& name);
        void clear();

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::string, SceneConfig> scenes_;
        std::unordered_map<std::string, SceneLoader> loaders_;
        std::unordered_map<std::string, SceneUnload> unloaders_;
    };

}

#endif // DORIAX_SCENE_STREAM_H
