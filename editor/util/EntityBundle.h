#pragma once

#include <map>
#include <vector>
#include <memory>
#include <cstdint>
#include <algorithm>

#include "Scene.h"     // Entity, EntityRegistry, NULL_ENTITY
#include "Catalog.h"   // ComponentType

namespace doriax::editor {

class EntityBundle {
public:
    struct EntityMember {
        Entity localEntity;     // Entity in the scene
        Entity registryEntity;  // Corresponding entity in the shared registry
    };

    struct Instance {
        uint32_t instanceId;
        Entity rootEntity = NULL_ENTITY; // Root entity of this instance in the scene
        std::vector<EntityMember> members;
        std::map<Entity, uint64_t> overrides; // entityId → bitmask of overridden ComponentTypes
    };

    // sceneId → list of instances
    std::map<uint32_t, std::vector<Instance>> instances;

    std::unique_ptr<EntityRegistry> registry;
    std::vector<Entity> registryEntities;

    bool isModified = false;
    uint32_t nextInstanceId = 1;

    uint32_t getInstanceId(uint32_t sceneId, Entity entity) const;
    Entity getRegistryEntity(uint32_t sceneId, Entity entity) const;
    Entity getLocalEntity(uint32_t sceneId, uint32_t instanceId, Entity registryEntity) const;
    Entity getRootEntity(uint32_t sceneId, Entity entity) const;

    // Membership and instance access
    bool hasInstances(uint32_t sceneId) const;
    std::vector<Entity> getAllEntities(uint32_t sceneId, uint32_t instanceId) const;
    bool containsEntity(uint32_t sceneId, Entity entity) const;
    Instance* getInstance(uint32_t sceneId, Entity entity);
    const Instance* getInstance(uint32_t sceneId, Entity entity) const;
    Instance* getInstanceById(uint32_t sceneId, uint32_t instanceId);
    const Instance* getInstanceById(uint32_t sceneId, uint32_t instanceId) const;

    // Override management
    void setComponentOverride(uint32_t sceneId, Entity entity, ComponentType componentType);
    void clearComponentOverride(uint32_t sceneId, Entity entity, ComponentType componentType);
    bool hasComponentOverride(uint32_t sceneId, Entity entity, ComponentType componentType) const;

};

} // namespace doriax::editor

