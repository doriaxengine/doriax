#pragma once

#include "command/Command.h"
#include "Project.h"

namespace doriax::editor {
    class AddChildSceneCmd : public Command {
    private:
        Project* project;
        uint32_t sceneId;
        uint32_t childSceneId;

        bool wasModified;

    public:
        AddChildSceneCmd(Project* project, uint32_t sceneId, uint32_t childSceneId);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };
}
