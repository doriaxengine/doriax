//
// (c) 2026 Eduardo Doria.
//
// Modern UI System for Doriax
//
// OVERVIEW:
// =========
// A professional, modern UI framework built on the ECS architecture.
// Features:
// - Hierarchical element system
// - Responsive layout (flex-like positioning and sizing)
// - Input integration (mouse clicks, keyboard)
// - Rich widget library (buttons, panels, sliders, toggles, etc.)
// - Full Lua scripting support
// - Callback/event-driven architecture
// - Canvas-based rendering model
//
// ARCHITECTURE:
// ==============
//
//  UICanvasManager (singleton)
//      ├─ UICanvas (HUD, Menu, Overlay)
//      │   └─ UIElement hierarchy (tree)
//      │       ├─ UIPanel
//      │       ├─ UIButton
//      │       ├─ UIText
//      │       ├─ UIInputField
//      │       ├─ UISlider
//      │       └─ UIToggle
//      └─ ...
//
// KEY CLASSES:
// ============
//
// UIElement - Base class for all UI elements
//   Properties: position, size, margin, alignment, color, border
//   Methods: addChild(), removeChild(), setVisible(), setEnabled()
//   Events: onClicked(), onHover(), onUnhover()
//   Layout: computeLayout() - recursive layout computation
//   Input: handleMouseClick(), handleMouseMove(), handleKeyInput()
//
// UIButton - Clickable button
//   Properties: text, textColor, pressedColor, hoverColor
//   Events: click callbacks triggered on mouse click
//
// UIText - Static text label
//   Properties: text, textColor, fontSize, textAlign
//
// UIInputField - Text input box
//   Properties: text, placeholder, maxLength, passwordMode
//   Events: onTextChanged() triggered on input
//
// UIPanel - Container with title and background
//   Properties: title, titleColor
//   Children: Can contain other UI elements
//
// UISlider - Numeric slider
//   Properties: value, min, max, step
//   Events: onValueChanged() triggered on drag
//   Input: Draggable to adjust value
//
// UIToggle - Checkbox/toggle control
//   Properties: checked, label
//   Events: onToggled() triggered on click
//
// UICanvas - UI hierarchy container
//   Properties: size, position, visible
//   Methods: addRootElement(), findElement(), processLayout()
//   Input dispatch: handleMouseClick(), handleMouseMove(), handleKeyInput()
//
// UICanvasManager - Global UI manager singleton
//   Creates and manages multiple canvases
//   Input dispatch to active canvas
//   Only one canvas active at a time (for input handling)
//
// USAGE - C++ EXAMPLE:
// ====================
//
//  // 1. Create a canvas
//  auto canvas = UICanvasManager::get().createCanvas("main_menu");
//  canvas->setSize(1920, 1080);
//  UICanvasManager::get().setActiveCanvas("main_menu");
//
//  // 2. Create UI hierarchy
//  auto panel = std::make_shared<UIPanel>("menu_panel");
//  panel->setPosition(0, 0);
//  panel->setSize(100, 100, true, true);  // 100% width/height
//  panel->setTitle("Main Menu");
//  panel->setBackgroundColor(Vector4(0.2f, 0.2f, 0.2f, 0.9f));
//
//  auto button = std::make_shared<UIButton>("play_button");
//  button->setText("Play Game");
//  button->setPosition(50, 200);
//  button->setSize(400, 80);
//  button->setHorizontalAlign(UIHorizontalAlign::Center);
//  button->onClicked([this]() {
//      startGame();
//  });
//  panel->addChild(button);
//
//  canvas->addRootElement(panel);
//  canvas->processLayout();
//
//  // 3. In game loop, dispatch input
//  UICanvasManager::get().handleMouseClick(mouseX, mouseY);
//  UICanvasManager::get().handleMouseMove(mouseX, mouseY);
//
// USAGE - LUA EXAMPLE:
// ====================
//
//  -- Create canvas and UI
//  local canvas = doriax.UICanvasManager.createCanvas("hud")
//  canvas:setSize(1920, 1080)
//  doriax.UICanvasManager.setActiveCanvas("hud")
//
//  -- Create a panel
//  local panel = doriax.UIFactory.createPanel("health_panel")
//  panel:setPosition(10, 10)
//  panel:setSize(200, 100)
//  panel:setTitle("Health")
//  panel:setBackgroundColor(doriax.Vector4(0.15, 0.15, 0.15, 0.8))
//
//  -- Add a text label
//  local text = doriax.UIFactory.createText("health_text")
//  text:setText("HP: 100/100")
//  text:setTextColor(doriax.Vector4(0.0, 1.0, 0.0, 1.0))
//  text:setPosition(10, 20)
//  panel:addChild(text)
//
//  -- Add a button
//  local button = doriax.UIFactory.createButton("heal_button")
//  button:setText("Heal")
//  button:setPosition(10, 60)
//  button:setSize(80, 30)
//  button:onClicked(function()
//      print("Healing!")
//      player:heal(20)
//  end)
//  panel:addChild(button)
//
//  canvas:addRootElement(panel)
//  canvas:processLayout()
//
// LAYOUT SYSTEM:
// ===============
// Position: Relative to parent, offset by margin
// Size: Can be absolute or percentage (widthPercent, heightPercent)
// Alignment:
//   Horizontal: Left, Center, Right, Stretch
//   Vertical: Top, Center, Bottom, Stretch
//
// Example - Center a button on screen:
//   button->setHorizontalAlign(UIHorizontalAlign::Center)
//   button->setVerticalAlign(UIVerticalAlign::Center)
//   button->setSize(400, 80)  // absolute size
//   // After processLayout(), button will be centered in parent
//
// INPUT HANDLING:
// ================
// 1. UICanvasManager receives input events
// 2. Dispatches to active UICanvas
// 3. Canvas propagates to root element
// 4. Elements check bounds and handle input
// 5. Mouse events: click, move, hover
// 6. Keyboard: routed to focused element
//
// Focus management:
//   canvas->setFocusedElement(inputField)
//   // Keyboard input now goes to inputField
//
// PROPERTY SYSTEM:
// =================
// Generic property getter/setter for serialization:
//   element->setProperty("text", "Hello")
//   element->setProperty("fontSize", "16")
//   std::string value = element->getProperty("text")
//
// EVENT CALLBACKS:
// =================
//
// Button clicks:
//   button->onClicked([]() {
//       Log::info("Button clicked!")
//   })
//
// Input field changes:
//   inputField->onTextChanged([](const std::string& text) {
//       Log::info("Text: %s", text.c_str())
//   })
//
// Slider value changes:
//   slider->onValueChanged([](float value) {
//       Log::info("Slider: %.2f", value)
//   })
//
// Toggle state:
//   toggle->onToggled([](bool checked) {
//       Log::info("Toggled: %s", checked ? "true" : "false")
//   })
//
// Mouse hover:
//   element->onHover([]() {
//       element->setBackgroundColor(Vector4(0.3f, ...))  // highlight
//   })

#ifndef DORIAX_MODERN_UI_SYSTEM_H
#define DORIAX_MODERN_UI_SYSTEM_H

// See comments above for comprehensive documentation and examples.

#endif // DORIAX_MODERN_UI_SYSTEM_H
