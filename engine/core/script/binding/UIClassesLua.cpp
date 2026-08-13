//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "ui/UIElement.h"
#include "ui/UIWidgets.h"
#include "ui/UICanvas.h"
#include "Log.h"

using namespace doriax;

void LuaBinding::registerUIClasses(lua_State *L) {
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("doriax")

        .beginEnum<UIElementType>("UIElementType")
            .addConstant("Container", UIElementType::Container)
            .addConstant("Panel", UIElementType::Panel)
            .addConstant("Button", UIElementType::Button)
            .addConstant("Text", UIElementType::Text)
            .addConstant("InputField", UIElementType::InputField)
            .addConstant("Slider", UIElementType::Slider)
            .addConstant("Toggle", UIElementType::Toggle)
        .endEnum()

        .beginEnum<UIHorizontalAlign>("UIHorizontalAlign")
            .addConstant("Left", UIHorizontalAlign::Left)
            .addConstant("Center", UIHorizontalAlign::Center)
            .addConstant("Right", UIHorizontalAlign::Right)
            .addConstant("Stretch", UIHorizontalAlign::Stretch)
        .endEnum()

        .beginEnum<UIVerticalAlign>("UIVerticalAlign")
            .addConstant("Top", UIVerticalAlign::Top)
            .addConstant("Center", UIVerticalAlign::Center)
            .addConstant("Bottom", UIVerticalAlign::Bottom)
            .addConstant("Stretch", UIVerticalAlign::Stretch)
        .endEnum()

        .beginClass<UIElement>("UIElement")
            .addFunction("name", &UIElement::name)
            .addFunction("setPosition", &UIElement::setPosition)
            .addFunction("getPosition", &UIElement::getPosition)
            .addFunction("setSize", &UIElement::setSize)
            .addFunction("getSize", +[](UIElement* self) {
                UISize s = self->getSize();
                return std::make_tuple(s.width, s.height);
            })
            .addFunction("setMargin", &UIElement::setMargin)
            .addFunction("setHorizontalAlign", &UIElement::setHorizontalAlign)
            .addFunction("setVerticalAlign", &UIElement::setVerticalAlign)
            .addFunction("addChild", &UIElement::addChild)
            .addFunction("removeChild", &UIElement::removeChild)
            .addFunction("getChild", &UIElement::getChild)
            .addFunction("setVisible", &UIElement::setVisible)
            .addFunction("isVisible", &UIElement::isVisible)
            .addFunction("setInteractive", &UIElement::setInteractive)
            .addFunction("isInteractive", &UIElement::isInteractive)
            .addFunction("setEnabled", &UIElement::setEnabled)
            .addFunction("isEnabled", &UIElement::isEnabled)
            .addFunction("setBackgroundColor", &UIElement::setBackgroundColor)
            .addFunction("getBackgroundColor", &UIElement::getBackgroundColor)
            .addFunction("setBorderColor", &UIElement::setBorderColor)
            .addFunction("getBorderColor", &UIElement::getBorderColor)
            .addFunction("setBorderWidth", &UIElement::setBorderWidth)
            .addFunction("getBorderWidth", &UIElement::getBorderWidth)
            .addFunction("onClicked", +[](UIElement* self, const luabridge::LuaRef& func) {
                if (!func.isFunction()) {
                    Log::error("onClicked: argument must be a function");
                    return -1;
                }
                return self->onClicked([func]() {
                    try {
                        func();
                    } catch (const std::exception& e) {
                        Log::error("onClicked callback error: %s", e.what());
                    }
                });
            })
            .addFunction("onHover", +[](UIElement* self, const luabridge::LuaRef& func) {
                if (!func.isFunction()) {
                    Log::error("onHover: argument must be a function");
                    return -1;
                }
                return self->onHover([func]() {
                    try {
                        func();
                    } catch (const std::exception& e) {
                        Log::error("onHover callback error: %s", e.what());
                    }
                });
            })
            .addFunction("setProperty", &UIElement::setProperty)
            .addFunction("getProperty", &UIElement::getProperty)
        .endClass()

        .deriveClass<UIButton, UIElement>("UIButton")
            .addFunction("setText", &UIButton::setText)
            .addFunction("getText", &UIButton::getText)
            .addFunction("setTextColor", &UIButton::setTextColor)
            .addFunction("getTextColor", &UIButton::getTextColor)
        .endClass()

        .deriveClass<UIText, UIElement>("UIText")
            .addFunction("setText", &UIText::setText)
            .addFunction("getText", &UIText::getText)
            .addFunction("setTextColor", &UIText::setTextColor)
            .addFunction("getTextColor", &UIText::getTextColor)
            .addFunction("setFontSize", &UIText::setFontSize)
            .addFunction("getFontSize", &UIText::getFontSize)
        .endClass()

        .deriveClass<UIInputField, UIElement>("UIInputField")
            .addFunction("setText", &UIInputField::setText)
            .addFunction("getText", &UIInputField::getText)
            .addFunction("setPlaceholder", &UIInputField::setPlaceholder)
            .addFunction("getPlaceholder", &UIInputField::getPlaceholder)
            .addFunction("setMaxLength", &UIInputField::setMaxLength)
            .addFunction("getMaxLength", &UIInputField::getMaxLength)
            .addFunction("onTextChanged", +[](UIInputField* self, const luabridge::LuaRef& func) {
                if (!func.isFunction()) {
                    Log::error("onTextChanged: argument must be a function");
                    return -1;
                }
                return self->onTextChanged([func](const std::string& text) {
                    try {
                        func(text);
                    } catch (const std::exception& e) {
                        Log::error("onTextChanged callback error: %s", e.what());
                    }
                });
            })
        .endClass()

        .deriveClass<UIPanel, UIElement>("UIPanel")
            .addFunction("setTitle", &UIPanel::setTitle)
            .addFunction("getTitle", &UIPanel::getTitle)
            .addFunction("setTitleColor", &UIPanel::setTitleColor)
            .addFunction("getTitleColor", &UIPanel::getTitleColor)
        .endClass()

        .deriveClass<UISlider, UIElement>("UISlider")
            .addFunction("setValue", &UISlider::setValue)
            .addFunction("getValue", &UISlider::getValue)
            .addFunction("setMinMax", &UISlider::setMinMax)
            .addFunction("getMin", &UISlider::getMin)
            .addFunction("getMax", &UISlider::getMax)
            .addFunction("onValueChanged", +[](UISlider* self, const luabridge::LuaRef& func) {
                if (!func.isFunction()) {
                    Log::error("onValueChanged: argument must be a function");
                    return -1;
                }
                return self->onValueChanged([func](float value) {
                    try {
                        func(value);
                    } catch (const std::exception& e) {
                        Log::error("onValueChanged callback error: %s", e.what());
                    }
                });
            })
        .endClass()

        .deriveClass<UIToggle, UIElement>("UIToggle")
            .addFunction("setChecked", &UIToggle::setChecked)
            .addFunction("isChecked", &UIToggle::isChecked)
            .addFunction("setLabel", &UIToggle::setLabel)
            .addFunction("getLabel", &UIToggle::getLabel)
            .addFunction("onToggled", +[](UIToggle* self, const luabridge::LuaRef& func) {
                if (!func.isFunction()) {
                    Log::error("onToggled: argument must be a function");
                    return -1;
                }
                return self->onToggled([func](bool checked) {
                    try {
                        func(checked);
                    } catch (const std::exception& e) {
                        Log::error("onToggled callback error: %s", e.what());
                    }
                });
            })
        .endClass()

        .beginClass<UICanvas>("UICanvas")
            .addFunction("name", &UICanvas::name)
            .addFunction("setSize", &UICanvas::setSize)
            .addFunction("getSize", &UICanvas::getSize)
            .addFunction("setPosition", &UICanvas::setPosition)
            .addFunction("getPosition", &UICanvas::getPosition)
            .addFunction("addRootElement", &UICanvas::addRootElement)
            .addFunction("getRootElement", &UICanvas::getRootElement)
            .addFunction("findElement", &UICanvas::findElement)
            .addFunction("processLayout", &UICanvas::processLayout)
            .addFunction("setVisible", &UICanvas::setVisible)
            .addFunction("isVisible", &UICanvas::isVisible)
        .endClass()

        .beginNamespace("UICanvasManager")
            .addFunction("get", +[]() {
                return &UICanvasManager::get();
            })
            .addFunction("createCanvas", +[](const std::string& name) {
                return UICanvasManager::get().createCanvas(name);
            })
            .addFunction("getCanvas", +[](const std::string& name) {
                return UICanvasManager::get().getCanvas(name);
            })
            .addFunction("setActiveCanvas", +[](const std::string& name) {
                UICanvasManager::get().setActiveCanvas(name);
            })
            .addFunction("getActiveCanvas", +[]() {
                return UICanvasManager::get().getActiveCanvas();
            })
            .addFunction("listCanvases", +[]() {
                return UICanvasManager::get().listCanvases();
            })
        .endNamespace()

        .beginNamespace("UIFactory")
            .addFunction("createButton", +[](const std::string& name) {
                return std::make_shared<UIButton>(name);
            })
            .addFunction("createText", +[](const std::string& name) {
                return std::make_shared<UIText>(name);
            })
            .addFunction("createPanel", +[](const std::string& name) {
                return std::make_shared<UIPanel>(name);
            })
            .addFunction("createInputField", +[](const std::string& name) {
                return std::make_shared<UIInputField>(name);
            })
            .addFunction("createSlider", +[](const std::string& name) {
                return std::make_shared<UISlider>(name);
            })
            .addFunction("createToggle", +[](const std::string& name) {
                return std::make_shared<UIToggle>(name);
            })
            .addFunction("createContainer", +[](const std::string& name) {
                return std::make_shared<UIElement>(name, UIElementType::Container);
            })
        .endNamespace()

        .endNamespace();

    Log::info("UI system Lua bindings registered");

#endif // DISABLE_LUA_BINDINGS
}
