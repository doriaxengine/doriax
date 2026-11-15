// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef TEXTUREPOOL_H
#define TEXTUREPOOL_H

#include "render/TextureRender.h"
#include "texture/TextureData.h"
#include <map>
#include <memory>
#include <array>
#include <set>

namespace doriax{

    typedef std::map< std::string, std::shared_ptr<TextureRender> > textures_t;

    class DORIAX_API TexturePool{
    private:
        static textures_t& getMap();
        static std::set<std::string>& getInvalidated();

    public:
        // the sampler is baked into the backend texture, so a non-default one is part of the key
        static std::string getRenderId(const std::string& id, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);

        static std::shared_ptr<TextureRender> get(const std::string& id);
        static std::shared_ptr<TextureRender> get(const std::string& id, TextureType type, const std::shared_ptr<std::array<TextureData,6>> &data, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);
        static void add(const std::string& id, std::shared_ptr<TextureRender> render);
        static void remove(const std::string& id);
        // also the id's sampler variants
        static void invalidate(const std::string& id);

        // necessary for engine shutdown
        static void clear();
        // destroys only entries no one else references (safe while other scenes are alive)
        static void clearUnused();
        // streamed textures share a slice of each frame, the rest wait
        static void beginFrame();
        static bool canUpload();

    };
}

#endif /* TEXTUREPOOL_H */
