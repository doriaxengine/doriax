//
// (c) 2026 Eduardo Doria.
//

#ifndef TEXTUREDATAPOOL_H
#define TEXTUREDATAPOOL_H

#include "Engine.h"
#include "render/TextureRender.h"
#include "texture/TextureData.h"

#include <map>
#include <memory>
#include <array>

#include <mutex>
#include <future>
#include <unordered_map>
#include <atomic>

namespace doriax{

    typedef std::map< std::string, std::shared_ptr<std::array<TextureData,6>> > texturesdata_t;

    struct TextureLoadResult {
        std::string id;
        ResourceLoadState state = ResourceLoadState::NotStarted;
        std::string errorMessage;
        std::shared_ptr<std::array<TextureData, 6>> data = nullptr;

        TextureLoadResult() = default;

        explicit operator bool() const {
            return state == ResourceLoadState::Finished;
        }
    };

    class DORIAX_API TextureDataPool{
    private:
        static texturesdata_t& getMap();

        static bool asyncLoading;
        static std::unordered_map<std::string, std::future<std::array<TextureData,6>>> pendingBuilds;
        static std::mutex cacheMutex;
        static std::atomic<bool> shutdownRequested;

        static std::array<TextureData,6> loadTextureInternal(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces);
        static std::string getTextureDisplayName(const std::string& path);
        static std::string validateTextureFaces(std::array<TextureData,6>& data, size_t numFaces);

    public:
        static std::shared_ptr<std::array<TextureData,6>> get(const std::string& id);
        static std::shared_ptr<std::array<TextureData,6>> get(const std::string& id, std::array<TextureData,6> data);

        static TextureLoadResult loadFromFile(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces);

        static void setAsyncLoading(bool enable);
        static bool isAsyncLoading();

        static void requestShutdown();

        static void remove(const std::string& id);
        static void clear();
    };
}

#endif /* TEXTUREDATAPOOL_H */
