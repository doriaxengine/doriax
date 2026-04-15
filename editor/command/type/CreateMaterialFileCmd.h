#pragma once

#include "command/Command.h"
#include "Project.h"
#include "util/EntityPayload.h"
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace doriax::editor {

    class CreateMaterialFileCmd: public Command {
    private:
        Project* project;
        fs::path targetFile;
        std::string payload;
        std::unique_ptr<Command> materialCmd;

        static fs::path findAvailableTargetFile(const fs::path& directory);

    public:
        CreateMaterialFileCmd(Project* project, const fs::path& directory, const char* materialContent,
                              size_t contentLen, const MaterialPayload* sourceMaterial = nullptr);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}