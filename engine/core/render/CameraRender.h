//
// (c) 2026 Eduardo Doria.
//

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
        void setLoadActionLoad();

        void startRenderPass(FramebufferRender* framebuffer, size_t face = 0);
        void startRenderPass(int width, int height);
        void startRenderPass();

        void applyViewport(Rect rect);
        void applyScissor(Rect rect);

        void endRenderPass();
    };
}


#endif /* CameraRender_h */