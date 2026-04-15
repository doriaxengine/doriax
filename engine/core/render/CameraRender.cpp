//
// (c) 2026 Eduardo Doria.
//

#include "CameraRender.h"

#include "sokol/SokolCamera.h"

using namespace doriax;

CameraRender::CameraRender(){ }

CameraRender::CameraRender(const CameraRender& rhs) : backend(rhs.backend) { }

CameraRender& CameraRender::operator=(const CameraRender& rhs) { 
    backend = rhs.backend; 
    return *this; 
}

CameraRender::~CameraRender(){ }

void CameraRender::setClearColor(Vector4 clearColor){
    backend.setClearColor(clearColor);
}

void CameraRender::setLoadActionLoad(){
    backend.setLoadActionLoad();
}

void CameraRender::startRenderPass(FramebufferRender* framebuffer, size_t face){
    backend.startRenderPass(framebuffer, face);
}

void CameraRender::startRenderPass(int width, int height){
    backend.startRenderPass(width, height);
}

void CameraRender::startRenderPass(){
    backend.startRenderPass();
}

void CameraRender::applyViewport(Rect rect){
    backend.applyViewport(rect);
}

void CameraRender::applyScissor(Rect rect){
    backend.applyScissor(rect);
}

void CameraRender::endRenderPass(){
    backend.endRenderPass();
}