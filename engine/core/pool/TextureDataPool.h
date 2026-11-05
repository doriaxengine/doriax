// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

        static std::unordered_map<std::string, std::future<std::shared_ptr<std::array<TextureData,6>>>> pendingBuilds;
        static std::mutex cacheMutex;
        static std::atomic<bool> shutdownRequested;

        static bool hasTexturePixels(const std::shared_ptr<std::array<TextureData,6>>& data, size_t numFaces);
        static size_t getTextureFaces(std::array<TextureData,6>& data);

        static std::shared_ptr<std::array<TextureData,6>> loadTextureInternal(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces, bool fixAlphaBorder, bool trackProgress);
        static std::string getTextureDisplayName(const std::string& path);
        static std::string validateTextureFaces(std::array<TextureData,6>& data, size_t numFaces);

    public:
        static std::shared_ptr<std::array<TextureData,6>> get(const std::string& id);
        static std::shared_ptr<std::array<TextureData,6>> get(const std::string& id, std::array<TextureData,6> data);
        // Inserts an already-built face array without copying it (used to hand pre-decoded model
        // textures to the pool). No-op if the id is already cached with pixels. Main-thread only.
        static void put(const std::string& id, std::shared_ptr<std::array<TextureData,6>> data);

        static TextureLoadResult loadFromFile(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces, bool fixAlphaBorder = false);

        static void requestShutdown();
        // re-allows async loads after a recreated view
        static void cancelShutdown();

        static void remove(const std::string& id);
        // Drops the entry even while others hold it, so the next load decodes again
        static void invalidate(const std::string& id);
        static void clear();
        // removes only entries no one else references (safe while other scenes are alive)
        static void clearUnused();
    };
}

#endif /* TEXTUREDATAPOOL_H */
