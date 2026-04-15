#pragma once

#include "command/Command.h"
#include "Project.h"
#include "util/ShapeParameters.h"
#include "yaml-cpp/yaml.h"

namespace doriax::editor{

    class MeshChangeCmd: public Command{

    private:
        YAML::Node oldMesh;
        YAML::Node newMesh;

        Project* project;
        uint32_t sceneId;
        Entity entity;

        bool wasModified;

    public:
        MeshChangeCmd(Project* project, uint32_t sceneId, Entity entity, MeshComponent mesh);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}