// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

void CameraRender::setClearDepth(float clearDepth){
    backend.setClearDepth(clearDepth);
}

void CameraRender::setLoadActionLoad(){
    backend.setLoadActionLoad();
}

void CameraRender::setDepthLoadActionLoad(){
    backend.setDepthLoadActionLoad();
}

void CameraRender::setStoreDepth(bool storeDepth){
    backend.setStoreDepth(storeDepth);
}

void CameraRender::startRenderPass(FramebufferRender* framebuffer, size_t face){
    backend.startRenderPass(framebuffer, face);
}

void CameraRender::startRenderPass(TextureRender* texture, size_t face, int mip){
    backend.startRenderPass(texture, face, mip);
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
