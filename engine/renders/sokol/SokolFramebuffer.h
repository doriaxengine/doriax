// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SokolFramebuffer_h
#define SokolFramebuffer_h

#include "render/Render.h"
#include "math/Vector4.h"
#include "render/TextureRender.h"

#include "sokol_gfx.h"

namespace doriax{
    class SokolFramebuffer{

    public:
        static const int MAX_COLOR_ATTACHMENTS = 4;

    private:
        // [attachment][face]; only 2D framebuffers use MRT (single face), cube
        // framebuffers use a single color attachment across 6 faces.
        sg_view colorAttachmentViews[MAX_COLOR_ATTACHMENTS][6];
        sg_view depthAttachmentView;

        TextureRender colorTexture[MAX_COLOR_ATTACHMENTS];
        TextureRender depthTexture;

        int numColorAttachments;
        ColorFormat colorFormat; // of the first color attachment

        void initializeAttachments(size_t faces);

    public:
        SokolFramebuffer();
        SokolFramebuffer(const SokolFramebuffer& rhs);
        SokolFramebuffer& operator=(const SokolFramebuffer& rhs);

        bool createFramebuffer(TextureType textureType, int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap);
        bool createDepthOnlyFramebuffer(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap);
        // Multi render target variant (2D only). formats[i] selects the pixel format
        // for color attachment i; pass numColor==1 for the regular single-target case.
        bool createFramebufferMRT(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, int numColor, const ColorFormat* formats);
        void destroyFramebuffer();
        bool isCreated();

        int getNumColorAttachments() const;
        ColorFormat getColorFormat() const;
        // can be drawn into, blended and filtered
        static bool isColorFormatSupported(ColorFormat format);

        TextureRender& getColorTexture();
        TextureRender& getColorTexture(int index);
        TextureRender& getDepthTexture();

        const void* getD3D11HandlerColorRTV() const;
        const void* getD3D11HandlerDSV() const;

        sg_attachments get(size_t face);
    };
}

#endif /* SokolFramebBuffer_h */
