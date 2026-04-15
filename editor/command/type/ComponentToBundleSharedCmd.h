#pragma once

#include "command/Command.h"
#include "Project.h"
#include "Catalog.h"

namespace doriax::editor {

    struct ComponentToBundleSharedData {
        Entity entity;
        YAML::Node recovery;
    };

    class ComponentToBundleSharedCmd: public Command {
    private:
        Project* project;
        uint32_t sceneId;
        ComponentType componentType;

        std::vector<ComponentToBundleSharedData> entities;

        bool wasModified;

    public:
        ComponentToBundleSharedCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType componentType);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
