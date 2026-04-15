#pragma once

#include "command/Command.h"
#include "Project.h"
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace doriax::editor{

    class RenameFileCmd: public Command{

    private:

        Project* project;
        fs::path oldFilename;
        fs::path newFilename;
        fs::path directory;

    public:
        RenameFileCmd(Project* project, std::string oldName, std::string newName, std::string directory);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}