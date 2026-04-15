#pragma once

#include "command/Command.h"
#include "Project.h"
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace doriax::editor{

    struct FileCopyData{
        fs::path filename;
        fs::path sourceDirectory;
        fs::path targetDirectory;
    };

    class CopyFileCmd: public Command{

    private:

        Project* project;
        std::vector<FileCopyData> files;
        bool copy;

    public:
        CopyFileCmd(Project* project, std::vector<std::string> sourceFiles, std::string currentDirectory, std::string targetDirectory, bool remove);
        CopyFileCmd(Project* project, std::vector<std::string> sourcePaths, std::string targetDirectory, bool remove);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}