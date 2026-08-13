//
// (c) 2026 Eduardo Doria.
//

#include "ui/UIElement.h"
#include "Log.h"
#include <algorithm>

namespace doriax {

    UIElement::UIElement(const std::string& name, UIElementType type)
        : name_(name), type_(type) {
    }

    UIElement::~UIElement() = default;

    const std::string& UIElement::name() const {
        return name_;
    }

    UIElementType UIElement::type() const {
        return type_;
    }

    void UIElement::addChild(UIElement::Ptr child) {
        if (!child) return;
        child->parent_ = shared_from_this();
        children_.push_back(child);
    }

    void UIElement::removeChild(UIElement::Ptr child) {
        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            children_.erase(it);
            child->parent_.reset();
        }
    }

    void UIElement::removeChildByName(const std::string& name) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [&name](const UIElement::Ptr& child) { return child->name() == name; }
        );
        if (it != children_.end()) {
            children_.erase(it);
        }
    }

    UIElement::Ptr UIElement::getChild(const std::string& name) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [&name](const UIElement::Ptr& child) { return child->name() == name; }
        );
        return it != children_.end() ? *it : nullptr;
    }

    const std::vector<UIElement::Ptr>& UIElement::getChildren() const {
        return children_;
    }

    UIElement::Ptr UIElement::getParent() const {
        return parent_;
    }

    void UIElement::setPosition(float x, float y) {
        position_ = Vector2(x, y);
    }

    void UIElement::setSize(float width, float height, bool percentW, bool percentH) {
        size_.width = width;
        size_.height = height;
        size_.widthPercent = percentW;
        size_.heightPercent = percentH;
    }

    void UIElement::setMargin(float left, float right, float top, float bottom) {
        margin_.left = left;
        margin_.right = right;
        margin_.top = top;
        margin_.bottom = bottom;
    }

    void UIElement::setHorizontalAlign(UIHorizontalAlign align) {
        hAlign_ = align;
    }

    void UIElement::setVerticalAlign(UIVerticalAlign align) {
        vAlign_ = align;
    }

    Vector2 UIElement::getPosition() const {
        return position_;
    }

    UISize UIElement::getSize() const {
        return size_;
    }

    UIMargin UIElement::getMargin() const {
        return margin_;
    }

    UIRect UIElement::getRect() const {
        return computedRect_;
    }

    void UIElement::setVisible(bool visible) {
        visible_ = visible;
    }

    bool UIElement::isVisible() const {
        return visible_;
    }

    void UIElement::setInteractive(bool interactive) {
        interactive_ = interactive;
    }

    bool UIElement::isInteractive() const {
        return interactive_;
    }

    void UIElement::setEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool UIElement::isEnabled() const {
        return enabled_;
    }

    void UIElement::setFocused(bool focused) {
        focused_ = focused;
    }

    bool UIElement::isFocused() const {
        return focused_;
    }

    int UIElement::onClicked(Callback callback) {
        int id = nextCallbackId_++;
        clickCallbacks_[id] = callback;
        return id;
    }

    int UIElement::onHover(Callback callback) {
        int id = nextCallbackId_++;
        hoverCallbacks_[id] = callback;
        return id;
    }

    int UIElement::onUnhover(Callback callback) {
        int id = nextCallbackId_++;
        unhoverCallbacks_[id] = callback;
        return id;
    }

    void UIElement::removeCallback(int callbackId) {
        clickCallbacks_.erase(callbackId);
        hoverCallbacks_.erase(callbackId);
        unhoverCallbacks_.erase(callbackId);
    }

    void UIElement::computeLayout(const UIRect& parentRect) {
        // Compute width
        if (size_.widthPercent) {
            computedRect_.width = (parentRect.width * size_.width) / 100.0f;
        } else {
            computedRect_.width = size_.width > 0 ? size_.width : parentRect.width;
        }

        // Compute height
        if (size_.heightPercent) {
            computedRect_.height = (parentRect.height * size_.height) / 100.0f;
        } else {
            computedRect_.height = size_.height > 0 ? size_.height : parentRect.height;
        }

        // Apply horizontal alignment
        switch (hAlign_) {
            case UIHorizontalAlign::Left:
                computedRect_.x = parentRect.x + position_.x + margin_.left;
                break;
            case UIHorizontalAlign::Center:
                computedRect_.x = parentRect.x + (parentRect.width - computedRect_.width) / 2.0f + position_.x;
                break;
            case UIHorizontalAlign::Right:
                computedRect_.x = parentRect.x + parentRect.width - computedRect_.width - margin_.right + position_.x;
                break;
            case UIHorizontalAlign::Stretch:
                computedRect_.x = parentRect.x + margin_.left;
                computedRect_.width = parentRect.width - margin_.left - margin_.right;
                break;
        }

        // Apply vertical alignment
        switch (vAlign_) {
            case UIVerticalAlign::Top:
                computedRect_.y = parentRect.y + position_.y + margin_.top;
                break;
            case UIVerticalAlign::Center:
                computedRect_.y = parentRect.y + (parentRect.height - computedRect_.height) / 2.0f + position_.y;
                break;
            case UIVerticalAlign::Bottom:
                computedRect_.y = parentRect.y + parentRect.height - computedRect_.height - margin_.bottom + position_.y;
                break;
            case UIVerticalAlign::Stretch:
                computedRect_.y = parentRect.y + margin_.top;
                computedRect_.height = parentRect.height - margin_.top - margin_.bottom;
                break;
        }

        // Recursively compute layout for children
        for (auto& child : children_) {
            child->computeLayout(computedRect_);
        }
    }

    bool UIElement::handleMouseClick(float x, float y) {
        if (!visible_ || !interactive_ || !enabled_) {
            return false;
        }

        // Check if click is within bounds
        if (computedRect_.containsPoint(x, y)) {
            // First, try to handle in children
            for (auto& child : children_) {
                if (child->handleMouseClick(x, y)) {
                    return true;
                }
            }

            // If no child handled it, handle it ourselves
            notifyClick();
            return true;
        }

        return false;
    }

    bool UIElement::handleMouseMove(float x, float y) {
        if (!visible_ || !interactive_) {
            return false;
        }

        bool isInBounds = computedRect_.containsPoint(x, y);

        if (isInBounds && !hovering_) {
            hovering_ = true;
            notifyHover();
        } else if (!isInBounds && hovering_) {
            hovering_ = false;
            notifyUnhover();
        }

        // Propagate to children
        for (auto& child : children_) {
            child->handleMouseMove(x, y);
        }

        return isInBounds;
    }

    bool UIElement::handleKeyInput(const std::string& key) {
        if (!focused_ || !enabled_) {
            return false;
        }
        // Override in derived classes
        return false;
    }

    void UIElement::setBackgroundColor(Vector4 color) {
        backgroundColor_ = color;
    }

    void UIElement::setBorderColor(Vector4 color) {
        borderColor_ = color;
    }

    void UIElement::setBorderWidth(float width) {
        borderWidth_ = width;
    }

    Vector4 UIElement::getBackgroundColor() const {
        return backgroundColor_;
    }

    Vector4 UIElement::getBorderColor() const {
        return borderColor_;
    }

    float UIElement::getBorderWidth() const {
        return borderWidth_;
    }

    void UIElement::setProperty(const std::string& key, const std::string& value) {
        // Base implementation - override in derived classes
    }

    std::string UIElement::getProperty(const std::string& key) const {
        return "";
    }

    void UIElement::notifyClick() {
        for (const auto& [id, callback] : clickCallbacks_) {
            (void)id;
            try {
                callback();
            } catch (const std::exception& e) {
                Log::error("UIElement click callback error: %s", e.what());
            }
        }
    }

    void UIElement::notifyHover() {
        for (const auto& [id, callback] : hoverCallbacks_) {
            (void)id;
            try {
                callback();
            } catch (const std::exception& e) {
                Log::error("UIElement hover callback error: %s", e.what());
            }
        }
    }

    void UIElement::notifyUnhover() {
        for (const auto& [id, callback] : unhoverCallbacks_) {
            (void)id;
            try {
                callback();
            } catch (const std::exception& e) {
                Log::error("UIElement unhover callback error: %s", e.what());
            }
        }
    }

}
