//
// (c) 2026 Eduardo Doria.
//

#ifndef BUNDLEMANAGER_H
#define BUNDLEMANAGER_H

#include "Export.h"
#include "Entity.h"
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace doriax {

    class Scene;

    // BundleManager allows registering named entity bundles and instantiating/destroying
    // them at runtime. A "bundle" is a reusable prefab-like group of entities that can
    // be spawned into any Scene under a given root entity.
    //
    // The factory function receives (Scene*, Entity root) and is responsible for creating
    // child entities, components, and hierarchy under the provided root.
    //
    // Usage from C++ (standalone generated code):
    //   BundleManager::registerBundle(1, "enemies/EnemyShip", create_bundle_enemies_EnemyShip);
    //   Entity root = BundleManager::createBundle("enemies/EnemyShip", scene);
    //   BundleManager::destroyBundle(scene, root);
    //
    // Usage from Lua script:
    //   local root = BundleManager.createBundle("enemies/EnemyShip", scene)
    //   BundleManager.destroyBundle(scene, root)

    class DORIAX_API BundleManager {
    private:
        struct BundleEntry {
            uint32_t id;
            std::string name;
            std::function<void(Scene*, Entity)> factory;
            std::function<bool(Scene*, Entity)> destroyer;
        };

        struct BundleInstance {
            Entity rootEntity;
            Scene* scene;
            uint32_t bundleId;
            std::vector<Entity> entities; // all entities including root
        };

        static std::vector<BundleEntry> entries;
        static std::vector<BundleInstance> instances;

    public:
        // Register a named bundle factory.
        // The factory receives (Scene*, Entity root) and must create entities/components
        // as children of root.
        // The optional destroyer is called by destroyBundle() instead of the default
        // entity destruction. It receives (Scene*, Entity root) and should return true
        // on success.
        static void registerBundle(uint32_t id, const std::string& name, std::function<void(Scene*, Entity)> factory, std::function<bool(Scene*, Entity)> destroyer = nullptr);

        // Create a bundle instance by name. Creates a new root entity in the scene,
        // invokes the factory, and tracks the instance. Returns the root entity,
        // or NULL_ENTITY if the bundle name is not found.
        static Entity createBundle(const std::string& name, Scene* scene);

        // Create a bundle instance by id.
        // Returns the root entity, or NULL_ENTITY if the id is not found.
        static Entity createBundle(uint32_t id, Scene* scene);

        // Create a bundle instance under an existing root entity (by name).
        // The root must already exist in the scene. Returns the root entity,
        // or NULL_ENTITY if the bundle name is not found.
        static Entity createBundle(const std::string& name, Scene* scene, Entity root);

        // Create a bundle instance under an existing root entity (by id).
        // Returns the root entity, or NULL_ENTITY if the id is not found.
        static Entity createBundle(uint32_t id, Scene* scene, Entity root);

        // Destroy a bundle instance by its root entity. Destroys all tracked
        // entities (children first, then root). Returns false if the root
        // entity was not found among tracked instances.
        static bool destroyBundle(Scene* scene, Entity rootEntity);

        // Return the bundle id by name, or 0 if not found.
        static uint32_t getBundleId(const std::string& name);

        // Return the bundle name by id, or "" if not found.
        static std::string getBundleName(uint32_t id);

        // Return all registered bundle names in registration order.
        static std::vector<std::string> getBundleNames();

        // Return the total number of registered bundles.
        static int getBundleCount();

        // Destroy all tracked instances for a given scene.
        // Calls the appropriate destroyer for each instance.
        static void destroyAllInstances(Scene* scene);

        // Remove all registered bundles and tracked instances.
        static void clearAll();
    };

} // namespace doriax

#endif // BUNDLEMANAGER_H
