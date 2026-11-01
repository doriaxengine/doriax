// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CameraRender_h
#define CameraRender_h

#include "Engine.h"
#include "math/Rect.h"
#include "sokol/SokolCamera.h"

namespace doriax{
    class DORIAX_API CameraRender{
    public:
        //***Backend***
        SokolCamera backend;
        //***

        CameraRender();
        CameraRender(const CameraRender& rhs);
        CameraRender& operator=(const CameraRender& rhs);
        virtual ~CameraRender();

        void setClearColor(Vector4 clearColor);
        void setClearDepth(float clearDepth = 1.0f);
        void setLoadActionLoad();
        // continues on the depth of an earlier pass that stored it
        void setDepthLoadActionLoad();
        void setStoreDepth(bool storeDepth);

        void startRenderPass(FramebufferRender* framebuffer, size_t face = 0);
        // into one face and mip level of a color attachment texture, with no depth
        void startRenderPass(TextureRender* texture, size_t face, int mip);
        void startRenderPass(int width, int height);
        void startRenderPass();

        void applyViewport(Rect rect);
        void applyScissor(Rect rect);

        void endRenderPass();
    };
}


#endif /* CameraRender_h */
