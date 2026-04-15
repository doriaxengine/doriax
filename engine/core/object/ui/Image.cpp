//
// (c) 2026 Eduardo Doria.
//

#include "Image.h"

#include "util/Color.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Image::Image(Scene* scene): UILayout(scene){
    addComponent<UIComponent>();
    addComponent<ImageComponent>();
}

Image::Image(Scene* scene, Entity entity): UILayout(scene, entity){
}

Image::~Image(){
}

bool Image::createImage(){
    ImageComponent& img = getComponent<ImageComponent>();
    UIComponent& ui = getComponent<UIComponent>();
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return scene->getSystem<UISystem>()->createOrUpdateImage(img, ui, layout);
}

bool Image::load(){
    UIComponent& ui = getComponent<UIComponent>();

    return scene->getSystem<RenderSystem>()->loadUI(entity, ui, PIP_DEFAULT | PIP_RTT, false);
}

void Image::setPatchMargin(unsigned int margin){
    ImageComponent& img = getComponent<ImageComponent>();
    
    img.patchMarginLeft = margin;
    img.patchMarginRight = margin;
    img.patchMarginBottom = margin;
    img.patchMarginTop = margin;

    img.needUpdatePatches = true;
}

void Image::setPatchMargin(unsigned int marginLeft, unsigned int marginRight, unsigned int marginTop, unsigned int marginBottom){
    ImageComponent& img = getComponent<ImageComponent>();

    img.patchMarginLeft = marginLeft;
    img.patchMarginRight = marginRight;
    img.patchMarginBottom = marginBottom;
    img.patchMarginTop = marginTop;

    img.needUpdatePatches = true;
}

void Image::setPatchMarginBottom(unsigned int marginBottom){
    ImageComponent& img = getComponent<ImageComponent>();
    
    img.patchMarginBottom = marginBottom;

    img.needUpdatePatches = true;
}

void Image::setPatchMarginLeft(unsigned int marginLeft){
    ImageComponent& img = getComponent<ImageComponent>();
    
    img.patchMarginLeft = marginLeft;

    img.needUpdatePatches = true;
}

void Image::setPatchMarginRight(unsigned int marginRight){
    ImageComponent& img = getComponent<ImageComponent>();
    
    img.patchMarginRight = marginRight;

    img.needUpdatePatches = true;
}

void Image::setPatchMarginTop(unsigned int marginTop){
    ImageComponent& img = getComponent<ImageComponent>();

    img.patchMarginTop = marginTop;

    img.needUpdatePatches = true;
}

unsigned int Image::getPatchMarginBottom() const{
    ImageComponent& img = getComponent<ImageComponent>();
    
    return img.patchMarginBottom;
}

unsigned int Image::getPatchMarginLeft() const{
    ImageComponent& img = getComponent<ImageComponent>();
    
    return img.patchMarginLeft;
}

unsigned int Image::getPatchMarginRight() const{
    ImageComponent& img = getComponent<ImageComponent>();
    
    return img.patchMarginRight;
}

unsigned int Image::getPatchMarginTop() const{
    ImageComponent& img = getComponent<ImageComponent>();
    
    return img.patchMarginTop;
}

void Image::setTexture(const std::string& path){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setPath(path);

    ui.needUpdateTexture = true;
}

void Image::setTexture(const std::string& id, TextureData data){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setData(id, data);

    ui.needUpdateTexture = true;
}

void Image::setTexture(Framebuffer* framebuffer){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setFramebuffer(framebuffer);

    ui.needUpdateTexture = true;
}

void Image::setTextureScaleFactor(float textureScaleFactor){
    ImageComponent& img = getComponent<ImageComponent>();
    
    if (img.textureScaleFactor != textureScaleFactor){
        img.textureScaleFactor = textureScaleFactor;
        img.needUpdatePatches = true;
    }
}

float Image::getTextureScaleFactor() const{
    ImageComponent& img = getComponent<ImageComponent>();
    
    return img.textureScaleFactor;
}

void Image::setColor(Vector4 color){
    UIComponent& ui = getComponent<UIComponent>();

    ui.color = Color::sRGBToLinear(color);
}

void Image::setColor(const float red, const float green, const float blue, const float alpha){
    setColor(Vector4(red, green, blue, alpha));
}

void Image::setColor(const float red, const float green, const float blue){
    setColor(Vector4(red, green, blue, getColor().w));
}

void Image::setAlpha(const float alpha){
    Vector4 color = getColor();
    setColor(Vector4(color.x, color.y, color.z, alpha));
}

Vector4 Image::getColor() const{
    UIComponent& ui = getComponent<UIComponent>();

    return Color::linearTosRGB(ui.color);
}

float Image::getAlpha() const{
    return getColor().w;
}

void Image::setFlipY(bool flipY){
    UIComponent& ui = getComponent<UIComponent>();
    ImageComponent& img = getComponent<ImageComponent>();

    ui.automaticFlipY = false;
    if (ui.flipY != flipY){
        ui.flipY = flipY;

        img.needUpdatePatches = true;
    }
}

bool Image::isFlipY() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.flipY;
}

AABB Image::getAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.aabb;
}

AABB Image::getWorldAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.worldAABB;
}