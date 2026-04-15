#include "RenameFileCmd.h"

#include "util/Util.h"

using namespace doriax;

editor::RenameFileCmd::RenameFileCmd(Project* project, std::string oldName, std::string newName, std::string directory){
    this->project = project;
    this->oldFilename = fs::path(oldName).filename();
    this->newFilename = fs::path(newName).filename();
    this->directory = fs::path(directory);
}

bool editor::RenameFileCmd::execute(){
    fs::path sourceFs = directory / oldFilename;
    fs::path destFs = directory / newFilename;
    try {
        if (fs::exists(sourceFs)) {
            bool isDir = fs::is_directory(sourceFs);
            fs::rename(sourceFs, destFs);
            if (project) {
                std::string extension = sourceFs.extension().string();
                if (isDir || Util::isMaterialFile(extension)) {
                    project->remapMaterialFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isSceneFile(extension)) {
                    project->remapSceneFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isBundleFile(extension)) {
                    project->remapEntityBundleFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isScriptFile(extension)) {
                    project->remapScriptFilePath(sourceFs, destFs);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        printf("Error: Renaming %s: %s\n", sourceFs.string().c_str(), e.what());
        return false;
    }

    return true;
}

void editor::RenameFileCmd::undo(){
    fs::path sourceFs = directory / newFilename;
    fs::path destFs = directory / oldFilename;
    try {
        if (fs::exists(sourceFs)) {
            bool isDir = fs::is_directory(sourceFs);
            fs::rename(sourceFs, destFs);
            if (project) {
                std::string extension = sourceFs.extension().string();
                if (isDir || Util::isMaterialFile(extension)) {
                    project->remapMaterialFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isSceneFile(extension)) {
                    project->remapSceneFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isBundleFile(extension)) {
                    project->remapEntityBundleFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isScriptFile(extension)) {
                    project->remapScriptFilePath(sourceFs, destFs);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        printf("Error: Undo renaming %s: %s\n", sourceFs.string().c_str(), e.what());
    }
}

bool editor::RenameFileCmd::mergeWith(editor::Command* otherCommand){
    return false;
}