#pragma once

#include "command/Command.h"
#include "Project.h"

namespace doriax::editor {
    class RemoveChildSceneCmd : public Command {
    private:
        Project* project;
        uint32_t sceneId;
        uint32_t childSceneId;
        int index;

        bool wasModified;

    public:
        RemoveChildSceneCmd(Project* project, uint32_t sceneId, uint32_t childSceneId);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };
}
