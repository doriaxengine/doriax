#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>

namespace doriax::editor{

    class SceneNameCmd: public Command{

    private:
        std::string oldName;
        std::string newName;

        Project* project;
        uint32_t sceneId;

        bool wasModified;

    public:
        SceneNameCmd(Project* project, uint32_t sceneId, std::string name);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}