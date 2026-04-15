#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>

namespace doriax::editor{

    class MoveEntityOrderCmd: public Command{

    friend class Project;
    friend class DeleteEntityCmd;

    private:
        Project* project;
        uint32_t sceneId;

        Entity source;
        Entity target;
        InsertionType type;

        size_t oldIndex;
        bool hasTransform;
        Entity oldParent;

        SharedMoveRecovery bundleMoveRecovery;

        bool wasModified;

    public:
        MoveEntityOrderCmd(Project* project, uint32_t sceneId, Entity source, Entity target, InsertionType type);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}