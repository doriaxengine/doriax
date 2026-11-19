// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

bool FramebufferRender::createDepthOnlyFramebuffer(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createDepthOnlyFramebuffer(width, height, minFilter, magFilter, wrapU, wrapV, shadowMap);
    else
        return false;
}

bool FramebufferRender::createFramebufferMRT(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, int numColor, const ColorFormat* formats){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createFramebufferMRT(width, height, minFilter, magFilter, wrapU, wrapV, numColor, formats);
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

TextureRender& FramebufferRender::getColorAttachmentTexture(int index){
    return backend.getColorTexture(index);
}

int FramebufferRender::getNumColorAttachments() const{
    return backend.getNumColorAttachments();
}

ColorFormat FramebufferRender::getColorFormat() const{
    return backend.getColorFormat();
}

bool FramebufferRender::isColorFormatSupported(ColorFormat format){
    return SokolFramebuffer::isColorFormatSupported(format);
}

TextureRender& FramebufferRender::getDepthTexture(){
    return backend.getDepthTexture();
}

const void* FramebufferRender::getD3D11HandlerColorRTV() const{
    return backend.getD3D11HandlerColorRTV();
}

const void* FramebufferRender::getD3D11HandlerDSV() const{
    return backend.getD3D11HandlerDSV();
}
