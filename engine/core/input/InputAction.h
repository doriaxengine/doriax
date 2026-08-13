//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_INPUT_ACTION_H
#define DORIAX_INPUT_ACTION_H

#include "Export.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace doriax {

    enum class InputActionType {
        Digital,        // On/Off (button press)
        Analog1D,       // Single axis (trigger)
        Analog2D        // Two axes (stick)
    };

    enum class InputValueType {
        Pressed,        // Key just pressed (frame)
        Released,       // Key just released (frame)
        Held            // Key is held down
    };

    struct InputValue {
        InputValueType type = InputValueType::Pressed;
        float value = 0.0f;     // For analog: 0.0 to 1.0
        float x = 0.0f;         // For 2D analog: x axis
        float y = 0.0f;         // For 2D analog: y axis
    };

    class DORIAX_API InputAction {
    public:
        using Callback = std::function<void(const InputValue&)>;

        InputAction();
        explicit InputAction(const std::string& name, InputActionType type = InputActionType::Digital);

        const std::string& name() const;
        InputActionType type() const;

        void setName(const std::string& name);
        void setType(InputActionType type);

        // Register a callback to be invoked when this action triggers
        int addCallback(Callback callback);
        void removeCallback(int callbackId);

        // Trigger the action (called by input system)
        void trigger(const InputValue& value);

        // Check current state
        bool isPressed() const;
        bool isHeld() const;
        float getValue() const;
        float getX() const;
        float getY() const;

        // Clear state after frame
        void clearFrame();

    private:
        std::string name_;
        InputActionType type_;
        InputValue currentValue_;
        InputValue lastValue_;
        std::unordered_map<int, Callback> callbacks_;
        int nextCallbackId_ = 0;
    };

}

#endif // DORIAX_INPUT_ACTION_H
