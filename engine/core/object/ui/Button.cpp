// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Button.h"

#include "component/TextComponent.h"
#include "component/UIComponent.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Button::Button(Scene* scene): Image(scene){
    addComponent<ButtonComponent>();
}

Button::Button(Scene* scene, Entity entity): Image(scene, entity){
}

Button::~Button(){
}

bool Button::hasLabel() const{
    const ButtonComponent& btcomp = getComponent<ButtonComponent>();
    if (btcomp.label == NULL_ENTITY){
        return false;
    }

    Signature signature = scene->getSignature(btcomp.label);
    return signature.test(scene->getComponentId<TextComponent>());
}

void Button::ensureLabel(){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    if (!hasLabel()){
        scene->getSystem<UISystem>()->createButtonObjects(getEntity(), btcomp);
    }
}

Text Button::getLabelObject() const{
    const ButtonComponent& btcomp = getComponent<ButtonComponent>();
    if (!hasLabel()){
        return Text(scene, NULL_ENTITY);
    }

    return Text(scene, btcomp.label);
}

void Button::setLabel(const std::string& text){
    ensureLabel();
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    if (textcomp.text != text){
        textcomp.text = text;
        textcomp.needUpdateText = true;
    }
}

std::string Button::getLabel() const{
    if (!hasLabel()){
        return "";
    }

    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    return textcomp.text;
}

void Button::setLabelColor(Vector4 color){
    ensureLabel();
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    UIComponent& uilabel = scene->getComponent<UIComponent>(btcomp.label);

    uilabel.color = Color::sRGBToLinear(color);
}

void Button::setLabelColor(const float red, const float green, const float blue, const float alpha){
    setLabelColor(Vector4(red, green, blue, alpha));
}

Vector4 Button::getLabelColor() const{
    if (!hasLabel()){
        return Vector4(0.0, 0.0, 0.0, 1.0);
    }

    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    UIComponent& uilabel = scene->getComponent<UIComponent>(btcomp.label);

    return Color::linearTosRGB(uilabel.color);
}

void Button::setLabelFont(const std::string& font){
    ensureLabel();
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    getLabelObject().setFont(font);
    btcomp.needUpdateButton = true;
}

std::string Button::getLabelFont() const{
    if (!hasLabel()){
        return "";
    }

    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    return textcomp.font[0];
}

void Button::setLabelFontSize(unsigned int fontSize){
    ensureLabel();
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    getLabelObject().setFontSize(fontSize);
    btcomp.needUpdateButton = true;
}

unsigned int Button::getLabelFontSize() const{
    if (!hasLabel()){
        return 0;
    }

    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(btcomp.label);

    return textcomp.fontSize;
}

void Button::setTextureNormal(const std::string& path){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.textureNormal.setPath(path);
    btcomp.needUpdateButton = true;

    setTexture(path);
}

void Button::setTextureHovered(const std::string& path){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.textureHovered.setPath(path);
    btcomp.needUpdateButton = true;
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
    Vector4 oldColorNormal = btcomp.colorNormal;
    btcomp.colorNormal = Color::sRGBToLinear(color);
    if (btcomp.colorHovered == oldColorNormal){
        btcomp.colorHovered = btcomp.colorNormal;
    }
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

void Button::setColorHovered(Vector4 color){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.colorHovered = Color::sRGBToLinear(color);
    btcomp.needUpdateButton = true;
}

void Button::setColorHovered(const float red, const float green, const float blue, const float alpha){
    setColorHovered(Vector4(red, green, blue, alpha));
}

void Button::setColorHovered(const float red, const float green, const float blue){
    setColorHovered(Vector4(red, green, blue, getColorHovered().w));
}

Vector4 Button::getColorHovered() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return Color::linearTosRGB(btcomp.colorHovered);
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

void Button::setScaleHovered(float scaleHovered){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.scaleHovered = scaleHovered;
    btcomp.needUpdateButton = true;
}

float Button::getScaleHovered() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return btcomp.scaleHovered;
}

void Button::setScalePressed(float scalePressed){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.scalePressed = scalePressed;
    btcomp.needUpdateButton = true;
}

float Button::getScalePressed() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return btcomp.scalePressed;
}

void Button::setTransitionTime(float transitionTime){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.transitionTime = transitionTime;
}

float Button::getTransitionTime() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return btcomp.transitionTime;
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

void Button::setFocusable(bool focusable){
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    btcomp.focusable = focusable;
    btcomp.needUpdateButton = true;
}

bool Button::isFocusable() const{
    ButtonComponent& btcomp = getComponent<ButtonComponent>();
    return btcomp.focusable;
}

void Button::setFocus(){
    scene->getSystem<UISystem>()->setFocus(getEntity());
}

bool Button::isFocused() const{
    UIComponent& ui = getComponent<UIComponent>();
    return ui.focused;
}