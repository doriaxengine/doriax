//
// (c) 2026 Eduardo Doria.
//

#ifndef FramebufferRender_h
#define FramebufferRender_h

#include "Render.h"
#include "sokol/SokolFramebuffer.h"

namespace doriax{
    class DORIAX_API FramebufferRender{

    public:
        //***Backend***
        SokolFramebuffer backend;
        //***
        
        FramebufferRender();
        FramebufferRender(const FramebufferRender& rhs);
        FramebufferRender& operator=(const FramebufferRender& rhs);
        virtual ~FramebufferRender();

        bool createFramebuffer(TextureType textureType, int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap);
        void destroyFramebuffer();
        bool isCreated();

        TextureRender& getColorTexture();
        TextureRender& getDepthTexture();

        uint32_t getGLHandler() const;
        const void* getD3D11HandlerColorRTV() const;
        const void* getD3D11HandlerDSV() const;
    };
}


#endif /* FramebufferRender_h */