//
// (c) 2026 Eduardo Doria.
//

#ifndef TEXTURE_H
#define TEXTURE_H

#include "render/TextureRender.h"
#include "texture/Framebuffer.h"
#include "texture/TextureData.h"
#include "pool/TexturePool.h"
#include "pool/TextureDataPool.h"
#include <string>
#include <memory>

namespace doriax{

    class DORIAX_API Texture{
        private:
            std::shared_ptr<TextureRender> render = NULL;
            Framebuffer* framebuffer;
            unsigned long lastFramebufferVersion = 0;

            TextureType type;
            std::string id;

            std::string paths[6];
            std::shared_ptr<std::array<TextureData,6>> data = NULL;

            size_t numFaces;

            bool loadFromPath;
            bool releaseDataAfterLoad;
            bool needLoad;

            //render properties
            TextureFilter minFilter;
            TextureFilter magFilter;
            TextureWrap wrapU;
            TextureWrap wrapV;

        public:
            Texture();
            Texture(const std::string& path);
            Texture(const std::string& id, TextureData data);
            Texture(Framebuffer* framebuffer);

            void setPath(const std::string& path);
            void setData(const std::string& id, TextureData data);
            void setId(const std::string& id);
            void setCubeMap(const std::string& path);
            void setCubePath(size_t index, const std::string& path);

            void setCubePaths(const std::string& front, const std::string& back,
                            const std::string& left, const std::string& right,
                            const std::string& up, const std::string& down);

            void setCubeDatas(const std::string& id, TextureData front, TextureData back, TextureData left, TextureData right, TextureData up, TextureData down);
            void setFramebuffer(Framebuffer* framebuffer);

            Texture(const Texture& rhs);
            Texture& operator=(const Texture& rhs);

            bool operator == ( const Texture& rhs ) const;
            bool operator != ( const Texture& rhs ) const;

            virtual ~Texture();

            TextureLoadResult load();
            void destroy();
            void invalidateRender();

            TextureRender* getRender(TextureRender* fallBackTexture = NULL);
            std::string getPath(size_t index = 0) const;
            TextureData& getData(size_t index = 0) const;
            std::string getId() const;

            size_t getNumFaces() const;
            TextureType getType() const;
            bool isCubeMap() const;

            unsigned int getWidth() const;
            unsigned int getHeight() const;
            bool isTransparent() const;

            void setReleaseDataAfterLoad(bool releaseDataAfterLoad);
            bool isReleaseDataAfterLoad() const;

            void releaseData();

            bool empty() const;
            bool isFramebuffer() const;
            bool isFramebufferOutdated() const;

            void setMinFilter(TextureFilter filter);
            TextureFilter getMinFilter() const;

            void setMagFilter(TextureFilter filter);
            TextureFilter getMagFilter() const;

            void setWrapU(TextureWrap wrapU);
            TextureWrap getWrapU() const;

            void setWrapV(TextureWrap wrapV);
            TextureWrap getWrapV() const;
    };
}

#endif //TEXTURE_H