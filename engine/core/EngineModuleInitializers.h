//
// (c) 2026 Eduardo Doria.
//
// EngineModuleInitializers - Example initialization patterns for modern engine modules
//
// This demonstrates how the new ECS, scene/prefab, asset, and threading systems
// are wired into the engine lifecycle via the EngineCore module system.
//
// Key Initialization Order:
// 1. ThreadPoolManager (foundation for async work)
// 2. JobSystem (task scheduling on thread pool)
// 3. AssetRegistry + AssetManager (asset loading infrastructure)
// 4. PrefabRegistry (prefab definitions)
// 5. SceneStream + SceneManager (scene loading)
// 6. World (ECS scheduler)
//
// Example in EngineCore::initialize():
// ====================================
//
//     EngineCore core;
//
//     // Initialize foundation systems
//     auto threadPool = ThreadPoolManager::get();
//     threadPool.initialize(4);  // 4 worker threads
//
//     JobSystem::get().initialize(&threadPool);
//     AssetManager::get().initialize();
//
//     // Setup scenes (assumes SceneManager already exists)
//     SceneConfig levelConfig;
//     levelConfig.name = "main_menu";
//     levelConfig.path = "assets/scenes/menu.scene";
//     SceneIntegration::registerScene(levelConfig);
//
//     // Setup prefabs (game-specific)
//     SceneIntegration::registerPrefabFactory("player", []( Scene* scene, Entity root) {
//         auto transform = scene->addComponent<Transform>(root);
//         auto mesh = scene->addComponent<MeshComponent>(root);
//         // ... more setup
//     });
//
//     // World scheduler initialization
//     World& world = engine.world();
//     world.addSystem<PhysicsSystem>(SystemStage::Update);
//     world.addSystem<RenderSystem>(SystemStage::Render);
//
// Cleanup Pattern:
// ================
//
//     core.shutdown();  // Calls all registered module shutdown hooks
//
//     // Module-specific cleanup (often automatic via RAII)
//     AssetManager::get().shutdown();
//     JobSystem::get().shutdown();
//     ThreadPoolManager::get().shutdown();

#ifndef DORIAX_ENGINE_MODULE_INITIALIZERS_H
#define DORIAX_ENGINE_MODULE_INITIALIZERS_H

// For documentation purposes. See EngineCore.h and related module headers for actual APIs.

#endif // DORIAX_ENGINE_MODULE_INITIALIZERS_H
