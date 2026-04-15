//
// (c) 2026 Eduardo Doria.
//

#include "Button.h"

#include "component/TextComponent.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Button::Button(Scene* scene): Image(scene){
    addComponent<ButtonComponent>();
}

Button::Button(Scene* scene, Entity entity): Image(scene, entity){
}

Button::~Button(){
}

Text Button::getLabelObject() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();

    return Text(scene, btcomp.label);
}

void Button::setLabel(const std::string& text){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    if (textcomp.text != text){
        textcomp.text = text;
        textcomp.needUpdateText = true;
    }
}

std::string Button::getLabel() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    return textcomp.text;
}

void Button::setLabelColor(Vector4 color){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    UIComponent& uilabel = scene->getComponent<UIComponent>(btcomp.label);

    uilabel.color = Color::sRGBToLinear(color);
}

void Button::setLabelColor(const float red, const float green, const float blue, const float alpha){
    setLabelColor(Vector4(red, green, blue, alpha));
}

Vector4 Button::getLabelColor() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    UIComponent& uilabel = scene->getComponent<UIComponent>(btcomp.label);

    return uilabel.color;
}

void Button::setLabelFont(const std::string& font){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    getLabelObject().setFont(font);
    btcomp.needUpdateButton = true;
}

std::string Button::getLabelFont() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    return textcomp.font;
}

void Button::setLabelFontSize(unsigned int fontSize){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    getLabelObject().setFontSize(fontSize);
    btcomp.needUpdateButton = true;
}

unsigned int Button::getLabelFontSize() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return getLabelObject().getFontSize();
}

void Button::setTextureNormal(const std::string& path){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.textureNormal.setPath(path);
    btcomp.needUpdateButton = true;

    setTexture(path);
}

void Button::setTexturePressed(const std::string& path){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.texturePressed.setPath(path);
    btcomp.needUpdateButton = true;
}

void Button::setTextureDisabled(const std::string& path){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.textureDisabled.setPath(path);
    btcomp.needUpdateButton = true;
}

void Button::setColorNormal(Vector4 color){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.colorNormal = Color::sRGBToLinear(color);
    btcomp.needUpdateButton = true;

    setColor(color);
}

void Button::setColorNormal(const float red, const float green, const float blue, const float alpha){
    setColorNormal(Vector4(red, green, blue, alpha));
}

void Button::setColorNormal(const float red, const float green, const float blue){
    setColorNormal(Vector4(red, green, blue, getColorNormal().w));
}

Vector4 Button::getColorNormal() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return Color::linearTosRGB(btcomp.colorNormal);
}

void Button::setColorPressed(Vector4 color){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.colorPressed = Color::sRGBToLinear(color);
    btcomp.needUpdateButton = true;
}

void Button::setColorPressed(const float red, const float green, const float blue, const float alpha){
    setColorPressed(Vector4(red, green, blue, alpha));
}

void Button::setColorPressed(const float red, const float green, const float blue){
    setColorPressed(Vector4(red, green, blue, getColorPressed().w));
}

Vector4 Button::getColorPressed() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return Color::linearTosRGB(btcomp.colorPressed);
}

void Button::setColorDisabled(Vector4 color){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.colorDisabled = Color::sRGBToLinear(color);
    btcomp.needUpdateButton = true;
}

void Button::setColorDisabled(const float red, const float green, const float blue, const float alpha){
    setColorDisabled(Vector4(red, green, blue, alpha));
}

void Button::setColorDisabled(const float red, const float green, const float blue){
    setColorDisabled(Vector4(red, green, blue, getColorDisabled().w));
}

Vector4 Button::getColorDisabled() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return Color::linearTosRGB(btcomp.colorDisabled);
}

void Button::setDisabled(bool disabled){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.disabled = disabled;
    btcomp.needUpdateButton = true;
}

bool Button::getDisabled() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return btcomp.disabled;
}