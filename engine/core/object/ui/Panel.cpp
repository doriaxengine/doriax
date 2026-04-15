//
// (c) 2026 Eduardo Doria.
//

#include "Panel.h"

#include "component/TextComponent.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Panel::Panel(Scene* scene): Image(scene){
    addComponent<PanelComponent>();
}

Panel::Panel(Scene* scene, Entity entity): Image(scene, entity){
}

Panel::~Panel(){
}

Image Panel::getHeaderImageObject() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return Image(scene, panelcomp.headerimage);
}

Container Panel::getHeaderContainerObject() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return Container(scene, panelcomp.headercontainer);
}

Text Panel::getHeaderTextObject() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return Text(scene, panelcomp.headertext);
}

void Panel::setTitle(const std::string& text){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(panelcomp.headertext);

    if (textcomp.text != text){
        textcomp.text = text;
        textcomp.needUpdateText = true;
    }
}

std::string Panel::getTitle() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(panelcomp.headertext);

    return textcomp.text;
}

void Panel::setTitleAnchorPreset(AnchorPreset titleAnchorPreset){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UILayoutComponent& titlelayout = scene->getComponent<UILayoutComponent>(panelcomp.headertext);

    titlelayout.anchorPreset = titleAnchorPreset;
    panelcomp.needUpdatePanel = true;
}

AnchorPreset Panel::getTitleAnchorPreset() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UILayoutComponent& titlelayout = scene->getComponent<UILayoutComponent>(panelcomp.headertext);

    return titlelayout.anchorPreset;
}

void Panel::setTitleColor(Vector4 color){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UIComponent& uititle = scene->getComponent<UIComponent>(panelcomp.headertext);

    uititle.color = Color::sRGBToLinear(color);
}

void Panel::setTitleColor(const float red, const float green, const float blue, const float alpha){
    setTitleColor(Vector4(red, green, blue, alpha));
}

Vector4 Panel::getTitleColor() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UIComponent& uititle = scene->getComponent<UIComponent>(panelcomp.headertext);

    return uititle.color;
}

void Panel::setTitleFont(const std::string& font){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    getHeaderTextObject().setFont(font);

    panelcomp.needUpdatePanel = true;
}

std::string Panel::getTitleFont() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    TextComponent& textcomp = scene->getComponent<TextComponent>(panelcomp.headertext);

    return textcomp.font;
}

void Panel::setTitleFontSize(unsigned int fontSize){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    getHeaderTextObject().setFontSize(fontSize);

    panelcomp.needUpdatePanel = true;
}

unsigned int Panel::getTitleFontSize() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return getHeaderTextObject().getFontSize();
}

void Panel::setHeaderColor(Vector4 color){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UIComponent& uiheaderimage = scene->getComponent<UIComponent>(panelcomp.headerimage);

    uiheaderimage.color = Color::sRGBToLinear(color);
}

void Panel::setHeaderColor(const float red, const float green, const float blue, const float alpha){
    setHeaderColor(Vector4(red, green, blue, alpha));
}

Vector4 Panel::getHeaderColor() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    UIComponent& uiheaderimage = scene->getComponent<UIComponent>(panelcomp.headerimage);

    return uiheaderimage.color;
}

void Panel::setHeaderPatchMargin(int margin){
    getHeaderImageObject().setPatchMargin(margin);
}

void Panel::setHeaderTexture(const std::string& path){
    getHeaderImageObject().setTexture(path);
}

void Panel::setHeaderMargin(int left, int top, int right, int bottom){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.headerMarginLeft = left;
    panelcomp.headerMarginTop = top;
    panelcomp.headerMarginRight = right;
    panelcomp.headerMarginBottom = bottom;

    panelcomp.defaultHeaderMargin = false;
    panelcomp.needUpdatePanel = true;
}

void Panel::setHeaderMarginLeft(int left){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.headerMarginLeft = left;

    panelcomp.defaultHeaderMargin = false;
    panelcomp.needUpdatePanel = true;
}

int Panel::getHeaderMarginLeft() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.headerMarginLeft;
}

void Panel::setHeaderMarginTop(int top){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.headerMarginTop = top;

    panelcomp.defaultHeaderMargin = false;
    panelcomp.needUpdatePanel = true;
}

int Panel::getHeaderMarginTop() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.headerMarginTop;
}

void Panel::setHeaderMarginRight(int right){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.headerMarginRight = right;

    panelcomp.defaultHeaderMargin = false;
    panelcomp.needUpdatePanel = true;
}

int Panel::getHeaderMarginRight() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.headerMarginRight;
}

void Panel::setHeaderMarginBottom(int bottom){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.headerMarginBottom = bottom;

    panelcomp.defaultHeaderMargin = false;
    panelcomp.needUpdatePanel = true;
}

int Panel::getHeaderMarginBottom() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.headerMarginBottom;
}

void Panel::setMinSize(int minWidth, int minHeight){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.minWidth = minWidth;
    panelcomp.minHeight = minHeight;

    panelcomp.needUpdatePanel = true;
}

void Panel::setMinWidth(int minWidth){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.minWidth = minWidth;

    panelcomp.needUpdatePanel = true;
}

void Panel::setMinHeight(int minHeight){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.minHeight = minHeight;

    panelcomp.needUpdatePanel = true;
}

int Panel::getMinWidth() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.minWidth;
}

int Panel::getMinHeight() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.minHeight;
}

void Panel::setResizeMargin(int resizeMargin){
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    panelcomp.resizeMargin = resizeMargin;

    panelcomp.needUpdatePanel = true;
}

int Panel::getResizeMargin() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();

    return panelcomp.resizeMargin;
}

void Panel::setWindowProperties(const bool canMove, const bool canResize, const bool canBringToFront){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    panelcomp.canMove = canMove;
    panelcomp.canResize = canResize;
    panelcomp.canBringToFront = canBringToFront;
}

void Panel::setCanMove(const bool canMove){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    panelcomp.canMove = canMove;
}

bool Panel::isCanMove() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    return panelcomp.canMove;
}

void Panel::setCanResize(const bool canResize){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    panelcomp.canResize = canResize;
}

bool Panel::isCanResize() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    return panelcomp.canResize;
}

void Panel::setCanBringToFront(const bool canBringToFront){
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    panelcomp.canBringToFront = canBringToFront;
}

bool Panel::isCanBringToFront() const{
    PanelComponent& panelcomp = getComponent<PanelComponent>();
    return panelcomp.canBringToFront;
}