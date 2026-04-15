//
// (c) 2026 Eduardo Doria.
//

#include "Framebuffer.h"

#include "Engine.h"
#include "Log.h"

using namespace doriax;

Framebuffer::Framebuffer(){
    this->width = 512;
    this->height = 512;
    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
    this->version = 0;
}

Framebuffer::~Framebuffer(){
    destroy();
}

Framebuffer::Framebuffer(const Framebuffer& rhs){
    this->render = rhs.render;
    this->width = rhs.width;
    this->height = rhs.height;
    this->minFilter = rhs.minFilter;
    this->magFilter = rhs.magFilter;
    this->wrapU = rhs.wrapU;
    this->wrapV = rhs.wrapV;
    this->version = rhs.version;
}

Framebuffer& Framebuffer::operator=(const Framebuffer& rhs){
    this->render = rhs.render;
    this->width = rhs.width;
    this->height = rhs.height;
    this->minFilter = rhs.minFilter;
    this->magFilter = rhs.magFilter;
    this->wrapU = rhs.wrapU;
    this->wrapV = rhs.wrapV;
    this->version = rhs.version;
    return *this;
}

void Framebuffer::create(){
    render.createFramebuffer(TextureType::TEXTURE_2D, width, height, minFilter, magFilter, wrapU, wrapV, false);
    version++;
}

void Framebuffer::destroy(){
    render.destroyFramebuffer();
}

bool Framebuffer::isCreated(){
    return render.isCreated();
}

FramebufferRender& Framebuffer::getRender(){
    return render;
}

unsigned long Framebuffer::getVersion(){
    return this->version;
}

void Framebuffer::setWidth(unsigned int width){
    this->width = width;
}

unsigned int Framebuffer::getWidth() const{
    return this->width;
}

void Framebuffer::setHeight(unsigned int height){
    this->height = height;
}

unsigned int Framebuffer::getHeight() const{
    return this->height;
}

void Framebuffer::setMinFilter(TextureFilter filter){
    this->minFilter = filter;
}

TextureFilter Framebuffer::getMinFilter() const{
    return this->minFilter;
}

void Framebuffer::setMagFilter(TextureFilter filter){
    this->magFilter = filter;
}

TextureFilter Framebuffer::getMagFilter() const{
    return this->magFilter;
}

void Framebuffer::setWrapU(TextureWrap wrapU){
    this->wrapU = wrapU;
}

TextureWrap Framebuffer::getWrapU() const{
    return this->wrapU;
}

void Framebuffer::setWrapV(TextureWrap wrapV){
    this->wrapV = wrapV;
}

TextureWrap Framebuffer::getWrapV() const{
    return this->wrapV;
}