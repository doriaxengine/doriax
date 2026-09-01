// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "UISystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "Scene.h"
#include "Input.h"
#include "Engine.h"
#include "System.h"
#include "util/STBText.h"
#include "util/StringUtils.h"
#include "pool/FontPool.h"

using namespace doriax;

static constexpr float UI_DRAG_START_DISTANCE = 4.0f;
static constexpr double UI_DOUBLE_CLICK_TIME = 0.3;

static void degenerateTextQuad(Buffer& buffer, Attribute* atrVertice, int firstVertex){
    for (int i = 0; i < 4; i++){
        buffer.setVector3(firstVertex + i, atrVertice, Vector3(0, 0, 0));
    }
}

static void clipTextQuadToLayout(Buffer& buffer, Attribute* atrVertice, Attribute* atrTexcoord, int firstVertex, unsigned int width, unsigned int height, bool clipWidth, bool clipHeight){
    Vector3 p0 = buffer.getVector3(atrVertice, firstVertex);
    Vector3 p1 = buffer.getVector3(atrVertice, firstVertex + 1);
    Vector3 p2 = buffer.getVector3(atrVertice, firstVertex + 2);
    Vector3 p3 = buffer.getVector3(atrVertice, firstVertex + 3);

    Vector2 uv0 = buffer.getVector2(atrTexcoord, firstVertex);
    Vector2 uv1 = buffer.getVector2(atrTexcoord, firstVertex + 1);
    Vector2 uv2 = buffer.getVector2(atrTexcoord, firstVertex + 2);
    Vector2 uv3 = buffer.getVector2(atrTexcoord, firstVertex + 3);

    float x0 = p0.x;
    float x1 = p1.x;
    float y0 = p0.y;
    float y1 = p2.y;

    if ((clipWidth && (x1 <= 0.0f || x0 >= (float)width)) ||
        (clipHeight && (y1 <= 0.0f || y0 >= (float)height))){
        degenerateTextQuad(buffer, atrVertice, firstVertex);
        return;
    }

    if (clipWidth && x0 < 0.0f){
        float ratio = (0.0f - x0) / (x1 - x0);
        float uLeftBottom = uv0.x + (uv1.x - uv0.x) * ratio;
        float uLeftTop = uv3.x + (uv2.x - uv3.x) * ratio;
        p0.x = 0.0f;
        p3.x = 0.0f;
        uv0.x = uLeftBottom;
        uv3.x = uLeftTop;
        x0 = 0.0f;
    }

    if (clipWidth && x1 > (float)width){
        float ratio = ((float)width - x0) / (x1 - x0);
        float uRightBottom = uv0.x + (uv1.x - uv0.x) * ratio;
        float uRightTop = uv3.x + (uv2.x - uv3.x) * ratio;
        p1.x = (float)width;
        p2.x = (float)width;
        uv1.x = uRightBottom;
        uv2.x = uRightTop;
        x1 = (float)width;
    }

    if (clipHeight && y0 < 0.0f){
        float ratio = (0.0f - y0) / (y1 - y0);
        float vBottomLeft = uv0.y + (uv3.y - uv0.y) * ratio;
        float vBottomRight = uv1.y + (uv2.y - uv1.y) * ratio;
        p0.y = 0.0f;
        p1.y = 0.0f;
        uv0.y = vBottomLeft;
        uv1.y = vBottomRight;
        y0 = 0.0f;
    }

    if (clipHeight && y1 > (float)height){
        float ratio = ((float)height - y0) / (y1 - y0);
        float vTopLeft = uv0.y + (uv3.y - uv0.y) * ratio;
        float vTopRight = uv1.y + (uv2.y - uv1.y) * ratio;
        p2.y = (float)height;
        p3.y = (float)height;
        uv2.y = vTopRight;
        uv3.y = vTopLeft;
    }

    buffer.setVector3(firstVertex, atrVertice, p0);
    buffer.setVector3(firstVertex + 1, atrVertice, p1);
    buffer.setVector3(firstVertex + 2, atrVertice, p2);
    buffer.setVector3(firstVertex + 3, atrVertice, p3);

    buffer.setVector2(firstVertex, atrTexcoord, uv0);
    buffer.setVector2(firstVertex + 1, atrTexcoord, uv1);
    buffer.setVector2(firstVertex + 2, atrTexcoord, uv2);
    buffer.setVector2(firstVertex + 3, atrTexcoord, uv3);
}

UISystem::UISystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<UILayoutComponent>());

    eventId.clear();
    lastUIFromPointer = NULL_ENTITY;
    lastUIFromPointerHover = NULL_ENTITY;
    lastUIFromClick = NULL_ENTITY;
    lastPanelFromPointer = NULL_ENTITY;
    textEditSelecting = NULL_ENTITY;
    lastPointerDownPos = Vector2(-1, -1);
    lastPointerPos = Vector2(-1, -1);
    pointerDragging = false;
    pointerInternalGesture = false;
    lastClickTime = -1.0;

    anchorReferenceWidth = 0;
    anchorReferenceHeight = 0;
}

UISystem::~UISystem(){
}

bool UISystem::createImagePatches(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    unsigned int texWidth = ui.texture.getWidth();
    unsigned int texHeight = ui.texture.getHeight();

    if ((texWidth == 0 || texHeight == 0) && !ui.texture.empty()){
        // Reuse cached texture metadata when available. Reloading here is only
        // needed for first-time loads, not when resize just rebuilds patches.
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

std::string UISystem::getFontId(const TextComponent& text) const{
    std::string fontId = text.font[0];
    if (fontId.empty())
        fontId = "font";

    //two texts only share an atlas when they fall back the same way
    for (size_t i = 1; i < text.font.size(); i++){
        if (!text.font[i].empty())
            fontId += std::string(";") + text.font[i];
    }

    return fontId + std::string("|") + std::to_string(text.fontSize);
}

std::string UISystem::getFontTextureId(const TextComponent& text) const{
    // Every atlas change gets its own texture. The previous one stays untouched for
    // the texts still using it, instead of being rebuilt while bound, and is
    // released when the last of them is rebuilt.
    return getFontId(text) + std::string("#") + std::to_string(text.stbtext->getAtlasVersion());
}

bool UISystem::isFontAtlasStale(const TextComponent& text) const{
    //another text using the same font can have added glyphs to the atlas
    return text.loaded && text.stbtext && text.stbtext->getAtlasVersion() != text.atlasVersion;
}

void UISystem::syncFontAtlas(TextComponent& text, UIComponent& ui){
    if (!text.stbtext || text.stbtext->getAtlasVersion() == text.atlasVersion)
        return;

    TextureData* atlas = text.stbtext->getTextureData();
    if (!atlas)
        return;

    ui.texture.setData(getFontTextureId(text), *atlas);

    text.atlasVersion = text.stbtext->getAtlasVersion();
    ui.needUpdateTexture = true;
}

bool UISystem::loadFontAtlas(TextComponent& text, UIComponent& ui, UILayoutComponent& layout){
    std::string fontId = getFontId(text);

    text.stbtext = FontPool::get(fontId);
    if (!text.stbtext){
        std::vector<std::string> fallbacks;
        for (size_t i = 1; i < text.font.size(); i++){
            if (!text.font[i].empty())
                fallbacks.push_back(text.font[i]);
        }

        text.stbtext = FontPool::get(fontId, text.font[0], fallbacks, text.fontSize);
        if (!text.stbtext) {
            Log::error("Cannot load font atlas from: %s", text.font[0].c_str());
            return false;
        }
    }

    //texture is published by syncFontAtlas, after createText rasterized the glyphs
    text.atlasVersion = 0;

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

    text.stbtext->createText(text.text, &ui.buffer, indices_array, text.charPositions, text.charExtents, layout.width, layout.height, text.fixedWidth, text.fixedHeight, text.multiline, ui.flipY);

    syncFontAtlas(text, ui);

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

    if ((text.fixedWidth || text.fixedHeight) && ui.buffer.getCount() >= 4){
        Attribute* atrVertice = ui.buffer.getAttribute(AttributeType::POSITION);
        Attribute* atrTexcoord = ui.buffer.getAttribute(AttributeType::TEXCOORD1);
        for (int i = 0; i + 3 < ui.buffer.getCount(); i += 4){
            clipTextQuadToLayout(ui.buffer, atrVertice, atrTexcoord, i, layout.width, layout.height, text.fixedWidth, text.fixedHeight);
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
    if (button.label != NULL_ENTITY && !scene->findComponent<TextComponent>(button.label)){
        button.label = NULL_ENTITY;
    }

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
    if (panel.headerimage != NULL_ENTITY && !scene->findComponent<ImageComponent>(panel.headerimage)){
        panel.headerimage = NULL_ENTITY;
    }
    if (panel.headercontainer != NULL_ENTITY && !scene->findComponent<UIContainerComponent>(panel.headercontainer)){
        panel.headercontainer = NULL_ENTITY;
    }
    if (panel.headertext != NULL_ENTITY && !scene->findComponent<TextComponent>(panel.headertext)){
        panel.headertext = NULL_ENTITY;
    }
    if (panel.headerimage == NULL_ENTITY){
        panel.headercontainer = NULL_ENTITY;
        panel.headertext = NULL_ENTITY;
    }else if (panel.headercontainer == NULL_ENTITY){
        panel.headertext = NULL_ENTITY;
    }

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
    if (scrollbar.bar != NULL_ENTITY && !scene->findComponent<ImageComponent>(scrollbar.bar)){
        scrollbar.bar = NULL_ENTITY;
    }

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

void UISystem::createProgressbarObjects(Entity entity, ProgressbarComponent& progressbar){
    if (progressbar.fill != NULL_ENTITY && !scene->findComponent<ImageComponent>(progressbar.fill)){
        progressbar.fill = NULL_ENTITY;
    }

    if (progressbar.fill == NULL_ENTITY){
        progressbar.fill = scene->createEntity();

        scene->addComponent<Transform>(progressbar.fill);
        scene->addComponent<UILayoutComponent>(progressbar.fill);
        scene->addComponent<UIComponent>(progressbar.fill);
        scene->addComponent<ImageComponent>(progressbar.fill);

        scene->addEntityChild(entity, progressbar.fill, false);

        UILayoutComponent& filllayout = scene->getComponent<UILayoutComponent>(progressbar.fill);

        filllayout.ignoreEvents = true;
    }
}

void UISystem::createTextEditObjects(Entity entity, TextEditComponent& textedit){
    if (textedit.text != NULL_ENTITY && !scene->findComponent<TextComponent>(textedit.text)){
        textedit.text = NULL_ENTITY;
    }
    if (textedit.selection != NULL_ENTITY && !scene->findComponent<PolygonComponent>(textedit.selection)){
        textedit.selection = NULL_ENTITY;
    }
    if (textedit.cursor != NULL_ENTITY && !scene->findComponent<PolygonComponent>(textedit.cursor)){
        textedit.cursor = NULL_ENTITY;
    }

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

    if (textedit.selection == NULL_ENTITY){
        textedit.selection = scene->createEntity();

        scene->addComponent<Transform>(textedit.selection);
        scene->addComponent<UILayoutComponent>(textedit.selection);
        scene->addComponent<UIComponent>(textedit.selection);
        scene->addComponent<PolygonComponent>(textedit.selection);

        scene->addEntityChild(entity, textedit.selection, false);

        UILayoutComponent& selectionlayout = scene->getComponent<UILayoutComponent>(textedit.selection);
        selectionlayout.ignoreEvents = true;
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

void UISystem::applyButtonVisual(ButtonComponent& button, UIComponent& ui){
    Texture targetTexture;
    Vector4 targetColor;

    if (button.disabled){
        targetTexture = button.textureDisabled;
        targetColor = button.colorDisabled;
    }else if (button.pressed){
        targetTexture = button.texturePressed;
        targetColor = button.colorPressed;
    }else if (button.hovered){
        bool hasHoveredTexture = !button.textureHovered.empty() &&
            (!button.textureHovered.getId().empty() || button.textureHovered.isFramebuffer());
        targetTexture = hasHoveredTexture ? button.textureHovered : button.textureNormal;
        targetColor = button.colorHovered;
    }else{
        targetTexture = button.textureNormal;
        targetColor = button.colorNormal;
    }

    if (ui.texture != targetTexture){
        ui.texture = targetTexture;
        ui.needUpdateTexture = true;
    }
    ui.color = targetColor;
}

void UISystem::updateButton(Entity entity, ButtonComponent& button, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    if (!ui.loaded){
        if (!button.textureNormal.load()){
            button.textureNormal = ui.texture;
        }
        Vector4 oldColorNormal = button.colorNormal;
        if (button.colorNormal == Vector4(1.0, 1.0, 1.0, 1.0)){
            button.colorNormal = ui.color;
        }
        if (button.colorHovered == oldColorNormal){
            button.colorHovered = button.colorNormal;
        }
        button.textureNormal.load();
        button.textureHovered.load();
        button.texturePressed.load();
        button.textureDisabled.load();
    }

    if (button.label == NULL_ENTITY || !scene->findComponent<TextComponent>(button.label)){
        applyButtonVisual(button, ui);
        return;
    }

    TextComponent& labeltext = scene->getComponent<TextComponent>(button.label);
    UIComponent& labelui = scene->getComponent<UIComponent>(button.label);
    UILayoutComponent& labellayout = scene->getComponent<UILayoutComponent>(button.label);

    labeltext.needUpdateText = true;
    createOrUpdateText(labeltext, labelui, labellayout);

    applyButtonVisual(button, ui);
}

void UISystem::updatePanel(Entity entity, PanelComponent& panel, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
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

    if (panel.headerimage != NULL_ENTITY && scene->findComponent<UILayoutComponent>(panel.headerimage)){
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
    }

    if (panel.headertext != NULL_ENTITY && scene->findComponent<TextComponent>(panel.headertext)){
        UILayoutComponent& titlelayout = scene->getComponent<UILayoutComponent>(panel.headertext);
        UIComponent& titleui = scene->getComponent<UIComponent>(panel.headertext);
        TextComponent& headertext = scene->getComponent<TextComponent>(panel.headertext);

        titlelayout.anchorPreset = panel.titleAnchorPreset;
        headertext.needUpdateText = true;
        createOrUpdateText(headertext, titleui, titlelayout);
    }
}

void UISystem::updateScrollbar(ScrollbarComponent& scrollbar, UILayoutComponent& layout){
    float barSize = scrollbar.barSize;
    float step = scrollbar.step;

    if (barSize > 1){
        barSize = 1;
        step = 0;
    }else if (barSize < 0){
        barSize = 0;
    }

    if (step > 1){
        step = 1;
    }else if (step < 0){
        step = 0;
    }

    bool changed = (step != scrollbar.step);
    scrollbar.barSize = barSize;
    scrollbar.step = step;

    if (scrollbar.bar != NULL_ENTITY && scene->findComponent<UILayoutComponent>(scrollbar.bar)){
        UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

        float innerHeight = std::max(0.0f, (float)layout.height - scrollbar.barMarginTop - scrollbar.barMarginBottom);
        float innerWidth = std::max(0.0f, (float)layout.width - scrollbar.barMarginLeft - scrollbar.barMarginRight);

        float barSizePixel = 0;
        float trackStartNorm = 0;
        float trackEndNorm = 1;
        float halfBarParent = 0;
        unsigned int oldBarWidth = barlayout.width;
        unsigned int oldBarHeight = barlayout.height;

        if (scrollbar.type == ScrollbarType::VERTICAL){
            barSizePixel = innerHeight * scrollbar.barSize;
            barlayout.width = innerWidth;
            barlayout.height = barSizePixel;
            trackStartNorm = layout.height > 0 ? scrollbar.barMarginTop / (float)layout.height : 0;
            trackEndNorm = layout.height > 0 ? 1.0f - scrollbar.barMarginBottom / (float)layout.height : 1;
            halfBarParent = layout.height > 0 ? (barSizePixel / 2.0f) / layout.height : 0;
        }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
            barSizePixel = innerWidth * scrollbar.barSize;
            barlayout.width = barSizePixel;
            barlayout.height = innerHeight;
            trackStartNorm = layout.width > 0 ? scrollbar.barMarginLeft / (float)layout.width : 0;
            trackEndNorm = layout.width > 0 ? 1.0f - scrollbar.barMarginRight / (float)layout.width : 1;
            halfBarParent = layout.width > 0 ? (barSizePixel / 2.0f) / layout.width : 0;
        }

        if (barlayout.width != oldBarWidth || barlayout.height != oldBarHeight){
            barlayout.needUpdateSizes = true;
        }

        float movableStart = trackStartNorm + halfBarParent;
        float movableEnd = trackEndNorm - halfBarParent;
        float pos = movableStart;
        if (movableEnd > movableStart){
            pos = movableStart + scrollbar.step * (movableEnd - movableStart);
        }

        if (scrollbar.type == ScrollbarType::VERTICAL){
            barlayout.anchorPointLeft = 0;
            barlayout.anchorPointTop = pos;
            barlayout.anchorPointRight = 1;
            barlayout.anchorPointBottom = pos;
            barlayout.anchorOffsetLeft = scrollbar.barMarginLeft;
            barlayout.anchorOffsetTop = -floor(barlayout.height / 2.0);
            barlayout.anchorOffsetRight = -scrollbar.barMarginRight;
            barlayout.anchorOffsetBottom = ceil(barlayout.height / 2.0);
        }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
            barlayout.anchorPointLeft = pos;
            barlayout.anchorPointTop = 0;
            barlayout.anchorPointRight = pos;
            barlayout.anchorPointBottom = 1;
            barlayout.anchorOffsetLeft = -floor(barlayout.width / 2.0);
            barlayout.anchorOffsetTop = scrollbar.barMarginTop;
            barlayout.anchorOffsetRight = ceil(barlayout.width / 2.0);
            barlayout.anchorOffsetBottom = -scrollbar.barMarginBottom;
        }
        barlayout.anchorPreset = AnchorPreset::NONE;
        barlayout.usingAnchors = true;
    }

    if (changed)
        scrollbar.onChange.call(step);
}

void UISystem::updateProgressbar(Entity entity, ProgressbarComponent& progressbar, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    if (progressbar.value > 1){
        progressbar.value = 1;
    }else if (progressbar.value < 0){
        progressbar.value = 0;
    }

    if (progressbar.fill == NULL_ENTITY || !scene->findComponent<UILayoutComponent>(progressbar.fill)){
        return;
    }

    UILayoutComponent& filllayout = scene->getComponent<UILayoutComponent>(progressbar.fill);

    if (progressbar.type == ProgressbarType::HORIZONTAL){
        filllayout.anchorPointLeft = 0;
        filllayout.anchorPointTop = 0;
        filllayout.anchorPointRight = progressbar.value;
        filllayout.anchorPointBottom = 1;
        filllayout.anchorOffsetLeft = progressbar.fillMarginLeft;
        filllayout.anchorOffsetTop = progressbar.fillMarginTop;
        filllayout.anchorOffsetRight = progressbar.value >= 1.0f ? -progressbar.fillMarginRight : 0;
        filllayout.anchorOffsetBottom = -progressbar.fillMarginBottom;
    }else if (progressbar.type == ProgressbarType::VERTICAL){
        filllayout.anchorPointLeft = 0;
        filllayout.anchorPointTop = 1 - progressbar.value;
        filllayout.anchorPointRight = 1;
        filllayout.anchorPointBottom = 1;
        filllayout.anchorOffsetLeft = progressbar.fillMarginLeft;
        filllayout.anchorOffsetTop = progressbar.value >= 1.0f ? progressbar.fillMarginTop : 0;
        filllayout.anchorOffsetRight = -progressbar.fillMarginRight;
        filllayout.anchorOffsetBottom = -progressbar.fillMarginBottom;
    }
    filllayout.anchorPreset = AnchorPreset::NONE;
    filllayout.usingAnchors = true;
}

bool UISystem::textEditHasSelection(const TextEditComponent& textedit) const{
    return textedit.selectionAnchor != textedit.cursorIndex;
}

int UISystem::textEditSelectionStart(const TextEditComponent& textedit) const{
    return std::min(textedit.selectionAnchor, textedit.cursorIndex);
}

int UISystem::textEditSelectionEnd(const TextEditComponent& textedit) const{
    return std::max(textedit.selectionAnchor, textedit.cursorIndex);
}

void UISystem::textEditClampCursor(TextEditComponent& textedit, size_t numCodepoints) const{
    int maxIndex = static_cast<int>(numCodepoints);
    textedit.cursorIndex = std::clamp(textedit.cursorIndex, 0, maxIndex);
    if (!textEditHasSelection(textedit)){
        textedit.selectionAnchor = textedit.cursorIndex;
    }else{
        textedit.selectionAnchor = std::clamp(textedit.selectionAnchor, 0, maxIndex);
    }
}

int UISystem::textEditVisualStep(const TextComponent& text, int cursorIndex, int direction) const{
    size_t numChars = text.charExtents.size();
    if (numChars == 0){
        return cursorIndex + direction;
    }

    //arrows follow the screen, like the click that places the caret. Where the
    //direction changes two stops land on the same pixel and both must stay reachable,
    //so the logical index breaks the tie and keeps the order total
    std::vector<std::pair<float, int>> stops;
    stops.reserve(numChars + 1);

    for (int i = 0; i <= (int)numChars; i++){
        stops.push_back({textEditCursorPixelX(text, i), i});
    }

    std::sort(stops.begin(), stops.end());

    for (size_t i = 0; i < stops.size(); i++){
        if (stops[i].second != cursorIndex)
            continue;

        if (direction < 0){
            return (i > 0) ? stops[i - 1].second : cursorIndex;
        }
        return (i + 1 < stops.size()) ? stops[i + 1].second : cursorIndex;
    }

    return cursorIndex + direction;
}

float UISystem::textEditCursorPixelX(const TextComponent& text, int cursorIndex) const{
    size_t numChars = text.charExtents.size();
    if (numChars == 0){
        return 0;
    }

    //the caret sits after the previous codepoint, which in an RTL run is its left side
    if (cursorIndex > 0){
        const CharExtent& previous = text.charExtents[std::min((size_t)cursorIndex, numChars) - 1];
        return previous.rtl ? previous.left : previous.right;
    }

    const CharExtent& first = text.charExtents[0];
    return first.rtl ? first.right : first.left;
}

int UISystem::textEditFindCursorIndexFromX(const TextComponent& text, float x) const{
    size_t numChars = text.charExtents.size();
    if (numChars == 0){
        return 0;
    }

    //visual order does not follow logical order, the codepoint under the pointer has
    //to be searched for instead of accumulated
    size_t nearest = 0;
    float nearestDistance = std::numeric_limits<float>::max();

    for (size_t i = 0; i < numChars; i++){
        const CharExtent& extent = text.charExtents[i];

        float distance = 0.0f;
        if (x < extent.left){
            distance = extent.left - x;
        }else if (x > extent.right){
            distance = x - extent.right;
        }

        if (distance < nearestDistance){
            nearestDistance = distance;
            nearest = i;
            if (distance == 0.0f)
                break;
        }
    }

    const CharExtent& extent = text.charExtents[nearest];
    float middle = (extent.left + extent.right) * 0.5f;
    bool afterChar = extent.rtl ? (x < middle) : (x > middle);

    return static_cast<int>(afterChar ? nearest + 1 : nearest);
}

void UISystem::textEditSelectionRects(const TextComponent& text, int start, int end, std::vector<Rect>& rects) const{
    rects.clear();

    size_t numChars = text.charExtents.size();
    if (start >= end || numChars == 0){
        return;
    }

    size_t from = std::min((size_t)std::max(start, 0), numChars);
    size_t to = std::min((size_t)std::max(end, 0), numChars);

    //contiguous in logical order can still be several pieces on screen, so the spans
    //are merged by position
    std::vector<Rect> spans;
    for (size_t i = from; i < to; i++){
        const CharExtent& extent = text.charExtents[i];
        if (extent.right <= extent.left)
            continue;

        spans.push_back(Rect(extent.left, extent.y, extent.right - extent.left, 0));
    }

    if (spans.empty()){
        return;
    }

    std::sort(spans.begin(), spans.end(), [](const Rect& a, const Rect& b){
        return a.getX() < b.getX();
    });

    Rect current = spans[0];
    for (size_t i = 1; i < spans.size(); i++){
        float currentRight = current.getX() + current.getWidth();
        if (spans[i].getX() <= currentRight + 0.5f){
            float right = std::max(currentRight, spans[i].getX() + spans[i].getWidth());
            current.setWidth(right - current.getX());
        }else{
            rects.push_back(current);
            current = spans[i];
        }
    }
    rects.push_back(current);
}

std::string UISystem::textEditMaskText(const std::string& text, char maskChar) const{
    return std::string(StringUtils::countCodepoints(text), maskChar);
}

void UISystem::textEditDeleteSelection(TextEditComponent& textedit, TextComponent& text){
    if (!textEditHasSelection(textedit)){
        return;
    }

    StringUtils::eraseCodepointsUtf8(text.text,
        static_cast<size_t>(textEditSelectionStart(textedit)),
        static_cast<size_t>(textEditSelectionEnd(textedit)));
    textedit.cursorIndex = textEditSelectionStart(textedit);
    textedit.selectionAnchor = textedit.cursorIndex;
}

void UISystem::textEditResetBlink(TextEditComponent& textedit) const{
    textedit.cursorBlinkTimer = 0;
    if (Transform* cursortransform = scene->findComponent<Transform>(textedit.cursor)){
        cursortransform->visible = true;
    }
}

TextEditComponent* UISystem::findTextEditForTextChild(Entity textEntity) const{
    Transform* transform = scene->findComponent<Transform>(textEntity);
    if (!transform || transform->parent == NULL_ENTITY){
        return nullptr;
    }

    TextEditComponent* textedit = scene->findComponent<TextEditComponent>(transform->parent);
    if (!textedit || textedit->text != textEntity){
        return nullptr;
    }

    return textedit;
}

void UISystem::updateTextEdit(Entity entity, TextEditComponent& textedit, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout){
    if (textedit.text == NULL_ENTITY || !scene->findComponent<TextComponent>(textedit.text)){
        return;
    }

    Transform& texttransform = scene->getComponent<Transform>(textedit.text);
    UILayoutComponent& textlayout = scene->getComponent<UILayoutComponent>(textedit.text);
    UIComponent& textui = scene->getComponent<UIComponent>(textedit.text);
    TextComponent& text = scene->getComponent<TextComponent>(textedit.text);

    textEditClampCursor(textedit, StringUtils::countCodepoints(text.text));

    std::string storedText = text.text;
    Vector4 storedColor = textui.color;
    bool showingPlaceholder = storedText.empty() && !textedit.placeholder.empty() && !ui.focused;
    bool showingPassword = textedit.password && !storedText.empty();

    if (showingPlaceholder){
        text.text = textedit.placeholder;
        textui.color = textedit.placeholderColor;
    }else if (showingPassword){
        text.text = textEditMaskText(storedText, textedit.passwordChar);
    }

    text.multiline = false;
    text.needUpdateText = true;
    createOrUpdateText(text, textui, textlayout);

    text.text = storedText;
    textui.color = storedColor;

    if (layout.height == 0){
        layout.height = textlayout.height + img.patchMarginTop + img.patchMarginBottom;
        layout.needUpdateSizes = true;
    }

    int heightArea = layout.height - img.patchMarginTop - img.patchMarginBottom;
    int widthArea = layout.width - img.patchMarginRight - img.patchMarginLeft - textedit.cursorWidth;

    float cursorPixelX = textEditCursorPixelX(text, textedit.cursorIndex);
    if (cursorPixelX - textedit.scrollOffset > widthArea){
        textedit.scrollOffset = cursorPixelX - widthArea;
    }
    if (cursorPixelX - textedit.scrollOffset < 0){
        textedit.scrollOffset = cursorPixelX;
    }
    if (textlayout.width - textedit.scrollOffset < widthArea && textlayout.width > widthArea){
        textedit.scrollOffset = textlayout.width - widthArea;
    }
    if (textedit.scrollOffset < 0){
        textedit.scrollOffset = 0;
    }

    float textX = static_cast<float>(img.patchMarginLeft) - textedit.scrollOffset;
    float textY = static_cast<float>(img.patchMarginTop) + (heightArea / 2.0f) - (textlayout.height / 2.0f);

    Vector3 textPosition = Vector3(textX, textY, 0);
    if (texttransform.position != textPosition){
        texttransform.position = textPosition;
        texttransform.needUpdate = true;
    }

    if (textedit.selection != NULL_ENTITY && scene->findComponent<PolygonComponent>(textedit.selection)){
        Transform& selectiontransform = scene->getComponent<Transform>(textedit.selection);
        UILayoutComponent& selectionlayout = scene->getComponent<UILayoutComponent>(textedit.selection);
        UIComponent& selectionui = scene->getComponent<UIComponent>(textedit.selection);
        PolygonComponent& selection = scene->getComponent<PolygonComponent>(textedit.selection);

        bool hasSelection = ui.focused && !textedit.disabled && textEditHasSelection(textedit) && !showingPlaceholder;

        std::vector<Rect> selectionRects;
        if (hasSelection){
            textEditSelectionRects(text, textEditSelectionStart(textedit), textEditSelectionEnd(textedit), selectionRects);
            hasSelection = !selectionRects.empty();
        }

        selectiontransform.visible = hasSelection;

        if (hasSelection){
            createOrUpdatePolygon(selection, selectionui, selectionlayout);

            float selectionHeight = static_cast<float>(textlayout.height);
            Vector4 white = Vector4(1.0, 1.0, 1.0, 1.0);

            selection.points.clear();
            for (const Rect& rect : selectionRects){
                float left = rect.getX();
                float right = left + rect.getWidth();

                //a repeated vertex on each side of a gap keeps the bridging triangles
                //degenerate, so one strip can hold several rectangles
                if (!selection.points.empty()){
                    selection.points.push_back(selection.points.back());
                    selection.points.push_back({Vector3(left, 0, 0), white});
                }

                selection.points.push_back({Vector3(left, 0, 0), white});
                selection.points.push_back({Vector3(right, 0, 0), white});
                selection.points.push_back({Vector3(left, selectionHeight, 0), white});
                selection.points.push_back({Vector3(right, selectionHeight, 0), white});
            }

            selectionui.color = textedit.selectionColor;
            selectiontransform.position = Vector3(textX, textY, 0.0);
            selectiontransform.needUpdate = true;
            selection.needUpdatePolygon = true;
        }
    }

    if (textedit.cursor != NULL_ENTITY && scene->findComponent<PolygonComponent>(textedit.cursor)){
        Transform& cursortransform = scene->getComponent<Transform>(textedit.cursor);
        UILayoutComponent& cursorlayout = scene->getComponent<UILayoutComponent>(textedit.cursor);
        UIComponent& cursorui = scene->getComponent<UIComponent>(textedit.cursor);
        PolygonComponent& cursor = scene->getComponent<PolygonComponent>(textedit.cursor);

        bool showCursor = ui.focused && !textedit.disabled && !showingPlaceholder;
        if (!showCursor){
            cursortransform.visible = false;
        }

        if (showCursor){
            createOrUpdatePolygon(cursor, cursorui, cursorlayout);

            float cursorHeight = static_cast<float>(textlayout.height);

            cursor.points.clear();
            cursor.points.push_back({Vector3(0, 0, 0), Vector4(1.0, 1.0, 1.0, 1.0)});
            cursor.points.push_back({Vector3(textedit.cursorWidth, 0, 0), Vector4(1.0, 1.0, 1.0, 1.0)});
            cursor.points.push_back({Vector3(0, cursorHeight, 0), Vector4(1.0, 1.0, 1.0, 1.0)});
            cursor.points.push_back({Vector3(textedit.cursorWidth, cursorHeight, 0), Vector4(1.0, 1.0, 1.0, 1.0)});

            float cursorX = textX + cursorPixelX;
            float cursorY = static_cast<float>(img.patchMarginTop) + (heightArea / 2.0f) - (cursorHeight / 2.0f);

            cursorui.color = textedit.cursorColor;
            cursortransform.position = Vector3(cursorX, cursorY, 0.0);
            cursortransform.needUpdate = true;
            cursor.needUpdatePolygon = true;
        }
    }
}

void UISystem::blinkCursorTextEdit(double dt, TextEditComponent& textedit, UIComponent& ui){
    if (textedit.cursor == NULL_ENTITY || !scene->findComponent<Transform>(textedit.cursor)){
        return;
    }

    Transform& cursortransform = scene->getComponent<Transform>(textedit.cursor);

    if (!ui.focused || textedit.disabled){
        cursortransform.visible = false;
        textedit.cursorBlinkTimer = 0;
        return;
    }

    float period = textedit.cursorBlink;
    if (period <= 0.0f){
        cursortransform.visible = true;
        textedit.cursorBlinkTimer = 0;
        return;
    }

    textedit.cursorBlinkTimer += static_cast<float>(dt);
    float cycle = period * 2.0f;
    if (textedit.cursorBlinkTimer >= cycle){
        textedit.cursorBlinkTimer = std::fmod(textedit.cursorBlinkTimer, cycle);
    }

    cursortransform.visible = textedit.cursorBlinkTimer < period;
}

void UISystem::setTextEditCursorFromLocalX(TextEditComponent& textedit, TextComponent& text, ImageComponent& img, float localX, bool extendSelection){
    float clickTextX = localX - img.patchMarginLeft + textedit.scrollOffset;
    textedit.cursorIndex = textEditFindCursorIndexFromX(text, clickTextX);
    if (!extendSelection){
        textedit.selectionAnchor = textedit.cursorIndex;
    }
    textEditResetBlink(textedit);
    textedit.needUpdateTextEdit = true;
}

bool UISystem::handleTextEditCharInput(TextEditComponent& textedit, TextComponent& text, UIComponent& ui, wchar_t codepoint){
    if (textedit.disabled){
        return false;
    }

    if (codepoint == '\x1b'){
        ui.focused = false;
        ui.onLostFocus.call();
        textedit.needUpdateTextEdit = true;
        return true;
    }

    if (codepoint == '\r' || codepoint == '\n'){
        textedit.onSubmit.call();
        return true;
    }

    if (codepoint == '\b'){
        if (textEditHasSelection(textedit)){
            textEditDeleteSelection(textedit, text);
        }else if (textedit.cursorIndex > 0){
            StringUtils::eraseCodepointsUtf8(text.text,
                static_cast<size_t>(textedit.cursorIndex - 1),
                static_cast<size_t>(textedit.cursorIndex));
            textedit.cursorIndex--;
            textedit.selectionAnchor = textedit.cursorIndex;
        }
    }else if (codepoint >= 32 || codepoint == '\t'){
        std::string insertText = StringUtils::toUTF8(codepoint);
        size_t insertCount = StringUtils::countCodepoints(insertText);
        size_t currentCount = StringUtils::countCodepoints(text.text);
        size_t selectionSize = textEditHasSelection(textedit)
            ? static_cast<size_t>(textEditSelectionEnd(textedit) - textEditSelectionStart(textedit))
            : 0;

        if (text.maxTextSize > 0 && currentCount - selectionSize + insertCount > text.maxTextSize){
            return true;
        }

        if (textEditHasSelection(textedit)){
            textEditDeleteSelection(textedit, text);
        }

        StringUtils::insertUtf8AtCodepoint(text.text, static_cast<size_t>(textedit.cursorIndex), insertText);
        textedit.cursorIndex += static_cast<int>(insertCount);
        textedit.selectionAnchor = textedit.cursorIndex;
    }else{
        return false;
    }

    text.needUpdateText = true;
    textEditResetBlink(textedit);
    textedit.needUpdateTextEdit = true;
    textedit.onChange.call();
    return true;
}

bool UISystem::handleTextEditKeyDown(TextEditComponent& textedit, TextComponent& text, UIComponent& ui, int key, bool repeat, int mods){
    if (textedit.disabled){
        return false;
    }

    bool shift = (mods & D_MODIFIER_SHIFT) != 0;
    bool control = (mods & D_MODIFIER_CONTROL) != 0;
    size_t numCodepoints = StringUtils::countCodepoints(text.text);
    bool changed = false;

    if (control && key == D_KEY_A){
        textedit.selectionAnchor = 0;
        textedit.cursorIndex = static_cast<int>(numCodepoints);
        textEditResetBlink(textedit);
        textedit.needUpdateTextEdit = true;
        return true;
    }

    auto moveCursor = [&](int newIndex){
        textedit.cursorIndex = std::clamp(newIndex, 0, static_cast<int>(numCodepoints));
        if (!shift){
            textedit.selectionAnchor = textedit.cursorIndex;
        }
        textEditResetBlink(textedit);
        textedit.needUpdateTextEdit = true;
        changed = true;
    };

    if (key == D_KEY_LEFT){
        moveCursor(textEditVisualStep(text, textedit.cursorIndex, -1));
    }else if (key == D_KEY_RIGHT){
        moveCursor(textEditVisualStep(text, textedit.cursorIndex, 1));
    }else if (key == D_KEY_HOME){
        moveCursor(0);
    }else if (key == D_KEY_END){
        moveCursor(static_cast<int>(numCodepoints));
    }else if (key == D_KEY_DELETE){
        if (textEditHasSelection(textedit)){
            textEditDeleteSelection(textedit, text);
            changed = true;
        }else if (textedit.cursorIndex < static_cast<int>(numCodepoints)){
            StringUtils::eraseCodepointsUtf8(text.text,
                static_cast<size_t>(textedit.cursorIndex),
                static_cast<size_t>(textedit.cursorIndex + 1));
            changed = true;
        }
    }else{
        return false;
    }

    if (changed){
        text.needUpdateText = true;
        textedit.onChange.call();
        return true;
    }

    return changed;
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

    if (isFontAtlasStale(text)){
        text.needUpdateText = true;
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

// --- Layout / anchor ---

Entity UISystem::findNearestLayoutParent(Entity startParent) const{
    Entity ancestor = startParent;
    while (ancestor != NULL_ENTITY) {
        if (scene->findComponent<UILayoutComponent>(ancestor)) {
            return ancestor;
        }
        Transform* ancestorTransform = scene->findComponent<Transform>(ancestor);
        ancestor = ancestorTransform ? ancestorTransform->parent : NULL_ENTITY;
    }
    return NULL_ENTITY;
}

Rect UISystem::transformRectToWorldAxisAligned(const Rect& rect, const Matrix4& matrix){
    const Vector3 corners[4] = {
        matrix * Vector3(rect.getX(), rect.getY(), 0.0f),
        matrix * Vector3(rect.getX() + rect.getWidth(), rect.getY(), 0.0f),
        matrix * Vector3(rect.getX(), rect.getY() + rect.getHeight(), 0.0f),
        matrix * Vector3(rect.getX() + rect.getWidth(), rect.getY() + rect.getHeight(), 0.0f),
    };

    float minX = corners[0].x;
    float maxX = corners[0].x;
    float minY = corners[0].y;
    float maxY = corners[0].y;

    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, corners[i].x);
        maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxY = std::max(maxY, corners[i].y);
    }

    return Rect(minX, minY, maxX - minX, maxY - minY);
}

Vector3 UISystem::mapLayoutPointToParentLocalSpace(float posX, float posY, Entity layoutParent, Entity targetParent) const{
    if (layoutParent == NULL_ENTITY || targetParent == NULL_ENTITY || layoutParent == targetParent) {
        return Vector3(posX, posY, 0.0f);
    }

    const Transform& layoutTransform = scene->getComponent<Transform>(layoutParent);
    const Transform& targetTransform = scene->getComponent<Transform>(targetParent);

    Vector3 worldPoint = layoutTransform.modelMatrix * Vector3(posX, posY, 0.0f);
    Matrix4 targetToLayout = targetTransform.modelMatrix.inverse();
    if (!targetToLayout.isValid()) {
        return Vector3(posX, posY, 0.0f);
    }

    return targetToLayout * worldPoint;
}

Vector3 UISystem::mapParentLocalPointToLayoutSpace(float posX, float posY, Entity layoutParent, Entity sourceParent) const{
    if (layoutParent == NULL_ENTITY || sourceParent == NULL_ENTITY || layoutParent == sourceParent) {
        return Vector3(posX, posY, 0.0f);
    }

    const Transform& layoutTransform = scene->getComponent<Transform>(layoutParent);
    const Transform& sourceTransform = scene->getComponent<Transform>(sourceParent);

    Vector3 worldPoint = sourceTransform.modelMatrix * Vector3(posX, posY, 0.0f);
    Matrix4 layoutToWorld = layoutTransform.modelMatrix.inverse();
    if (!layoutToWorld.isValid()) {
        return Vector3(posX, posY, 0.0f);
    }

    return layoutToWorld * worldPoint;
}

void UISystem::convertLayoutSpaceToLocalParentSpace(Entity transformParent, Entity layoutParent, float& posX, float& posY) const{
    Vector3 localPoint = mapLayoutPointToParentLocalSpace(posX, posY, layoutParent, transformParent);
    posX = localPoint.x;
    posY = localPoint.y;
}

void UISystem::convertLocalParentSpaceToLayoutSpace(Entity transformParent, Entity layoutParent, float& posX, float& posY) const{
    Vector3 layoutPoint = mapParentLocalPointToLayoutSpace(posX, posY, layoutParent, transformParent);
    posX = layoutPoint.x;
    posY = layoutPoint.y;
}

Rect UISystem::getAnchorReferenceRect(const UILayoutComponent& layout, const Transform& transform, bool worldPosition){
    auto canvasRect = [this]() {
        int finalCanvasWidth = Engine::getCanvasWidth();
        if (anchorReferenceWidth != 0)
            finalCanvasWidth = anchorReferenceWidth;

        int finalCanvasHeight = Engine::getCanvasHeight();
        if (anchorReferenceHeight != 0)
            finalCanvasHeight = anchorReferenceHeight;

        return Rect(0, 0, finalCanvasWidth, finalCanvasHeight);
    };

    if (transform.parent == NULL_ENTITY){
        return canvasRect();
    }

    // Bundle roots and other non-UI parents have no UILayout; walk up to the nearest layout parent.
    Entity layoutParent = findNearestLayoutParent(transform.parent);

    if (layoutParent == NULL_ENTITY){
        Rect boxRect = canvasRect();
        if (worldPosition && transform.parent != NULL_ENTITY) {
            const Transform* parentTransform = scene->findComponent<Transform>(transform.parent);
            if (parentTransform){
                boxRect = transformRectToWorldAxisAligned(boxRect, parentTransform->modelMatrix);
            }
        }
        return boxRect;
    }

    UILayoutComponent* parentlayout = scene->findComponent<UILayoutComponent>(layoutParent);
    Rect boxRect = Rect(0, 0, parentlayout->width, parentlayout->height);

    UIContainerComponent* parentcontainer = scene->findComponent<UIContainerComponent>(layoutParent);
    if (parentcontainer){
        boxRect = parentcontainer->boxes[layout.containerBoxIndex].rect;
    }

    ImageComponent* parentimage = scene->findComponent<ImageComponent>(layoutParent);
    if (parentimage && !layout.ignoreScissor){
        boxRect.setX(boxRect.getX() + parentimage->patchMarginLeft);
        boxRect.setWidth(boxRect.getWidth() - parentimage->patchMarginRight - parentimage->patchMarginLeft);
        boxRect.setY(boxRect.getY() + parentimage->patchMarginTop);
        boxRect.setHeight(boxRect.getHeight() - parentimage->patchMarginBottom - parentimage->patchMarginTop);
    }

    if (worldPosition){
        const Transform& layoutTransform = scene->getComponent<Transform>(layoutParent);
        boxRect = transformRectToWorldAxisAligned(boxRect, layoutTransform.modelMatrix);
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
    // framebuffer textures need no special case: offscreen passes render with a
    // Y-flipped projection on GL, so they are top-left origin on every backend
    ui.flipY = false;
    if (camera.type != CameraType::CAMERA_UI){
        ui.flipY = !ui.flipY;
    }
}

void UISystem::destroyText(TextComponent& text){
    text.loaded = false;
    text.needReloadAtlas = false;

    text.needUpdateText = true;

    text.atlasVersion = 0;

    if (text.stbtext){
        text.stbtext.reset();
    }
}

void UISystem::destroyButton(ButtonComponent& button){
    button.textureNormal.destroy();
    button.textureHovered.destroy();
    button.texturePressed.destroy();
    button.textureDisabled.destroy();

    button.needUpdateButton = true;
}

bool UISystem::isEntityChild(Entity child, Entity parent) const{
    if (child == NULL_ENTITY || parent == NULL_ENTITY){
        return false;
    }

    Transform* transform = scene->findComponent<Transform>(child);
    return transform && transform->parent == parent;
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
    }
}

void UISystem::draw(){
}

void UISystem::createOrUpdateUiComponent(UILayoutComponent& layout, Entity entity, Signature signature){
    if (signature.test(scene->getComponentId<UIComponent>())){
        UIComponent& ui = scene->getComponent<UIComponent>(entity);

        // Texts
        if (signature.test(scene->getComponentId<TextComponent>())){
            TextComponent& text = scene->getComponent<TextComponent>(entity);

            if (TextEditComponent* ownerEdit = findTextEditForTextChild(entity)){
                if (text.needUpdateText || text.needReloadAtlas || isFontAtlasStale(text)){
                    ownerEdit->needUpdateTextEdit = true;
                }
            }else{
                createOrUpdateText(text, ui, layout);
            }
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
                    scrollbar.needUpdateScrollbar = false;
                    // onChange can invalidate img, ui and layout: leave the remaining
                    // updates for the next frame, their flags are still set
                    updateScrollbar(scrollbar, layout);
                    return;
                }
            }

            // Progressbar
            if (signature.test(scene->getComponentId<ProgressbarComponent>())){
                ProgressbarComponent& progressbar = scene->getComponent<ProgressbarComponent>(entity);

                if (progressbar.needUpdateProgressbar){
                    updateProgressbar(entity, progressbar, img, ui, layout);

                    progressbar.needUpdateProgressbar = false;
                }
            }

            // Textedits
            if (signature.test(scene->getComponentId<TextEditComponent>())){
                TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);

                if (textedit.needUpdateTextEdit){
                    updateTextEdit(entity, textedit, img, ui, layout);

                    textedit.needUpdateTextEdit = false;
                }
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
            Entity layoutParentEntity = findNearestLayoutParent(transform.parent);
            if (layoutParentEntity != NULL_ENTITY){
                UILayoutComponent* parentlayout = scene->findComponent<UILayoutComponent>(layoutParentEntity);
                UIContainerComponent* parentcontainer = scene->findComponent<UIContainerComponent>(layoutParentEntity);
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

                PanelComponent* parentpanel = scene->findComponent<PanelComponent>(layoutParentEntity);
                if (parentpanel){
                    layout.panel = layoutParentEntity;
                }

                if (parentlayout->panel != NULL_ENTITY){
                    layout.panel = parentlayout->panel;
                }
            }
        }
    }

    // reverse Transform order to get sizes
    // iterating over a snapshot because createOrUpdateUiComponent can run user callbacks
    // that add or remove UI entities, reordering the component array
    std::vector<Entity> layoutEntities;
    layoutEntities.reserve(layouts->size());
    for (int i = layouts->size()-1; i >= 0; i--){
        layoutEntities.push_back(layouts->getEntity(i));
    }

    for (Entity entity : layoutEntities){
        UILayoutComponent* layoutPtr = scene->findComponent<UILayoutComponent>(entity);
        if (!layoutPtr){
            continue;
        }

        createOrUpdateUiComponent(*layoutPtr, entity, scene->getSignature(entity));

        layoutPtr = scene->findComponent<UILayoutComponent>(entity);
        if (!layoutPtr){
            continue;
        }

        UILayoutComponent& layout = *layoutPtr;
        Signature signature = scene->getSignature(entity);

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
            Entity layoutParentEntity = findNearestLayoutParent(transform.parent);
            UIContainerComponent* parentcontainer = layoutParentEntity != NULL_ENTITY
                ? scene->findComponent<UIContainerComponent>(layoutParentEntity)
                : nullptr;
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
                    float rawPosX = transform.position.x;
                    float rawPosY = transform.position.y;

                    if (transform.parent != NULL_ENTITY){
                        Entity layoutParentEntity = findNearestLayoutParent(transform.parent);
                        convertLocalParentSpaceToLayoutSpace(transform.parent, layoutParentEntity, rawPosX, rawPosY);
                    }

                    rawPosX -= layout.positionOffset.x;
                    rawPosY -= layout.positionOffset.y;

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

                if (transform.parent != NULL_ENTITY){
                    Entity layoutParentEntity = findNearestLayoutParent(transform.parent);
                    convertLayoutSpaceToLocalParentSpace(transform.parent, layoutParentEntity, posX, posY);
                }

                if (posX != transform.position.x || posY != transform.position.y){
                    transform.position.x = posX;
                    transform.position.y = posY;
                    transform.needUpdate = true;
                }

            }else{
                float layoutPosX = transform.position.x;
                float layoutPosY = transform.position.y;

                if (transform.parent != NULL_ENTITY){
                    Entity layoutParentEntity = findNearestLayoutParent(transform.parent);
                    convertLocalParentSpaceToLayoutSpace(transform.parent, layoutParentEntity, layoutPosX, layoutPosY);
                }

                layout.anchorOffsetLeft = layoutPosX - abAnchorLeft;
                layout.anchorOffsetTop = layoutPosY - abAnchorTop;
                layout.anchorOffsetRight = layout.width + layoutPosX - abAnchorRight;
                layout.anchorOffsetBottom = layout.height + layoutPosY - abAnchorBottom;
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

                if (TextEditComponent* ownerEdit = findTextEditForTextChild(entity)){
                    ownerEdit->needUpdateTextEdit = true;
                }else{
                    text.needUpdateText = true;
                }
            }

            if (signature.test(scene->getComponentId<ScrollbarComponent>())){
                ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);

                scrollbar.needUpdateScrollbar = true;
            }

            if (signature.test(scene->getComponentId<ProgressbarComponent>())){
                ProgressbarComponent& progressbar = scene->getComponent<ProgressbarComponent>(entity);

                progressbar.needUpdateProgressbar = true;
            }

            layout.needUpdateSizes = false;
        }

        createOrUpdateUiComponent(layout, entity, signature);
        
    }

    auto textedits = scene->getComponentArray<TextEditComponent>();
    for (int i = 0; i < textedits->size(); i++){
        Entity entity = textedits->getEntity(i);
        UIComponent* ui = scene->findComponent<UIComponent>(entity);
        if (ui){
            blinkCursorTextEdit(dt, textedits->getComponentFromIndex(i), *ui);
        }
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

void UISystem::resetButtonStates() {
    auto buttons = scene->getComponentArray<ButtonComponent>();
    for (int i = 0; i < buttons->size(); i++) {
        ButtonComponent& button = buttons->getComponentFromIndex(i);
        if (!button.pressed) continue;

        Entity entity = buttons->getEntity(i);
        Signature signature = scene->getSignature(entity);
        button.pressed = false;
        if (signature.test(scene->getComponentId<UIComponent>())) {
            UIComponent& ui = scene->getComponent<UIComponent>(entity);
            applyButtonVisual(button, ui);
        }
    }
}

bool UISystem::eventOnCharInput(wchar_t codepoint){
    auto layouts = scene->getComponentArray<UILayoutComponent>();
    for (int i = 0; i < layouts->size(); i++){
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<TextEditComponent>()) && signature.test(scene->getComponentId<UIComponent>())){
            TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
            UIComponent& ui = scene->getComponent<UIComponent>(entity);

            if (ui.focused){
                TextComponent* text = scene->findComponent<TextComponent>(textedit.text);
                if (text && handleTextEditCharInput(textedit, *text, ui, codepoint)){
                    return true;
                }
            }
        }
    }

    return false;
}

bool UISystem::eventOnKeyDown(int key, bool repeat, int mods){
    auto layouts = scene->getComponentArray<UILayoutComponent>();
    for (int i = 0; i < layouts->size(); i++){
        Entity entity = layouts->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<TextEditComponent>()) && signature.test(scene->getComponentId<UIComponent>())){
            TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
            UIComponent& ui = scene->getComponent<UIComponent>(entity);

            if (ui.focused){
                TextComponent* text = scene->findComponent<TextComponent>(textedit.text);
                if (text && handleTextEditKeyDown(textedit, *text, ui, key, repeat, mods)){
                    return true;
                }
            }
        }
    }

    return false;
}

bool UISystem::isLiveUIEntity(Entity entity) const {
    if (entity == NULL_ENTITY || !scene->isEntityCreated(entity))
        return false;
    return isLiveUIEntity(entity, scene->getSignature(entity));
}

bool UISystem::isLiveUIEntity(Entity entity, Signature signature) const {
    return entity != NULL_ENTITY
        && scene->isEntityCreated(entity)
        && signature.test(scene->getComponentId<Transform>())
        && signature.test(scene->getComponentId<UIComponent>());
}

// Entity must be a live UI entity. Every user callback here can create or destroy
// entities, which invalidates the component references
void UISystem::pointerDownOnUI(Entity entity, float x, float y){
    Signature signature = scene->getSignature(entity);
    UIComponent* ui = scene->findComponent<UIComponent>(entity);
    ui->pointerMoved = false;

    if (signature.test(scene->getComponentId<ButtonComponent>())){
        ButtonComponent* button = scene->findComponent<ButtonComponent>(entity);
        if (button && !button->disabled && !button->pressed){
            button->pressed = true;
            applyButtonVisual(*button, *ui);
            button->onPress.call();

            ui = scene->findComponent<UIComponent>(entity);
            if (!ui)
                return;
            signature = scene->getSignature(entity);
        }
    }

    Transform* transform = scene->findComponent<Transform>(entity);
    UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity);
    if (!transform || !layout)
        return;

    if (signature.test(scene->getComponentId<TextEditComponent>())){
        TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
        if (!textedit.disabled){
            TextComponent* text = scene->findComponent<TextComponent>(textedit.text);
            ImageComponent* img = scene->findComponent<ImageComponent>(entity);
            if (text && img){
                float localX = x - transform->worldPosition.x;
                bool extendSelection = (Input::getModifiers() & D_MODIFIER_SHIFT) != 0;
                if (text->text.empty()){
                    textedit.cursorIndex = 0;
                    if (!extendSelection){
                        textedit.selectionAnchor = 0;
                    }
                    textEditResetBlink(textedit);
                    textedit.needUpdateTextEdit = true;
                }else{
                    setTextEditCursorFromLocalX(textedit, *text, *img, localX, extendSelection);
                }
                textEditSelecting = entity;

                bool hadInvalid = false;
                std::wstring wideText = StringUtils::utf8ToWString(text->text, hadInvalid);
                System::instance().showVirtualKeyboard(wideText);
            }
        }
    }else{
        System::instance().hideVirtualKeyboard();
    }

    if (signature.test(scene->getComponentId<ScrollbarComponent>())){
        ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);
        if (scrollbar.bar != NULL_ENTITY &&
            scene->findComponent<Transform>(scrollbar.bar) &&
            scene->findComponent<UILayoutComponent>(scrollbar.bar)){
            Transform& bartransform = scene->getComponent<Transform>(scrollbar.bar);
            UILayoutComponent& barlayout = scene->getComponent<UILayoutComponent>(scrollbar.bar);

            if (isCoordInside(x, y, bartransform, barlayout)){
                scrollbar.barPointerDown = true;
                pointerInternalGesture = true;
                if (scrollbar.type == ScrollbarType::VERTICAL){
                    scrollbar.barPointerPos = y - transform->worldPosition.y - (bartransform.position.y * bartransform.worldScale.y);
                }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
                    scrollbar.barPointerPos = x - transform->worldPosition.x - (bartransform.position.x * bartransform.worldScale.x);
                }
            }else if (scrollbar.barSize < 1.0){
                scrollbar.barPointerDown = true;
                pointerInternalGesture = true;
                if (scrollbar.type == ScrollbarType::VERTICAL){
                    float innerHeight = std::max(0.0f, (float)layout->height - scrollbar.barMarginTop - scrollbar.barMarginBottom);
                    scrollbar.barPointerPos = innerHeight * scrollbar.barSize / 2.0f;
                }else if (scrollbar.type == ScrollbarType::HORIZONTAL){
                    float innerWidth = std::max(0.0f, (float)layout->width - scrollbar.barMarginLeft - scrollbar.barMarginRight);
                    scrollbar.barPointerPos = innerWidth * scrollbar.barSize / 2.0f;
                }
                applyScrollbarPointerPosition(entity, x, y);
            }
        }
    }

    if (!scene->isEntityCreated(entity))
        return;
    signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<Transform>()) ||
        !signature.test(scene->getComponentId<UILayoutComponent>()) ||
        !signature.test(scene->getComponentId<UIComponent>()))
        return;
    transform = &scene->getComponent<Transform>(entity);
    layout = &scene->getComponent<UILayoutComponent>(entity);
    ui = &scene->getComponent<UIComponent>(entity);

    if (signature.test(scene->getComponentId<PanelComponent>())){
        PanelComponent& panel = scene->getComponent<PanelComponent>(entity);
        UILayoutComponent defaultHeaderLayout;
        const UILayoutComponent& headerlayout = panel.headercontainer != NULL_ENTITY && scene->findComponent<UILayoutComponent>(panel.headercontainer)
            ? scene->getComponent<UILayoutComponent>(panel.headercontainer)
            : defaultHeaderLayout;

        Rect edgeRight;
        Rect edgeRightBottom;
        Rect edgeBottom;
        Rect edgeLeftBottom;
        Rect edgeLeft;
        getPanelEdges(panel, *layout, *transform, headerlayout, edgeRight, edgeRightBottom, edgeBottom, edgeLeftBottom, edgeLeft);

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

            if (panel.edgePointerDown != PanelEdge::NONE){
                pointerInternalGesture = true;
            }
        }

        if (panel.canMove && panel.headercontainer != NULL_ENTITY){
            Transform* headertransform = scene->findComponent<Transform>(panel.headercontainer);
            UILayoutComponent* headerlayoutPtr = scene->findComponent<UILayoutComponent>(panel.headercontainer);
            if (headertransform && headerlayoutPtr && isCoordInside(x, y, *headertransform, *headerlayoutPtr)){
                panel.headerPointerDown = true;
                pointerInternalGesture = true;
            }
        }
    }

    ui->onPointerDown(x - transform->worldPosition.x, y - transform->worldPosition.y);

    ui = scene->findComponent<UIComponent>(entity);
    if (!ui)
        return;

    if (!ui->focused){
        ui->focused = true;
        ui->onGetFocus.call();
        ui = scene->findComponent<UIComponent>(entity);
        if (!ui)
            return;
    }

    if (scene->findComponent<TextEditComponent>(entity))
        scene->getComponent<TextEditComponent>(entity).needUpdateTextEdit = true;
}

bool UISystem::eventOnPointerDown(float x, float y){
    lastUIFromPointer = NULL_ENTITY;
    lastPanelFromPointer = NULL_ENTITY;
    lastPointerDownPos = Vector2(x, y);
    lastPointerPos = Vector2(x, y);
    pointerDragging = false;
    pointerInternalGesture = false;

    auto layouts = scene->getComponentArray<UILayoutComponent>();

    std::vector<Entity> layoutEntities;
    layoutEntities.reserve(layouts->size());
    for (int i = 0; i < layouts->size(); i++){
        layoutEntities.push_back(layouts->getEntity(i));
    }

    for (Entity entity : layoutEntities){
        UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity);
        if (!layout){
            continue;
        }

        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            if (transform.visible){
                if (signature.test(scene->getComponentId<ImageComponent>())){
                    Rect uirect(transform.worldPosition.x, transform.worldPosition.y, layout->width * transform.worldScale.x, layout->height * transform.worldScale.y);

                    if (layout->panel != NULL_ENTITY){
                        uirect = fitOnPanel(uirect, layout->panel);
                    }

                    if (uirect.contains(Vector2(x, y)) && !layout->ignoreEvents){ //TODO: inside to polygon
                        lastUIFromPointer = entity;
                        lastPanelFromPointer = layout->panel;
                    }

                    if (signature.test(scene->getComponentId<PanelComponent>())){
                        if (uirect.contains(Vector2(x, y)) && !layout->ignoreEvents){
                            lastPanelFromPointer = entity;
                        }
                    }
                }

                if (signature.test(scene->getComponentId<UIComponent>())){
                    UIComponent& ui = scene->getComponent<UIComponent>(entity);
                    if (ui.focused){
                        ui.focused = false;
                        ui.onLostFocus.call();
                        if (scene->isEntityCreated(entity)){
                            signature = scene->getSignature(entity);
                            if (signature.test(scene->getComponentId<TextEditComponent>())){
                                TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
                                textedit.needUpdateTextEdit = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (isLiveUIEntity(lastUIFromPointer)){
        pointerDownOnUI(lastUIFromPointer, x, y);
    }else{
        lastUIFromPointer = NULL_ENTITY;
        System::instance().hideVirtualKeyboard();
    }

    if (PanelComponent* panel = scene->findComponent<PanelComponent>(lastPanelFromPointer)){
        if (panel->canBringToFront){
            scene->moveChildToTop(lastPanelFromPointer);
        }
    }else{
        lastPanelFromPointer = NULL_ENTITY;
    }

    if (!isLiveUIEntity(lastUIFromPointer)){
        lastUIFromPointer = NULL_ENTITY;
    }

    if (lastUIFromPointer != NULL_ENTITY)
        return true;

    return false;
}

bool UISystem::eventOnPointerUp(float x, float y){
    lastPointerPos = Vector2(-1, -1);

    auto layouts = scene->getComponentArray<UILayoutComponent>();
    Entity currentUIFromPointer = NULL_ENTITY;

    std::vector<Entity> layoutEntities;
    layoutEntities.reserve(layouts->size());
    for (int i = 0; i < layouts->size(); i++){
        layoutEntities.push_back(layouts->getEntity(i));
    }

    for (Entity entity : layoutEntities){
        UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity);
        if (!layout)
            continue;
        Signature signature = scene->getSignature(entity);
        if (!isLiveUIEntity(entity, signature))
            continue;

        Transform* transform = scene->findComponent<Transform>(entity);
        UIComponent* ui = scene->findComponent<UIComponent>(entity);

        if (transform->visible && signature.test(scene->getComponentId<ImageComponent>())){
            Rect uirect(transform->worldPosition.x, transform->worldPosition.y, layout->width * transform->worldScale.x, layout->height * transform->worldScale.y);

            if (layout->panel != NULL_ENTITY){
                uirect = fitOnPanel(uirect, layout->panel);
            }

            if (uirect.contains(Vector2(x, y)) && !layout->ignoreEvents){
                currentUIFromPointer = entity;
            }
        }

        if (signature.test(scene->getComponentId<ButtonComponent>())){
            ButtonComponent* button = scene->findComponent<ButtonComponent>(entity);
            if (button && !button->disabled && button->pressed){
                button->pressed = false;
                applyButtonVisual(*button, *ui);
                button->onRelease.call();

                transform = scene->findComponent<Transform>(entity);
                ui = scene->findComponent<UIComponent>(entity);
                if (!transform || !ui)
                    continue;
                signature = scene->getSignature(entity);
            }
        }

        if (signature.test(scene->getComponentId<ScrollbarComponent>())){
            scene->getComponent<ScrollbarComponent>(entity).barPointerDown = false;
        }

        if (signature.test(scene->getComponentId<PanelComponent>())){
            PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

            panel.headerPointerDown = false;
            panel.edgePointerDown = PanelEdge::NONE;
        }

        ui->onPointerUp(x - transform->worldPosition.x, y - transform->worldPosition.y);
    }

    if (isLiveUIEntity(lastUIFromPointer)){
        Signature signature = scene->getSignature(lastUIFromPointer);
        Transform* transform = scene->findComponent<Transform>(lastUIFromPointer);
        UIComponent* ui = scene->findComponent<UIComponent>(lastUIFromPointer);
        if (transform && ui){
            if (pointerDragging){
                ui->onDragEnd.call(x - transform->worldPosition.x, y - transform->worldPosition.y);
            }else if (!pointerInternalGesture && currentUIFromPointer == lastUIFromPointer){
                bool canClick = true;
                if (signature.test(scene->getComponentId<ButtonComponent>())){
                    ButtonComponent* button = scene->findComponent<ButtonComponent>(lastUIFromPointer);
                    canClick = button && !button->disabled;
                }

                if (canClick){
                    float localX = x - transform->worldPosition.x;
                    float localY = y - transform->worldPosition.y;
                    ui->onClick.call(localX, localY);

                    transform = scene->findComponent<Transform>(lastUIFromPointer);
                    ui = scene->findComponent<UIComponent>(lastUIFromPointer);
                    if (transform && ui){
                        double clickTime = Engine::getSystemTime();
                        if (lastUIFromClick == lastUIFromPointer && clickTime - lastClickTime <= UI_DOUBLE_CLICK_TIME){
                            ui->onDoubleClick.call(x - transform->worldPosition.x, y - transform->worldPosition.y);
                            lastUIFromClick = NULL_ENTITY;
                            lastClickTime = -1.0;
                        }else{
                            lastUIFromClick = lastUIFromPointer;
                            lastClickTime = clickTime;
                        }
                    }
                }
            }
        }
    }

    lastPanelFromPointer = NULL_ENTITY;
    pointerDragging = false;
    pointerInternalGesture = false;
    textEditSelecting = NULL_ENTITY;

    if (lastUIFromPointer != NULL_ENTITY){
        lastUIFromPointer = NULL_ENTITY;

        Engine::setMouseCursor(CursorType::ARROW);
        return true;
    }

    return false;
}

void UISystem::applyScrollbarPointerPosition(Entity entity, float x, float y){
    Transform* transform = scene->findComponent<Transform>(entity);
    UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity);
    ScrollbarComponent* scrollbar = scene->findComponent<ScrollbarComponent>(entity);
    if (!transform || !layout || !scrollbar || !scrollbar->barPointerDown || scrollbar->barSize >= 1.0)
        return;
    if (!scene->findComponent<Transform>(scrollbar->bar) || !scene->findComponent<UILayoutComponent>(scrollbar->bar))
        return;

    Entity bar = scrollbar->bar;

    float innerHeight = std::max(0.0f, (float)layout->height - scrollbar->barMarginTop - scrollbar->barMarginBottom);
    float innerWidth = std::max(0.0f, (float)layout->width - scrollbar->barMarginLeft - scrollbar->barMarginRight);

    float trackStartNorm = 0;
    float trackEndNorm = 1;
    float halfBarParent = 0;
    float pos = 0;

    if (scrollbar->type == ScrollbarType::VERTICAL){
        float barSizePixel = innerHeight * scrollbar->barSize;
        float barLocalY = (y - transform->worldPosition.y) / transform->worldScale.y;
        float posAlongInner = innerHeight > 0 ? (barLocalY - scrollbar->barMarginTop + (barSizePixel / 2.0) - scrollbar->barPointerPos) / innerHeight : 0;
        trackStartNorm = layout->height > 0 ? scrollbar->barMarginTop / (float)layout->height : 0;
        trackEndNorm = layout->height > 0 ? 1.0f - scrollbar->barMarginBottom / (float)layout->height : 1;
        halfBarParent = layout->height > 0 ? (barSizePixel / 2.0f) / layout->height : 0;
        pos = trackStartNorm + posAlongInner * (trackEndNorm - trackStartNorm);
    }else if (scrollbar->type == ScrollbarType::HORIZONTAL){
        float barSizePixel = innerWidth * scrollbar->barSize;
        float barLocalX = (x - transform->worldPosition.x) / transform->worldScale.x;
        float posAlongInner = innerWidth > 0 ? (barLocalX - scrollbar->barMarginLeft + (barSizePixel / 2.0) - scrollbar->barPointerPos) / innerWidth : 0;
        trackStartNorm = layout->width > 0 ? scrollbar->barMarginLeft / (float)layout->width : 0;
        trackEndNorm = layout->width > 0 ? 1.0f - scrollbar->barMarginRight / (float)layout->width : 1;
        halfBarParent = layout->width > 0 ? (barSizePixel / 2.0f) / layout->width : 0;
        pos = trackStartNorm + posAlongInner * (trackEndNorm - trackStartNorm);
    }

    float movableStart = trackStartNorm + halfBarParent;
    float movableEnd = trackEndNorm - halfBarParent;
    if (pos < movableStart){
        pos = movableStart;
    }else if (pos > movableEnd){
        pos = movableEnd;
    }

    float newStep = movableEnd > movableStart ? (pos - movableStart) / (movableEnd - movableStart) : 0;

    if (newStep != scrollbar->step){
        scrollbar->step = newStep;
        scrollbar->onChange.call(newStep);
        scrollbar = scene->findComponent<ScrollbarComponent>(entity);
    }

    UILayoutComponent* barlayout = scene->findComponent<UILayoutComponent>(bar);
    if (scrollbar && barlayout){
        if (scrollbar->type == ScrollbarType::VERTICAL){
            barlayout->anchorPointTop = pos;
            barlayout->anchorPointBottom = pos;
        }else if (scrollbar->type == ScrollbarType::HORIZONTAL){
            barlayout->anchorPointLeft = pos;
            barlayout->anchorPointRight = pos;
        }
    }
}

// Every user callback here can create or destroy entities, which invalidates the
// component references, so they are fetched again after each call
void UISystem::pointerMoveOnUI(Entity entity, float x, float y, Vector2 pointerDiff, CursorType& cursor){
    Transform* transform = scene->findComponent<Transform>(entity);
    UIComponent* ui = scene->findComponent<UIComponent>(entity);
    if (!transform || !ui)
        return;

    float localX = x - transform->worldPosition.x;
    float localY = y - transform->worldPosition.y;

    ui->onPointerMove.call(localX, localY);
    ui = scene->findComponent<UIComponent>(entity);
    if (!ui)
        return;
    ui->pointerMoved = true;

    if (!pointerInternalGesture){
        if (!pointerDragging && Vector2(x, y).squaredDistance(lastPointerDownPos) >= UI_DRAG_START_DISTANCE * UI_DRAG_START_DISTANCE){
            pointerDragging = true;
            ui->onDragStart.call(localX, localY);
            ui = scene->findComponent<UIComponent>(entity);
            if (!ui)
                return;
        }

        if (pointerDragging){
            ui->onDrag.call(localX, localY);
            ui = scene->findComponent<UIComponent>(entity);
            if (!ui)
                return;
        }

        if (textEditSelecting == entity){
            TextEditComponent* textedit = scene->findComponent<TextEditComponent>(entity);
            if (textedit && !textedit->disabled){
                TextComponent* text = scene->findComponent<TextComponent>(textedit->text);
                ImageComponent* img = scene->findComponent<ImageComponent>(entity);
                if (text && img){
                    setTextEditCursorFromLocalX(*textedit, *text, *img, localX, true);
                }
            }
        }
    }

    applyScrollbarPointerPosition(entity, x, y);

    transform = scene->findComponent<Transform>(entity);
    UILayoutComponent* layout = scene->findComponent<UILayoutComponent>(entity);
    PanelComponent* panel = scene->findComponent<PanelComponent>(entity);
    if (!transform || !layout || !panel)
        return;

    if (panel->headerPointerDown){
        transform->position += Vector3(pointerDiff.x / transform->worldScale.x, pointerDiff.y / transform->worldScale.y, 0);
        transform->needUpdate = true;

        panel->onMove.call();
        transform = scene->findComponent<Transform>(entity);
        layout = scene->findComponent<UILayoutComponent>(entity);
        panel = scene->findComponent<PanelComponent>(entity);
        if (!transform || !layout || !panel)
            return;
    }

    if (panel->edgePointerDown != PanelEdge::NONE){
        panelSizeAcc += Vector2(pointerDiff.x / transform->worldScale.x, pointerDiff.y / transform->worldScale.y);
        if (panel->edgePointerDown == PanelEdge::RIGHT){
            layout->width += (int)panelSizeAcc.x;
            layout->needUpdateSizes = true;
            cursor = CursorType::RESIZE_EW;
        }else if (panel->edgePointerDown == PanelEdge::RIGHT_BOTTOM){
            layout->width += (int)panelSizeAcc.x;
            layout->height += (int)panelSizeAcc.y;
            layout->needUpdateSizes = true;
            cursor = CursorType::RESIZE_NWSE;
        }else if (panel->edgePointerDown == PanelEdge::BOTTOM){
            layout->height += (int)panelSizeAcc.y;
            layout->needUpdateSizes = true;
            cursor = CursorType::RESIZE_NS;
        }else if (panel->edgePointerDown == PanelEdge::LEFT_BOTTOM){
            transform->position += Vector3(pointerDiff.x / transform->worldScale.x, 0, 0);
            transform->needUpdate = true;
            layout->width -= (int)panelSizeAcc.x;
            layout->height += (int)panelSizeAcc.y;
            layout->needUpdateSizes = true;
            cursor = CursorType::RESIZE_NESW;
        }else if (panel->edgePointerDown == PanelEdge::LEFT){
            transform->position += Vector3(pointerDiff.x / transform->worldScale.x, 0, 0);
            transform->needUpdate = true;
            layout->width -= (int)panelSizeAcc.x;
            layout->needUpdateSizes = true;
            cursor = CursorType::RESIZE_EW;
        }
        if (layout->width < panel->minWidth){
            layout->width = panel->minWidth;
        }
        if (layout->height < panel->minHeight){
            layout->height = panel->minHeight;
        }
        panelSizeAcc -= Vector2((int)panelSizeAcc.x, (int)panelSizeAcc.y);

        panel->onResize.call(layout->width, layout->height);
    }
}

bool UISystem::eventOnPointerMove(float x, float y){
    auto layouts = scene->getComponentArray<UILayoutComponent>();

    CursorType cursor = CursorType::ARROW;
    Entity currentUIFromPointerHover = NULL_ENTITY;

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
                        currentUIFromPointerHover = entity;
                        cursor = CursorType::ARROW;

                        if (signature.test(scene->getComponentId<TextEditComponent>())){
                            cursor = CursorType::IBEAM;

                        }else if (signature.test(scene->getComponentId<PanelComponent>())){
                            PanelComponent& panel = scene->getComponent<PanelComponent>(entity);

                            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
                            Transform& transform = scene->getComponent<Transform>(entity);
                            UILayoutComponent defaultHeaderLayout;
                            const UILayoutComponent& headerlayout = panel.headercontainer != NULL_ENTITY && scene->findComponent<UILayoutComponent>(panel.headercontainer)
                                ? scene->getComponent<UILayoutComponent>(panel.headercontainer)
                                : defaultHeaderLayout;

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

    if (currentUIFromPointerHover != lastUIFromPointerHover){
        if (isLiveUIEntity(lastUIFromPointerHover)){
            Signature signature = scene->getSignature(lastUIFromPointerHover);
            Transform* transform = scene->findComponent<Transform>(lastUIFromPointerHover);
            UIComponent* ui = scene->findComponent<UIComponent>(lastUIFromPointerHover);
            if (transform && ui){
                if (signature.test(scene->getComponentId<ButtonComponent>())){
                    ButtonComponent* button = scene->findComponent<ButtonComponent>(lastUIFromPointerHover);
                    if (button){
                        button->hovered = false;
                        applyButtonVisual(*button, *ui);
                    }
                }

                ui->onPointerLeave.call(x - transform->worldPosition.x, y - transform->worldPosition.y);
            }
        }

        if (isLiveUIEntity(currentUIFromPointerHover)){
            Signature signature = scene->getSignature(currentUIFromPointerHover);
            Transform* transform = scene->findComponent<Transform>(currentUIFromPointerHover);
            UIComponent* ui = scene->findComponent<UIComponent>(currentUIFromPointerHover);
            if (transform && ui){
                if (signature.test(scene->getComponentId<ButtonComponent>())){
                    ButtonComponent* button = scene->findComponent<ButtonComponent>(currentUIFromPointerHover);
                    if (button){
                        button->hovered = true;
                        applyButtonVisual(*button, *ui);
                    }
                }

                ui->onPointerEnter.call(x - transform->worldPosition.x, y - transform->worldPosition.y);
            }
        }

        lastUIFromPointerHover = currentUIFromPointerHover;
    }

    if (isLiveUIEntity(lastUIFromPointer)){
        pointerMoveOnUI(lastUIFromPointer, x, y, Vector2(x, y) - lastPointerPos, cursor);
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


void UISystem::onComponentRemoved(Entity entity, ComponentId componentId) {
    if (componentId == scene->getComponentId<UIComponent>() ||
        componentId == scene->getComponentId<UILayoutComponent>() ||
        componentId == scene->getComponentId<ImageComponent>() ||
        componentId == scene->getComponentId<Transform>()) {
        if (entity == lastUIFromPointer) {
            lastUIFromPointer = NULL_ENTITY;
            pointerDragging = false;
            pointerInternalGesture = false;
        }
        if (entity == lastUIFromPointerHover) {
            lastUIFromPointerHover = NULL_ENTITY;
        }
        if (entity == lastUIFromClick) {
            lastUIFromClick = NULL_ENTITY;
            lastClickTime = -1.0;
        }
    }

    if (componentId == scene->getComponentId<ButtonComponent>()) {
        ButtonComponent& button = scene->getComponent<ButtonComponent>(entity);
        if (button.label != NULL_ENTITY){
            if (isEntityChild(button.label, entity)){
                scene->destroyEntity(button.label);
            }
            button.label = NULL_ENTITY;
        }
        destroyButton(button);
    } else if (componentId == scene->getComponentId<PanelComponent>()) {
        PanelComponent& panel = scene->getComponent<PanelComponent>(entity);
        if (panel.headertext != NULL_ENTITY){
            if (isEntityChild(panel.headertext, entity)){
                scene->destroyEntity(panel.headertext);
            }
            panel.headertext = NULL_ENTITY;
        }
        if (panel.headercontainer != NULL_ENTITY){
            if (isEntityChild(panel.headercontainer, entity)){
                scene->destroyEntity(panel.headercontainer);
            }
            panel.headercontainer = NULL_ENTITY;
        }
        if (panel.headerimage != NULL_ENTITY){
            if (isEntityChild(panel.headerimage, entity)){
                scene->destroyEntity(panel.headerimage);
            }
            panel.headerimage = NULL_ENTITY;
        }
    } else if (componentId == scene->getComponentId<ScrollbarComponent>()) {
        ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(entity);
        if (scrollbar.bar != NULL_ENTITY){
            if (isEntityChild(scrollbar.bar, entity)){
                scene->destroyEntity(scrollbar.bar);
            }
            scrollbar.bar = NULL_ENTITY;
        }
    } else if (componentId == scene->getComponentId<ProgressbarComponent>()) {
        ProgressbarComponent& progressbar = scene->getComponent<ProgressbarComponent>(entity);
        if (progressbar.fill != NULL_ENTITY){
            if (isEntityChild(progressbar.fill, entity)){
                scene->destroyEntity(progressbar.fill);
            }
            progressbar.fill = NULL_ENTITY;
        }
    } else if (componentId == scene->getComponentId<TextEditComponent>()) {
        TextEditComponent& textedit = scene->getComponent<TextEditComponent>(entity);
        if (textedit.text != NULL_ENTITY){
            if (isEntityChild(textedit.text, entity)){
                scene->destroyEntity(textedit.text);
            }
            textedit.text = NULL_ENTITY;
        }
        if (textedit.selection != NULL_ENTITY){
            if (isEntityChild(textedit.selection, entity)){
                scene->destroyEntity(textedit.selection);
            }
            textedit.selection = NULL_ENTITY;
        }
        if (textedit.cursor != NULL_ENTITY){
            if (isEntityChild(textedit.cursor, entity)){
                scene->destroyEntity(textedit.cursor);
            }
            textedit.cursor = NULL_ENTITY;
        }
    } else if (componentId == scene->getComponentId<TextComponent>()) {
        TextComponent& text = scene->getComponent<TextComponent>(entity);
        destroyText(text);
    }
}
