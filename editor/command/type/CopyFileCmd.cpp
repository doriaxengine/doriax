#include "CopyFileCmd.h"

#include "util/Util.h"

using namespace doriax;

editor::CopyFileCmd::CopyFileCmd(Project* project, std::vector<std::string> sourceFiles, std::string currentDirectory, std::string targetDirectory, bool copy){
    this->project = project;
    for (const auto& sourceFile : sourceFiles) {
        FileCopyData fdata;
        fdata.filename = fs::path(sourceFile).filename();
        fdata.sourceDirectory = fs::path(currentDirectory);
        fdata.targetDirectory = fs::path(targetDirectory);

        this->files.push_back(fdata);
    }
    this->copy = copy;
}

editor::CopyFileCmd::CopyFileCmd(Project* project, std::vector<std::string> sourcePaths, std::string targetDirectory, bool copy){
    this->project = project;
    for (const auto& sourcePath : sourcePaths) {
        FileCopyData fdata;
        fdata.filename = fs::path(sourcePath).filename();
        fdata.sourceDirectory = fs::path(sourcePath).remove_filename();
        fdata.targetDirectory = fs::path(targetDirectory);

        this->files.push_back(fdata);
    }
    this->copy = copy;
}

bool editor::CopyFileCmd::execute(){
    for (const auto& fdata : files) {
        fs::path sourceFs = fdata.sourceDirectory / fdata.filename;
        fs::path destFs = fdata.targetDirectory / fdata.filename;
        try {
            if (fs::exists(sourceFs)) {
                if (fs::is_directory(sourceFs)) {
                    if (copy){
                        fs::copy(sourceFs, destFs, fs::copy_options::recursive);
                    }else{
                        fs::rename(sourceFs, destFs);
                        if (project) {
                            project->remapMaterialFilePath(sourceFs, destFs);
                            project->remapSceneFilePath(sourceFs, destFs);
                            project->remapEntityBundleFilePath(sourceFs, destFs);
                            project->remapScriptFilePath(sourceFs, destFs);
                        }
                    }
                } else {
                    if (copy){
                        fs::copy(sourceFs, destFs, fs::copy_options::overwrite_existing);
                    }else{
                        fs::rename(sourceFs, destFs);
                        if (project) {
                            std::string extension = sourceFs.extension().string();
                            if (Util::isMaterialFile(extension)) {
                                project->remapMaterialFilePath(sourceFs, destFs);
                            }
                            if (Util::isSceneFile(extension)) {
                                project->remapSceneFilePath(sourceFs, destFs);
                            }
                            if (Util::isBundleFile(extension)) {
                                project->remapEntityBundleFilePath(sourceFs, destFs);
                            }
                            if (Util::isScriptFile(extension)) {
                                project->remapScriptFilePath(sourceFs, destFs);
                            }
                        }
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            printf("Error: Moving/Copying %s: %s\n", sourceFs.string().c_str(), e.what());
            return false;
        }
    }

    return true;
}

void editor::CopyFileCmd::undo(){
    for (const auto& fdata : files) {
        fs::path sourceFs = fdata.targetDirectory / fdata.filename;
        fs::path destFs = fdata.sourceDirectory / fdata.filename;
        try {
            if (fs::exists(sourceFs)) {
                if (fs::is_directory(sourceFs)) {
                    if (copy) {
                        fs::remove_all(sourceFs);
                    }else{
                        fs::rename(sourceFs, destFs);
                        if (project) {
                            project->remapMaterialFilePath(sourceFs, destFs);
                            project->remapSceneFilePath(sourceFs, destFs);
                            project->remapEntityBundleFilePath(sourceFs, destFs);
                            project->remapScriptFilePath(sourceFs, destFs);
                        }
                    }
                } else {
                    if (copy) {
                        fs::remove(sourceFs);
                    }else{
                        fs::rename(sourceFs, destFs);
                        if (project) {
                            std::string extension = sourceFs.extension().string();
                            if (Util::isMaterialFile(extension)) {
                                project->remapMaterialFilePath(sourceFs, destFs);
                            }
                            if (Util::isSceneFile(extension)) {
                                project->remapSceneFilePath(sourceFs, destFs);
                            }
                            if (Util::isBundleFile(extension)) {
                                project->remapEntityBundleFilePath(sourceFs, destFs);
                            }
                            if (Util::isScriptFile(extension)) {
                                project->remapScriptFilePath(sourceFs, destFs);
                            }
                        }
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            printf("Error: Undo moving/Copying %s: %s\n", sourceFs.string().c_str(), e.what());
        }
    }
}

bool editor::CopyFileCmd::mergeWith(editor::Command* otherCommand){
    return false;
}