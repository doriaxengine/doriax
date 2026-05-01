#pragma once

#include "command/Command.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "Project.h"
#include "math/Vector3.h"
#include "yaml-cpp/yaml.h"

namespace doriax::editor{

    class ModelLoadCmd: public Command{

    private:
        YAML::Node oldTransform;
        YAML::Node oldMesh;
        YAML::Node oldModel;
        DeleteEntityCmd* oldSubEntitiesDeleteCmd = nullptr;
        CreateEntityCmd* createEntityCmd = nullptr;

        Project* project;
        uint32_t sceneId;
        Entity entity;

        std::string modelPath;

        bool wasModified;

        static std::vector<Entity> collectModelDeleteRoots(const ModelComponent& model);

    public:
        ModelLoadCmd(Project* project, uint32_t sceneId, Entity entity, const std::string& modelPath);
        ModelLoadCmd(Project* project, uint32_t sceneId, const std::string& entityName, const Vector3& position, const std::string& modelPath);
        ~ModelLoadCmd() override;

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
