#pragma once

#include "command/Command.h"
#include "Project.h"
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace doriax::editor{

    class DeleteFileCmd: public Command{

        struct DeleteFilesData{
            fs::path originalFile;
            fs::path trashFile;
        };

    private:

        Project* project;
        std::vector<DeleteFilesData> files;
        fs::path trash;

        fs::path generateUniqueTrashPath(const fs::path& trashDir, const fs::path& originalFile);

    public:
        DeleteFileCmd(Project* project, std::vector<fs::path> filePaths, fs::path rootPath);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}