//
// (c) 2026 Eduardo Doria.
//

#include "BundleManager.h"
#include "Scene.h"
#include "Log.h"

#include <algorithm>
#include <unordered_set>

using namespace doriax;

std::vector<BundleManager::BundleEntry> BundleManager::entries;
std::vector<BundleManager::BundleInstance> BundleManager::instances;

void BundleManager::registerBundle(uint32_t id, const std::string& name, std::function<void(Scene*, Entity)> factory, std::function<bool(Scene*, Entity)> destroyer) {
    // Overwrite if the id already exists
    for (auto& entry : entries) {
        if (entry.id == id) {
            entry.name = name;
            entry.factory = std::move(factory);
            entry.destroyer = std::move(destroyer);
            return;
        }
    }
    entries.push_back({id, name, std::move(factory), std::move(destroyer)});
}

Entity BundleManager::createBundle(const std::string& name, Scene* scene) {
    for (auto& entry : entries) {
        if (entry.name == name) {
            return createBundle(entry.id, scene);
        }
    }
    Log::error("BundleManager: bundle '%s' not found", name.c_str());
    return NULL_ENTITY;
}

Entity BundleManager::createBundle(uint32_t id, Scene* scene) {
    for (auto& entry : entries) {
        if (entry.id == id) {
            Entity root = scene->createEntity();
            return createBundle(id, scene, root);
        }
    }
    Log::error("BundleManager: bundle id %u not found", id);
    return NULL_ENTITY;
}

Entity BundleManager::createBundle(const std::string& name, Scene* scene, Entity root) {
    for (auto& entry : entries) {
        if (entry.name == name) {
            return createBundle(entry.id, scene, root);
        }
    }
    Log::error("BundleManager: bundle '%s' not found", name.c_str());
    return NULL_ENTITY;
}

Entity BundleManager::createBundle(uint32_t id, Scene* scene, Entity root) {
    for (auto& entry : entries) {
        if (entry.id == id) {
            // Snapshot entities before factory call
            std::vector<Entity> beforeEntities = scene->getEntityList();
            std::unordered_set<Entity> beforeSet(beforeEntities.begin(), beforeEntities.end());

            entry.factory(scene, root);

            // Determine which entities were created by the factory
            std::vector<Entity> afterEntities = scene->getEntityList();

            BundleInstance instance;
            instance.rootEntity = root;
            instance.scene = scene;
            instance.bundleId = id;
            instance.entities.push_back(root);
            for (Entity e : afterEntities) {
                if (e != root && beforeSet.find(e) == beforeSet.end()) {
                    instance.entities.push_back(e);
                }
            }

            instances.push_back(std::move(instance));
            return root;
        }
    }
    Log::error("BundleManager: bundle id %u not found", id);
    return NULL_ENTITY;
}

bool BundleManager::destroyBundle(Scene* scene, Entity rootEntity) {
    for (auto it = instances.begin(); it != instances.end(); ++it) {
        if (it->scene == scene && it->rootEntity == rootEntity) {
            uint32_t bundleId = it->bundleId;

            // Check for a custom destroyer
            for (auto& entry : entries) {
                if (entry.id == bundleId && entry.destroyer) {
                    bool result = entry.destroyer(scene, rootEntity);
                    instances.erase(it);
                    return result;
                }
            }

            // Default: destroy in reverse order (children before root)
            for (auto eit = it->entities.rbegin(); eit != it->entities.rend(); ++eit) {
                if (scene->isEntityCreated(*eit)) {
                    scene->destroyEntity(*eit);
                }
            }
            instances.erase(it);
            return true;
        }
    }
    Log::error("BundleManager: bundle instance with root %u not found in scene", rootEntity);
    return false;
}

uint32_t BundleManager::getBundleId(const std::string& name) {
    for (auto& entry : entries) {
        if (entry.name == name) return entry.id;
    }
    return 0;
}

std::string BundleManager::getBundleName(uint32_t id) {
    for (auto& entry : entries) {
        if (entry.id == id) return entry.name;
    }
    return "";
}

std::vector<std::string> BundleManager::getBundleNames() {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    return names;
}

int BundleManager::getBundleCount() {
    return (int)entries.size();
}

void BundleManager::destroyAllInstances(Scene* scene) {
    // Collect roots first to avoid iterator invalidation
    std::vector<Entity> roots;
    for (const auto& inst : instances) {
        if (inst.scene == scene) {
            roots.push_back(inst.rootEntity);
        }
    }
    for (Entity root : roots) {
        destroyBundle(scene, root);
    }
}

void BundleManager::clearAll() {
    entries.clear();
    instances.clear();
}
