//
// (c) 2026 Eduardo Doria.
//

#include "input/InputAction.h"
#include "Log.h"

namespace doriax {

    InputAction::InputAction() : name_("Unnamed"), type_(InputActionType::Digital) {
    }

    InputAction::InputAction(const std::string& name, InputActionType type)
        : name_(name), type_(type) {
    }

    const std::string& InputAction::name() const {
        return name_;
    }

    InputActionType InputAction::type() const {
        return type_;
    }

    void InputAction::setName(const std::string& name) {
        name_ = name;
    }

    void InputAction::setType(InputActionType type) {
        type_ = type;
    }

    int InputAction::addCallback(Callback callback) {
        int id = nextCallbackId_++;
        callbacks_[id] = callback;
        return id;
    }

    void InputAction::removeCallback(int callbackId) {
        callbacks_.erase(callbackId);
    }

    void InputAction::trigger(const InputValue& value) {
        lastValue_ = currentValue_;
        currentValue_ = value;

        // Invoke all registered callbacks
        for (const auto& [id, callback] : callbacks_) {
            (void)id;  // Suppress unused warning
            try {
                callback(currentValue_);
            } catch (const std::exception& e) {
                Log::error("InputAction callback error: %s", e.what());
            }
        }
    }

    bool InputAction::isPressed() const {
        return currentValue_.type == InputValueType::Pressed;
    }

    bool InputAction::isHeld() const {
        return currentValue_.type == InputValueType::Held;
    }

    float InputAction::getValue() const {
        return currentValue_.value;
    }

    float InputAction::getX() const {
        return currentValue_.x;
    }

    float InputAction::getY() const {
        return currentValue_.y;
    }

    void InputAction::clearFrame() {
        // Reset pressed/released to default after frame
        if (currentValue_.type == InputValueType::Pressed || 
            currentValue_.type == InputValueType::Released) {
            currentValue_.type = InputValueType::Held;
        }
    }

}
