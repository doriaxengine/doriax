//
// (c) 2026 Eduardo Doria.
//

#ifndef SokolFramebuffer_h
#define SokolFramebuffer_h

#include "render/Render.h"
#include "math/Vector4.h"
#include "render/TextureRender.h"

#include "sokol_gfx.h"

namespace doriax{
    class SokolFramebuffer{

    private:
        sg_attachments attachments[6];

        TextureRender colorTexture;
        TextureRender depthTexture;

    public:
        SokolFramebuffer();
        SokolFramebuffer(const SokolFramebuffer& rhs);
        SokolFramebuffer& operator=(const SokolFramebuffer& rhs);

        bool createFramebuffer(TextureType textureType, int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap);
        void destroyFramebuffer();
        bool isCreated();

        TextureRender& getColorTexture();
        TextureRender& getDepthTexture();

        uint32_t getGLHandler() const;
        const void* getD3D11HandlerColorRTV() const;
        const void* getD3D11HandlerDSV() const;

        sg_attachments get(size_t face);
    };
}

#endif /* SokolFramebBuffer_h */