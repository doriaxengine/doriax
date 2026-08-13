//
// (c) 2026 Eduardo Doria.
//
// SceneIntegrationSetup - Example initialization pattern for the modern scene/prefab system
//
// This module demonstrates how to integrate SceneStream, Prefab, and PrefabRegistry
// into your game's initialization and gameplay loop.
//
// USAGE PATTERN:
// ==============
//
// 1. During engine initialization (e.g., in your game's App::init()):
//
//    #include "scene/SceneIntegration.h"
//
//    void MyGame::init() {
//        // Register prefabs early
//        SceneIntegration::registerPrefabFactory(
//            "player",
//            [](Scene* scene, Entity root) {
//                // Create player entity hierarchy
//                auto playerComp = scene->addComponent<MeshComponent>(root);
//                // ... more setup
//            }
//        );
//
//        // Register scenes with SceneStream
//        SceneConfig levelConfig;
//        levelConfig.name = "level_1";
//        levelConfig.path = "assets/scenes/level_1.scene";
//        levelConfig.additive = false;
//        levelConfig.streamingMode = SceneStreamingMode::Manual;
//
//        SceneIntegration::registerScene(
//            levelConfig,
//            SceneIntegration::makeSceneManagerLoader(SCENE_LEVEL_1, load_Level_1),
//            [](const std::string& name) {
//                Log::info("Unloaded scene: %s", name.c_str());
//            }
//        );
//    }
//
// 2. During gameplay, load scenes and instantiate prefabs:
//
//    void MyGame::loadLevel(const std::string& levelName) {
//        SceneIntegration::loadScene(levelName);
//    }
//
//    void MyGame::spawnEnemy(const std::string& prefabName, Scene* scene) {
//        Entity enemy = SceneIntegration::instantiatePrefab(prefabName, scene);
//        if (enemy != NULL_ENTITY) {
//            // Position the enemy, etc.
//        }
//    }
//
// 3. From Lua scripts:
//
//    -- Spawn a prefab instance
//    local player = doriax.SceneIntegration.instantiatePrefab("player", scene)
//
//    -- Load a scene
//    if doriax.SceneIntegration.loadScene("level_1") then
//        print("Level loaded!")
//    end
//
//    -- Check if a prefab exists
//    if doriax.SceneIntegration.isPrefabRegistered("enemies/Goblin") then
//        local goblin = doriax.SceneIntegration.instantiatePrefab("enemies/Goblin", scene)
//    end
//
// KEY INTEGRATION POINTS:
// =======================
//
// - SceneIntegration sits between game code and the low-level ECS/asset systems
// - PrefabRegistry holds all prefab factories (C++ and Lua-callable)
// - SceneStream handles scene metadata and loading callbacks
// - SceneManager integration via makeSceneManagerLoader() bridges old and new
// - Lua bindings expose the full API to scripts for runtime scene/prefab control
//
// ARCHITECTURE:
// ==============
//
//  Game Code (C++ / Lua)
//      |
//  SceneIntegration API (bridge layer)
//      |
//  +---+---+---+
//  |   |   |   |
//  PrefabRegistry  SceneStream  SceneManager  BundleManager
//  |
//  Prefab (factory/destroyer)
//  |
//  EntityRegistry / Scene (low-level ECS)

#ifndef DORIAX_SCENE_INTEGRATION_SETUP_H
#define DORIAX_SCENE_INTEGRATION_SETUP_H

// This file is primarily for documentation. Include SceneIntegration.h for the actual API.
// See the comments above for usage patterns and architecture.

#include "scene/SceneIntegration.h"

#endif // DORIAX_SCENE_INTEGRATION_SETUP_H
