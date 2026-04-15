#pragma once

#include "command/Command.h"
#include "Project.h"
#include "Catalog.h"

namespace doriax::editor {

    class ComponentToBundleLocalCmd: public Command {
    private:
        Project* project;
        uint32_t sceneId;
        std::vector<Entity> entities;
        ComponentType componentType;

        bool wasModified;

    public:
        ComponentToBundleLocalCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType componentType);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
