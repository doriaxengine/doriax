// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "RenameFileCmd.h"

#include "EditorHost.h"
#include "util/Util.h"
#include "window/CodeEditor.h"
#include "window/GraphEditorWindow.h"
#include "window/ImageViewerWindow.h"

using namespace doriax;

namespace {

    // an open tab still holds the old path, where a later save would write the file again
    void remapWindows(const fs::path& from, const fs::path& to) {
        if (editor::CodeEditor* codeEditor = editor::getEditorHost().getCodeEditor())
            codeEditor->handleFileRename(from, to);
        if (editor::ImageViewerWindow* imageViewer = editor::getEditorHost().getImageViewerWindow())
            imageViewer->handleFileRename(from, to);
        if (editor::GraphEditorWindow* graphEditor = editor::getEditorHost().getGraphEditorWindow())
            graphEditor->handleFileRename(from, to);
    }

}

editor::RenameFileCmd::RenameFileCmd(Project* project, std::string oldName, std::string newName, std::string directory){
    this->project = project;
    this->oldFilename = fs::path(oldName).filename();
    this->newFilename = fs::path(newName).filename();
    this->directory = fs::path(directory);
}

void editor::RenameFileCmd::moveThumbnail(const fs::path& oldThumbnail, const fs::path& renamedFile){
    if (!project || oldThumbnail.empty()) {
        return;
    }

    std::error_code ec;
    // Nothing to move when the file never had a cached thumbnail.
    if (!fs::exists(oldThumbnail, ec) || ec) {
        return;
    }

    fs::path newThumbnail = project->getThumbnailPath(renamedFile);
    if (newThumbnail.empty() || newThumbnail == oldThumbnail) {
        return;
    }

    fs::create_directories(newThumbnail.parent_path(), ec);
    fs::rename(oldThumbnail, newThumbnail, ec);
}

bool editor::RenameFileCmd::execute(){
    fs::path sourceFs = directory / oldFilename;
    fs::path destFs = directory / newFilename;
    try {
        if (fs::exists(sourceFs)) {
            bool isDir = fs::is_directory(sourceFs);
            // Resolve the thumbnail path before the rename, while the source still exists.
            fs::path oldThumbnail = project ? project->getThumbnailPath(sourceFs) : fs::path();
            fs::rename(sourceFs, destFs);
            if (project) {
                moveThumbnail(oldThumbnail, destFs);
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
                if (isDir || Util::isScriptFile(extension) || Util::isLogicGraphFile(extension)) {
                    project->remapScriptFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isAssetFile(extension)) {
                    project->remapAssetFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isShaderFile(extension)) {
                    project->remapShaderFilePath(sourceFs, destFs);
                    project->invalidateCustomShaders();
                }
                remapWindows(sourceFs, destFs);
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
            // Resolve the thumbnail path before the rename, while the source still exists.
            fs::path oldThumbnail = project ? project->getThumbnailPath(sourceFs) : fs::path();
            fs::rename(sourceFs, destFs);
            if (project) {
                moveThumbnail(oldThumbnail, destFs);
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
                if (isDir || Util::isScriptFile(extension) || Util::isLogicGraphFile(extension)) {
                    project->remapScriptFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isAssetFile(extension)) {
                    project->remapAssetFilePath(sourceFs, destFs);
                }
                if (isDir || Util::isShaderFile(extension)) {
                    project->remapShaderFilePath(sourceFs, destFs);
                    project->invalidateCustomShaders();
                }
                remapWindows(sourceFs, destFs);
            }
        }
    } catch (const fs::filesystem_error& e) {
        printf("Error: Undo renaming %s: %s\n", sourceFs.string().c_str(), e.what());
    }
}

bool editor::RenameFileCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
