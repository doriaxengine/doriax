#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <string>

#include "yaml-cpp/yaml.h"

namespace doriax::editor{

    class DuplicateEntityCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;

        std::vector<Entity> sourceEntities;
        std::vector<Entity> createdEntities;
        std::vector<Entity> lastSelected;
        bool wasModified;

        static void stripEntityIds(YAML::Node node);
        static std::string makeUniqueCopyName(const std::string& name, std::unordered_set<std::string>& existingNames);

    public:
        DuplicateEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;

        std::vector<Entity> getCreatedEntities() const;
    };

}
