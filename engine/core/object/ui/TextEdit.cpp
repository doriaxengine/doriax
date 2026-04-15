//
// (c) 2026 Eduardo Doria.
//

#include "TextEdit.h"

#include "component/TextComponent.h"
#include "subsystem/UISystem.h"

using namespace doriax;

TextEdit::TextEdit(Scene* scene): Image(scene){
    addComponent<TextEditComponent>();
}

TextEdit::TextEdit(Scene* scene, Entity entity): Image(scene, entity){
}

TextEdit::~TextEdit(){
}

Text TextEdit::getTextObject() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return Text(scene, tecomp.text);
}

doriax::Polygon TextEdit::getCursorObject() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return Polygon(scene, tecomp.cursor);
}

void TextEdit::setDisabled(bool disabled){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    tecomp.disabled = disabled;

    tecomp.needUpdateTextEdit = true;
}

bool TextEdit::getDisabled() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return  tecomp.disabled;
}

void TextEdit::setText(const std::string& text){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(tecomp.text);

    if (textcomp.text != text){
        textcomp.text = text;
        textcomp.needUpdateText = true;
    }
}

std::string TextEdit::getText() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(tecomp.text);

    return textcomp.text;
}

void TextEdit::setTextColor(Vector4 color){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    UIComponent& uitext = scene->getComponent<UIComponent>(tecomp.text);

    uitext.color = Color::sRGBToLinear(color);
}

void TextEdit::setTextColor(const float red, const float green, const float blue, const float alpha){
    setTextColor(Vector4(red, green, blue, alpha));
}

void TextEdit::setTextColor(const float red, const float green, const float blue){
    setTextColor(Vector4(red, green, blue, getTextColor().w));
}

Vector4 TextEdit::getTextColor() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    UIComponent& uitext = scene->getComponent<UIComponent>(tecomp.text);

    return Color::linearTosRGB(uitext.color);
}

void TextEdit::setTextFont(const std::string& font){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    getTextObject().setFont(font);

    tecomp.needUpdateTextEdit = true;
}

std::string TextEdit::getTextFont() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(tecomp.text);

    return textcomp.font;
}

void TextEdit::setFontSize(unsigned int fontSize){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    getTextObject().setFontSize(fontSize);

    tecomp.needUpdateTextEdit = true;
}

unsigned int TextEdit::getFontSize() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return getTextObject().getFontSize();
}

void TextEdit::setMaxTextSize(unsigned int maxTextSize){
    getTextObject().setMaxTextSize(maxTextSize);
}

unsigned int TextEdit::getMaxTextSize() const{
    return getTextObject().getMaxTextSize();
}

void TextEdit::setCursorBlink(float cursorBlink){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    tecomp.cursorBlink = cursorBlink;
}

float TextEdit::getCursorBlink() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return tecomp.cursorBlink;
}

void TextEdit::setCursorWidth(float cursorWidth){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    if (tecomp.cursorWidth != cursorWidth){
        tecomp.cursorWidth = cursorWidth;
        tecomp.needUpdateTextEdit = true;
    }
}

float TextEdit::getCursorWidth() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();

    return tecomp.cursorWidth;
}

void TextEdit::setCursorColor(Vector4 color){
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    tecomp.needUpdateTextEdit = true;
    tecomp.cursorColor = Color::sRGBToLinear(color);
}

void TextEdit::setCursorColor(const float red, const float green, const float blue, const float alpha){
    setCursorColor(Vector4(red, green, blue, alpha));
}

void TextEdit::setCursorColor(const float red, const float green, const float blue){
    setCursorColor(Vector4(red, green, blue, getCursorColor().w));
}

Vector4 TextEdit::getCursorColor() const{
    TextEditComponent& tecomp = getComponent<TextEditComponent>();
    return Color::linearTosRGB(tecomp.cursorColor);
}