// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
        fs::path targetFilename; // empty until execute creates it
    };

    class CopyFileCmd: public Command{

    private:

        Project* project;
        std::vector<FileCopyData> files;
        bool copy;

        void remapPaths(const fs::path& from, const fs::path& to, bool isDir, const std::string& extension);

    public:
        CopyFileCmd(Project* project, std::vector<std::string> sourceFiles, std::string currentDirectory, std::string targetDirectory, bool copy);
        CopyFileCmd(Project* project, std::vector<std::string> sourcePaths, std::string targetDirectory, bool copy);

        std::vector<fs::path> getTargetPaths() const;

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
