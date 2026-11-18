// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>

namespace doriax::editor{

    class SceneTypeCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;

        SceneType oldType;
        SceneType newType;

        bool wasModified;

    public:
        SceneTypeCmd(Project* project, uint32_t sceneId, SceneType type);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
