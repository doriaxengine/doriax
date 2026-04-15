#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace doriax::editor{

    class ImportEntityBundleCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        fs::path filepath;
        Entity parent;
        bool needSaveScene;

        std::vector<Entity> importedEntities;
        Entity rootEntity;
        std::vector<Entity> lastSelected;
        bool wasModified;
        bool addedToParentBundle;

    public:
        ImportEntityBundleCmd(Project* project, uint32_t sceneId, const fs::path& filepath, Entity parent = NULL_ENTITY, bool needSaveScene = true);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;

        std::vector<Entity> getImportedEntities() const;
    };

}
