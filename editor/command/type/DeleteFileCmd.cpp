#include "DeleteFileCmd.h"

#include <filesystem>
#include <string>

using namespace doriax;

editor::DeleteFileCmd::DeleteFileCmd(Project* project, std::vector<fs::path> filePaths, fs::path rootPath){
    this->project = project;
    for (fs::path& file : filePaths){
        this->files.push_back({file, fs::path()});
    }
    this->trash = rootPath / ".trash";
}

fs::path editor::DeleteFileCmd::generateUniqueTrashPath(const fs::path& trashDir, const fs::path& originalFile) {
    fs::path basePath = trashDir / originalFile.filename();
    fs::path extension = originalFile.extension();
    fs::path stemPath = basePath.stem();
    
    if (!fs::exists(basePath)) {
        return basePath;
    }

    int counter = 1;
    fs::path newPath;
    do {
        newPath = trashDir / (stemPath.string() + " (" + std::to_string(counter) + ")" + extension.string());
        counter++;
    } while (fs::exists(newPath));

    return newPath;
}

bool editor::DeleteFileCmd::execute(){
    if (!fs::exists(trash)) {
        fs::create_directory(trash);
    }

    std::vector<fs::path> deletedPaths;

    for (DeleteFilesData& file : files){
        try {
            if (fs::exists(file.originalFile)) {
                // Check if file is inside .trash directory or is .trash directory
                if (file.originalFile == trash || 
                    fs::relative(file.originalFile, trash).string().find("..") == std::string::npos) {
                    if (fs::is_directory(file.originalFile)) {
                        fs::remove_all(file.originalFile);
                    } else {
                        fs::remove(file.originalFile);
                    }
                } else {
                    // Normal deletion - move to trash with unique name
                    if (!fs::exists(trash)) {
                        fs::create_directory(trash);
                    }
                    file.trashFile = generateUniqueTrashPath(trash, file.originalFile);
                    fs::rename(file.originalFile, file.trashFile);
                }

                deletedPaths.push_back(file.originalFile);
            }
        } catch (const fs::filesystem_error& e) {
            printf("Error: Deleting %s: %s\n", file.originalFile.string().c_str(), e.what());
            return false;
        }
    }

    if (project) {
        for (const auto& deletedPath : deletedPaths) {
            project->cleanupMaterialFilePath(deletedPath);
            project->cleanupSceneFilePath(deletedPath);
            project->cleanupEntityBundleFilePath(deletedPath);
            project->cleanupScriptFilePath(deletedPath);
        }
    }

    return true;
}

void editor::DeleteFileCmd::undo(){
    for (DeleteFilesData& file : files){
        try {
            // Only attempt to restore if the file was moved to trash (not permanently deleted)
            if (!file.trashFile.empty() && fs::exists(file.trashFile)) {
                fs::rename(file.trashFile, file.originalFile);
            }
        } catch (const fs::filesystem_error& e) {
            printf("Error: Restoring %s: %s\n", file.originalFile.string().c_str(), e.what());
        }
    }
}

bool editor::DeleteFileCmd::mergeWith(editor::Command* otherCommand){
    return false;
}