#pragma once

#include "command/Command.h"
#include "Project.h"
#include "Catalog.h"
#include "yaml-cpp/yaml.h"

namespace doriax::editor {

    struct RemoveComponentData {
        Entity entity;
        YAML::Node oldComponent; // For no shared entities and no override components
        bool hasOverride;
        ComponentRecovery recovery; // For shared entities
    };

    class RemoveComponentCmd : public Command {
    private:
        Project* project;
        size_t sceneId;
        ComponentType componentType;

        std::vector<RemoveComponentData> entities;

        bool wasModified;

    public:
        RemoveComponentCmd(Project* project, size_t sceneId, Entity entity, ComponentType componentType);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };
}