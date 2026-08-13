//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_INPUT_BINDER_H
#define DORIAX_INPUT_BINDER_H

#include "Export.h"
#include "input/InputAction.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace doriax {

    enum class InputDeviceType {
        Keyboard,
        Mouse,
        Gamepad
    };

    enum class KeyCode {
        // Keyboard keys (subset)
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Space, Enter, Escape, Backspace, Tab,
        LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
        Left, Right, Up, Down,
        PageUp, PageDown, Home, End, Delete,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
    };

    enum class GamepadButton {
        A, B, X, Y,
        LB, RB,
        Back, Start,
        LeftStick, RightStick,
        Guide
    };

    enum class GamepadAxis {
        LeftStickX, LeftStickY,
        RightStickX, RightStickY,
        LeftTrigger, RightTrigger
    };

    enum class MouseButton {
        Left, Right, Middle
    };

    // InputBinding describes how a physical input maps to an action
    struct InputBinding {
        std::string actionName;
        InputDeviceType deviceType;
        
        // Keyboard specific
        KeyCode keyCode = KeyCode::A;
        
        // Gamepad specific
        GamepadButton gamepadButton = GamepadButton::A;
        GamepadAxis gamepadAxis = GamepadAxis::LeftStickX;
        
        // Mouse specific
        MouseButton mouseButton = MouseButton::Left;
        
        // Axis scale (for analog: deadzone, sensitivity, etc.)
        float scale = 1.0f;
        float deadzone = 0.1f;
    };

    // InputBinder bridges raw input events to InputActions
    class DORIAX_API InputBinder {
    public:
        static InputBinder& get();

        // Bind keyboard key to an action
        void bindKey(KeyCode key, const std::string& actionName, float scale = 1.0f);

        // Bind gamepad button to an action
        void bindGamepadButton(GamepadButton button, const std::string& actionName);

        // Bind gamepad axis to an action
        void bindGamepadAxis(GamepadAxis axis, const std::string& actionName, float scale = 1.0f, float deadzone = 0.1f);

        // Bind mouse button to an action
        void bindMouseButton(MouseButton button, const std::string& actionName);

        // Get all bindings for an action
        std::vector<InputBinding> getBindingsForAction(const std::string& actionName) const;

        // Remove a binding
        void removeBinding(const InputBinding& binding);

        // Clear all bindings
        void clearBindings();

        // Called by engine to process raw input and trigger actions
        void processKeyDown(KeyCode key);
        void processKeyUp(KeyCode key);
        void processGamepadButton(GamepadButton button);
        void processGamepadAxis(GamepadAxis axis, float value);
        void processMouseButton(MouseButton button);
        void processMouseMove(float x, float y);

        // Remap a binding at runtime
        void remapBinding(const std::string& actionName, KeyCode oldKey, KeyCode newKey);

    private:
        InputBinder() = default;
        
        std::vector<InputBinding> bindings_;
        std::unordered_map<std::string, std::vector<InputBinding>> bindingsByAction_;
    };

}

#endif // DORIAX_INPUT_BINDER_H
