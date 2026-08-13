//
// (c) 2026 Eduardo Doria.
//

#include "ui/UIWidgets.h"
#include "Log.h"

namespace doriax {

    // UIButton Implementation
    UIButton::UIButton(const std::string& name)
        : UIElement(name, UIElementType::Button) {
    }

    void UIButton::setText(const std::string& text) {
        text_ = text;
    }

    const std::string& UIButton::getText() const {
        return text_;
    }

    void UIButton::setTextColor(Vector4 color) {
        textColor_ = color;
    }

    Vector4 UIButton::getTextColor() const {
        return textColor_;
    }

    void UIButton::setPressedColor(Vector4 color) {
        pressedColor_ = color;
    }

    Vector4 UIButton::getPressedColor() const {
        return pressedColor_;
    }

    void UIButton::setHoverColor(Vector4 color) {
        hoverColor_ = color;
    }

    Vector4 UIButton::getHoverColor() const {
        return hoverColor_;
    }

    void UIButton::setProperty(const std::string& key, const std::string& value) {
        if (key == "text") {
            setText(value);
        } else if (key == "label") {
            setText(value);
        }
    }

    std::string UIButton::getProperty(const std::string& key) const {
        if (key == "text") {
            return text_;
        }
        return "";
    }

    // UIText Implementation
    UIText::UIText(const std::string& name)
        : UIElement(name, UIElementType::Text) {
    }

    void UIText::setText(const std::string& text) {
        text_ = text;
    }

    const std::string& UIText::getText() const {
        return text_;
    }

    void UIText::setTextColor(Vector4 color) {
        textColor_ = color;
    }

    Vector4 UIText::getTextColor() const {
        return textColor_;
    }

    void UIText::setFontSize(float size) {
        fontSize_ = size;
    }

    float UIText::getFontSize() const {
        return fontSize_;
    }

    void UIText::setHorizontalTextAlign(HorizontalTextAlign align) {
        textAlign_ = align;
    }

    UIText::HorizontalTextAlign UIText::getHorizontalTextAlign() const {
        return textAlign_;
    }

    void UIText::setProperty(const std::string& key, const std::string& value) {
        if (key == "text" || key == "content") {
            setText(value);
        } else if (key == "fontSize") {
            try {
                setFontSize(std::stof(value));
            } catch (...) {}
        }
    }

    std::string UIText::getProperty(const std::string& key) const {
        if (key == "text") {
            return text_;
        }
        return "";
    }

    // UIInputField Implementation
    UIInputField::UIInputField(const std::string& name)
        : UIElement(name, UIElementType::InputField) {
    }

    void UIInputField::setText(const std::string& text) {
        if ((int)text.length() <= maxLength_) {
            text_ = text;
            cursorPosition_ = text_.length();
        }
    }

    const std::string& UIInputField::getText() const {
        return text_;
    }

    void UIInputField::setPlaceholder(const std::string& placeholder) {
        placeholder_ = placeholder;
    }

    const std::string& UIInputField::getPlaceholder() const {
        return placeholder_;
    }

    void UIInputField::setMaxLength(int maxLen) {
        maxLength_ = maxLen;
    }

    int UIInputField::getMaxLength() const {
        return maxLength_;
    }

    void UIInputField::setPasswordMode(bool passwordMode) {
        passwordMode_ = passwordMode;
    }

    bool UIInputField::isPasswordMode() const {
        return passwordMode_;
    }

    bool UIInputField::handleKeyInput(const std::string& key) {
        if (!focused_) return false;

        if (key == "Backspace" && cursorPosition_ > 0) {
            text_.erase(cursorPosition_ - 1, 1);
            cursorPosition_--;
        } else if (key != "Enter" && (int)text_.length() < maxLength_) {
            text_.insert(cursorPosition_, key);
            cursorPosition_++;
        }

        // Notify callbacks
        for (const auto& [id, callback] : textChangedCallbacks_) {
            (void)id;
            try {
                callback(text_);
            } catch (const std::exception& e) {
                Log::error("UIInputField text changed callback error: %s", e.what());
            }
        }

        return true;
    }

    int UIInputField::onTextChanged(OnTextChanged callback) {
        int id = nextCallbackId_++;
        textChangedCallbacks_[id] = callback;
        return id;
    }

    void UIInputField::setProperty(const std::string& key, const std::string& value) {
        if (key == "placeholder") {
            setPlaceholder(value);
        } else if (key == "text") {
            setText(value);
        } else if (key == "maxLength") {
            try {
                setMaxLength(std::stoi(value));
            } catch (...) {}
        }
    }

    std::string UIInputField::getProperty(const std::string& key) const {
        if (key == "text") {
            return text_;
        } else if (key == "placeholder") {
            return placeholder_;
        }
        return "";
    }

    // UIPanel Implementation
    UIPanel::UIPanel(const std::string& name)
        : UIElement(name, UIElementType::Panel) {
        setBackgroundColor(Vector4(0.15f, 0.15f, 0.15f, 1.0f));
    }

    void UIPanel::setTitle(const std::string& title) {
        title_ = title;
    }

    const std::string& UIPanel::getTitle() const {
        return title_;
    }

    void UIPanel::setTitleColor(Vector4 color) {
        titleColor_ = color;
    }

    Vector4 UIPanel::getTitleColor() const {
        return titleColor_;
    }

    void UIPanel::setProperty(const std::string& key, const std::string& value) {
        if (key == "title") {
            setTitle(value);
        }
    }

    std::string UIPanel::getProperty(const std::string& key) const {
        if (key == "title") {
            return title_;
        }
        return "";
    }

    // UISlider Implementation
    UISlider::UISlider(const std::string& name)
        : UIElement(name, UIElementType::Slider) {
    }

    void UISlider::setValue(float value) {
        value_ = std::max(minValue_, std::min(maxValue_, value));
    }

    float UISlider::getValue() const {
        return value_;
    }

    void UISlider::setMinMax(float min, float max) {
        minValue_ = min;
        maxValue_ = max;
        value_ = std::max(minValue_, std::min(maxValue_, value_));
    }

    void UISlider::setStep(float step) {
        step_ = step;
    }

    int UISlider::onValueChanged(OnValueChanged callback) {
        int id = nextCallbackId_++;
        valueChangedCallbacks_[id] = callback;
        return id;
    }

    bool UISlider::handleMouseClick(float x, float y) {
        if (!visible_ || !interactive_ || !enabled_) {
            return false;
        }

        if (computedRect_.containsPoint(x, y)) {
            dragging_ = true;
            // Compute value from x position
            float ratio = (x - computedRect_.x) / computedRect_.width;
            float newValue = minValue_ + ratio * (maxValue_ - minValue_);
            setValue(newValue);

            for (const auto& [id, callback] : valueChangedCallbacks_) {
                (void)id;
                try {
                    callback(value_);
                } catch (const std::exception& e) {
                    Log::error("UISlider value changed callback error: %s", e.what());
                }
            }

            return true;
        }

        return false;
    }

    bool UISlider::handleMouseMove(float x, float y) {
        if (dragging_ && computedRect_.containsPoint(x, y)) {
            float ratio = (x - computedRect_.x) / computedRect_.width;
            float newValue = minValue_ + ratio * (maxValue_ - minValue_);
            setValue(newValue);

            for (const auto& [id, callback] : valueChangedCallbacks_) {
                (void)id;
                try {
                    callback(value_);
                } catch (const std::exception& e) {
                    Log::error("UISlider value changed callback error: %s", e.what());
                }
            }

            return true;
        }

        dragging_ = false;
        return false;
    }

    void UISlider::setProperty(const std::string& key, const std::string& value) {
        if (key == "value") {
            try {
                setValue(std::stof(value));
            } catch (...) {}
        } else if (key == "min") {
            try {
                minValue_ = std::stof(value);
            } catch (...) {}
        } else if (key == "max") {
            try {
                maxValue_ = std::stof(value);
            } catch (...) {}
        }
    }

    std::string UISlider::getProperty(const std::string& key) const {
        if (key == "value") {
            return std::to_string(value_);
        }
        return "";
    }

    // UIToggle Implementation
    UIToggle::UIToggle(const std::string& name)
        : UIElement(name, UIElementType::Toggle) {
    }

    void UIToggle::setChecked(bool checked) {
        checked_ = checked;
    }

    bool UIToggle::isChecked() const {
        return checked_;
    }

    void UIToggle::setLabel(const std::string& label) {
        label_ = label;
    }

    const std::string& UIToggle::getLabel() const {
        return label_;
    }

    int UIToggle::onToggled(OnToggled callback) {
        int id = nextCallbackId_++;
        toggledCallbacks_[id] = callback;
        return id;
    }

    bool UIToggle::handleMouseClick(float x, float y) {
        if (!visible_ || !interactive_ || !enabled_) {
            return false;
        }

        if (computedRect_.containsPoint(x, y)) {
            checked_ = !checked_;

            for (const auto& [id, callback] : toggledCallbacks_) {
                (void)id;
                try {
                    callback(checked_);
                } catch (const std::exception& e) {
                    Log::error("UIToggle toggled callback error: %s", e.what());
                }
            }

            notifyClick();
            return true;
        }

        return false;
    }

    void UIToggle::setProperty(const std::string& key, const std::string& value) {
        if (key == "label") {
            setLabel(value);
        } else if (key == "checked") {
            setChecked(value == "true");
        }
    }

    std::string UIToggle::getProperty(const std::string& key) const {
        if (key == "label") {
            return label_;
        } else if (key == "checked") {
            return checked_ ? "true" : "false";
        }
        return "";
    }

}
