// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef UISYSTEM_H
#define UISYSTEM_H

#include "SubSystem.h"
#include "System.h"

#include <vector>

#include "component/UILayoutComponent.h"
#include "component/UIContainerComponent.h"
#include "component/TextComponent.h"
#include "component/ImageComponent.h"
#include "component/UIComponent.h"
#include "component/ButtonComponent.h"
#include "component/PanelComponent.h"
#include "component/ScrollbarComponent.h"
#include "component/ProgressbarComponent.h"
#include "component/PolygonComponent.h"
#include "component/TextEditComponent.h"
#include "component/Transform.h"
#include "component/CameraComponent.h"

namespace doriax{

    class DORIAX_API UISystem : public SubSystem {

    private:

        std::string eventId;
        Entity lastUIFromPointer;
        Entity lastUIFromPointerHover;
        Entity lastUIFromClick;
        Entity lastPanelFromPointer;
        Entity textEditSelecting;
        Vector2 lastPointerDownPos;
        Vector2 lastPointerPos;
        Vector2 panelSizeAcc; // to add accumulation from float to int
        bool pointerDragging;
        bool pointerInternalGesture; // gesture captured by a built-in widget (panel move/resize, scrollbar)
        double lastClickTime; // monotonic wall-clock seconds (Engine::getSystemTime)

        int anchorReferenceWidth;
        int anchorReferenceHeight;

        bool pendingImages; // an image waited for its texture in the last update
        uint32_t pendingHighSurrogate; // first half of a character past U+FFFF

        void createOrUpdateUiComponent(UILayoutComponent& layout, Entity entity, Signature signature);
        void getPanelEdges(const PanelComponent& panel, const UILayoutComponent& layout, const Transform& transform, const UILayoutComponent& headerlayout,  Rect& edgeRight, Rect& edgeRightBottom, Rect& edgeBottom, Rect& edgeLeftBottom, Rect& edgeLeft);
        Rect fitOnPanel(Rect uiRect, Entity parentPanel);
        void calculateUIAABB(UIComponent& ui);

        //Image
        bool createImagePatches(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        // Text
        std::string getFontId(const TextComponent& text) const;
        std::string getFontTextureId(const TextComponent& text) const;
        bool isFontAtlasStale(const TextComponent& text) const;
        void syncFontAtlas(TextComponent& text, UIComponent& ui);
        bool loadFontAtlas(TextComponent& text, UIComponent& ui, UILayoutComponent& layout);
        void createText(TextComponent& text, UIComponent& ui, UILayoutComponent& layout);

        //Button
        void applyButtonVisual(Entity entity, ButtonComponent& button, UIComponent& ui, bool transition);
        void updateButtonTransition(double dt, Entity entity, ButtonComponent& button, UIComponent& ui);
        void updateButton(Entity entity, ButtonComponent& button, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //Panel
        void updatePanel(Entity entity, PanelComponent& panel, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //Scrollbar
        void updateScrollbar(ScrollbarComponent& scrollbar, UILayoutComponent& layout);

        //Progressbar
        void updateProgressbar(Entity entity, ProgressbarComponent& progressbar, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //TextEdit
        void updateTextEdit(Entity entity, TextEditComponent& textedit, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);
        void blinkCursorTextEdit(double dt, TextEditComponent& textedit, UIComponent& ui);
        bool handleTextEditCharInput(TextEditComponent& textedit, TextComponent& text, UIComponent& ui, uint32_t codepoint);
        bool handleTextEditKeyDown(TextEditComponent& textedit, TextComponent& text, UIComponent& ui, int key, bool repeat, int mods);
        void setTextEditCursorFromLocalX(TextEditComponent& textedit, TextComponent& text, ImageComponent& img, float localX, bool extendSelection);
        bool textEditHasSelection(const TextEditComponent& textedit) const;
        int textEditSelectionStart(const TextEditComponent& textedit) const;
        int textEditSelectionEnd(const TextEditComponent& textedit) const;
        void textEditClampCursor(TextEditComponent& textedit, size_t numCodepoints) const;
        void textEditSelectionRects(const TextComponent& text, int start, int end, std::vector<Rect>& rects) const;
        //one caret stop to the left or right on screen, not in the string
        int textEditVisualStep(const TextComponent& text, int cursorIndex, int direction) const;
        float textEditCursorPixelX(const TextComponent& text, int cursorIndex) const;
        int textEditFindCursorIndexFromX(const TextComponent& text, float x) const;
        std::string textEditMaskText(const std::string& text, char maskChar) const;
        void textEditDeleteSelection(TextEditComponent& textedit, TextComponent& text);
        void textEditResetBlink(TextEditComponent& textedit) const;
        TextEditComponent* findTextEditForTextChild(Entity textEntity) const;

        //UI Polygon
        void createUIPolygon(PolygonComponent& polygon, UIComponent& ui, UILayoutComponent& layout);

        // True when the entity is alive and still usable as a UI event target
        bool isLiveUIEntity(Entity entity) const;
        bool isLiveUIEntity(Entity entity, Signature signature) const;

        void pointerDownOnUI(Entity entity, float x, float y);
        void pointerMoveOnUI(Entity entity, float x, float y, Vector2 pointerDiff, CursorType& cursor);
        void applyScrollbarPointerPosition(Entity entity, float x, float y);

        bool isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout);
        bool isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout, Vector2 center);

        static Rect transformRectToWorldAxisAligned(const Rect& rect, const Matrix4& matrix);
        Entity findNearestLayoutParent(Entity startParent) const;
        Vector3 mapLayoutPointToParentLocalSpace(float posX, float posY, Entity layoutParent, Entity targetParent) const;
        Vector3 mapParentLocalPointToLayoutSpace(float posX, float posY, Entity layoutParent, Entity sourceParent) const;
        void convertLayoutSpaceToLocalParentSpace(Entity transformParent, Entity layoutParent, float& posX, float& posY) const;
        void convertLocalParentSpaceToLayoutSpace(Entity transformParent, Entity layoutParent, float& posX, float& posY) const;

        void applyAnchorPreset(UILayoutComponent& layout);
        void changeFlipY(UIComponent& ui, CameraComponent& camera);

        void destroyText(TextComponent& text);
        void destroyButton(ButtonComponent& button);

        bool isEntityChild(Entity child, Entity parent) const;

    public:
        // An anchored element's offset is measured in its nearest layout parent's space,
        // its position in its own parent's space
        Vector2 parentToLayoutSpace(Entity entity, const Vector2& point) const;
        Vector2 layoutToParentSpace(Entity entity, const Vector2& point) const;

        UISystem(Scene* scene);
        virtual ~UISystem();

        Rect getAnchorReferenceRect(const UILayoutComponent& layout, const Transform& transform, bool worldPosition);

        void setAnchorReferenceSize(int width, int height);
        void clearAnchorReferenceSize();
        int getAnchorReferenceWidth() const;
        int getAnchorReferenceHeight() const;

        bool isTextEditFocused();

        // an image is still waiting for its texture to build its geometry
        bool hasPendingImages() const;

        Vector2 getTextMinSize(TextComponent& text);

        void resetButtonStates();

        bool eventOnCharInput(wchar_t input);
        bool eventOnKeyDown(int key, bool repeat, int mods);
        bool eventOnPointerDown(float x, float y);
        bool eventOnPointerUp(float x, float y);
        bool eventOnPointerMove(float x, float y);

        // basic UIs
        bool createOrUpdatePolygon(PolygonComponent& polygon, UIComponent& ui, UILayoutComponent& layout);
        bool createOrUpdateImage(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);
        bool createOrUpdateText(TextComponent& text, UIComponent& ui, UILayoutComponent& layout);

        // advanced UIs
        void createButtonObjects(Entity entity, ButtonComponent& button);
        void createPanelObjects(Entity entity, PanelComponent& panel);
        void createScrollbarObjects(Entity entity, ScrollbarComponent& scrollbar);
        void createProgressbarObjects(Entity entity, ProgressbarComponent& progressbar);
        void createTextEditObjects(Entity entity, TextEditComponent& textedit);

        void load() override;
        void draw() override;
        void destroy() override;
        void update(double dt) override;

        void onComponentRemoved(Entity entity, ComponentId componentId) override;
    };

}

#endif //UISYSTEM_H