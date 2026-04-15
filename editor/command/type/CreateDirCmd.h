#pragma once

#include "command/Command.h"
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace doriax::editor{

    class CreateDirCmd: public Command{

    private:

        fs::path directory;

    public:
        CreateDirCmd(std::string dirName, std::string dirPath);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}