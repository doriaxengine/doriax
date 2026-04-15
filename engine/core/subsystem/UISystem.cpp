//
// (c) 2026 Eduardo Doria.
//

#include "UISystem.h"

#include <algorithm>
#include "Scene.h"
#include "Input.h"
#include "Engine.h"
#include "System.h"
#include "util/STBText.h"
#include "util/StringUtils.h"
#include "pool/FontPool.h"

using namespace doriax;


UISystem::UISystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<UILayoutComponent>());

    eventId.clear();
    lastUIFromPointer = NULL_ENTITY;
    lastPanelFromPointer = NULL_ENTITY;
    lastPointerPos = Vector2(-1, -1);

    anchorReferenceWidth = 0;
    anchorReferenceHeight = 0;
}

UISystem::~UISystem(){
}

Rect UISystem::getAnchorReferenceRect(const UILayoutComponent& layout, const Transform& transform, bool worldPosition){
    if (transform.parent == NULL_ENTITY){
        int finalCanvasWidth = Engine::getCanvasWidth();
        if (anchorReferenceWidth != 0)
            finalCanvasWidth = anchorReferenceWidth;

        int finalCanvasHeight = Engine::getCanvasHeight();
        if (anchorReferenceHeight != 0)
            finalCanvasHeight = anchorReferenceHeight;

        return Rect(0, 0, finalCanvasWidth, finalCanvasHeight);
    }

    UILayoutComponent* parentlayout = scene->findComponent<UILayoutComponent>(transform.parent);
    if (!parentlayout){
        return Rect(0, 0, 0, 0);
    }

    Rect boxRect = Rect(0, 0, parentlayout->width, parentlayout->height);

    UIContainerComponent* parentcontainer = scene->findComponent<UIContainerComponent>(transform.parent);
    if (parentcontainer){
        boxRect = parentcontainer->boxes[layout.containerBoxIndex].rect;
    }

    ImageComponent* parentimage = scene->findComponent<ImageComponent>(transform.parent);
    if (parentimage && !layout.ignoreScissor){
        boxRect.setX(boxRect.getX() + parentimage->patchMarginLeft);
        boxRect.setWidth(boxRect.getWidth() - parentimage->patchMarginRight - parentimage->patchMarginLeft);
        boxRect.setY(boxRect.getY() + parentimage->patchMarginTop);
        boxRect.setHeight(boxRect.getHeight() - parentimage->patchMarginBottom - parentimage->patchMarginTop);
    }

    if (worldPosition){
        Transform* parentTransform = scene->findComponent<Transform>(transform.parent);
        if (parentTransform){
            Vector3 worldPos = parentTransform->worldPosition;
            boxRect.setX(boxRect.getX() + worldPos.x);
            boxRect.setY(boxRect.getY() + worldPos.y);
        }
    }

    return boxRect;
}

void UISystem::setAnchorReferenceSize(int width, int height){
    this->anchorReferenceWidth = width;
    this->anchorReferenceHeight = height;
}

void UISystem::clearAnchorReferenceSize(){
    this->anchorReferenceWidth = 0;
    this->anchorReferenceHeight = 0;
}

int UISystem::getAnchorReferenceWidth() const{
    return anchorReferenceWidth;
}

int UISystem::getAnchorReferenceHeight() const{
    return anchorReferenceHeight;
}

bool UISystem::createImagePatches(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    unsigned int texWidth = 0;
    unsigned int texHeight = 0;

    if (!ui.texture.empty()){
        TextureLoadResult texResult = ui.texture.load();
        if (texResult.state == ResourceLoadState::Finished){
            texWidth = ui.texture.getWidth();
            texHeight = ui.texture.getHeight();
        }else if (texResult.state == ResourceLoadState::Loading){
            return false;
        }
    }

    if (texWidth == 0 || texHeight == 0){
        texWidth = layout.width;
        texHeight = layout.height;
    }

    if (layout.width == 0 && layout.height == 0){
        layout.width = texWidth;
        layout.height = texHeight;
    }

    ui.primitiveType = PrimitiveType::TRIANGLES;

	ui.buffer.clear();
	ui.buffer.addAttribute(AttributeType::POSITION, 3);
	ui.buffer.addAttribute(AttributeType::TEXCOORD1, 2);
    ui.buffer.addAttribute(AttributeType::COLOR, 4);
    ui.buffer.setUsage(BufferUsage::DYNAMIC);

    ui.indices.clear();
    ui.indices.setUsage(BufferUsage::DYNAMIC);

    Attribute* atrVertex = ui.buffer.getAttribute(AttributeType::POSITION);

    ui.buffer.addVector3(atrVertex, Vector3(0, 0, 0)); //0
    ui.buffer.addVector3(atrVertex, Vector3(layout.width, 0, 0)); //1
    ui.buffer.addVector3(atrVertex, Vector3(layout.width,  layout.height, 0)); //2
    ui.buffer.addVector3(atrVertex, Vector3(0,  layout.height, 0)); //3

    ui.buffer.addVector3(atrVertex, Vector3(img.patchMarginLeft, img.patchMarginTop, 0)); //4
    ui.buffer.addVector3(atrVertex, Vector3(layout.width-img.patchMarginRight, img.patchMarginTop, 0)); //5
    ui.buffer.addVector3(atrVertex, Vector3(layout.width-img.patchMarginRight,  layout.height-img.patchMarginBottom, 0)); //6
    ui.buffer.addVector3(atrVertex, Vector3(img.patchMarginLeft,  layout.height-img.patchMarginBottom, 0)); //7

    ui.buffer.addVector3(atrVertex, Vector3(img.patchMarginLeft, 0, 0)); //8
    ui.buffer.addVector3(atrVertex, Vector3(0, img.patchMarginTop, 0)); //9

    ui.buffer.addVector3(atrVertex, Vector3(layout.width-img.patchMarginRight, 0, 0)); //10
    ui.buffer.addVector3(atrVertex, Vector3(layout.width, img.patchMarginTop, 0)); //11

    ui.buffer.addVector3(atrVertex, Vector3(layout.width-img.patchMarginRight, layout.height, 0)); //12
    ui.buffer.addVector3(atrVertex, Vector3(layout.width, layout.height-img.patchMarginBottom, 0)); //13

    ui.buffer.addVector3(atrVertex, Vector3(img.patchMarginLeft, layout.height, 0)); //14
    ui.buffer.addVector3(atrVertex, Vector3(0, layout.height-img.patchMarginBottom, 0)); //15

    Attribute* atrTexcoord = ui.buffer.getAttribute(AttributeType::TEXCOORD1);

    float texCutRatioW = 0;
    float texCutRatioH = 0;
    if (texWidth != 0 && texHeight != 0){
        texCutRatioW = 1.0 / texWidth * img.textureScaleFactor;
        texCutRatioH = 1.0 / texHeight * img.textureScaleFactor;
    }

    float x0 = texCutRatioW;
    float x1 = 1.0-texCutRatioW;
    float y0 = texCutRatioH;
    float y1 = 1.0-texCutRatioH;

    ui.buffer.addVector2(atrTexcoord, Vector2(x0, y0));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1, y0));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1, y1));
    ui.buffer.addVector2(atrTexcoord, Vector2(x0, y1));

    ui.buffer.addVector2(atrTexcoord, Vector2(img.patchMarginLeft/(float)texWidth, img.patchMarginTop/(float)texHeight));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1-(img.patchMarginRight/(float)texWidth), img.patchMarginTop/(float)texHeight));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1-(img.patchMarginRight/(float)texWidth), y1-(img.patchMarginBottom/(float)texHeight)));
    ui.buffer.addVector2(atrTexcoord, Vector2(img.patchMarginLeft/(float)texWidth, y1-(img.patchMarginBottom/(float)texHeight)));

    ui.buffer.addVector2(atrTexcoord, Vector2(img.patchMarginLeft/(float)texWidth, y0));
    ui.buffer.addVector2(atrTexcoord, Vector2(x0, img.patchMarginTop/(float)texHeight));

    ui.buffer.addVector2(atrTexcoord, Vector2(x1-(img.patchMarginRight/(float)texWidth), y0));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1, img.patchMarginTop/(float)texHeight));

    ui.buffer.addVector2(atrTexcoord, Vector2(x1-(img.patchMarginRight/(float)texWidth), y1));
    ui.buffer.addVector2(atrTexcoord, Vector2(x1, y1-(img.patchMarginBottom/(float)texHeight)));

    ui.buffer.addVector2(atrTexcoord, Vector2((img.patchMarginLeft/(float)texWidth), y1));
    ui.buffer.addVector2(atrTexcoord, Vector2(x0, y1-(img.patchMarginBottom/(float)texHeight)));

    if (ui.flipY){
        for (int i = 0; i < ui.buffer.getCount(); i++){
            Vector2 uv = ui.buffer.getVector2(atrTexcoord, i);
            uv.y = 1.0 - uv.y;
            ui.buffer.setVector2(i, atrTexcoord, uv);
        }
    }

    Attribute* atrColor = ui.buffer.getAttribute(AttributeType::COLOR);

    for (int i = 0; i < ui.buffer.getCount(); i++){
        ui.buffer.addVector4(atrColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    static const uint16_t indices_array[] = {
        4,  5,  6, // Center
        4,  6,  7,

        0,  8,  4, // Top-left
        0,  4,  9,

        8, 10,  5, // Top
        8,  5,  4,

        10, 1, 11, // Top-right
        10, 11, 5,

        5, 11, 13, // Right
        5, 13,  6,

        6, 13,  2, // Bottom-right
        6,  2, 12,

        7,  6, 12, // Bottom
        7, 12, 14,

        15, 7, 14, // Bottom-left
        15, 14, 3,

        9,  4,  7, // Left
        9,  7, 15
    };

    ui.indices.setValues(
        0, ui.indices.getAttribute(AttributeType::INDEX),
        54, (char*)&indices_array[0], sizeof(uint16_t));

    calculateUIAABB(ui);

    if (ui.loaded)
        ui.needUpdateBuffer = true;

    return true;
}

bool UISystem::loadFontAtlas(TextComponent& text, UIComponent& ui, UILayoutComponent& layout){
    std::string fontId = text.font;
    if (text.font.empty())
        fontId = "font";
    fontId = fontId + std::string("|") + std::to_string(text.fontSize);

    text.stbtext = FontPool::get(fontId);
    if (!text.stbtext){
        text.stbtext = FontPool::get(fontId, text.font, text.fontSize);
        if (!text.stbtext) {
            Log::error("Cannot load font atlas from: %s", text.font.c_str());
            return false;
        }
    }

    ui.texture.setData(fontId, *text.stbtext->getTextureData());

    ui.needUpdateTexture = true;

    text.needReloadAtlas = false;
    text.loaded = true;

    return true;
}

void UISystem::createText(TextComponent& text, UIComponent& ui, UILayoutComponent& layout){

    ui.primitiveType = PrimitiveType::TRIANGLES;

    ui.buffer.clear();
	ui.buffer.addAttribute(AttributeType::POSITION, 3);
	ui.buffer.addAttribute(AttributeType::TEXCOORD1, 2);
    ui.buffer.setUsage(BufferUsage::DYNAMIC);

    ui.indices.clear();
    ui.indices.setUsage(BufferUsage::DYNAMIC);

    std::vector<uint16_t> indices_array;

    if (text.text.length() > text.maxTextSize){
        unsigned int newSize = text.maxTextSize;
        if (newSize == 0) newSize = 1;
        while (newSize < text.text.length()){
            newSize *= 2;
        }

        Log::warn("Text is bigger than maxTextSize: %i. Increasing it to: %i", text.maxTextSize, newSize);
        text.maxTextSize = newSize;
        if (ui.loaded){
            ui.needReload = true;
        }
    }

    ui.minBufferCount = text.maxTextSize * 4;
    ui.minIndicesCount = text.maxTextSize * 6;

    text.stbtext->createText(text.text, &ui.buffer, indices_array, text.charPositions, layout.width, layout.height, text.fixedWidth, text.fixedHeight, text.multiline, ui.flipY);

    if (text.pivotCentered || !text.pivotBaseline){
        Attribute* atrVertice = ui.buffer.getAttribute(AttributeType::POSITION);
        for (int i = 0; i < ui.buffer.getCount(); i++){
            Vector3 vertice = ui.buffer.getVector3(atrVertice, i);

            if (text.pivotCentered){
                vertice.x = vertice.x - (layout.width / 2.0);
            }

            if (!text.pivotBaseline){
                if (!ui.flipY){
                    vertice.y = vertice.y + text.stbtext->getAscent();
                }else{
                    vertice.y = vertice.y + layout.height - text.stbtext->getAscent();
                }
            }

            ui.buffer.setVector3(i, atrVertice, vertice);
        }
    }

    ui.indices.setValues(
            0, ui.indices.getAttribute(AttributeType::INDEX),
            indices_array.size(), (char*)&indices_array[0], sizeof(uint16_t));

    calculateUIAABB(ui);

    if (ui.loaded)
        ui.needUpdateBuffer = true;
}

void UISystem::createButtonObjects(Entity entity, ButtonComponent& button){
    if (button.label == NULL_ENTITY){
        button.label = scene->createEntity();

        scene->addComponent<Transform>(button.label);
        scene->addComponent<UILayoutComponent>(button.label);
        scene->addComponent<UIComponent>(button.label);
        scene->addComponent<TextComponent>(button.label);

        scene->addEntityChild(entity, button.label, false);

        UIComponent& labelui = scene->getComponent<UIComponent>(button.label);
        UILayoutComponent& labellayout = scene->getComponent<UILayoutComponent>(button.label);

        labelui.color = Vector4(0.0, 0.0, 0.0, 1.0);
        labellayout.ignoreEvents = true;
        labellayout.anchorPreset = AnchorPreset::CENTER;
        labellayout.usingAnchors = true;
    }
}

void UISystem::createPanelObjects(Entity entity, PanelComponent& panel){
    if (panel.headerimage == NULL_ENTITY){
        panel.headerimage = scene->createEntity();

        scene->addComponent<Transform>(panel.headerimage);
        scene->addComponent<UILayoutComponent>(panel.headerimage);
        scene->addComponent<UIComponent>(panel.headerimage);
        scene->addComponent<ImageComponent>(panel.headerimage);

        scene->addEntityChild(entity, panel.headerimage, false);

        UIComponent& headerui = scene->getComponent<UIComponent>(panel.headerimage);
        UILayoutComponent& headerimagelayout = scene->getComponent<UILayoutComponent>(panel.headerimage);

        headerui.color = Vector4(0, 0, 0, 0);
        headerimagelayout.ignoreEvents = true;
        headerimagelayout.ignoreScissor = true;
        headerimagelayout.usingAnchors = true;
        // same of TOP_WIDE
        headerimagelayout.anchorPointLeft = 0;
        headerimagelayout.anchorPointTop = 0;
        headerimagelayout.anchorPointRight = 1;
        headerimagelayout.anchorPointBottom = 0;
    }
    if (panel.headercontainer == NULL_ENTITY){
        panel.headercontainer = scene->createEntity();

        scene->addComponent<Transform>(panel.headercontainer);
        scene->addComponent<UILayoutComponent>(panel.headercontainer);
        scene->addComponent<UIContainerComponent>(panel.headercontainer);

        scene->addEntityChild(panel.headerimage, panel.headercontainer, false);

        UIContainerComponent& containerui = scene->getComponent<UIContainerComponent>(panel.headercontainer);
        UILayoutComponent& containerlayout = scene->getComponent<UILayoutComponent>(panel.headercontainer);

        containerlayout.ignoreEvents = true;
        containerlayout.anchorPreset = AnchorPreset::FULL_LAYOUT;
        containerlayout.ignoreScissor = true;
        containerlayout.usingAnchors = true;
        containerui.type = ContainerType::HORIZONTAL;
    }
    if (panel.headertext == NULL_ENTITY){
        panel.headertext = scene->createEntity();

        scene->addComponent<Transform>(panel.headertext);
        scene->addComponent<UILayoutComponent>(panel.headertext);
        scene->addComponent<UIComponent>(panel.headertext);
        scene->addComponent<TextComponent>(panel.headertext);

        scene->addEntityChild(panel.headercontainer, panel.headertext, false);

        UIComponent& titleui = scene->getComponent<UIComponent>(panel.headertext);
        UILayoutComponent& titlelayout = scene->getComponent<UILayoutComponent>(panel.headertext);

        titleui.color = Vector4(0.0, 0.0, 0.0, 1.0);
        titlelayout.ignoreEvents = true;
        titlelayout.anchorPreset = panel.titleAnchorPreset;
        //titlelayout.ignoreScissor = true; // to hide header text in small panels
        titlelayout.usingAnchors = true;
    }
}

void UISystem::createScrollbarObjects(Entity entity, ScrollbarComponent& scrollbar){
    if (scrollbar.bar == NULL_ENTITY){
        scrollbar.bar = scene->createEntity();

        scene->addComponent<Transform>(scrollbar.bar);
        scene->addComponent<UILayoutComponent>(scrollbar.bar);
        scene->addComponent<UIComponent>(scrollbar.bar);
        scene->addComponent<ImageComponent>(scrollbar.bar);

        scene->addEntityChild(entity, scrollbar.bar, false);

        UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

        barlayout.ignoreEvents = true;
    }
}

void UISystem::createTextEditObjects(Entity entity, TextEditComponent& textedit){
    if (textedit.text == NULL_ENTITY){
        textedit.text = scene->createEntity();

        scene->addComponent<Transform>(textedit.text);
        scene->addComponent<UILayoutComponent>(textedit.text);
        scene->addComponent<UIComponent>(textedit.text);
        scene->addComponent<TextComponent>(textedit.text);

        scene->addEntityChild(entity, textedit.text, false);

        UILayoutComponent& textlayout = scene->getComponent<UILayoutComponent>(textedit.text);
        UIComponent& textui = scene->getComponent<UIComponent>(textedit.text);
        textui.color = Vector4(0.0, 0.0, 0.0, 1.0);
        textlayout.ignoreEvents = true;
    }

    if (textedit.cursor == NULL_ENTITY){
        textedit.cursor = scene->createEntity();

        scene->addComponent<Transform>(textedit.cursor);
        scene->addComponent<UILayoutComponent>(textedit.cursor);
        scene->addComponent<UIComponent>(textedit.cursor);
        scene->addComponent<PolygonComponent>(textedit.cursor);

        scene->addEntityChild(entity, textedit.cursor, false);

        UILayoutComponent& cursorlayout = scene->getComponent<UILayoutComponent>(textedit.cursor);

        cursorlayout.ignoreEvents = true;
    }
}

void UISystem::updateButton(Entity entity, ButtonComponent& button, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    createButtonObjects(entity, button);

    if (!ui.loaded){
        if (!button.textureNormal.load()){
            button.textureNormal = ui.texture;
        }
        if (button.colorNormal == Vector4(1.0, 1.0, 1.0, 1.0)){
            button.colorNormal = ui.color;
        }
        button.textureNormal.load();
        button.texturePressed.load();
        button.textureDisabled.load();
    }

    TextComponent& labeltext = scene->getComponent<TextComponent>(button.label);
    UIComponent& labelui = scene->getComponent<UIComponent>(button.label);
    UILayoutComponent& labellayout = scene->getComponent<UILayoutComponent>(button.label);

    labeltext.needUpdateText = true;
    createOrUpdateText(labeltext, labelui, labellayout);

    if (button.disabled){
        if (ui.texture != button.textureDisabled){
            ui.texture = button.textureDisabled;
            ui.needUpdateTexture = true;
        }
        ui.color = button.colorDisabled;
    }else{
        if (!button.pressed){
            if (ui.texture != button.textureNormal){
                ui.texture = button.textureNormal;
                ui.needUpdateTexture = true;
            }
            ui.color = button.colorNormal;
        }else{
            if (ui.texture != button.texturePressed){
                ui.texture = button.texturePressed;
                ui.needUpdateTexture = true;
            }
            ui.color = button.colorPressed;
        }
    }
}

void UISystem::updatePanel(Entity entity, PanelComponent& panel, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    createPanelObjects(entity, panel);

    UILayoutComponent& headerimagelayout = scene->getComponent<UILayoutComponent>(panel.headerimage);

    if (panel.defaultHeaderMargin){
        panel.headerMarginLeft = img.patchMarginLeft;
        panel.headerMarginRight = img.patchMarginRight;
        panel.headerMarginTop = img.patchMarginBottom;
        panel.headerMarginBottom = img.patchMarginBottom;
    }

    headerimagelayout.anchorOffsetLeft = panel.headerMarginLeft;
    headerimagelayout.anchorOffsetTop = panel.headerMarginTop;
    headerimagelayout.anchorOffsetRight = -panel.headerMarginRight;
    headerimagelayout.anchorOffsetBottom = img.patchMarginTop - panel.headerMarginBottom;

    if (panel.minWidth > layout.width){
        panel.minWidth = layout.width;
    }
    if (panel.minHeight > layout.height){
        panel.minHeight = layout.height;
    }
    if (panel.minWidth < (img.patchMarginLeft + img.patchMarginRight + 1)){
        panel.minWidth = static_cast<unsigned int>(img.patchMarginLeft + img.patchMarginRight + 1);
    }
    if (panel.minHeight < (img.patchMarginTop + img.patchMarginBottom + 1)){
        panel.minHeight = static_cast<unsigned int>(img.patchMarginTop + img.patchMarginBottom + 1);
    }

    UILayoutComponent& titlelayout = scene->getComponent<UILayoutComponent>(panel.headertext);
    UIComponent& titleui = scene->getComponent<UIComponent>(panel.headertext);
    TextComponent& headertext = scene->getComponent<TextComponent>(panel.headertext);

    titlelayout.anchorPreset = panel.titleAnchorPreset;
    headertext.needUpdateText = true;
    createOrUpdateText(headertext, titleui, titlelayout);
}

void UISystem::updateScrollbar(Entity entity, ScrollbarComponent& scrollbar, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    createScrollbarObjects(entity, scrollbar);

    UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

    if (scrollbar.barSize > 1){
        scrollbar.barSize = 1;
        if (scrollbar.step != 0){
            scrollbar.step = 0;
            scrollbar.onChange.call(scrollbar.step);
        }
    }else if (scrollbar.barSize < 0){
        scrollbar.barSize = 0;
    }

    if (scrollbar.step > 1){
        scrollbar.step = 1;
        scrollbar.onChange.call(scrollbar.step);
    }else if (scrollbar.step < 0){
        scrollbar.step = 0;
        scrollbar.onChange.call(scrollbar.step);
    }

    float barSizePixel = 0;
    float halfBar = 0;
    if (scrollbar.type == ScrollbarType::VERTICAL){
        barSizePixel = layout.height * scrollbar.barSize;
        halfBar = (barSizePixel / 2.0) / layout.height;
    }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
        barSizePixel = layout.width * scrollbar.barSize;
        halfBar = (barSizePixel / 2.0) / layout.width;
    }

    if (barlayout.height != barSizePixel || barlayout.width != barSizePixel){
        barlayout.height = barSizePixel;
        barlayout.width = barSizePixel;
    }

    float pos = (scrollbar.step * ((1.0 - halfBar) - halfBar)) + halfBar;

    if (scrollbar.type == ScrollbarType::VERTICAL){
        barlayout.anchorPointLeft = 0;
        barlayout.anchorPointTop = pos;
        barlayout.anchorPointRight = 1;
        barlayout.anchorPointBottom = pos;
        barlayout.anchorOffsetLeft = 0;
        barlayout.anchorOffsetTop = -floor(barlayout.height / 2.0);
        barlayout.anchorOffsetRight = 0;
        barlayout.anchorOffsetBottom = ceil(barlayout.height / 2.0);
    }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
        barlayout.anchorPointLeft = pos;
        barlayout.anchorPointTop = 0;
        barlayout.anchorPointRight = pos;
        barlayout.anchorPointBottom = 1;
        barlayout.anchorOffsetLeft = -floor(barlayout.width / 2.0);
        barlayout.anchorOffsetTop = 0;
        barlayout.anchorOffsetRight = ceil(barlayout.width / 2.0);
        barlayout.anchorOffsetBottom = 0;
    }
    barlayout.anchorPreset = AnchorPreset::NONE;
    barlayout.usingAnchors = true;
}

void UISystem::updateTextEdit(Entity entity, TextEditComponent& textedit, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    createTextEditObjects(entity, textedit);

    // Text
    Transform& texttransform = scene->getComponent<Transform>(textedit.text);
    UILayoutComponent& textlayout = scene->getComponent<UILayoutComponent>(textedit.text);
    UIComponent& textui = scene->getComponent<UIComponent>(textedit.text);
    TextComponent& text = scene->getComponent<TextComponent>(textedit.text);

    text.needUpdateText = true;
    createOrUpdateText(text, textui, textlayout);

    if (layout.height == 0){
        layout.height = textlayout.height + img.patchMarginTop + img.patchMarginBottom;
        layout.needUpdateSizes = true;
    }

    int heightArea = layout.height - img.patchMarginTop - img.patchMarginBottom;
    int widthArea = layout.width - img.patchMarginRight - img.patchMarginLeft - textedit.cursorWidth;

    text.multiline = false;

    int textXOffset = 0;
    if (textlayout.width > widthArea){
        textXOffset = textlayout.width - widthArea;
    }

    float textX = static_cast<float>(img.patchMarginLeft) - textXOffset;
    // descend is negative
    float textY = static_cast<float>(img.patchMarginTop) + (heightArea / 2) - (textlayout.height / 2);

    Vector3 textPosition = Vector3(textX, textY, 0);

    if (texttransform.position != textPosition){
        texttransform.position = textPosition;
        texttransform.needUpdate = true;
    }

    // Cursor
    Transform& cursortransform = scene->getComponent<Transform>(textedit.cursor);
    UILayoutComponent& cursorlayout = scene->getComponent<UILayoutComponent>(textedit.cursor);
    UIComponent& cursorui = scene->getComponent<UIComponent>(textedit.cursor);
    PolygonComponent& cursor = scene->getComponent<PolygonComponent>(textedit.cursor);

    createOrUpdatePolygon(cursor, cursorui, cursorlayout);

    float cursorHeight = textlayout.height;

    cursor.points.clear();
    cursor.points.push_back({Vector3(0,  0, 0),                                 Vector4(1.0, 1.0, 1.0, 1.0)});
    cursor.points.push_back({Vector3(textedit.cursorWidth, 0, 0),               Vector4(1.0, 1.0, 1.0, 1.0)});
    cursor.points.push_back({Vector3(0,  cursorHeight, 0),                      Vector4(1.0, 1.0, 1.0, 1.0)});
    cursor.points.push_back({Vector3(textedit.cursorWidth, cursorHeight, 0),    Vector4(1.0, 1.0, 1.0, 1.0)});

    float cursorX = textX + textlayout.width;
    float cursorY = img.patchMarginTop + ((float)heightArea / 2) - ((float)cursorHeight / 2);

    cursorui.color = textedit.cursorColor;
    cursortransform.position = Vector3(cursorX, cursorY, 0.0);
    cursortransform.needUpdate = true;

    cursor.needUpdatePolygon = true;
}

void UISystem::blinkCursorTextEdit(double dt, TextEditComponent& textedit, UIComponent& ui){
    textedit.cursorBlinkTimer += dt;

    Transform& cursortransform = scene->getComponent<Transform>(textedit.cursor);

    if (ui.focused){
        if (textedit.cursorBlinkTimer > textedit.cursorBlink) {
            cursortransform.visible = !cursortransform.visible;
            textedit.cursorBlinkTimer = 0;
        }
    }else{
        cursortransform.visible = false;
    }
}

void UISystem::createUIPolygon(PolygonComponent& polygon, UIComponent& ui, UILayoutComponent& layout){

    ui.primitiveType = PrimitiveType::TRIANGLE_STRIP;

    ui.buffer.clear();
	ui.buffer.addAttribute(AttributeType::POSITION, 3);
	ui.buffer.addAttribute(AttributeType::TEXCOORD1, 2);
    ui.buffer.addAttribute(AttributeType::COLOR, 4);
    ui.buffer.setUsage(BufferUsage::DYNAMIC);

    ui.indices.clear();

    for (int i = 0; i < polygon.points.size(); i++){
        ui.buffer.addVector3(AttributeType::POSITION, polygon.points[i].position);
        ui.buffer.addVector3(AttributeType::NORMAL, Vector3(0.0f, 0.0f, 1.0f));
        ui.buffer.addVector4(AttributeType::COLOR, polygon.points[i].color);
    }

    // Generation texcoords
    float min_X = std::numeric_limits<float>::max();
    float max_X = std::numeric_limits<float>::min();
    float min_Y = std::numeric_limits<float>::max();
    float max_Y = std::numeric_limits<float>::min();

    Attribute* attVertex = ui.buffer.getAttribute(AttributeType::POSITION);

    for (unsigned int i = 0; i < ui.buffer.getCount(); i++){
        min_X = fmin(min_X, ui.buffer.getFloat(attVertex, i, 0));
        min_Y = fmin(min_Y, ui.buffer.getFloat(attVertex, i, 1));
        max_X = fmax(max_X, ui.buffer.getFloat(attVertex, i, 0));
        max_Y = fmax(max_Y, ui.buffer.getFloat(attVertex, i, 1));
    }

    double k_X = 1/(max_X - min_X);
    double k_Y = 1/(max_Y - min_Y);

    float u = 0;
    float v = 0;

    for ( unsigned int i = 0; i < ui.buffer.getCount(); i++){
        u = (ui.buffer.getFloat(attVertex, i, 0) - min_X) * k_X;
        v = (ui.buffer.getFloat(attVertex, i, 1) - min_Y) * k_Y;

        if (ui.flipY)
            v = 1.0 - v;
        ui.buffer.addVector2(AttributeType::TEXCOORD1, Vector2(u, v));
    }

    layout.width = static_cast<unsigned int>(max_X - min_X);
    layout.height = static_cast<unsigned int>(max_Y - min_Y);

    calculateUIAABB(ui);

    if (ui.loaded)
        ui.needUpdateBuffer = true;
}

bool UISystem::createOrUpdatePolygon(PolygonComponent& polygon, UIComponent& ui, UILayoutComponent& layout){
    if (polygon.needUpdatePolygon){
        if (ui.automaticFlipY){
            CameraComponent& camera = scene->getComponent<CameraComponent>(scene->getCamera());
            changeFlipY(ui, camera);
        }

        createUIPolygon(polygon, ui, layout);

        polygon.needUpdatePolygon = false;
    }

    return true;
}

bool UISystem::createOrUpdateImage(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    if (img.needUpdatePatches){
        if (ui.automaticFlipY){
            CameraComponent& camera = scene->getComponent<CameraComponent>(scene->getCamera());
            changeFlipY(ui, camera);
        }

        if (createImagePatches(img, ui, layout)){
            img.needUpdatePatches = false;
        }else{
            return false;
        }
    }

    return true;
}

Vector2 UISystem::getTextMinSize(TextComponent& text){
    int minHeight = 0;
    float minWidth = 0;

    if (text.stbtext){
        minHeight = text.stbtext->getLineHeight();
        minWidth = text.stbtext->getCharWidth('W');
    }

    return Vector2(minWidth, minHeight);
}

bool UISystem::createOrUpdateText(TextComponent& text, UIComponent& ui, UILayoutComponent& layout){
    if (text.loaded && text.needReloadAtlas){
        destroyText(text);
    }

    if (text.needUpdateText){
        if (ui.automaticFlipY){
            CameraComponent& camera = scene->getComponent<CameraComponent>(scene->getCamera());
            changeFlipY(ui, camera);
        }

        if (!text.loaded){
            loadFontAtlas(text, ui, layout);
        }
        createText(text, ui, layout);

        text.needUpdateText = false;
    }

    return true;
}

void UISystem::applyAnchorPreset(UILayoutComponent& layout){
    if (layout.anchorPreset == AnchorPreset::TOP_LEFT){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 0;
        layout.anchorPointBottom = 0;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = layout.width;
        layout.anchorOffsetBottom = layout.height;
    }else if (layout.anchorPreset == AnchorPreset::TOP_RIGHT){
        layout.anchorPointLeft = 1;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 0;
        layout.anchorOffsetLeft = -layout.width;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = layout.height;
    }else if (layout.anchorPreset == AnchorPreset::BOTTOM_RIGHT){
        layout.anchorPointLeft = 1;
        layout.anchorPointTop = 1;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = -layout.width;
        layout.anchorOffsetTop = -layout.height;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::BOTTOM_LEFT){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 1;
        layout.anchorPointRight = 0;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = -layout.height;
        layout.anchorOffsetRight = layout.width;
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::CENTER_LEFT){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0.5;
        layout.anchorPointRight = 0;
        layout.anchorPointBottom = 0.5;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = -floor(layout.height / 2.0);
        layout.anchorOffsetRight = layout.width;
        layout.anchorOffsetBottom = ceil(layout.height / 2.0);
    }else if (layout.anchorPreset == AnchorPreset::CENTER_TOP){
        layout.anchorPointLeft = 0.5;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 0.5;
        layout.anchorPointBottom = 0;
        layout.anchorOffsetLeft = -floor(layout.width / 2.0);
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = ceil(layout.width / 2.0);
        layout.anchorOffsetBottom = layout.height;
    }else if (layout.anchorPreset == AnchorPreset::CENTER_RIGHT){
        layout.anchorPointLeft = 1;
        layout.anchorPointTop = 0.5;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 0.5;
        layout.anchorOffsetLeft = -layout.width;
        layout.anchorOffsetTop = -floor(layout.height / 2.0);
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = ceil(layout.height / 2.0);
    }else if (layout.anchorPreset == AnchorPreset::CENTER_BOTTOM){
        layout.anchorPointLeft = 0.5;
        layout.anchorPointTop = 1;
        layout.anchorPointRight = 0.5;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = -floor(layout.width / 2.0);
        layout.anchorOffsetTop = -layout.height;
        layout.anchorOffsetRight = ceil(layout.width / 2.0);
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::CENTER){
        layout.anchorPointLeft = 0.5;
        layout.anchorPointTop = 0.5;
        layout.anchorPointRight = 0.5;
        layout.anchorPointBottom = 0.5;
        layout.anchorOffsetLeft = -floor(layout.width / 2.0);
        layout.anchorOffsetTop = -floor(layout.height / 2.0);
        layout.anchorOffsetRight = ceil(layout.width / 2.0);
        layout.anchorOffsetBottom = ceil(layout.height / 2.0);
    }else if (layout.anchorPreset == AnchorPreset::LEFT_WIDE){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 0;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = layout.width;
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::TOP_WIDE){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 0;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = layout.height;
    }else if (layout.anchorPreset == AnchorPreset::RIGHT_WIDE){
        layout.anchorPointLeft = 1;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = -layout.width;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::BOTTOM_WIDE){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 1;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = -layout.height;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::VERTICAL_CENTER_WIDE){
        layout.anchorPointLeft = 0.5;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 0.5;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = -floor(layout.width / 2.0);
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = ceil(layout.width / 2.0);
        layout.anchorOffsetBottom = 0;
    }else if (layout.anchorPreset == AnchorPreset::HORIZONTAL_CENTER_WIDE){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0.5;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 0.5;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = -floor(layout.height / 2.0);
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = ceil(layout.height / 2.0);
    }else if (layout.anchorPreset == AnchorPreset::FULL_LAYOUT){
        layout.anchorPointLeft = 0;
        layout.anchorPointTop = 0;
        layout.anchorPointRight = 1;
        layout.anchorPointBottom = 1;
        layout.anchorOffsetLeft = 0;
        layout.anchorOffsetTop = 0;
        layout.anchorOffsetRight = 0;
        layout.anchorOffsetBottom = 0;
    }
}

void UISystem::changeFlipY(UIComponent& ui, CameraComponent& camera){
    ui.flipY = false;
    if (camera.type != CameraType::CAMERA_UI){
        ui.flipY = !ui.flipY;
    }

    if (ui.texture.isFramebuffer() && Engine::isOpenGL()){
        ui.flipY = !ui.flipY;
    }
}

void UISystem::destroyText(TextComponent& text){
    text.loaded = false;
    text.needReloadAtlas = false;

    text.needUpdateText = true;

    if (text.stbtext){
        text.stbtext.reset();
    }
}

void UISystem::destroyButton(ButtonComponent& button){
    button.textureNormal.destroy();
    button.texturePressed.destroy();
    button.textureDisabled.destroy();

    button.needUpdateButton = true;

    if (button.label != NULL_ENTITY){
        scene->destroyEntity(button.label);
        button.label = NULL_ENTITY;
    }
}

void UISystem::destroyPanel(PanelComponent& panel){
    panel.needUpdatePanel = true;

    if (panel.headertext != NULL_ENTITY){
        scene->destroyEntity(panel.headertext);
        panel.headertext = NULL_ENTITY;
    }
    if (panel.headercontainer != NULL_ENTITY){
        scene->destroyEntity(panel.headercontainer);
        panel.headercontainer = NULL_ENTITY;
    }
    if (panel.headerimage != NULL_ENTITY){
        scene->destroyEntity(panel.headerimage);
        panel.headerimage = NULL_ENTITY;
    }
}

void UISystem::destroyScrollbar(ScrollbarComponent& scrollbar){
    scrollbar.needUpdateScrollbar = true;

    if (scrollbar.bar != NULL_ENTITY){
        scene->destroyEntity(scrollbar.bar);
        scrollbar.bar = NULL_ENTITY;
    }
}

void UISystem::destroyTextEdit(TextEditComponent& textedit){
    textedit.needUpdateTextEdit = true;

    if (textedit.text != NULL_ENTITY){
        scene->destroyEntity(textedit.text);
        textedit.text = NULL_ENTITY;
    }
    if (textedit.cursor != NULL_ENTITY){
        scene->destroyEntity(textedit.cursor);
        textedit.cursor = NULL_ENTITY;
    }
}

void UISystem::load(){
    update(0);
}

void UISystem::destroy(){
    auto layouts = scene->getComponentArray<UILayoutComponent>();
    for (int i = 0; i < layouts->size(); i++) {
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<TextComponent>())) {
            TextComponent &text = scene->getComponent<TextComponent>(entity);

            destroyText(text);
        }
        if (signature.test(scene->getComponentId<ButtonComponent>())) {
            ButtonComponent &button = scene->getComponent<ButtonComponent>(entity);

            destroyButton(button);
        }
        if (signature.test(scene->getComponentId<PanelComponent>())){
            PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

            destroyPanel(panel);
        }
        if (signature.test(scene->getComponentId<ScrollbarComponent>())){
            ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);

            destroyScrollbar(scrollbar);
        }
        if (signature.test(scene->getComponentId<TextEditComponent>())) {
            TextEditComponent &textedit = scene->getComponent<TextEditComponent>(entity);

            destroyTextEdit(textedit);
        }
    }
}

void UISystem::draw(){
}

void UISystem::createOrUpdateUiComponent(double dt, UILayoutComponent& layout, Entity entity, Signature signature){
    if (signature.test(scene->getComponentId<UIComponent>())){
        UIComponent& ui = scene->getComponent<UIComponent>(entity);

        // Texts
        if (signature.test(scene->getComponentId<TextComponent>())){
            TextComponent& text = scene->getComponent<TextComponent>(entity);

            createOrUpdateText(text, ui, layout);
        }

        // UI Polygons
        if (signature.test(scene->getComponentId<PolygonComponent>())){
            PolygonComponent& polygon = scene->getComponent<PolygonComponent>(entity);

            createOrUpdatePolygon(polygon, ui, layout);
        }

        // Images
        if (signature.test(scene->getComponentId<ImageComponent>())){
            ImageComponent& img = scene->getComponent<ImageComponent>(entity);

            createOrUpdateImage(img, ui, layout);

            // Buttons
            if (signature.test(scene->getComponentId<ButtonComponent>())){
                ButtonComponent& button = scene->getComponent<ButtonComponent>(entity);

                if (button.needUpdateButton){
                    updateButton(entity, button, img, ui, layout);

                    button.needUpdateButton = false;
                }
            }

            // Panels
            if (signature.test(scene->getComponentId<PanelComponent>())){
                PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

                if (panel.needUpdatePanel){
                    updatePanel(entity, panel, img, ui, layout);

                    panel.needUpdatePanel = false;
                }
            }

            // Scrollbar
            if (signature.test(scene->getComponentId<ScrollbarComponent>())){
                ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);

                if (scrollbar.needUpdateScrollbar){
                    updateScrollbar(entity, scrollbar, img, ui, layout);

                    scrollbar.needUpdateScrollbar = false;
                }
            }

            // Textedits
            if (signature.test(scene->getComponentId<TextEditComponent>())){
                TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);

                if (textedit.needUpdateTextEdit){
                    updateTextEdit(entity, textedit, img, ui, layout);

                    textedit.needUpdateTextEdit = false;
                }

                blinkCursorTextEdit(dt, textedit, ui);
            }
        }
    }
}

void UISystem::getPanelEdges(const PanelComponent& panel, const UILayoutComponent& layout, const Transform& transform, const UILayoutComponent& headerlayout,  Rect& edgeRight, Rect& edgeRightBottom, Rect& edgeBottom, Rect& edgeLeftBottom, Rect& edgeLeft){
    int minX;
    int minY;
    int width;
    int height;

    Vector2 scaledSize = Vector2(layout.width * transform.worldScale.x, layout.height * transform.worldScale.y);
    Vector2 scaledResizeSize = Vector2(panel.resizeMargin * transform.worldScale.x, panel.resizeMargin * transform.worldScale.y);
    float scaledHeaderHeight = headerlayout.height * transform.worldScale.y;

    // right
    minX = transform.worldPosition.x + scaledSize.x - scaledResizeSize.x;
    minY = transform.worldPosition.y + scaledHeaderHeight;
    width = scaledResizeSize.x;
    height = scaledSize.y - scaledResizeSize.y - scaledHeaderHeight - 1;
    edgeRight = Rect(minX, minY, width, height);

    // right-bottom
    minX = transform.worldPosition.x + scaledSize.x - scaledResizeSize.x;
    minY = transform.worldPosition.y + scaledSize.y - scaledResizeSize.y;
    width = scaledResizeSize.x;
    height = scaledResizeSize.y;
    edgeRightBottom = Rect(minX, minY, width, height);

    // bottom
    minX = transform.worldPosition.x + scaledResizeSize.x + 1;
    minY = transform.worldPosition.y + scaledSize.y - scaledResizeSize.y;
    width = scaledSize.x - 2;
    height = scaledResizeSize.y;
    edgeBottom = Rect(minX, minY, width, height);

    // left-bottom
    minX = transform.worldPosition.x;
    minY = transform.worldPosition.y + scaledSize.y - scaledResizeSize.y;
    width = scaledResizeSize.x;
    height = scaledResizeSize.y;
    edgeLeftBottom = Rect(minX, minY, width, height);

    // left
    minX = transform.worldPosition.x;
    minY = transform.worldPosition.y + scaledHeaderHeight;
    width = scaledResizeSize.x;
    height = scaledSize.y - scaledResizeSize.y - scaledHeaderHeight - 1;
    edgeLeft = Rect(minX, minY, width, height);
}

Rect UISystem::fitOnPanel(Rect uiRect, Entity parentPanel){
    Transform& paneltransform =  scene->getComponent<Transform>(parentPanel);
    UILayoutComponent& panellayout =  scene->getComponent<UILayoutComponent>(parentPanel);
    ImageComponent& panelimage =  scene->getComponent<ImageComponent>(parentPanel);

    float x = paneltransform.worldPosition.x + (panelimage.patchMarginLeft * paneltransform.worldScale.x);
    float y = paneltransform.worldPosition.y + (panelimage.patchMarginTop * paneltransform.worldScale.y);
    float width = (panellayout.width - (panelimage.patchMarginLeft + panelimage.patchMarginRight)) * paneltransform.worldScale.x;
    float height = (panellayout.height - (panelimage.patchMarginTop + panelimage.patchMarginBottom)) * paneltransform.worldScale.y;

    return uiRect.fitOnRect(Rect(x, y, width, height));
}

void UISystem::calculateUIAABB(UIComponent& ui){
    ui.aabb = AABB::ZERO;
    Attribute* vertexAttr = ui.buffer.getAttribute(AttributeType::POSITION);
    for (int i = 0; i < ui.buffer.getCount(); i++){
        Vector3 vertice = ui.buffer.getVector3(vertexAttr, i);

        ui.aabb.merge(vertice);
    }

    ui.needUpdateAABB = true;
}

void UISystem::update(double dt){
    if (paused) {
        return;
    }

    // need to be ordered by Transform
    auto layouts = scene->getComponentArray<UILayoutComponent>();

    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<UIContainerComponent>())){
            UIContainerComponent& container = scene->getComponent<UIContainerComponent>(entity);
            // reseting all container boxes
            for (int b = 0; b < container.numBoxes; b++){
                container.boxes[b].layout = NULL_ENTITY;
            }
            container.numBoxes = 0;
        }

        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);
            UILayoutComponent* parentlayout = scene->findComponent<UILayoutComponent>(transform.parent);
            if (parentlayout){
                UIContainerComponent* parentcontainer = scene->findComponent<UIContainerComponent>(transform.parent);
                if (parentcontainer && transform.visible){
                    if (parentcontainer->numBoxes < MAX_CONTAINER_BOXES){
                        layout.containerBoxIndex = parentcontainer->numBoxes;
                        if (!layout.usingAnchors){
                            layout.anchorPreset = AnchorPreset::TOP_LEFT;
                            layout.usingAnchors = true;
                        }
                        parentcontainer->boxes[layout.containerBoxIndex].layout = entity;

                        parentcontainer->numBoxes = parentcontainer->numBoxes + 1;
                    }else{
                        transform.parent = NULL_ENTITY;
                        Log::error("The UI container has exceeded the maximum allowed of %i children. Please, increase MAX_CONTAINER_BOXES value.", MAX_CONTAINER_BOXES);
                    }
                }

                PanelComponent* parentpanel = scene->findComponent<PanelComponent>(transform.parent);
                if (parentpanel){
                    layout.panel = transform.parent;
                }

                if (parentlayout->panel != NULL_ENTITY){
                    layout.panel = parentlayout->panel;
                }
            }
        }
    }

    // reverse Transform order to get sizes
    for (int i = layouts->size()-1; i >= 0; i--){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);

        createOrUpdateUiComponent(dt, layout, entity, signature);

        if (signature.test(scene->getComponentId<UIContainerComponent>())){
            UIContainerComponent& container = scene->getComponent<UIContainerComponent>(entity);

            // reset cached intrinsic minimums for this frame
            container.contentMinWidth = 0;
            container.contentMinHeight = 0;

            // configuring all container boxes
            if (container.numBoxes > 0){

                container.fixedWidth = 0;
                container.fixedHeight = 0;
                container.numBoxExpand = 0;
                container.maxWidth = 0;
                container.maxHeight = 0;
                int totalWidth = 0;
                int totalHeight = 0;
                // Content-min totals exclude children whose size is derived from parent via anchors
                // to avoid a circular dependency that prevents the container from shrinking
                int contentMinTotalWidth = 0;
                int contentMinTotalHeight = 0;
                unsigned int contentMinMaxWidth = 0;
                unsigned int contentMinMaxHeight = 0;
                for (int b = 0; b < container.numBoxes; b++){
                    if (container.boxes[b].layout != NULL_ENTITY){
                        if (!container.boxes[b].expand){
                            container.fixedWidth += container.boxes[b].rect.getWidth();
                        }
                        if (!container.boxes[b].expand){
                            container.fixedHeight += container.boxes[b].rect.getHeight();
                        }
                        totalWidth += container.boxes[b].rect.getWidth();
                        totalHeight += container.boxes[b].rect.getHeight();
                        container.maxHeight = std::max(container.maxHeight, (unsigned int)container.boxes[b].rect.getHeight());
                        container.maxWidth = std::max(container.maxWidth, (unsigned int)container.boxes[b].rect.getWidth());

                        // Check if child size is anchor-derived from parent in each dimension
                        UILayoutComponent* childLayout = scene->findComponent<UILayoutComponent>(container.boxes[b].layout);
                        bool widthFromParent = childLayout && childLayout->usingAnchors &&
                            (childLayout->anchorPointLeft != childLayout->anchorPointRight);
                        bool heightFromParent = childLayout && childLayout->usingAnchors &&
                            (childLayout->anchorPointTop != childLayout->anchorPointBottom);

                        // If child is anchor-derived but is itself a Container, use its intrinsic
                        // content minimum so the parent cannot shrink below the child's needs
                        UIContainerComponent* childContainer = scene->findComponent<UIContainerComponent>(container.boxes[b].layout);

                        if (!widthFromParent){
                            contentMinTotalWidth += container.boxes[b].rect.getWidth();
                            contentMinMaxWidth = std::max(contentMinMaxWidth, (unsigned int)container.boxes[b].rect.getWidth());
                        } else if (childContainer) {
                            contentMinTotalWidth += childContainer->contentMinWidth;
                            contentMinMaxWidth = std::max(contentMinMaxWidth, childContainer->contentMinWidth);
                        }
                        if (!heightFromParent){
                            contentMinTotalHeight += container.boxes[b].rect.getHeight();
                            contentMinMaxHeight = std::max(contentMinMaxHeight, (unsigned int)container.boxes[b].rect.getHeight());
                        } else if (childContainer) {
                            contentMinTotalHeight += childContainer->contentMinHeight;
                            contentMinMaxHeight = std::max(contentMinMaxHeight, childContainer->contentMinHeight);
                        }
                    }
                    if (container.boxes[b].expand){
                        container.numBoxExpand++;
                    }
                }


                if (container.type == ContainerType::HORIZONTAL){
                    layout.width = (layout.width > contentMinTotalWidth)? layout.width : contentMinTotalWidth;
                    layout.height = (layout.height > contentMinMaxHeight)? layout.height : contentMinMaxHeight;
                }else if (container.type == ContainerType::VERTICAL){
                    layout.width = (layout.width > contentMinMaxWidth)? layout.width : contentMinMaxWidth;
                    layout.height = (layout.height > contentMinTotalHeight)? layout.height : contentMinTotalHeight;
                }else if (container.type == ContainerType::HORIZONTAL_WRAP){
                    layout.width = (layout.width > container.maxWidth)? layout.width : container.maxWidth;
                    unsigned int effCellW = (container.wrapCellWidth > 0) ? container.wrapCellWidth : container.maxWidth;
                    unsigned int effCellH = (container.wrapCellHeight > 0) ? container.wrapCellHeight : container.maxHeight;
                    if (effCellW > 0 && effCellH > 0){
                        int itemsPerRow = layout.width / effCellW;
                        if (itemsPerRow < 1) itemsPerRow = 1;
                        int numRows = (container.numBoxes + itemsPerRow - 1) / itemsPerRow;
                        unsigned int minHeight = numRows * effCellH;
                        layout.height = (layout.height > minHeight)? layout.height : minHeight;
                    }else{
                        layout.height = (layout.height > container.maxHeight)? layout.height : container.maxHeight;
                    }
                }else if (container.type == ContainerType::VERTICAL_WRAP){
                    layout.height = (layout.height > container.maxHeight)? layout.height : container.maxHeight;
                    unsigned int effCellW = (container.wrapCellWidth > 0) ? container.wrapCellWidth : container.maxWidth;
                    unsigned int effCellH = (container.wrapCellHeight > 0) ? container.wrapCellHeight : container.maxHeight;
                    if (effCellH > 0 && effCellW > 0){
                        int itemsPerCol = layout.height / effCellH;
                        if (itemsPerCol < 1) itemsPerCol = 1;
                        int numCols = (container.numBoxes + itemsPerCol - 1) / itemsPerCol;
                        unsigned int minWidth = numCols * effCellW;
                        layout.width = (layout.width > minWidth)? layout.width : minWidth;
                    }else{
                        layout.width = (layout.width > container.maxWidth)? layout.width : container.maxWidth;
                    }
                }

                if (layout.width < 1) layout.width = 1;
                if (layout.height < 1) layout.height = 1;

                // Store the intrinsic content minimum for parent containers to use
                if (container.type == ContainerType::HORIZONTAL){
                    container.contentMinWidth = contentMinTotalWidth;
                    container.contentMinHeight = contentMinMaxHeight;
                } else if (container.type == ContainerType::VERTICAL){
                    container.contentMinWidth = contentMinMaxWidth;
                    container.contentMinHeight = contentMinTotalHeight;
                } else if (container.type == ContainerType::HORIZONTAL_WRAP){
                    container.contentMinWidth = contentMinMaxWidth;
                    container.contentMinHeight = contentMinMaxHeight;
                } else if (container.type == ContainerType::VERTICAL_WRAP){
                    container.contentMinWidth = contentMinMaxWidth;
                    container.contentMinHeight = contentMinMaxHeight;
                }
            }
        }

        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);
            UIContainerComponent* parentcontainer = scene->findComponent<UIContainerComponent>(transform.parent);
            if (parentcontainer && layout.containerBoxIndex >= 0){
                parentcontainer->boxes[layout.containerBoxIndex].rect = Rect(0, 0, layout.width, layout.height);
            }
        }
    }

    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            if (layout.usingAnchors){
                if (layout.anchorPointRight < layout.anchorPointLeft)
                    layout.anchorPointRight = layout.anchorPointLeft;
                if (layout.anchorPointBottom < layout.anchorPointTop)
                    layout.anchorPointBottom = layout.anchorPointTop;

                applyAnchorPreset(layout);
            }

            Rect anchorRefRect = getAnchorReferenceRect(layout, transform, false);
            float abAnchorLeft = (anchorRefRect.getWidth() * layout.anchorPointLeft) + anchorRefRect.getX();
            float abAnchorRight = (anchorRefRect.getWidth() * layout.anchorPointRight) + anchorRefRect.getX();
            float abAnchorTop = (anchorRefRect.getHeight() * layout.anchorPointTop) + anchorRefRect.getY();
            float abAnchorBottom = (anchorRefRect.getHeight() * layout.anchorPointBottom) + anchorRefRect.getY();

            if (layout.usingAnchors){

                if (layout.anchorPreset == AnchorPreset::NONE && layout.needUpdateAnchorOffsets){
                    // Convert current manual transform/size back into anchor offsets
                    float rawPosX = transform.position.x - layout.positionOffset.x;
                    float rawPosY = transform.position.y - layout.positionOffset.y;

                    layout.anchorOffsetLeft = rawPosX - abAnchorLeft;
                    layout.anchorOffsetTop = rawPosY - abAnchorTop;

                    layout.anchorOffsetRight = layout.width - (abAnchorRight - abAnchorLeft) + layout.anchorOffsetLeft;
                    layout.anchorOffsetBottom = layout.height - (abAnchorBottom - abAnchorTop) + layout.anchorOffsetTop;
                }

                layout.needUpdateAnchorOffsets = false;

                float posX = abAnchorLeft + layout.anchorOffsetLeft;
                float posY = abAnchorTop + layout.anchorOffsetTop;

                float computedWidth = abAnchorRight - posX + layout.anchorOffsetRight;
                float computedHeight = abAnchorBottom - posY + layout.anchorOffsetBottom;

                if (computedWidth < 1.0f) computedWidth = 1.0f;
                if (computedHeight < 1.0f) computedHeight = 1.0f;
                unsigned int width = static_cast<unsigned int>(std::ceil(computedWidth));
                unsigned int height = static_cast<unsigned int>(std::ceil(computedHeight));

                if (width != layout.width || height != layout.height){
                    layout.width = width;
                    layout.height = height;
                    layout.needUpdateSizes = true;
                }

                posX += layout.positionOffset.x;
                posY += layout.positionOffset.y;

                if (posX != transform.position.x || posY != transform.position.y){
                    transform.position.x = posX;
                    transform.position.y = posY;
                    transform.needUpdate = true;
                }

            }else{
                layout.anchorOffsetLeft = transform.position.x - abAnchorLeft;
                layout.anchorOffsetTop = transform.position.y - abAnchorTop;
                layout.anchorOffsetRight = layout.width + transform.position.x - abAnchorRight;
                layout.anchorOffsetBottom = layout.height + transform.position.y - abAnchorBottom;
            }
        }

        if (signature.test(scene->getComponentId<UIContainerComponent>())){
            UIContainerComponent& container = scene->getComponent<UIContainerComponent>(entity);
            int numObjInLine = 0;

            // Resolve effective cell sizes: custom override or auto from children
            unsigned int effectiveCellWidth = (container.wrapCellWidth > 0) ? container.wrapCellWidth : container.maxWidth;
            unsigned int effectiveCellHeight = (container.wrapCellHeight > 0) ? container.wrapCellHeight : container.maxHeight;

            if (container.type == ContainerType::HORIZONTAL_WRAP){
                if (effectiveCellWidth > 0){
                    numObjInLine = floor((float)layout.width / (float)effectiveCellWidth);
                }
                if (numObjInLine < 1) numObjInLine = 1;
            }else if (container.type == ContainerType::VERTICAL_WRAP){
                if (effectiveCellHeight > 0){
                    numObjInLine = floor((float)layout.height / (float)effectiveCellHeight);
                }
                if (numObjInLine < 1) numObjInLine = 1;
            }
            // configuring all container boxes
            if (container.numBoxes > 0){

                // Pre-compute sizes for expand boxes in HORIZONTAL/VERTICAL using
                // freeze-and-redistribute: non-reducible boxes (images) that exceed
                // their equal share are frozen at their natural size; the remaining
                // space is distributed equally among the rest.
                int resolved[MAX_CONTAINER_BOXES];
                std::fill(resolved, resolved + container.numBoxes, -1);
                if (container.type == ContainerType::HORIZONTAL || container.type == ContainerType::VERTICAL){
                    bool horiz = (container.type == ContainerType::HORIZONTAL);
                    int available = horiz
                        ? static_cast<int>(layout.width)  - static_cast<int>(container.fixedWidth)
                        : static_cast<int>(layout.height) - static_cast<int>(container.fixedHeight);
                    bool frozen[MAX_CONTAINER_BOXES] = {};
                    int frozenTotal = 0, frozenCount = 0;
                    bool changed = true;
                    while (changed && available > 0){
                        changed = false;
                        int freeCount = (int)container.numBoxExpand - frozenCount;
                        if (freeCount <= 0) break;
                        int share = (available - frozenTotal) / freeCount;
                        for (int b = 0; b < container.numBoxes; b++){
                            if (!frozen[b] && container.boxes[b].layout != NULL_ENTITY && container.boxes[b].expand){
                                bool ar = scene->findComponent<UIContainerComponent>(container.boxes[b].layout) != nullptr;
                                int nat = horiz ? (int)container.boxes[b].rect.getWidth() : (int)container.boxes[b].rect.getHeight();
                                if (!ar && nat > share){
                                    frozen[b] = true; frozenTotal += nat; frozenCount++; changed = true; break;
                                }
                            }
                        }
                    }
                    int fc = (int)container.numBoxExpand - frozenCount;
                    int rem = available - frozenTotal;
                    int base = (fc > 0 && rem > 0) ? rem / fc : 1;
                    int leftover = (fc > 0 && rem > 0) ? rem % fc : 0;
                    int fi = 0;
                    for (int b = 0; b < container.numBoxes; b++){
                        if (container.boxes[b].layout != NULL_ENTITY && container.boxes[b].expand){
                            int nat = horiz ? (int)container.boxes[b].rect.getWidth() : (int)container.boxes[b].rect.getHeight();
                            resolved[b] = frozen[b] ? nat : std::max(1, base + (fi++ < leftover ? 1 : 0));
                        }
                    }
                }

                for (int b = 0; b < container.numBoxes; b++){
                    if (container.boxes[b].layout != NULL_ENTITY){
                        if (container.type == ContainerType::HORIZONTAL){
                            if (b > 0){
                                container.boxes[b].rect.setX(container.boxes[b-1].rect.getX() + container.boxes[b-1].rect.getWidth());
                            }

                            if (container.boxes[b].expand && container.numBoxExpand > 0){
                                if (resolved[b] >= 0){
                                    container.boxes[b].rect.setWidth(resolved[b]);
                                }
                            }
                            container.boxes[b].rect.setHeight(layout.height);
                        }else if (container.type == ContainerType::VERTICAL){
                            if (b > 0){
                                container.boxes[b].rect.setY(container.boxes[b-1].rect.getY() + container.boxes[b-1].rect.getHeight());
                            }

                            if (container.boxes[b].expand && container.numBoxExpand > 0){
                                if (resolved[b] >= 0){
                                    container.boxes[b].rect.setHeight(resolved[b]);
                                }
                            }
                            container.boxes[b].rect.setWidth(layout.width);
                        }else if (container.type == ContainerType::HORIZONTAL_WRAP){
                            if (container.useAllWrapSpace){
                                int line = b / numObjInLine;
                                int lineIndex = b % numObjInLine;
                                int firstInLine = line * numObjInLine;
                                int itemsInLine = std::min(numObjInLine, static_cast<int>(container.numBoxes) - firstInLine);

                                if (itemsInLine > 0){
                                    int baseWidth = layout.width / itemsInLine;
                                    int remainder = layout.width % itemsInLine;

                                    int lineX = 0;
                                    for (int item = 0; item < lineIndex; item++){
                                        lineX += baseWidth + ((item < remainder) ? 1 : 0);
                                    }

                                    int itemWidth = baseWidth + ((lineIndex < remainder) ? 1 : 0);

                                    container.boxes[b].rect.setX(lineX);
                                    container.boxes[b].rect.setY(line * effectiveCellHeight);
                                    container.boxes[b].rect.setWidth(itemWidth);
                                }
                            }else{
                                // compute box width using signed math to avoid unsigned underflow
                                int containerW = static_cast<int>(layout.width);
                                int cellW = static_cast<int>(effectiveCellWidth);
                                int boxWidth = static_cast<int>(container.boxes[b].rect.getWidth());

                                if (container.boxes[b].expand && numObjInLine > 0 && cellW > 0){
                                    int diff = containerW - (numObjInLine * cellW);
                                    boxWidth = cellW + diff / numObjInLine;
                                }
                                boxWidth = std::clamp(boxWidth, 1, std::max(1, containerW));

                                container.boxes[b].rect.setWidth(boxWidth);

                                if (b == 0){
                                    container.boxes[b].rect.setX(0);
                                    container.boxes[b].rect.setY(0);
                                }else{
                                    int nextX = static_cast<int>(container.boxes[b-1].rect.getX() + container.boxes[b-1].rect.getWidth());
                                    int nextY = static_cast<int>(container.boxes[b-1].rect.getY());

                                    if (nextX + boxWidth > containerW){
                                        nextX = 0;
                                        nextY = static_cast<int>(container.boxes[b-1].rect.getY()) + static_cast<int>(effectiveCellHeight);
                                    }

                                    container.boxes[b].rect.setX(nextX);
                                    container.boxes[b].rect.setY(nextY);
                                }
                            }
                            container.boxes[b].rect.setHeight(effectiveCellHeight);
                        }else if (container.type == ContainerType::VERTICAL_WRAP){
                            if (container.useAllWrapSpace){
                                int column = b / numObjInLine;
                                int columnIndex = b % numObjInLine;
                                int firstInColumn = column * numObjInLine;
                                int itemsInColumn = std::min(numObjInLine, static_cast<int>(container.numBoxes) - firstInColumn);

                                if (itemsInColumn > 0){
                                    int baseHeight = layout.height / itemsInColumn;
                                    int remainder = layout.height % itemsInColumn;

                                    int columnY = 0;
                                    for (int item = 0; item < columnIndex; item++){
                                        columnY += baseHeight + ((item < remainder) ? 1 : 0);
                                    }

                                    int itemHeight = baseHeight + ((columnIndex < remainder) ? 1 : 0);

                                    container.boxes[b].rect.setX(column * effectiveCellWidth);
                                    container.boxes[b].rect.setY(columnY);
                                    container.boxes[b].rect.setHeight(itemHeight);
                                }
                            }else{
                                // compute box height using signed math to avoid unsigned underflow
                                int containerH = static_cast<int>(layout.height);
                                int cellH = static_cast<int>(effectiveCellHeight);
                                int boxHeight = static_cast<int>(container.boxes[b].rect.getHeight());

                                if (container.boxes[b].expand && numObjInLine > 0 && cellH > 0){
                                    int diff = containerH - (numObjInLine * cellH);
                                    boxHeight = cellH + diff / numObjInLine;
                                }
                                boxHeight = std::clamp(boxHeight, 1, std::max(1, containerH));

                                container.boxes[b].rect.setHeight(boxHeight);

                                if (b == 0){
                                    container.boxes[b].rect.setX(0);
                                    container.boxes[b].rect.setY(0);
                                }else{
                                    int nextX = static_cast<int>(container.boxes[b-1].rect.getX());
                                    int nextY = static_cast<int>(container.boxes[b-1].rect.getY() + container.boxes[b-1].rect.getHeight());

                                    if (nextY + boxHeight > containerH){
                                        nextX = static_cast<int>(container.boxes[b-1].rect.getX()) + static_cast<int>(effectiveCellWidth);
                                        nextY = 0;
                                    }

                                    container.boxes[b].rect.setX(nextX);
                                    container.boxes[b].rect.setY(nextY);
                                }
                            }
                            container.boxes[b].rect.setWidth(effectiveCellWidth);
                        }
                    }
                }
            }
        }

        if (layout.needUpdateSizes){
            if (signature.test(scene->getComponentId<ImageComponent>())){
                ImageComponent& img = scene->getComponent<ImageComponent>(entity);

                img.needUpdatePatches = true;
            }

            if (signature.test(scene->getComponentId<TextComponent>())){
                TextComponent& text = scene->getComponent<TextComponent>(entity);

                Vector2 minSize = getTextMinSize(text);
                if (layout.width < minSize.x) layout.width = minSize.x;
                if (layout.height < minSize.y) layout.height = minSize.y;

                text.needUpdateText = true;
            }

            if (signature.test(scene->getComponentId<ScrollbarComponent>())){
                ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);

                scrollbar.needUpdateScrollbar = true;
            }

            layout.needUpdateSizes = false;
        }

        createOrUpdateUiComponent(dt, layout, entity, signature);
        
    }

}

bool UISystem::isTextEditFocused(){
    auto textedits = scene->getComponentArray<TextEditComponent>();
    for (int i = 0; i < textedits->size(); i++){
        Entity entity = textedits->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<UIComponent>())){
            UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (ui.focused){
                return true;
            }
        }
    }
    return false;
}

bool UISystem::eventOnCharInput(wchar_t codepoint){
    auto layouts = scene->getComponentArray<UILayoutComponent>();
    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);

        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<TextEditComponent>()) && signature.test(scene->getComponentId<UIComponent>())){
            TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
            UIComponent& ui = scene->getComponent<UIComponent>(entity);

            if (ui.focused){
                TextComponent& text = scene->getComponent<TextComponent>(textedit.text);
                if (codepoint == '\b'){
                    if (text.text.length() > 0){
                        StringUtils::eraseLastCodepointUtf8(text.text);
                    }
                }else{
                    text.text = text.text + StringUtils::toUTF8(codepoint);
                }
                text.needUpdateText = true;

                textedit.needUpdateTextEdit = true;

                return true;
            }
        }
    }

    return false;
}

bool UISystem::eventOnPointerDown(float x, float y){
    lastUIFromPointer = NULL_ENTITY;
    lastPanelFromPointer = NULL_ENTITY;
    lastPointerPos = Vector2(x, y);

    auto layouts = scene->getComponentArray<UILayoutComponent>();

    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);

        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            if (transform.visible){
                if (signature.test(scene->getComponentId<ImageComponent>())){
                    Rect uirect(transform.worldPosition.x, transform.worldPosition.y, layout.width * transform.worldScale.x, layout.height * transform.worldScale.y);

                    if (layout.panel != NULL_ENTITY){
                        uirect = fitOnPanel(uirect, layout.panel);
                    }

                    if (uirect.contains(Vector2(x, y)) && !layout.ignoreEvents){ //TODO: inside to polygon
                        lastUIFromPointer = entity;
                        lastPanelFromPointer = layout.panel;
                    }

                    if (signature.test(scene->getComponentId<PanelComponent>())){
                        if (uirect.contains(Vector2(x, y)) && !layout.ignoreEvents){
                            lastPanelFromPointer = entity;
                        }
                    }
                }

                if (signature.test(scene->getComponentId<UIComponent>())){
                    UIComponent& ui = scene->getComponent<UIComponent>(entity);
                    if (ui.focused){
                        ui.focused = false;
                        ui.onLostFocus.call();
                    }
                }
            }
        }
    }

    if (lastUIFromPointer != NULL_ENTITY){
        UILayoutComponent& layout = layouts->getComponentFromIndex(layouts->getIndex(lastUIFromPointer));
        Signature signature = scene->getSignature(lastUIFromPointer);

        if (signature.test(scene->getComponentId<Transform>()) && signature.test(scene->getComponentId<UIComponent>())){
            Transform& transform = scene->getComponent<Transform>(lastUIFromPointer);
            UIComponent& ui = scene->getComponent<UIComponent>(lastUIFromPointer);
            
            if (signature.test(scene->getComponentId<ButtonComponent>())){
                ButtonComponent& button = scene->getComponent<ButtonComponent>(lastUIFromPointer);
                if (!button.disabled && !button.pressed){
                    ui.texture = button.texturePressed;
                    ui.color = button.colorPressed;
                    ui.needUpdateTexture = true;
                    button.onPress.call();
                    button.pressed = true;
                }
            }

            if (signature.test(scene->getComponentId<TextEditComponent>())){
                TextEditComponent& textedit = scene->getComponent<TextEditComponent>(lastUIFromPointer);
                TextComponent& text = scene->getComponent<TextComponent>(textedit.text);

                bool hadInvalid = false;
                std::wstring wideText = StringUtils::utf8ToWString(text.text, hadInvalid);
                System::instance().showVirtualKeyboard(wideText);
            }else{
                System::instance().hideVirtualKeyboard();
            }

            if (signature.test(scene->getComponentId<ScrollbarComponent>())){
                ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(lastUIFromPointer);
                Transform& bartransform = scene->getComponent<Transform>(scrollbar.bar);
                UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

                if (isCoordInside(x, y, bartransform, barlayout)){
                    scrollbar.barPointerDown = true;
                    if (scrollbar.type == ScrollbarType::VERTICAL){
                        scrollbar.barPointerPos = y - transform.worldPosition.y - (bartransform.position.y * bartransform.worldScale.y);
                    }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
                        scrollbar.barPointerPos = x - transform.worldPosition.x - (bartransform.position.x * bartransform.worldScale.x);
                    }
                }
            }

            if (signature.test(scene->getComponentId<PanelComponent>())){
                PanelComponent& panel = scene->getComponent<PanelComponent>(lastUIFromPointer);
                Transform& headertransform = scene->getComponent<Transform>(panel.headercontainer);
                UILayoutComponent& headerlayout = scene->getComponent<UILayoutComponent>(panel.headercontainer);

                Rect edgeRight;
                Rect edgeRightBottom;
                Rect edgeBottom;
                Rect edgeLeftBottom;
                Rect edgeLeft;
                getPanelEdges(panel, layout, transform, headerlayout, edgeRight, edgeRightBottom, edgeBottom, edgeLeftBottom, edgeLeft);

                if (panel.canResize){
                    panelSizeAcc = Vector2(0, 0);
                    if (edgeRight.contains(Vector2(x, y))){
                        panel.edgePointerDown = PanelEdge::RIGHT;
                    }else if (edgeRightBottom.contains(Vector2(x, y))){
                        panel.edgePointerDown = PanelEdge::RIGHT_BOTTOM;
                    }else if (edgeBottom.contains(Vector2(x, y))){
                        panel.edgePointerDown = PanelEdge::BOTTOM;
                    }else if (edgeLeftBottom.contains(Vector2(x, y))){
                        panel.edgePointerDown = PanelEdge::LEFT_BOTTOM;
                    }else if (edgeLeft.contains(Vector2(x, y))){
                        panel.edgePointerDown = PanelEdge::LEFT;
                    }else{
                        panel.edgePointerDown = PanelEdge::NONE;
                    }
                }

                if (panel.canMove){
                    if (isCoordInside(x, y, headertransform, headerlayout)){
                        panel.headerPointerDown = true;
                    }
                }
            }

            ui.onPointerDown(x - transform.worldPosition.x, y - transform.worldPosition.y);

            if (!ui.focused){
                ui.focused = true;
                ui.onGetFocus.call();
            }
        }
    }else{
        System::instance().hideVirtualKeyboard();
    }

    if (lastPanelFromPointer != NULL_ENTITY){
        PanelComponent& panel = scene->getComponent<PanelComponent>(lastPanelFromPointer);

        if (panel.canBringToFront){
            scene->moveChildToTop(lastPanelFromPointer);
        }
    }

    if (lastUIFromPointer != NULL_ENTITY)
        return true;

    return false;
}

bool UISystem::eventOnPointerUp(float x, float y){
    lastPointerPos = Vector2(-1, -1);

    auto layouts = scene->getComponentArray<UILayoutComponent>();
    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);

        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<Transform>()) && signature.test(scene->getComponentId<UIComponent>())){
            Transform& transform = scene->getComponent<Transform>(entity);
            UIComponent& ui = scene->getComponent<UIComponent>(entity);

            if (signature.test(scene->getComponentId<ButtonComponent>())){
                ButtonComponent& button = scene->getComponent<ButtonComponent>(entity);
                if (!button.disabled && button.pressed){
                    ui.texture = button.textureNormal;
                    ui.color = button.colorNormal;
                    ui.needUpdateTexture = true;
                    button.pressed = false;
                    button.onRelease.call();
                }
            }

            if (signature.test(scene->getComponentId<ScrollbarComponent>())){
                ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);
                Transform& bartransform = scene->getComponent<Transform>(scrollbar.bar);
                UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

                if (isCoordInside(x, y, bartransform, barlayout)){
                    scrollbar.barPointerDown = false;
                }
            }

            if (signature.test(scene->getComponentId<PanelComponent>())){
                PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

                panel.headerPointerDown = false;
                panel.edgePointerDown = PanelEdge::NONE;
            }

            ui.onPointerUp(x - transform.worldPosition.x, y - transform.worldPosition.y);
        }
    }

    lastPanelFromPointer = NULL_ENTITY;

    if (lastUIFromPointer != NULL_ENTITY){
        lastUIFromPointer = NULL_ENTITY;

        Engine::setMouseCursor(CursorType::ARROW);
        return true;
    }

    return false;
}

bool UISystem::eventOnPointerMove(float x, float y){
    auto layouts = scene->getComponentArray<UILayoutComponent>();

    CursorType cursor = CursorType::ARROW;

    for (int i = 0; i < layouts->size(); i++){
        UILayoutComponent& layout = layouts->getComponentFromIndex(i);

        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            if (transform.visible){
                if (signature.test(scene->getComponentId<ImageComponent>())){
                    Rect uirect(transform.worldPosition.x, transform.worldPosition.y, layout.width * transform.worldScale.x, layout.height * transform.worldScale.y);

                    if (layout.panel != NULL_ENTITY){
                        uirect = fitOnPanel(uirect, layout.panel);
                    }

                    if (uirect.contains(Vector2(x, y)) && !layout.ignoreEvents){
                        cursor = CursorType::ARROW;

                        if (signature.test(scene->getComponentId<TextEditComponent>())){
                            cursor = CursorType::IBEAM;

                        }else if (signature.test(scene->getComponentId<PanelComponent>())){
                            PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

                            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                            Transform& transform = scene->getComponent<Transform>(entity);
                            UILayoutComponent& headerlayout = scene->getComponent<UILayoutComponent>(panel.headercontainer);

                            Rect edgeRight;
                            Rect edgeRightBottom;
                            Rect edgeBottom;
                            Rect edgeLeftBottom;
                            Rect edgeLeft;
                            getPanelEdges(panel, layout, transform, headerlayout, edgeRight, edgeRightBottom, edgeBottom, edgeLeftBottom, edgeLeft);

                            if (panel.canResize){
                                if (edgeRight.contains(Vector2(x, y))){
                                    cursor = CursorType::RESIZE_EW;
                                }else if (edgeRightBottom.contains(Vector2(x, y))){
                                    cursor = CursorType::RESIZE_NWSE;
                                }else if (edgeBottom.contains(Vector2(x, y))){
                                    cursor = CursorType::RESIZE_NS;
                                }else if (edgeLeftBottom.contains(Vector2(x, y))){
                                    cursor = CursorType::RESIZE_NESW;
                                }else if (edgeLeft.contains(Vector2(x, y))){
                                    cursor = CursorType::RESIZE_EW;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (lastUIFromPointer != NULL_ENTITY){
        UILayoutComponent& layout = layouts->getComponentFromIndex(layouts->getIndex(lastUIFromPointer));
        Signature signature = scene->getSignature(lastUIFromPointer);

        Vector2 pointerDiff = Vector2(x, y) - lastPointerPos;

        if (signature.test(scene->getComponentId<Transform>()) && signature.test(scene->getComponentId<UIComponent>())){
            Transform& transform = scene->getComponent<Transform>(lastUIFromPointer);
            UIComponent& ui = scene->getComponent<UIComponent>(lastUIFromPointer);

            ui.onPointerMove.call(x - transform.worldPosition.x, y - transform.worldPosition.y);
            ui.pointerMoved = true;
        }

        if (signature.test(scene->getComponentId<ScrollbarComponent>())){
            ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(lastUIFromPointer);
            Transform& transform = scene->getComponent<Transform>(lastUIFromPointer);
            Transform& bartransform = scene->getComponent<Transform>(scrollbar.bar);
            UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

            if (scrollbar.barPointerDown && scrollbar.barSize < 1.0){

                float pos = 0;
                float halfBar = 0;

                if (scrollbar.type == ScrollbarType::VERTICAL){
                    float barSizePixel = (layout.height * scrollbar.barSize) * transform.worldScale.y;
                    pos = (y - transform.worldPosition.y + ((barSizePixel / 2.0) - scrollbar.barPointerPos)) / (layout.height * transform.worldScale.y);
                    halfBar = (barSizePixel / 2.0) / (layout.height * transform.worldScale.y);
                }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
                    float barSizePixel = (layout.width * scrollbar.barSize) * transform.worldScale.x;
                    pos = (x - transform.worldPosition.x + ((barSizePixel / 2.0) - scrollbar.barPointerPos)) / (layout.width * transform.worldScale.x);
                    halfBar = (barSizePixel / 2.0) / (layout.width * transform.worldScale.x);
                }

                if (pos < halfBar){
                    pos = halfBar;
                }else if (pos > (1.0 - halfBar)){
                    pos = (1.0 - halfBar);
                }

                float newStep = (pos - halfBar) / ((1.0 - halfBar) - halfBar);

                if (newStep != scrollbar.step){
                    scrollbar.step = newStep;
                    scrollbar.onChange.call(scrollbar.step);
                }

                if (scrollbar.type == ScrollbarType::VERTICAL){
                    barlayout.anchorPointTop = pos;
                    barlayout.anchorPointBottom = pos;
                }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
                    barlayout.anchorPointLeft = pos;
                    barlayout.anchorPointRight = pos;
                }
            }
        }

        if (signature.test(scene->getComponentId<PanelComponent>())){
            PanelComponent& panel = scene->getComponent<PanelComponent>(lastUIFromPointer);
            Transform& transform = scene->getComponent<Transform>(lastUIFromPointer);
            ImageComponent& image = scene->getComponent<ImageComponent>(lastUIFromPointer);
            if (panel.headerPointerDown){
                transform.position += Vector3(pointerDiff.x / transform.worldScale.x, pointerDiff.y / transform.worldScale.y, 0);
                transform.needUpdate = true;

                panel.onMove.call();
            }
            if (panel.edgePointerDown != PanelEdge::NONE){
                panelSizeAcc += Vector2(pointerDiff.x / transform.worldScale.x, pointerDiff.y / transform.worldScale.y);
                if (panel.edgePointerDown == PanelEdge::RIGHT){
                    layout.width += (int)panelSizeAcc.x;
                    layout.needUpdateSizes = true;
                    cursor = CursorType::RESIZE_EW;
                }else if (panel.edgePointerDown == PanelEdge::RIGHT_BOTTOM){
                    layout.width += (int)panelSizeAcc.x;
                    layout.height += (int)panelSizeAcc.y;
                    layout.needUpdateSizes = true;
                    cursor = CursorType::RESIZE_NWSE;
                }else if (panel.edgePointerDown == PanelEdge::BOTTOM){
                    layout.height += (int)panelSizeAcc.y;
                    layout.needUpdateSizes = true;
                    cursor = CursorType::RESIZE_NS;
                }else if (panel.edgePointerDown == PanelEdge::LEFT_BOTTOM){
                    transform.position += Vector3(pointerDiff.x / transform.worldScale.x, 0, 0);
                    transform.needUpdate = true;
                    layout.width -= (int)panelSizeAcc.x;
                    layout.height += (int)panelSizeAcc.y;
                    layout.needUpdateSizes = true;
                    cursor = CursorType::RESIZE_NESW;
                }else if (panel.edgePointerDown == PanelEdge::LEFT){
                    transform.position += Vector3(pointerDiff.x / transform.worldScale.x, 0, 0);
                    transform.needUpdate = true;
                    layout.width -= (int)panelSizeAcc.x;
                    layout.needUpdateSizes = true;
                    cursor = CursorType::RESIZE_EW;
                }
                if (layout.width < panel.minWidth){
                    layout.width = panel.minWidth;
                }
                if (layout.height < panel.minHeight){
                    layout.height = panel.minHeight;
                }
                panelSizeAcc -= Vector2((int)panelSizeAcc.x, (int)panelSizeAcc.y);

                panel.onResize.call(layout.width, layout.height);
            }
        }
    }

    Engine::setMouseCursor(cursor);

    lastPointerPos = Vector2(x, y);

    if (lastUIFromPointer != NULL_ENTITY)
        return true;

    return false;
}

bool UISystem::isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout){
    Vector3 point = transform.worldRotation.getRotationMatrix() * Vector3(x, y, 0);

    if (point.x >= (transform.worldPosition.x) &&
        point.x <= (transform.worldPosition.x + abs(layout.width * transform.worldScale.x)) &&
        point.y >= (transform.worldPosition.y) &&
        point.y <= (transform.worldPosition.y + abs(layout.height * transform.worldScale.y))) {
        return true;
    }
    return false;
}

bool UISystem::isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout, Vector2 center){
    Vector3 point = transform.worldRotation.getRotationMatrix() * Vector3(x, y, 0);

    if (point.x >= (transform.worldPosition.x - center.x) &&
        point.x <= (transform.worldPosition.x - center.x + abs(layout.width * transform.worldScale.x)) &&
        point.y >= (transform.worldPosition.y - center.y) &&
        point.y <= (transform.worldPosition.y - center.y + abs(layout.height * transform.worldScale.y))) {
        return true;
    }
    return false;
}


void UISystem::onComponentAdded(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<ButtonComponent>()) {
		ButtonComponent& button = scene->getComponent<ButtonComponent>(entity);
		createButtonObjects(entity, button);
	} else if (componentId == scene->getComponentId<PanelComponent>()) {
		PanelComponent& panel = scene->getComponent<PanelComponent>(entity);
		createPanelObjects(entity, panel);
	} else if (componentId == scene->getComponentId<ScrollbarComponent>()) {
		ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);
		createScrollbarObjects(entity, scrollbar);
	} else if (componentId == scene->getComponentId<TextEditComponent>()) {
		TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
		createTextEditObjects(entity, textedit);
	}
}

void UISystem::onComponentRemoved(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<ButtonComponent>()) {
		ButtonComponent& button = scene->getComponent<ButtonComponent>(entity);
		destroyButton(button);
	} else if (componentId == scene->getComponentId<PanelComponent>()) {
		PanelComponent& panel = scene->getComponent<PanelComponent>(entity);
		destroyPanel(panel);
	} else if (componentId == scene->getComponentId<ScrollbarComponent>()) {
		ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);
		destroyScrollbar(scrollbar);
	} else if (componentId == scene->getComponentId<TextEditComponent>()) {
		TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
		destroyTextEdit(textedit);
	} else if (componentId == scene->getComponentId<TextComponent>()) {
		TextComponent& text = scene->getComponent<TextComponent>(entity);
		destroyText(text);
	}
}