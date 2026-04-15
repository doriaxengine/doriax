#pragma once

#include "command/Command.h"
#include "Project.h"
#include "Catalog.h"

namespace doriax::editor {
    class AddComponentCmd : public Command {
    private:
        Project* project;
        size_t sceneId;
        std::vector<Entity> entities;
        ComponentType componentType;

        bool wasModified;

    public:
        AddComponentCmd(Project* project, size_t sceneId, Entity entity, ComponentType componentType);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };
}