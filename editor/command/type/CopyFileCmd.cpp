// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CopyFileCmd.h"

#include "Out.h"
#include "util/Util.h"
#include "util/FileUtils.h"
#include "util/TextEncoding.h"

using namespace doriax;

namespace {

// Also true for a dangling symlink, which fs::exists() misses
bool isTaken(const fs::path& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

// name_2.ext, name_3.ext...
fs::path findAvailableName(const fs::path& directory, const fs::path& filename, bool isDir) {
    const fs::path stem = isDir ? filename : filename.stem();
    const fs::path extension = isDir ? fs::path() : filename.extension();

    for (int counter = 2; ; counter++) {
        fs::path candidate = stem;
        candidate += "_" + std::to_string(counter);
        candidate += extension;
        if (!isTaken(directory / candidate)) {
            return candidate;
        }
    }
}

} // namespace

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

std::vector<fs::path> editor::CopyFileCmd::getTargetPaths() const{
    std::vector<fs::path> paths;
    for (const auto& fdata : files) {
        if (!fdata.targetFilename.empty()) {
            paths.push_back(fdata.targetDirectory / fdata.targetFilename);
        }
    }
    return paths;
}

void editor::CopyFileCmd::remapPaths(const fs::path& from, const fs::path& to, bool isDir, const std::string& extension){
    if (!project) return;

    if (isDir || Util::isMaterialFile(extension)) project->remapMaterialFilePath(from, to);
    if (isDir || Util::isSceneFile(extension)) project->remapSceneFilePath(from, to);
    if (isDir || Util::isBundleFile(extension)) project->remapEntityBundleFilePath(from, to);
    if (isDir || Util::isScriptFile(extension)) project->remapScriptFilePath(from, to);
    if (isDir || Util::isAssetFile(extension)) project->remapAssetFilePath(from, to);
    if (isDir || Util::isShaderFile(extension)) project->remapShaderFilePath(from, to);
}

// Two files sharing a scene id make the loader give one a new id on every load
void editor::CopyFileCmd::assignNewSceneIds(const fs::path& sourcePath, const fs::path& copyPath, bool isDir){
    if (!project) return;

    // A copy made inside the project leaves its original there with the same id
    bool imported = !Util::isInsidePath(sourcePath, project->getProjectPath());

    std::vector<fs::path> scenePaths;
    if (isDir) {
        for (const auto& entry : fs::recursive_directory_iterator(copyPath)) {
            if (Util::isSceneFile(entry.path().extension().string())) {
                scenePaths.push_back(entry.path());
            }
        }
    } else if (Util::isSceneFile(copyPath.extension().string())) {
        scenePaths.push_back(copyPath);
    }

    for (const auto& scenePath : scenePaths) {
        YAML::Node root;
        try {
            root = TextEncoding::loadYamlFile(scenePath);
        } catch (const std::exception&) {
            continue; // not a scene the editor reads, so no id to clash
        }
        if (!root.IsMap() || !root["id"]) continue;
        uint32_t fileId = root["id"].as<uint32_t>(NULL_PROJECT_SCENE);

        // Chosen once, so redo gives the copy the same id even after it joined the project.
        // Imports keep an id no project scene uses, so links between imported scenes hold.
        auto it = sceneIds.find(scenePath);
        if (it == sceneIds.end()) {
            bool keepId = imported && !project->getScene(fileId);
            it = sceneIds.emplace(scenePath, keepId ? fileId : project->allocateSceneId()).first;
        }
        if (it->second == fileId) continue;
        root["id"] = it->second;

        std::error_code ec;
        if (!FileUtils::writeFileReplacing(scenePath, YAML::Dump(root) + "\n", ec)) {
            throw fs::filesystem_error("cannot write scene id", scenePath, ec);
        }
    }
}

bool editor::CopyFileCmd::execute(){
    bool touchedShaderSource = false;
    bool changed = false;

    for (auto& fdata : files) {
        fs::path sourceFs = fdata.sourceDirectory / fdata.filename;
        try {
            if (!fs::exists(sourceFs)) continue;

            bool isDir = fs::is_directory(sourceFs);
            std::string extension = sourceFs.extension().string();
            // Copies count too: a .glsl arriving where a shader file's includes
            // resolve changes which file wins for it.
            touchedShaderSource |= isDir || Util::isShaderFile(extension);

            if (isDir && Util::isInsidePath(fs::weakly_canonical(fdata.targetDirectory), fs::weakly_canonical(sourceFs))) {
                Out::error("Cannot %s \"%s\" into itself", copy ? "copy" : "move", FileUtils::pathToUtf8(fdata.filename).c_str());
                undo();
                return false;
            }

            fs::path targetName = fdata.filename;
            if (isTaken(fdata.targetDirectory / targetName)) {
                // Moved onto itself
                std::error_code ec;
                if (!copy && fs::equivalent(sourceFs, fdata.targetDirectory / targetName, ec)) continue;
                targetName = findAvailableName(fdata.targetDirectory, fdata.filename, isDir);
            }
            fs::path destFs = fdata.targetDirectory / targetName;

            if (copy) {
                if (isDir) {
                    // Created here first, so a failed copy never removes someone else's folder
                    if (!fs::create_directory(destFs, sourceFs)) {
                        throw fs::filesystem_error("cannot copy", destFs, std::make_error_code(std::errc::file_exists));
                    }
                    fdata.targetFilename = targetName;
                    fs::copy(sourceFs, destFs, fs::copy_options::recursive);
                } else {
                    std::error_code ec;
                    if (!fs::copy_file(sourceFs, destFs, ec)) {
                        // copy_file never opens a file that exists, so anything else there is its partial output
                        std::error_code removeEc;
                        if (ec != std::errc::file_exists) fs::remove(destFs, removeEc);
                        throw fs::filesystem_error("cannot copy file", sourceFs, destFs, ec);
                    }
                    fdata.targetFilename = targetName;
                }
                assignNewSceneIds(sourceFs, destFs, isDir);
            } else {
                fs::rename(sourceFs, destFs);
                fdata.targetFilename = targetName;
                remapPaths(sourceFs, destFs, isDir, extension);
            }
            changed = true;
        } catch (const fs::filesystem_error& e) {
            Out::error("Cannot %s \"%s\": %s", copy ? "copy" : "move", FileUtils::pathToUtf8(fdata.filename).c_str(), e.what());
            undo();
            return false;
        }
    }

    if (project && touchedShaderSource)
        project->invalidateCustomShaders();

    // A no-op stays out of the history
    return changed;
}

void editor::CopyFileCmd::undo(){
    bool touchedShaderSource = false;

    for (auto& fdata : files) {
        if (fdata.targetFilename.empty()) continue;

        fs::path targetFs = fdata.targetDirectory / fdata.targetFilename;
        fs::path sourceFs = fdata.sourceDirectory / fdata.filename;
        try {
            bool isDir = fs::is_directory(targetFs);
            std::string extension = fdata.filename.extension().string();
            touchedShaderSource |= isDir || Util::isShaderFile(extension);

            if (copy) {
                fs::remove_all(targetFs);
            } else {
                fs::rename(targetFs, sourceFs);
                remapPaths(targetFs, sourceFs, isDir, extension);
            }
        } catch (const fs::filesystem_error& e) {
            Out::error("Cannot undo %s of \"%s\": %s", copy ? "copy" : "move", FileUtils::pathToUtf8(fdata.targetFilename).c_str(), e.what());
        }
        fdata.targetFilename.clear();
    }

    if (project && touchedShaderSource)
        project->invalidateCustomShaders();
}

bool editor::CopyFileCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
