//
// (c) 2026 Eduardo Doria.
//

#ifndef UISYSTEM_H
#define UISYSTEM_H

#include "SubSystem.h"

#include "component/UILayoutComponent.h"
#include "component/UIContainerComponent.h"
#include "component/TextComponent.h"
#include "component/ImageComponent.h"
#include "component/UIComponent.h"
#include "component/ButtonComponent.h"
#include "component/PanelComponent.h"
#include "component/ScrollbarComponent.h"
#include "component/PolygonComponent.h"
#include "component/TextEditComponent.h"
#include "component/Transform.h"
#include "component/CameraComponent.h"

namespace doriax{

    class DORIAX_API UISystem : public SubSystem {

    private:

        std::string eventId;
        Entity lastUIFromPointer;
        Entity lastPanelFromPointer;
        Vector2 lastPointerPos;
        Vector2 panelSizeAcc; // to add accumulation from float to int

        int anchorReferenceWidth;
        int anchorReferenceHeight;

        void createOrUpdateUiComponent(double dt, UILayoutComponent& layout, Entity entity, Signature signature);
        void getPanelEdges(const PanelComponent& panel, const UILayoutComponent& layout, const Transform& transform, const UILayoutComponent& headerlayout,  Rect& edgeRight, Rect& edgeRightBottom, Rect& edgeBottom, Rect& edgeLeftBottom, Rect& edgeLeft);
        Rect fitOnPanel(Rect uiRect, Entity parentPanel);
        void calculateUIAABB(UIComponent& ui);

        //Image
        bool createImagePatches(ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        // Text
        bool loadFontAtlas(TextComponent& text, UIComponent& ui, UILayoutComponent& layout);
        void createText(TextComponent& text, UIComponent& ui, UILayoutComponent& layout);

        //Button
        void updateButton(Entity entity, ButtonComponent& button, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //Panel
        void updatePanel(Entity entity, PanelComponent& panel, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //Scrollbar
        void updateScrollbar(Entity entity, ScrollbarComponent& scrollbar, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);

        //TextEdit
        void updateTextEdit(Entity entity, TextEditComponent& textedit, ImageComponent& img, UIComponent& ui, UILayoutComponent& layout);
        void blinkCursorTextEdit(double dt, TextEditComponent& textedit, UIComponent& ui);

        //UI Polygon
        void createUIPolygon(PolygonComponent& polygon, UIComponent& ui, UILayoutComponent& layout);

        bool isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout);
        bool isCoordInside(float x, float y, Transform& transform, UILayoutComponent& layout, Vector2 center);

        void applyAnchorPreset(UILayoutComponent& layout);
        void changeFlipY(UIComponent& ui, CameraComponent& camera);

        void destroyText(TextComponent& text);
        void destroyButton(ButtonComponent& button);
        void destroyPanel(PanelComponent& panel);
        void destroyScrollbar(ScrollbarComponent& scrollbar);
        void destroyTextEdit(TextEditComponent& textedit);

    public:
        UISystem(Scene* scene);
        virtual ~UISystem();

        Rect getAnchorReferenceRect(const UILayoutComponent& layout, const Transform& transform, bool worldPosition);

        void setAnchorReferenceSize(int width, int height);
        void clearAnchorReferenceSize();
        int getAnchorReferenceWidth() const;
        int getAnchorReferenceHeight() const;

        bool isTextEditFocused();

        Vector2 getTextMinSize(TextComponent& text);

        bool eventOnCharInput(wchar_t codepoint);
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
        void createTextEditObjects(Entity entity, TextEditComponent& textedit);

        void load() override;
        void draw() override;
        void destroy() override;
        void update(double dt) override;

        void onComponentAdded(Entity entity, ComponentId componentId) override;
        void onComponentRemoved(Entity entity, ComponentId componentId) override;
    };

}

#endif //UISYSTEM_H