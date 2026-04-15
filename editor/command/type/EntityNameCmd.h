#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>

namespace doriax::editor{

    class EntityNameCmd: public Command{

    private:
        std::string oldName;
        std::string newName;

        Project* project;
        uint32_t sceneId;
        Entity entity;

        bool wasModified;

    public:
        EntityNameCmd(Project* project, uint32_t sceneId, Entity entity, std::string name);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}