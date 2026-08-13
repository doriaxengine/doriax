//
// (c) 2026 Eduardo Doria.
//

#include "ui/UICanvas.h"
#include "Log.h"

namespace doriax {

    // UICanvas Implementation

    UICanvas::Ptr UICanvas::create(const std::string& name) {
        return std::make_shared<UICanvas>(name);
    }

    UICanvas::UICanvas(const std::string& name) : name_(name) {
    }

    const std::string& UICanvas::name() const {
        return name_;
    }

    void UICanvas::addRootElement(UIElement::Ptr element) {
        if (!element) return;
        rootElement_ = element;
    }

    void UICanvas::removeRootElement(UIElement::Ptr element) {
        if (rootElement_ == element) {
            rootElement_.reset();
        }
    }

    UIElement::Ptr UICanvas::getRootElement() const {
        return rootElement_;
    }

    UIElement::Ptr UICanvas::findElement(const std::string& name) {
        if (!rootElement_) return nullptr;
        return findElementRecursive(rootElement_, name);
    }

    UIElement::Ptr UICanvas::findElementRecursive(UIElement::Ptr element, const std::string& name) {
        if (!element) return nullptr;

        if (element->name() == name) {
            return element;
        }

        for (const auto& child : element->getChildren()) {
            auto found = findElementRecursive(child, name);
            if (found) return found;
        }

        return nullptr;
    }

    void UICanvas::setSize(float width, float height) {
        size_ = Vector2(width, height);
    }

    Vector2 UICanvas::getSize() const {
        return size_;
    }

    void UICanvas::setPosition(float x, float y) {
        position_ = Vector2(x, y);
    }

    Vector2 UICanvas::getPosition() const {
        return position_;
    }

    void UICanvas::processLayout() {
        if (!rootElement_) return;

        UIRect canvasRect;
        canvasRect.x = position_.x;
        canvasRect.y = position_.y;
        canvasRect.width = size_.x;
        canvasRect.height = size_.y;

        rootElement_->computeLayout(canvasRect);
    }

    void UICanvas::handleMouseClick(float x, float y) {
        if (!visible_ || !rootElement_) return;
        rootElement_->handleMouseClick(x, y);
    }

    void UICanvas::handleMouseMove(float x, float y) {
        if (!visible_ || !rootElement_) return;
        rootElement_->handleMouseMove(x, y);
    }

    void UICanvas::handleKeyInput(const std::string& key) {
        if (!visible_ || !focusedElement_) return;
        focusedElement_->handleKeyInput(key);
    }

    void UICanvas::setFocusedElement(UIElement::Ptr element) {
        if (focusedElement_) {
            focusedElement_->setFocused(false);
        }
        focusedElement_ = element;
        if (focusedElement_) {
            focusedElement_->setFocused(true);
        }
    }

    UIElement::Ptr UICanvas::getFocusedElement() const {
        return focusedElement_;
    }

    void UICanvas::setVisible(bool visible) {
        visible_ = visible;
    }

    bool UICanvas::isVisible() const {
        return visible_;
    }

    void UICanvas::clear() {
        rootElement_.reset();
        focusedElement_.reset();
    }

    void UICanvas::update(float deltaTime) {
        // Placeholder for future animation/transition logic
        (void)deltaTime;
    }

    // UICanvasManager Implementation

    UICanvasManager& UICanvasManager::get() {
        static UICanvasManager manager;
        return manager;
    }

    UICanvas::Ptr UICanvasManager::createCanvas(const std::string& name) {
        auto canvas = UICanvas::create(name);
        canvases_[name] = canvas;
        Log::info("UICanvasManager: created canvas '%s'", name.c_str());
        return canvas;
    }

    UICanvas::Ptr UICanvasManager::getCanvas(const std::string& name) {
        auto it = canvases_.find(name);
        return it != canvases_.end() ? it->second : nullptr;
    }

    void UICanvasManager::removeCanvas(const std::string& name) {
        canvases_.erase(name);
        if (activeCanvasName_ == name) {
            activeCanvasName_.clear();
        }
    }

    UICanvas::Ptr UICanvasManager::getActiveCanvas() {
        return activeCanvasName_.empty() ? nullptr : getCanvas(activeCanvasName_);
    }

    void UICanvasManager::setActiveCanvas(const std::string& name) {
        if (canvases_.find(name) != canvases_.end()) {
            activeCanvasName_ = name;
            Log::info("UICanvasManager: activated canvas '%s'", name.c_str());
        } else {
            Log::error("UICanvasManager: canvas '%s' not found", name.c_str());
        }
    }

    std::vector<std::string> UICanvasManager::listCanvases() const {
        std::vector<std::string> result;
        result.reserve(canvases_.size());
        for (const auto& [name, canvas] : canvases_) {
            (void)canvas;
            result.push_back(name);
        }
        return result;
    }

    void UICanvasManager::handleMouseClick(float x, float y) {
        auto active = getActiveCanvas();
        if (active) {
            active->handleMouseClick(x, y);
        }
    }

    void UICanvasManager::handleMouseMove(float x, float y) {
        auto active = getActiveCanvas();
        if (active) {
            active->handleMouseMove(x, y);
        }
    }

    void UICanvasManager::handleKeyInput(const std::string& key) {
        auto active = getActiveCanvas();
        if (active) {
            active->handleKeyInput(key);
        }
    }

    void UICanvasManager::update(float deltaTime) {
        for (auto& [name, canvas] : canvases_) {
            (void)name;
            if (canvas && canvas->isVisible()) {
                canvas->update(deltaTime);
            }
        }
    }

    void UICanvasManager::clear() {
        canvases_.clear();
        activeCanvasName_.clear();
    }

}
