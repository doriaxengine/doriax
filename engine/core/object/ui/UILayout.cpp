//
// (c) 2026 Eduardo Doria.
//

#include "UILayout.h"

#include "util/Color.h"

using namespace doriax;

UILayout::UILayout(Scene* scene): Object(scene){
    addComponent<UILayoutComponent>();
}

UILayout::UILayout(Scene* scene, Entity entity): Object(scene, entity){
}

UILayout::~UILayout(){
}

void UILayout::setSize(unsigned int width, unsigned int height){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.width = width;
    layout.height = height;

    layout.needUpdateSizes = true;
}

void UILayout::setWidth(unsigned int width){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.width = width;

    layout.needUpdateSizes = true;
}

void UILayout::setHeight(unsigned int height){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.height = height;

    layout.needUpdateSizes = true;
}

unsigned int UILayout::getWidth() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.width;
}

unsigned int UILayout::getHeight() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.height;
}

void UILayout::setAnchorPoints(float left, float top, float right, float bottom){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPointLeft = left;
    layout.anchorPointTop = top;
    layout.anchorPointRight = right;
    layout.anchorPointBottom = bottom;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorPointLeft(float left){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPointLeft = left;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorPointTop(float top){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPointTop = top;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorPointRight(float right){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPointRight = right;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorPointBottom(float bottom){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPointBottom = bottom;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

float UILayout::getAnchorPointLeft() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorPointLeft;
}

float UILayout::getAnchorPointTop() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorPointTop;
}

float UILayout::getAnchorPointRight() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorPointRight;
}

float UILayout::getAnchorPointBottom() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorPointBottom;
}

void UILayout::setAnchorOffsets(int left, int top, int right, int bottom){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorOffsetLeft = left;
    layout.anchorOffsetTop = top;
    layout.anchorOffsetRight = right;
    layout.anchorOffsetBottom = bottom;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorOffsetLeft(int left){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorOffsetLeft = left;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorOffsetTop(int top){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorOffsetTop = top;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorOffsetRight(int right){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorOffsetRight = right;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

void UILayout::setAnchorOffsetBottom(int bottom){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorOffsetBottom = bottom;

    layout.anchorPreset = AnchorPreset::NONE;
    layout.usingAnchors = true;
}

int UILayout::getAnchorOffsetLeft() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorOffsetLeft;
}

int UILayout::getAnchorOffsetTop() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorOffsetTop;
}

int UILayout::getAnchorOffsetRight() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorOffsetRight;
}

int UILayout::getAnchorOffsetBottom() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorOffsetBottom;
}

void UILayout::setPositionXOffset(float xOffset){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.positionOffset = Vector2(xOffset, layout.positionOffset.y);
}

float UILayout::getPositionXOffset() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.positionOffset.x;
}

void UILayout::setPositionYOffset(float yOffset){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.positionOffset = Vector2(layout.positionOffset.x, yOffset);
}

float UILayout::getPositionYOffset() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.positionOffset.y;
}

void UILayout::setPositionOffset(Vector2 positionOffset){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.positionOffset = positionOffset;
}

Vector2 UILayout::getPositionOffset() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.positionOffset;
}

void UILayout::setAnchorPreset(AnchorPreset anchorPreset){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.anchorPreset = anchorPreset;

    layout.usingAnchors = true;
}

AnchorPreset UILayout::getAnchorPreset() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.anchorPreset;
}

void UILayout::setUsingAnchors(bool usingAnchors){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.usingAnchors = usingAnchors;
}

bool UILayout::isUsingAnchors() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.usingAnchors;
}

void UILayout::setIgnoreScissor(bool ignoreScissor){
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    layout.ignoreScissor = ignoreScissor;
}

bool UILayout::isIgnoreScissor() const{
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return layout.ignoreScissor;
}