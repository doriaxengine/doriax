#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>

namespace doriax::editor{

    struct TransformCmdValue{
        Vector3 newPosition;
        Quaternion newRotation;
        Vector3 newScale;

        Vector3 oldPosition;
        Quaternion oldRotation;
        Vector3 oldScale;
    };

    class ObjectTransformCmd: public Command{

    private:
        Project* project;
        size_t sceneId;

        std::map<Entity, TransformCmdValue> props;

        bool wasModified;

    public:
        ObjectTransformCmd(Project* project, size_t sceneId, Entity entity, Matrix4 localMatrix);
        ObjectTransformCmd(Project* project, size_t sceneId, Entity entity, Vector3 position, Quaternion rotation, Vector3 scale);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}