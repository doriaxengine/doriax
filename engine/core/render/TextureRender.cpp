// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TextureRender.h"
#include "Engine.h"

using namespace doriax;

TextureRender::TextureRender(){
 }

TextureRender::TextureRender(const TextureRender& rhs) : backend(rhs.backend), transparent(rhs.transparent) { }

TextureRender& TextureRender::operator=(const TextureRender& rhs) { 
    backend = rhs.backend;
    transparent = rhs.transparent;
    return *this; 
}

TextureRender::~TextureRender(){
    //Cannot destroy because its a handle
}

bool TextureRender::createTexture(
                const std::string& label, int width, int height,
                ColorFormat colorFormat, TextureType type, int numFaces, void* data[], size_t size[], 
                TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createTexture(label, width, height, colorFormat, type, numFaces, data, size, minFilter, magFilter, wrapU, wrapV);
    else
        return false;
}

bool TextureRender::createTextureCubeWithMips(
                const std::string& label, int width,
                ColorFormat colorFormat, int numMipmaps, void* data[], size_t size[],
                TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createTextureCubeWithMips(label, width, colorFormat, numMipmaps, data, size, minFilter, magFilter, wrapU, wrapV);
    else
        return false;
}

bool TextureRender::createFramebufferTexture(
                TextureType type, bool depth, bool shadowMap, int width, int height,
                TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV,
                ColorFormat colorFormat, int numMipmaps){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createFramebufferTexture(type, depth, shadowMap, width, height, minFilter, magFilter, wrapU, wrapV, colorFormat, numMipmaps);
    else
        return false;
}

bool TextureRender::createDynamicTexture(const std::string& label, int width, int height){
    return Engine::isViewLoaded() && !isCreated() &&
        backend.createDynamicTexture(label, width, height);
}

void TextureRender::updateTexture(const void* data, size_t size){
    backend.updateTexture(data, size);
}

void TextureRender::destroyTexture(){
    backend.destroyTexture();
}

uint32_t TextureRender::getGLHandler() const{
    return backend.getGLHandler();
}

const void* TextureRender::getMetalHandler() const{
    return backend.getMetalHandler();
}

const void* TextureRender::getD3D11Handler() const{
    return backend.getD3D11Handler();
}

const void* TextureRender::getVulkanHandler() const{
    return backend.getVulkanHandler();
}

const void* TextureRender::getVulkanImageHandler() const{
    return backend.getVulkanImageHandler();
}

uint32_t TextureRender::getViewId() const{
    return backend.getViewId();
}

bool TextureRender::isViewValid(uint32_t viewId){
    return SokolTexture::isViewValid(viewId);
}

bool TextureRender::isCreated(){
    return backend.isCreated();
}

void TextureRender::setTransparent(bool transparent){
    this->transparent = transparent;
}

bool TextureRender::isTransparent() const{
    return transparent;
}
