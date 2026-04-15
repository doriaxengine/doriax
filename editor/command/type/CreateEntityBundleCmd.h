#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <filesystem>
#include <vector>
#include "yaml-cpp/yaml.h"

namespace fs = std::filesystem;

namespace doriax::editor{

    class CreateEntityBundleCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        fs::path filepath;
        YAML::Node bundleNode; // Optional: if provided, use this instead of encoding

        // For undo
        bool wasModified;
        std::vector<Entity> lastSelected;
        YAML::Node savedEntityNode;
        bool wasSuccessful;

    public:
        CreateEntityBundleCmd(Project* project, uint32_t sceneId, const fs::path& filepath, const YAML::Node& bundleNode);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}