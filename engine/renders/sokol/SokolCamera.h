// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SokolCamera_h
#define SokolCamera_h

#include "math/Rect.h"
#include "math/Vector4.h"
#include "render/FramebufferRender.h"

#include "sokol_gfx.h"

namespace doriax{
    class SokolCamera{

    private:
        sg_pass pass;
        // attachment view of a texture pass, released when the pass ends
        sg_view textureView;

    public:
        SokolCamera();
        SokolCamera(const SokolCamera& rhs);
        SokolCamera& operator=(const SokolCamera& rhs);
        virtual ~SokolCamera();

        void setClearColor(Vector4 clearColor);
        void setClearDepth(float clearDepth = 1.0f);
        void setLoadActionLoad();
        void setDepthLoadActionLoad();
        void setStoreDepth(bool storeDepth);

        void startRenderPass(FramebufferRender* framebuffer, size_t face);
        void startRenderPass(TextureRender* texture, size_t face, int mip);
        void startRenderPass(int width, int height);
        void startRenderPass();

        void applyViewport(Rect rect);
        void applyScissor(Rect rect);

        void endRenderPass();
    };
}

#endif //SokolCamera_h
