//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_UI_WIDGETS_H
#define DORIAX_UI_WIDGETS_H

#include "Export.h"
#include "ui/UIElement.h"
#include <string>

namespace doriax {

    // Button widget
    class DORIAX_API UIButton : public UIElement {
    public:
        UIButton(const std::string& name);

        void setText(const std::string& text);
        const std::string& getText() const;

        void setTextColor(Vector4 color);
        Vector4 getTextColor() const;

        void setPressedColor(Vector4 color);
        Vector4 getPressedColor() const;

        void setHoverColor(Vector4 color);
        Vector4 getHoverColor() const;

        bool isPressed() const { return pressed_; }

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        std::string text_;
        Vector4 textColor_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Vector4 pressedColor_ = Vector4(0.15f, 0.15f, 0.15f, 1.0f);
        Vector4 hoverColor_ = Vector4(0.3f, 0.3f, 0.3f, 1.0f);
        bool pressed_ = false;
    };

    // Text label widget
    class DORIAX_API UIText : public UIElement {
    public:
        UIText(const std::string& name);

        void setText(const std::string& text);
        const std::string& getText() const;

        void setTextColor(Vector4 color);
        Vector4 getTextColor() const;

        void setFontSize(float size);
        float getFontSize() const;

        enum class HorizontalTextAlign {
            Left, Center, Right
        };

        void setHorizontalTextAlign(HorizontalTextAlign align);
        HorizontalTextAlign getHorizontalTextAlign() const;

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        std::string text_;
        Vector4 textColor_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        float fontSize_ = 14.0f;
        HorizontalTextAlign textAlign_ = HorizontalTextAlign::Left;
    };

    // Input field widget
    class DORIAX_API UIInputField : public UIElement {
    public:
        UIInputField(const std::string& name);

        void setText(const std::string& text);
        const std::string& getText() const;

        void setPlaceholder(const std::string& placeholder);
        const std::string& getPlaceholder() const;

        void setMaxLength(int maxLen);
        int getMaxLength() const;

        void setPasswordMode(bool passwordMode);
        bool isPasswordMode() const;

        bool handleKeyInput(const std::string& key) override;

        using OnTextChanged = std::function<void(const std::string&)>;
        int onTextChanged(OnTextChanged callback);

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        std::string text_;
        std::string placeholder_;
        int maxLength_ = 256;
        bool passwordMode_ = false;
        int cursorPosition_ = 0;
        std::unordered_map<int, OnTextChanged> textChangedCallbacks_;
        int nextCallbackId_ = 0;
    };

    // Panel/Container widget with background
    class DORIAX_API UIPanel : public UIElement {
    public:
        UIPanel(const std::string& name);

        void setTitle(const std::string& title);
        const std::string& getTitle() const;

        void setTitleColor(Vector4 color);
        Vector4 getTitleColor() const;

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        std::string title_;
        Vector4 titleColor_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    // Slider widget
    class DORIAX_API UISlider : public UIElement {
    public:
        UISlider(const std::string& name);

        void setValue(float value);
        float getValue() const;

        void setMinMax(float min, float max);
        float getMin() const { return minValue_; }
        float getMax() const { return maxValue_; }

        void setStep(float step);
        float getStep() const { return step_; }

        using OnValueChanged = std::function<void(float)>;
        int onValueChanged(OnValueChanged callback);

        bool handleMouseClick(float x, float y) override;
        bool handleMouseMove(float x, float y) override;

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        float value_ = 0.5f;
        float minValue_ = 0.0f;
        float maxValue_ = 1.0f;
        float step_ = 0.01f;
        bool dragging_ = false;
        std::unordered_map<int, OnValueChanged> valueChangedCallbacks_;
        int nextCallbackId_ = 0;
    };

    // Toggle/Checkbox widget
    class DORIAX_API UIToggle : public UIElement {
    public:
        UIToggle(const std::string& name);

        void setChecked(bool checked);
        bool isChecked() const;

        void setLabel(const std::string& label);
        const std::string& getLabel() const;

        using OnToggled = std::function<void(bool)>;
        int onToggled(OnToggled callback);

        bool handleMouseClick(float x, float y) override;

        void setProperty(const std::string& key, const std::string& value) override;
        std::string getProperty(const std::string& key) const override;

    private:
        bool checked_ = false;
        std::string label_;
        std::unordered_map<int, OnToggled> toggledCallbacks_;
        int nextCallbackId_ = 0;
    };

}

#endif // DORIAX_UI_WIDGETS_H
