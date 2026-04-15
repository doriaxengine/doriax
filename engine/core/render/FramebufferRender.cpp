//
// (c) 2026 Eduardo Doria.
//

#include "FramebufferRender.h"
#include "Engine.h"

using namespace doriax;

FramebufferRender::FramebufferRender(){ }

FramebufferRender::FramebufferRender(const FramebufferRender& rhs) : backend(rhs.backend) { }

FramebufferRender& FramebufferRender::operator=(const FramebufferRender& rhs) { 
    backend = rhs.backend;
    return *this; 
}

FramebufferRender::~FramebufferRender(){
    //Cannot destroy because its a handle
}

bool FramebufferRender::createFramebuffer(TextureType textureType, int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createFramebuffer(textureType, width, height, minFilter, magFilter, wrapU, wrapV, shadowMap);
    else
        return false;
}

void FramebufferRender::destroyFramebuffer(){
    backend.destroyFramebuffer();
}

bool FramebufferRender::isCreated(){
    return backend.isCreated();
}

TextureRender& FramebufferRender::getColorTexture(){
    return backend.getColorTexture();
}

TextureRender& FramebufferRender::getDepthTexture(){
    return backend.getDepthTexture();
}

uint32_t FramebufferRender::getGLHandler() const{
    return backend.getGLHandler();
}

const void* FramebufferRender::getD3D11HandlerColorRTV() const{
    return backend.getD3D11HandlerColorRTV();
}

const void* FramebufferRender::getD3D11HandlerDSV() const{
    return backend.getD3D11HandlerDSV();
}
