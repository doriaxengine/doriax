#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>

#include "yaml-cpp/yaml.h"

namespace doriax::editor{

    struct DeleteEntityData{
        Entity entity;

        size_t entityIndex;
        bool hasTransform = false;
        Entity parent = NULL_ENTITY;

        YAML::Node data;

        // Track if entity was part of a bundle
        NodeRecovery recoveryBundleData;
        uint32_t instanceId;
        bool isBundleRoot = false;
    };

    class DeleteEntityCmd: public Command{

    friend class Project;
    friend class CreateEntityCmd;
    friend class DuplicateEntityCmd;

    private:
        Project* project;
        uint32_t sceneId;

        std::vector<Entity> requestedEntities;
        bool allowLockedRoots;

        std::vector<Entity> lastSelected;

        std::vector<DeleteEntityData> entities;

        bool wasModified;

        static void destroyEntity(EntityRegistry* registry, Entity entity, std::vector<Entity>& entities, Project* project = nullptr, uint32_t sceneId = NULL_PROJECT_SCENE);

    public:
        DeleteEntityCmd(Project* project, uint32_t sceneId, Entity entity, bool allowLockedRoots = false);
        DeleteEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities, bool allowLockedRoots = false);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}