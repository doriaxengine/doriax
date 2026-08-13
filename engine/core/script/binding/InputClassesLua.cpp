//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "input/InputAction.h"
#include "input/InputActionMap.h"
#include "input/InputBinder.h"
#include "Log.h"

using namespace doriax;

void LuaBinding::registerInputClasses(lua_State *L) {
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("doriax")

        .beginClass<InputActionType>("InputActionType")
        .endClass()
        .addVariable("InputActionType_Digital", static_cast<int>(InputActionType::Digital), false)
        .addVariable("InputActionType_Analog1D", static_cast<int>(InputActionType::Analog1D), false)
        .addVariable("InputActionType_Analog2D", static_cast<int>(InputActionType::Analog2D), false)

        .beginClass<InputValueType>("InputValueType")
        .endClass()
        .addVariable("InputValueType_Pressed", static_cast<int>(InputValueType::Pressed), false)
        .addVariable("InputValueType_Released", static_cast<int>(InputValueType::Released), false)
        .addVariable("InputValueType_Held", static_cast<int>(InputValueType::Held), false)

        .beginClass<KeyCode>("KeyCode")
        .endClass()
        .addVariable("KeyCode_A", static_cast<int>(KeyCode::A), false)
        .addVariable("KeyCode_D", static_cast<int>(KeyCode::D), false)
        .addVariable("KeyCode_W", static_cast<int>(KeyCode::W), false)
        .addVariable("KeyCode_S", static_cast<int>(KeyCode::S), false)
        .addVariable("KeyCode_Space", static_cast<int>(KeyCode::Space), false)
        .addVariable("KeyCode_Enter", static_cast<int>(KeyCode::Enter), false)
        .addVariable("KeyCode_Escape", static_cast<int>(KeyCode::Escape), false)
        .addVariable("KeyCode_Left", static_cast<int>(KeyCode::Left), false)
        .addVariable("KeyCode_Right", static_cast<int>(KeyCode::Right), false)
        .addVariable("KeyCode_Up", static_cast<int>(KeyCode::Up), false)
        .addVariable("KeyCode_Down", static_cast<int>(KeyCode::Down), false)
        .addVariable("KeyCode_LeftShift", static_cast<int>(KeyCode::LeftShift), false)
        .addVariable("KeyCode_LeftControl", static_cast<int>(KeyCode::LeftControl), false)

        .beginClass<GamepadButton>("GamepadButton")
        .endClass()
        .addVariable("GamepadButton_A", static_cast<int>(GamepadButton::A), false)
        .addVariable("GamepadButton_B", static_cast<int>(GamepadButton::B), false)
        .addVariable("GamepadButton_X", static_cast<int>(GamepadButton::X), false)
        .addVariable("GamepadButton_Y", static_cast<int>(GamepadButton::Y), false)
        .addVariable("GamepadButton_LB", static_cast<int>(GamepadButton::LB), false)
        .addVariable("GamepadButton_RB", static_cast<int>(GamepadButton::RB), false)
        .addVariable("GamepadButton_Start", static_cast<int>(GamepadButton::Start), false)

        .beginClass<GamepadAxis>("GamepadAxis")
        .endClass()
        .addVariable("GamepadAxis_LeftStickX", static_cast<int>(GamepadAxis::LeftStickX), false)
        .addVariable("GamepadAxis_LeftStickY", static_cast<int>(GamepadAxis::LeftStickY), false)
        .addVariable("GamepadAxis_RightStickX", static_cast<int>(GamepadAxis::RightStickX), false)
        .addVariable("GamepadAxis_RightStickY", static_cast<int>(GamepadAxis::RightStickY), false)
        .addVariable("GamepadAxis_LeftTrigger", static_cast<int>(GamepadAxis::LeftTrigger), false)
        .addVariable("GamepadAxis_RightTrigger", static_cast<int>(GamepadAxis::RightTrigger), false)

        .beginClass<InputAction>("InputAction")
            .addFunction("name", &InputAction::name)
            .addFunction("setName", &InputAction::setName)
            .addFunction("isPressed", &InputAction::isPressed)
            .addFunction("isHeld", &InputAction::isHeld)
            .addFunction("getValue", &InputAction::getValue)
            .addFunction("getX", &InputAction::getX)
            .addFunction("getY", &InputAction::getY)
            .addFunction("addCallback", +[](InputAction* self, const luabridge::LuaRef& func) -> int {
                if (!func.isFunction()) {
                    Log::error("InputAction.addCallback: argument must be a function");
                    return -1;
                }
                return self->addCallback([func](const InputValue& val) {
                    try {
                        func(val.value, val.x, val.y);
                    } catch (const std::exception& e) {
                        Log::error("InputAction callback error: %s", e.what());
                    }
                });
            })
        .endClass()

        .beginClass<InputActionMap>("InputActionMap")
            .addFunction("name", &InputActionMap::name)
            .addFunction("addAction", +[](InputActionMap* self, const std::string& name, InputActionType type) {
                return self->addAction(name, type);
            })
            .addFunction("getAction", &InputActionMap::getAction)
            .addFunction("hasAction", &InputActionMap::hasAction)
            .addFunction("enable", &InputActionMap::enable)
            .addFunction("disable", &InputActionMap::disable)
            .addFunction("isEnabled", &InputActionMap::isEnabled)
            .addFunction("listActions", &InputActionMap::listActions)
        .endClass()

        .beginNamespace("InputActionMapRegistry")
            .addFunction("get", +[]() {
                return &InputActionMapRegistry::get();
            })
            .addFunction("createMap", +[](const std::string& name) {
                return InputActionMapRegistry::get().createMap(name);
            })
            .addFunction("getMap", +[](const std::string& name) {
                return InputActionMapRegistry::get().getMap(name);
            })
            .addFunction("activateMap", +[](const std::string& name) {
                InputActionMapRegistry::get().activateMap(name);
            })
            .addFunction("getActiveMap", +[]() {
                return InputActionMapRegistry::get().getActiveMap();
            })
            .addFunction("listMaps", +[]() {
                return InputActionMapRegistry::get().listMaps();
            })
        .endNamespace()

        .beginNamespace("InputBinder")
            .addFunction("get", +[]() {
                return &InputBinder::get();
            })
            .addFunction("bindKey", +[](KeyCode key, const std::string& action, float scale) {
                InputBinder::get().bindKey(key, action, scale);
            })
            .addFunction("bindGamepadButton", +[](GamepadButton button, const std::string& action) {
                InputBinder::get().bindGamepadButton(button, action);
            })
            .addFunction("bindGamepadAxis", +[](GamepadAxis axis, const std::string& action, float scale, float deadzone) {
                InputBinder::get().bindGamepadAxis(axis, action, scale, deadzone);
            })
            .addFunction("remapBinding", +[](const std::string& action, KeyCode oldKey, KeyCode newKey) {
                InputBinder::get().remapBinding(action, oldKey, newKey);
            })
        .endNamespace()

        .endNamespace();

    Log::info("Input action system Lua bindings registered");

#endif // DISABLE_LUA_BINDINGS
}
