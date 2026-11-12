// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Text.h"

#include "Log.h"
#include "util/Color.h"
#include "util/STBText.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Text::Text(Scene* scene): UILayout(scene){
    addComponent<UIComponent>();
    addComponent<TextComponent>();
}

Text::Text(Scene* scene, Entity entity): UILayout(scene, entity){
}

Text::~Text(){
}

bool Text::createText(){
    TextComponent& textcomp = getComponent<TextComponent>();
    UIComponent& ui = getComponent<UIComponent>();
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return scene->getSystem<UISystem>()->createOrUpdateText(textcomp, ui, layout);
}

bool Text::load(){
    UIComponent& ui = getComponent<UIComponent>();

    auto renderSystem = scene->getSystem<RenderSystem>();
    return renderSystem->loadUI(entity, ui, renderSystem->getScenePipelines(), true);
}

void Text::setFixedSize(bool fixedSize){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (textcomp.fixedWidth != fixedSize || textcomp.fixedHeight != fixedSize){
        textcomp.fixedWidth = fixedSize;
        textcomp.fixedHeight = fixedSize;

        textcomp.needUpdateText = true;
    }
}

void Text::setFixedWidth(bool fixedWidth){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (textcomp.fixedWidth != fixedWidth){
        textcomp.fixedWidth = fixedWidth;

        textcomp.needUpdateText = true;
    }
}

bool Text::isFixedWidth() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.fixedWidth;
}

void Text::setFixedHeight(bool fixedHeight){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (textcomp.fixedHeight != fixedHeight){
        textcomp.fixedHeight = fixedHeight;

        textcomp.needUpdateText = true;
    }
}

bool Text::isFixedHeight() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.fixedHeight;
}

void Text::setMaxTextSize(unsigned int maxTextSize){
    TextComponent& textcomp = getComponent<TextComponent>();
    UIComponent& ui = getComponent<UIComponent>();

    if (textcomp.maxTextSize != maxTextSize){
        textcomp.maxTextSize = maxTextSize;

        textcomp.needReloadAtlas = true;
        textcomp.needUpdateText = true;
        ui.needReload = true;
    }
}

unsigned int Text::getMaxTextSize() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.maxTextSize;
}

void Text::setText(const std::string &text){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (textcomp.text != text){
        textcomp.text = text;
        textcomp.needUpdateText = true;
    }
}

std::string Text::getText() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.text;
}

void Text::setFont(const std::string &font){
    setFont(0, font);
}

void Text::setFont(unsigned int index, const std::string &font){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (index >= textcomp.font.size()){
        Log::error("Cannot use invalid font index: %u", index);
        return;
    }

    if (textcomp.font[index] != font){
        textcomp.font[index] = font;

        textcomp.needReloadAtlas = true;
        textcomp.needUpdateText = true;
    }
}

std::string Text::getFont() const{
    return getFont(0);
}

std::string Text::getFont(unsigned int index) const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (index >= textcomp.font.size()){
        Log::error("Cannot use invalid font index: %u", index);
        return "";
    }

    return textcomp.font[index];
}


void Text::setFontSize(unsigned int fontSize){
    TextComponent& textcomp = getComponent<TextComponent>();

    if (textcomp.fontSize != fontSize){
        textcomp.fontSize = fontSize;

        textcomp.needReloadAtlas = true;
        textcomp.needUpdateText = true;
    }
}

unsigned int Text::getFontSize() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.fontSize;
}

void Text::setMultiline(bool multiline){
    TextComponent& textcomp = getComponent<TextComponent>();

    textcomp.multiline = multiline;
    textcomp.needUpdateText = true;
}

bool Text::getMultiline() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.multiline;
}

void Text::setColor(Vector4 color){
    UIComponent& ui = getComponent<UIComponent>();

    ui.color = Color::sRGBToLinear(color);
}

void Text::setColor(const float red, const float green, const float blue, const float alpha){
    setColor(Vector4(red, green, blue, alpha));
}

void Text::setColor(const float red, const float green, const float blue){
    setColor(Vector4(red, green, blue, getColor().w));
}

void Text::setAlpha(const float alpha){
    Vector4 color = getColor();
    setColor(Vector4(color.x, color.y, color.z, alpha));
}

Vector4 Text::getColor() const{
    UIComponent& ui = getComponent<UIComponent>();

    return Color::linearTosRGB(ui.color);
}

float Text::getAlpha() const{
    return getColor().w;
}

float Text::getAscent() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (!textcomp.stbtext)
        return 0;
    else
        return textcomp.stbtext->getAscent();
}

float Text::getDescent() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (!textcomp.stbtext)
        return 0;
    else
        return textcomp.stbtext->getDescent();
}

float Text::getLineGap() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (!textcomp.stbtext)
        return 0;
    else
        return textcomp.stbtext->getLineGap();
}

int Text::getLineHeight() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (!textcomp.stbtext)
        return 0;
    else
        return textcomp.stbtext->getLineHeight();
}

unsigned int Text::getNumChars() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.charPositions.size();
}

Vector2 Text::getCharPosition(unsigned int index) const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (index >= 0 && index < textcomp.charPositions.size()){
        return textcomp.charPositions[index];
    }

    throw std::out_of_range("charPositions out of range");
}

float Text::getCharWidth(uint32_t codepoint) const{
    TextComponent& textcomp = getComponent<TextComponent>();

    if (!textcomp.stbtext)
        return 0;
    else
        return textcomp.stbtext->getCharWidth(codepoint);
}

void Text::setFlipY(bool flipY){
    UIComponent& ui = getComponent<UIComponent>();
    TextComponent& textcomp = getComponent<TextComponent>();

    ui.automaticFlipY = false;
    if (ui.flipY != flipY){
        ui.flipY = flipY;

        textcomp.needUpdateText = true;
    }
}

bool Text::isFlipY() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.flipY;
}

void Text::setPivotBaseline(bool pivotBaseline){
    TextComponent& textcomp = getComponent<TextComponent>();

    textcomp.pivotBaseline = pivotBaseline;
    textcomp.needUpdateText = true;
}

bool Text::isPivotBaseline() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.pivotBaseline;
}

void Text::setPivotCentered(bool pivotCentered){
    TextComponent& textcomp = getComponent<TextComponent>();

    textcomp.pivotCentered = pivotCentered;
    textcomp.needUpdateText = true;
}

bool Text::isPivotCentered() const{
    TextComponent& textcomp = getComponent<TextComponent>();

    return textcomp.pivotCentered;
}

AABB Text::getAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.aabb;
}

AABB Text::getWorldAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.worldAABB;
}

void Text::setShader(const std::string& path){
    UIComponent& ui = getComponent<UIComponent>();

    if (ui.shader != path){
        ui.shader = path;

        ui.needReload = true;
    }
}

std::string Text::getShader() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.shader;
}

void Text::setShaderUniform(const std::string& name, const Vector4& value){
    UIComponent& ui = getComponent<UIComponent>();

    ShaderUniforms::set(ui.shaderUniforms, name, value);
    ui.needUpdateShaderUniforms = true;
}

void Text::setShaderUniform(const std::string& name, const Vector3& value){
    setShaderUniform(name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Text::setShaderUniform(const std::string& name, const Vector2& value){
    setShaderUniform(name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Text::setShaderUniform(const std::string& name, float value){
    setShaderUniform(name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Text::getShaderUniform(const std::string& name) const{
    UIComponent& ui = getComponent<UIComponent>();

    return ShaderUniforms::get(ui.shaderUniforms, name);
}

bool Text::removeShaderUniform(const std::string& name){
    UIComponent& ui = getComponent<UIComponent>();

    if (!ShaderUniforms::remove(ui.shaderUniforms, name))
        return false;

    ui.needUpdateShaderUniforms = true;
    return true;
}

void Text::setShaderTexture(const std::string& name, const Texture& texture){
    UIComponent& ui = getComponent<UIComponent>();

    ShaderTextures::set(ui.shaderTextures, name, texture);
    ui.needReload = true;
}

Texture Text::getShaderTexture(const std::string& name) const{
    UIComponent& ui = getComponent<UIComponent>();

    return ShaderTextures::get(ui.shaderTextures, name);
}