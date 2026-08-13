//
// (c) 2026 Eduardo Doria.
//

#include "input/InputBinder.h"
#include "input/InputActionMap.h"
#include "Log.h"
#include <algorithm>

namespace doriax {

    InputBinder& InputBinder::get() {
        static InputBinder binder;
        return binder;
    }

    void InputBinder::bindKey(KeyCode key, const std::string& actionName, float scale) {
        InputBinding binding;
        binding.actionName = actionName;
        binding.deviceType = InputDeviceType::Keyboard;
        binding.keyCode = key;
        binding.scale = scale;

        bindings_.push_back(binding);
        bindingsByAction_[actionName].push_back(binding);

        Log::info("InputBinder: bound key to action '%s'", actionName.c_str());
    }

    void InputBinder::bindGamepadButton(GamepadButton button, const std::string& actionName) {
        InputBinding binding;
        binding.actionName = actionName;
        binding.deviceType = InputDeviceType::Gamepad;
        binding.gamepadButton = button;

        bindings_.push_back(binding);
        bindingsByAction_[actionName].push_back(binding);

        Log::info("InputBinder: bound gamepad button to action '%s'", actionName.c_str());
    }

    void InputBinder::bindGamepadAxis(GamepadAxis axis, const std::string& actionName, float scale, float deadzone) {
        InputBinding binding;
        binding.actionName = actionName;
        binding.deviceType = InputDeviceType::Gamepad;
        binding.gamepadAxis = axis;
        binding.scale = scale;
        binding.deadzone = deadzone;

        bindings_.push_back(binding);
        bindingsByAction_[actionName].push_back(binding);

        Log::info("InputBinder: bound gamepad axis to action '%s'", actionName.c_str());
    }

    void InputBinder::bindMouseButton(MouseButton button, const std::string& actionName) {
        InputBinding binding;
        binding.actionName = actionName;
        binding.deviceType = InputDeviceType::Mouse;
        binding.mouseButton = button;

        bindings_.push_back(binding);
        bindingsByAction_[actionName].push_back(binding);

        Log::info("InputBinder: bound mouse button to action '%s'", actionName.c_str());
    }

    std::vector<InputBinding> InputBinder::getBindingsForAction(const std::string& actionName) const {
        auto it = bindingsByAction_.find(actionName);
        return it != bindingsByAction_.end() ? it->second : std::vector<InputBinding>();
    }

    void InputBinder::removeBinding(const InputBinding& binding) {
        auto it = std::find_if(bindings_.begin(), bindings_.end(), 
            [&binding](const InputBinding& b) {
                return b.actionName == binding.actionName &&
                       b.deviceType == binding.deviceType;
            }
        );
        if (it != bindings_.end()) {
            bindings_.erase(it);
        }

        auto& byAction = bindingsByAction_[binding.actionName];
        auto it2 = std::find_if(byAction.begin(), byAction.end(),
            [&binding](const InputBinding& b) {
                return b.deviceType == binding.deviceType;
            }
        );
        if (it2 != byAction.end()) {
            byAction.erase(it2);
        }
    }

    void InputBinder::clearBindings() {
        bindings_.clear();
        bindingsByAction_.clear();
    }

    void InputBinder::processKeyDown(KeyCode key) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        // Find all bindings for this key
        for (const auto& binding : bindings_) {
            if (binding.deviceType == InputDeviceType::Keyboard && binding.keyCode == key) {
                auto action = activeMap->getAction(binding.actionName);
                if (action) {
                    InputValue val;
                    val.type = InputValueType::Pressed;
                    val.value = 1.0f;
                    action->trigger(val);
                }
            }
        }
    }

    void InputBinder::processKeyUp(KeyCode key) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        for (const auto& binding : bindings_) {
            if (binding.deviceType == InputDeviceType::Keyboard && binding.keyCode == key) {
                auto action = activeMap->getAction(binding.actionName);
                if (action) {
                    InputValue val;
                    val.type = InputValueType::Released;
                    val.value = 0.0f;
                    action->trigger(val);
                }
            }
        }
    }

    void InputBinder::processGamepadButton(GamepadButton button) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        for (const auto& binding : bindings_) {
            if (binding.deviceType == InputDeviceType::Gamepad && binding.gamepadButton == button) {
                auto action = activeMap->getAction(binding.actionName);
                if (action) {
                    InputValue val;
                    val.type = InputValueType::Pressed;
                    val.value = 1.0f;
                    action->trigger(val);
                }
            }
        }
    }

    void InputBinder::processGamepadAxis(GamepadAxis axis, float value) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        for (const auto& binding : bindings_) {
            if (binding.deviceType == InputDeviceType::Gamepad && binding.gamepadAxis == axis) {
                // Apply deadzone
                float processedValue = value;
                if (std::abs(value) < binding.deadzone) {
                    processedValue = 0.0f;
                } else {
                    processedValue = (value - (value > 0 ? binding.deadzone : -binding.deadzone)) /
                                   (1.0f - binding.deadzone);
                }
                processedValue *= binding.scale;

                auto action = activeMap->getAction(binding.actionName);
                if (action) {
                    InputValue val;
                    val.type = InputValueType::Held;
                    val.value = std::abs(processedValue);
                    if (axis == GamepadAxis::LeftStickX || axis == GamepadAxis::RightStickX) {
                        val.x = processedValue;
                    } else if (axis == GamepadAxis::LeftStickY || axis == GamepadAxis::RightStickY) {
                        val.y = processedValue;
                    }
                    action->trigger(val);
                }
            }
        }
    }

    void InputBinder::processMouseButton(MouseButton button) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        for (const auto& binding : bindings_) {
            if (binding.deviceType == InputDeviceType::Mouse && binding.mouseButton == button) {
                auto action = activeMap->getAction(binding.actionName);
                if (action) {
                    InputValue val;
                    val.type = InputValueType::Pressed;
                    val.value = 1.0f;
                    action->trigger(val);
                }
            }
        }
    }

    void InputBinder::processMouseMove(float x, float y) {
        auto& registry = InputActionMapRegistry::get();
        auto activeMap = registry.getActiveMap();
        if (!activeMap) return;

        // Find mouse move actions
        auto moveAction = activeMap->getAction("_MouseMove");
        if (moveAction) {
            InputValue val;
            val.type = InputValueType::Held;
            val.x = x;
            val.y = y;
            moveAction->trigger(val);
        }
    }

    void InputBinder::remapBinding(const std::string& actionName, KeyCode oldKey, KeyCode newKey) {
        for (auto& binding : bindings_) {
            if (binding.actionName == actionName && 
                binding.deviceType == InputDeviceType::Keyboard && 
                binding.keyCode == oldKey) {
                binding.keyCode = newKey;
                Log::info("InputBinder: remapped '%s' from key to new key", actionName.c_str());
                return;
            }
        }
    }

}
