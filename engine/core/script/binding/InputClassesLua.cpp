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

        .beginEnum<InputActionType>("InputActionType")
            .addConstant("Digital", InputActionType::Digital)
            .addConstant("Analog1D", InputActionType::Analog1D)
            .addConstant("Analog2D", InputActionType::Analog2D)
        .endEnum()

        .beginEnum<InputValueType>("InputValueType")
            .addConstant("Pressed", InputValueType::Pressed)
            .addConstant("Released", InputValueType::Released)
            .addConstant("Held", InputValueType::Held)
        .endEnum()

        .beginEnum<KeyCode>("KeyCode")
            .addConstant("A", KeyCode::A)
            .addConstant("D", KeyCode::D)
            .addConstant("W", KeyCode::W)
            .addConstant("S", KeyCode::S)
            .addConstant("Space", KeyCode::Space)
            .addConstant("Enter", KeyCode::Enter)
            .addConstant("Escape", KeyCode::Escape)
            .addConstant("Left", KeyCode::Left)
            .addConstant("Right", KeyCode::Right)
            .addConstant("Up", KeyCode::Up)
            .addConstant("Down", KeyCode::Down)
            .addConstant("LeftShift", KeyCode::LeftShift)
            .addConstant("LeftControl", KeyCode::LeftControl)
        .endEnum()

        .beginEnum<GamepadButton>("GamepadButton")
            .addConstant("A", GamepadButton::A)
            .addConstant("B", GamepadButton::B)
            .addConstant("X", GamepadButton::X)
            .addConstant("Y", GamepadButton::Y)
            .addConstant("LB", GamepadButton::LB)
            .addConstant("RB", GamepadButton::RB)
            .addConstant("Start", GamepadButton::Start)
        .endEnum()

        .beginEnum<GamepadAxis>("GamepadAxis")
            .addConstant("LeftStickX", GamepadAxis::LeftStickX)
            .addConstant("LeftStickY", GamepadAxis::LeftStickY)
            .addConstant("RightStickX", GamepadAxis::RightStickX)
            .addConstant("RightStickY", GamepadAxis::RightStickY)
            .addConstant("LeftTrigger", GamepadAxis::LeftTrigger)
            .addConstant("RightTrigger", GamepadAxis::RightTrigger)
        .endEnum()

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
