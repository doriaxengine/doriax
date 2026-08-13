//
// (c) 2026 Eduardo Doria.
//
// InputSystemUsage - Modern action-based input system for Doriax
//
// OVERVIEW:
// =========
// The modern input system provides:
// 1. Action-based input (not raw key polling)
// 2. Configurable key/button/axis bindings (remappable at runtime)
// 3. Input contexts/maps (gameplay, menu, cutscene can have different bindings)
// 4. Lua scripting support for input events
// 5. Support for keyboard, gamepad, and mouse
//
// ARCHITECTURE:
// ==============
//
//  Raw Input (Keyboard/Gamepad/Mouse)
//      ↓
//  InputBinder (maps raw input to actions)
//      ↓
//  InputAction (digital/analog1d/analog2d)
//      ↓
//  InputActionMap (context: gameplay, menu, etc.)
//      ↓
//  Lua Scripts / Gameplay Code (react to actions)
//
// USAGE - C++ EXAMPLE:
// ====================
//
//  // 1. Create an action map for gameplay
//  auto gameplayMap = InputActionMapRegistry::get().createMap("gameplay");
//
//  // 2. Add actions to the map
//  gameplayMap->addAction("move", InputActionType::Analog2D);
//  gameplayMap->addAction("jump", InputActionType::Digital);
//  gameplayMap->addAction("shoot", InputActionType::Digital);
//
//  // 3. Bind raw input to actions
//  InputBinder::get().bindKey(KeyCode::W, "move");      // W key
//  InputBinder::get().bindKey(KeyCode::A, "move");      // A key
//  InputBinder::get().bindGamepadAxis(GamepadAxis::LeftStickY, "move", 1.0f, 0.1f);
//
//  InputBinder::get().bindKey(KeyCode::Space, "jump");
//  InputBinder::get().bindGamepadButton(GamepadButton::A, "jump");
//
//  InputBinder::get().bindKey(KeyCode::LControl, "shoot");
//  InputBinder::get().bindGamepadButton(GamepadButton::RB, "shoot");
//
//  // 4. Activate the map
//  InputActionMapRegistry::get().activateMap("gameplay");
//
//  // 5. Register callbacks for actions
//  auto moveAction = gameplayMap->getAction("move");
//  moveAction->addCallback([](const InputValue& val) {
//      // val.x and val.y are analog stick values
//      player->move(val.x, val.y);
//  });
//
//  auto jumpAction = gameplayMap->getAction("jump");
//  jumpAction->addCallback([](const InputValue& val) {
//      if (val.type == InputValueType::Pressed) {
//          player->jump();
//      }
//  });
//
//  // 6. In your game loop, call this after processing OS input events
//  InputActionMapRegistry::get().processFrame();
//
// USAGE - LUA EXAMPLE:
// ====================
//
//  -- Create action maps
//  local gameplayMap = doriax.InputActionMapRegistry.createMap("gameplay")
//  local menuMap = doriax.InputActionMapRegistry.createMap("menu")
//
//  -- Add actions
//  gameplayMap:addAction("move", doriax.InputActionType.Analog2D)
//  gameplayMap:addAction("jump", doriax.InputActionType.Digital)
//
//  -- Bind keys
//  doriax.InputBinder.bindKey(doriax.KeyCode.W, "move")
//  doriax.InputBinder.bindKey(doriax.KeyCode.Space, "jump")
//  doriax.InputBinder.bindGamepadButton(doriax.GamepadButton.A, "jump")
//
//  -- Activate map
//  doriax.InputActionMapRegistry.activateMap("gameplay")
//
//  -- Register Lua callbacks
//  local moveAction = gameplayMap:getAction("move")
//  moveAction:addCallback(function(value, x, y)
//      print("Move: x=" .. x .. ", y=" .. y)
//      player:move(x, y)
//  end)
//
//  local jumpAction = gameplayMap:getAction("jump")
//  jumpAction:addCallback(function(value, x, y)
//      if value > 0 then
//          player:jump()
//      end
//  end)
//
// RUNTIME REMAPPING:
// ===================
//
//  // Change jump key from Space to Enter at runtime
//  InputBinder::get().remapBinding("jump", KeyCode::Space, KeyCode::Enter);
//
//  // Lua equivalent
//  doriax.InputBinder.remapBinding("jump", doriax.KeyCode.Space, doriax.KeyCode.Enter)
//
// INPUT CONTEXTS:
// ================
//
//  // Switch from gameplay to menu input
//  InputActionMapRegistry::get().activateMap("menu");
//
//  // Menu map can have completely different bindings
//  auto menuMap = InputActionMapRegistry::get().createMap("menu");
//  menuMap->addAction("ui_up", InputActionType::Digital);
//  menuMap->addAction("ui_down", InputActionType::Digital);
//  menuMap->addAction("ui_select", InputActionType::Digital);
//
//  InputBinder::get().bindKey(KeyCode::Up, "ui_up");
//  InputBinder::get().bindKey(KeyCode::Down, "ui_down");
//  InputBinder::get().bindKey(KeyCode::Enter, "ui_select");
//
//  // Back to gameplay
//  InputActionMapRegistry::get().activateMap("gameplay");

#ifndef DORIAX_INPUT_SYSTEM_USAGE_H
#define DORIAX_INPUT_SYSTEM_USAGE_H

// See comments above for usage patterns and examples.

#endif // DORIAX_INPUT_SYSTEM_USAGE_H
