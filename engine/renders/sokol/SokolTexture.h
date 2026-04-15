//
// (c) 2026 Eduardo Doria.
//

#ifndef SokolTexture_h
#define SokolTexture_h

#include "render/Render.h"
#include "sokol_gfx.h"
#include <string>

namespace doriax{
    class SokolTexture{

    private:
        sg_image image;
        sg_sampler sampler;

        sg_image_type getTextureType(TextureType textureType);
        sg_filter getFilter(TextureFilter textureFilter);
        sg_filter getFilterMipmap(TextureFilter textureFilter);
        sg_wrap getWrap(TextureWrap textureWrap);

        sg_image generateMipmaps(const sg_image_desc* desc_);

        static void cleanupMipmapTexture(void* data);

    public:
        SokolTexture();
        SokolTexture(const SokolTexture& rhs);
        SokolTexture& operator=(const SokolTexture& rhs);

        bool createTexture(
                    const std::string& label, int width, int height,
                    ColorFormat colorFormat, TextureType type, int numFaces, void* data[6], size_t size[6], 
                    TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);

        bool createFramebufferTexture(
                    TextureType type, bool depth, bool shadowMap, int width, int height, 
                    TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);

        void destroyTexture();

        uint32_t getGLHandler() const;
        const void* getMetalHandler() const;
        const void* getD3D11Handler() const;

        bool isCreated();

        sg_image get();
        sg_sampler getSampler();
    };
}

#endif /* SokolTexture_h */
