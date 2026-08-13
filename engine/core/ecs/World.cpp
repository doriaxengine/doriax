//
// (c) 2026 Eduardo Doria.
//

#include "ecs/World.h"
#include "Log.h"
#include <algorithm>
#include <set>

namespace doriax {

    World::World() = default;
    World::~World() = default;

    Entity World::createEntity() {
        return entityManager_.createUserEntity();
    }

    Entity World::createUserEntity() {
        return entityManager_.createUserEntity();
    }

    bool World::isEntityCreated(Entity entity) const {
        return entityManager_.isCreated(entity);
    }

    void World::destroyEntity(Entity entity) {
        if (!entityManager_.isCreated(entity)) {
            return;
        }

        componentManager_.entityDestroyed(entity);
        entityManager_.destroy(entity);
    }

    void World::clear() {
        for (Entity entity : entityManager_.getEntityList()) {
            componentManager_.entityDestroyed(entity);
        }
        entityManager_.getEntityList();
        entityManager_ = EntityManager();
    }

    EntityManager& World::entityManager() {
        return entityManager_;
    }

    const EntityManager& World::entityManager() const {
        return entityManager_;
    }

    ComponentManager& World::componentManager() {
        return componentManager_;
    }

    const ComponentManager& World::componentManager() const {
        return componentManager_;
    }

    void World::addSystem(const std::string& name, SystemStage stage, SystemCallback callback,
                          const std::vector<std::string>& dependencies) {
        if (callback == nullptr) {
            Log::warn("World::addSystem: callback was null for system '%s'", name.c_str());
            return;
        }

        if (systems_.find(name) != systems_.end()) {
            Log::warn("World::addSystem: system '%s' already registered", name.c_str());
            return;
        }

        systems_[name] = SystemEntry{ name, stage, dependencies, std::move(callback) };
        systemOrder_.push_back(name);
    }

    std::vector<std::string> World::sortDependencies(const std::vector<std::string>& names,
        const std::unordered_map<std::string, SystemEntry>& systems,
        SystemStage stage) {
        std::vector<std::string> ordered;
        std::set<std::string> seen;
        std::set<std::string> visiting;

        std::function<void(const std::string&)> visit = [&](const std::string& name) {
            if (seen.find(name) != seen.end()) {
                return;
            }
            if (visiting.find(name) != visiting.end()) {
                Log::warn("World::execute: dependency cycle detected for system '%s'", name.c_str());
                return;
            }

            auto it = systems.find(name);
            if (it == systems.end()) {
                return;
            }
            if (it->second.stage != stage) {
                return;
            }

            visiting.insert(name);
            for (const std::string& dependency : it->second.dependencies) {
                auto depIt = systems.find(dependency);
                if (depIt != systems.end() && depIt->second.stage == stage) {
                    visit(dependency);
                }
            }
            visiting.erase(name);
            seen.insert(name);
            ordered.push_back(name);
        };

        for (const std::string& name : names) {
            visit(name);
        }

        return ordered;
    }

    void World::execute(SystemStage stage) {
        std::vector<std::string> orderedSystems;
        for (const auto& entry : systems_) {
            if (entry.second.stage == stage) {
                orderedSystems.push_back(entry.first);
            }
        }

        orderedSystems = sortDependencies(orderedSystems, systems_, stage);
        for (const std::string& name : orderedSystems) {
            auto it = systems_.find(name);
            if (it == systems_.end() || it->second.callback == nullptr) {
                continue;
            }
            it->second.callback(*this);
        }
    }

}
