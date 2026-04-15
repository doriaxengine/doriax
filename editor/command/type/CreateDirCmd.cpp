#include "CreateDirCmd.h"

using namespace doriax;

editor::CreateDirCmd::CreateDirCmd(std::string dirName, std::string dirPath){
    this->directory = fs::path(dirPath) / fs::path(dirName);
}

bool editor::CreateDirCmd::execute(){
    try {
        fs::create_directory(directory);
    } catch (const fs::filesystem_error& e) {
        printf("Error: Creating directory %s: %s\n", directory.string().c_str(), e.what());
        return false;
    }

    return true;
}

void editor::CreateDirCmd::undo(){
    try {
        fs::remove_all(directory);
    } catch (const fs::filesystem_error& e) {
        printf("Error: Undo creating directory %s: %s\n", directory.string().c_str(), e.what());
    }
}

bool editor::CreateDirCmd::mergeWith(editor::Command* otherCommand){
    return false;
}