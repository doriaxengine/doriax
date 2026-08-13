//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_UI_CANVAS_H
#define DORIAX_UI_CANVAS_H

#include "Export.h"
#include "ui/UIElement.h"
#include "math/Vector2.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace doriax {

    // UICanvas manages a hierarchy of UI elements
    // Handles layout computation, input dispatch, and rendering integration
    class DORIAX_API UICanvas {
    public:
        using Ptr = std::shared_ptr<UICanvas>;

        static Ptr create(const std::string& name);

        explicit UICanvas(const std::string& name);

        const std::string& name() const;

        // Add root element to canvas
        void addRootElement(UIElement::Ptr element);

        // Remove root element
        void removeRootElement(UIElement::Ptr element);

        // Get root element
        UIElement::Ptr getRootElement() const;

        // Find element by name (recursive search)
        UIElement::Ptr findElement(const std::string& name);

        // Set canvas size (used for layout computation)
        void setSize(float width, float height);
        Vector2 getSize() const;

        // Set canvas position offset
        void setPosition(float x, float y);
        Vector2 getPosition() const;

        // Process layout - recompute all element positions and sizes
        void processLayout();

        // Input handling
        void handleMouseClick(float x, float y);
        void handleMouseMove(float x, float y);
        void handleKeyInput(const std::string& key);

        // Focus management
        void setFocusedElement(UIElement::Ptr element);
        UIElement::Ptr getFocusedElement() const;

        // Set/get visible
        void setVisible(bool visible);
        bool isVisible() const;

        // Clear all elements
        void clear();

        // Update
        void update(float deltaTime);

    private:
        std::string name_;
        UIElement::Ptr rootElement_;
        UIElement::Ptr focusedElement_;
        Vector2 size_;
        Vector2 position_;
        bool visible_ = true;

        UIElement::Ptr findElementRecursive(UIElement::Ptr element, const std::string& name);
    };

    // UICanvasManager - global registry of UI canvases
    class DORIAX_API UICanvasManager {
    public:
        static UICanvasManager& get();

        // Create and register a canvas
        UICanvas::Ptr createCanvas(const std::string& name);

        // Get a canvas by name
        UICanvas::Ptr getCanvas(const std::string& name);

        // Remove a canvas
        void removeCanvas(const std::string& name);

        // Get active canvas (topmost/last focused)
        UICanvas::Ptr getActiveCanvas();

        // Set active canvas
        void setActiveCanvas(const std::string& name);

        // List all canvas names
        std::vector<std::string> listCanvases() const;

        // Input dispatch to active canvas
        void handleMouseClick(float x, float y);
        void handleMouseMove(float x, float y);
        void handleKeyInput(const std::string& key);

        // Update all canvases
        void update(float deltaTime);

        // Clear all canvases
        void clear();

    private:
        UICanvasManager() = default;
        std::unordered_map<std::string, UICanvas::Ptr> canvases_;
        std::string activeCanvasName_;
    };

}

#endif // DORIAX_UI_CANVAS_H
