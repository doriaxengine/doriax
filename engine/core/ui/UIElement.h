//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_UI_ELEMENT_H
#define DORIAX_UI_ELEMENT_H

#include "Export.h"
#include "math/Vector2.h"
#include "math/Vector4.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace doriax {

    enum class UIElementType {
        Container,
        Panel,
        Button,
        Text,
        Image,
        InputField,
        Slider,
        Toggle,
        Scrollview,
        Grid,
        Custom
    };

    enum class UIHorizontalAlign {
        Left,
        Center,
        Right,
        Stretch
    };

    enum class UIVerticalAlign {
        Top,
        Center,
        Bottom,
        Stretch
    };

    struct UIMargin {
        float left = 0.0f;
        float right = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
    };

    struct UISize {
        float width = 0.0f;
        float height = 0.0f;
        bool widthPercent = false;   // If true, width is % of parent
        bool heightPercent = false;  // If true, height is % of parent
    };

    struct UIRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool containsPoint(float px, float py) const {
            return px >= x && px < x + width && py >= y && py < y + height;
        }
    };

    // Base UI element
    class DORIAX_API UIElement : public std::enable_shared_from_this<UIElement> {
    public:
        using Ptr = std::shared_ptr<UIElement>;
        using Callback = std::function<void()>;

        UIElement(const std::string& name, UIElementType type = UIElementType::Container);
        virtual ~UIElement();

        const std::string& name() const;
        UIElementType type() const;

        // Hierarchy
        void addChild(UIElement::Ptr child);
        void removeChild(UIElement::Ptr child);
        void removeChildByName(const std::string& name);
        UIElement::Ptr getChild(const std::string& name);
        const std::vector<UIElement::Ptr>& getChildren() const;
        UIElement::Ptr getParent() const;

        // Layout properties
        void setPosition(float x, float y);
        void setSize(float width, float height, bool percentW = false, bool percentH = false);
        void setMargin(float left, float right, float top, float bottom);
        void setHorizontalAlign(UIHorizontalAlign align);
        void setVerticalAlign(UIVerticalAlign align);

        Vector2 getPosition() const;
        UISize getSize() const;
        UIMargin getMargin() const;
        UIRect getRect() const;  // Computed rect after layout

        // Visibility and interactivity
        void setVisible(bool visible);
        bool isVisible() const;

        void setInteractive(bool interactive);
        bool isInteractive() const;

        // State
        void setEnabled(bool enabled);
        bool isEnabled() const;

        void setFocused(bool focused);
        bool isFocused() const;

        // Events
        int onClicked(Callback callback);
        int onHover(Callback callback);
        int onUnhover(Callback callback);
        void removeCallback(int callbackId);

        // Compute final layout (called by parent or layout manager)
        virtual void computeLayout(const UIRect& parentRect);

        // Input handling
        virtual bool handleMouseClick(float x, float y);
        virtual bool handleMouseMove(float x, float y);
        virtual bool handleKeyInput(const std::string& key);

        // Styling
        void setBackgroundColor(Vector4 color);
        void setBorderColor(Vector4 color);
        void setBorderWidth(float width);
        Vector4 getBackgroundColor() const;
        Vector4 getBorderColor() const;
        float getBorderWidth() const;

        // Serialization
        virtual void setProperty(const std::string& key, const std::string& value);
        virtual std::string getProperty(const std::string& key) const;

    protected:
        std::string name_;
        UIElementType type_;
        UIElement::Ptr parent_;
        std::vector<UIElement::Ptr> children_;

        // Layout
        Vector2 position_;
        UISize size_;
        UIMargin margin_;
        UIHorizontalAlign hAlign_ = UIHorizontalAlign::Left;
        UIVerticalAlign vAlign_ = UIVerticalAlign::Top;
        UIRect computedRect_;

        // State
        bool visible_ = true;
        bool interactive_ = true;
        bool enabled_ = true;
        bool focused_ = false;
        bool hovering_ = false;

        // Style
        Vector4 backgroundColor_ = Vector4(0.2f, 0.2f, 0.2f, 1.0f);
        Vector4 borderColor_ = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        float borderWidth_ = 0.0f;

        // Events
        std::unordered_map<int, Callback> clickCallbacks_;
        std::unordered_map<int, Callback> hoverCallbacks_;
        std::unordered_map<int, Callback> unhoverCallbacks_;
        int nextCallbackId_ = 0;

        void notifyClick();
        void notifyHover();
        void notifyUnhover();
    };

}

#endif // DORIAX_UI_ELEMENT_H
