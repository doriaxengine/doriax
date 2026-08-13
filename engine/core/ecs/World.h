//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_WORLD_H
#define DORIAX_WORLD_H

#include "ComponentManager.h"
#include "EntityManager.h"
#include "Export.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    enum class SystemStage {
        PreUpdate,
        Update,
        FixedUpdate,
        PostUpdate
    };

    class DORIAX_API World {
    public:
        using SystemCallback = std::function<void(World&)>;

        World();
        ~World();

        Entity createEntity();
        Entity createUserEntity();
        bool isEntityCreated(Entity entity) const;
        void destroyEntity(Entity entity);
        void clear();

        EntityManager& entityManager();
        const EntityManager& entityManager() const;
        ComponentManager& componentManager();
        const ComponentManager& componentManager() const;

        template<typename T>
        void registerComponent() {
            componentManager_.registerComponent<T>();
        }

        template<typename T>
        void addComponent(Entity entity, const T& component) {
            componentManager_.addComponent<T>(entity, component);
            auto signature = entityManager_.getSignature(entity);
            signature.set(componentManager_.getComponentId<T>(), true);
            entityManager_.setSignature(entity, signature);
        }

        template<typename T>
        void addComponent(Entity entity) {
            componentManager_.addComponent<T>(entity);
            auto signature = entityManager_.getSignature(entity);
            signature.set(componentManager_.getComponentId<T>(), true);
            entityManager_.setSignature(entity, signature);
        }

        template<typename T>
        void removeComponent(Entity entity) {
            if (!isEntityCreated(entity)) {
                return;
            }
            componentManager_.removeComponent<T>(entity);
            auto signature = entityManager_.getSignature(entity);
            signature.set(componentManager_.getComponentId<T>(), false);
            entityManager_.setSignature(entity, signature);
        }

        template<typename T>
        T* findComponent(Entity entity) {
            return componentManager_.findComponent<T>(entity);
        }

        template<typename T>
        T& getComponent(Entity entity) const {
            return componentManager_.getComponent<T>(entity);
        }

        void addSystem(const std::string& name, SystemStage stage, SystemCallback callback,
                       const std::vector<std::string>& dependencies = {});
        void execute(SystemStage stage);

    private:
        struct SystemEntry {
            std::string name;
            SystemStage stage;
            std::vector<std::string> dependencies;
            SystemCallback callback;
        };

        EntityManager entityManager_;
        ComponentManager componentManager_;
        std::unordered_map<std::string, SystemEntry> systems_;
        std::vector<std::string> systemOrder_;

        static std::vector<std::string> sortDependencies(const std::vector<std::string>& names,
            const std::unordered_map<std::string, SystemEntry>& systems,
            SystemStage stage);
    };

}

#endif // DORIAX_WORLD_H
