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

        // editor views, brought back by undo and redo
        YAML::Node oldCamera;
        YAML::Node newCamera;

        bool wasModified;

        bool canChangeType() const;
        YAML::Node editorCamera() const;

    public:
        SceneTypeCmd(Project* project, uint32_t sceneId, SceneType type);

        bool execute() override;
        void undo() override;
        bool canUndo() const override;
        bool canRedo() const override;

        bool mergeWith(Command* otherCommand) override;
    };

}
