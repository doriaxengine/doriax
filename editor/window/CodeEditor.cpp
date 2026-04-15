#include "CodeEditor.h"

#include "Backend.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace doriax;

editor::CodeEditor::CodeEditor(Project* project) : isFileChangePopupOpen(false), windowFocused(false), lastFocused(nullptr) {
    this->project = project;
}

editor::CodeEditor::~CodeEditor() {
}

fs::path editor::CodeEditor::resolveFilepath(const fs::path& relPath) const {
    if (relPath.is_absolute()) return relPath;
    return project->getProjectPath() / relPath;
}

std::string editor::CodeEditor::toRelativePath(const std::string& filepath) const {
    fs::path inputPath(filepath);
    if (inputPath.is_relative()) return filepath;
    fs::path projectPath = project->getProjectPath();
    if (!projectPath.empty()) {
        std::error_code ec;
        fs::path relPath = fs::relative(inputPath, projectPath, ec);
        if (!ec && !relPath.empty()) {
            return relPath.string();
        }
    }
    return filepath;
}

bool editor::CodeEditor::loadFileContent(EditorInstance& instance) {
    try {
        fs::path fullPath = resolveFilepath(instance.filepath);
        std::ifstream file(fullPath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            instance.editor->SetText(buffer.str());
            instance.savedUndoIndex = instance.editor->GetUndoIndex();
            instance.lastWriteTime = fs::last_write_time(fullPath);
            instance.isModified = false;

            return true;
        }
    } catch (const std::exception& e) {
        // Handle file access errors
    }
    return false;
}

void editor::CodeEditor::checkFileChanges(EditorInstance& instance) {
    try {
        fs::path fullPath = resolveFilepath(instance.filepath);
        auto currentWriteTime = fs::last_write_time(fullPath);
        if (currentWriteTime != instance.lastWriteTime) {
            // Read the file to check if content actually changed
            std::ifstream file(fullPath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();
                if (buffer.str() == instance.editor->GetText()) {
                    // Content is the same (e.g. we just saved), update timestamp only
                    instance.lastWriteTime = currentWriteTime;
                    return;
                }
            }
            if (instance.isModified) {
                // Check if this file is already in the queue
                auto it = std::find_if(changedFilesQueue.begin(), changedFilesQueue.end(),
                    [&](const PendingFileChange& change) {
                        return change.filepath == instance.filepath;
                    });

                // Only add to queue if not already present
                if (it == changedFilesQueue.end()) {
                    changedFilesQueue.push_back({instance.filepath, currentWriteTime});
                }
            } else {
                // If no unsaved changes, silently reload the file
                loadFileContent(instance);
                updateScriptProperties(instance);
            }
        }
    } catch (const std::exception& e) {
        // Handle file access errors
    }
}

void editor::CodeEditor::handleFileChangePopup() {
    if (changedFilesQueue.empty()) {
        return;
    }

    if (!isFileChangePopupOpen) {
        ImGui::OpenPopup("Files Changed###FilesChanged");
        isFileChangePopupOpen = true;
    }

    // Center popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Files Changed###FilesChanged", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (changedFilesQueue.size() == 1) {
            ImGui::Text("The file '%s' has been modified externally.", 
                changedFilesQueue[0].filepath.filename().string().c_str());
        } else {
            ImGui::Text("%zu files have been modified externally:", changedFilesQueue.size());

            // Display up to 10 files
            int fileCount = 0;
            for (const auto& change : changedFilesQueue) {
                if (fileCount < 10) {
                    ImGui::BulletText("%s", change.filepath.filename().string().c_str());
                }
                fileCount++;
            }

            // If there are more files, show a message
            if (fileCount > 10) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "And %zu more files...", changedFilesQueue.size() - 10);
            }
        }

        ImGui::Text("Do you want to reload the modified files?");
        ImGui::Text("Warning: You have unsaved changes that will be lost.");
        ImGui::Separator();

        float buttonWidth = 120.0f;
        float windowWidth = ImGui::GetWindowSize().x;

        ImGui::SetCursorPosX((windowWidth - buttonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f);

        if (ImGui::Button("Yes", ImVec2(buttonWidth, 0))) {
            // Reload all files in the queue
            for (const auto& change : changedFilesQueue) {
                auto it = editors.find(change.filepath.string());
                if (it != editors.end()) {
                    loadFileContent(it->second);
                    updateScriptProperties(it->second);
                }
            }
            changedFilesQueue.clear();
            isFileChangePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(buttonWidth, 0))) {
            // Update timestamps without reloading
            for (const auto& change : changedFilesQueue) {
                auto it = editors.find(change.filepath.string());
                if (it != editors.end()) {
                    it->second.lastWriteTime = change.newWriteTime;
                }
            }
            changedFilesQueue.clear();
            isFileChangePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        isFileChangePopupOpen = false;
    }
}

std::string editor::CodeEditor::getWindowTitle(const EditorInstance& instance) const {
    std::string filename = instance.filepath.filename().string();
    return filename + (instance.isModified ? " *" : "") + "###" + instance.filepath.string();
}

void editor::CodeEditor::updateScriptProperties(const EditorInstance& instance){
    // Update script properties if this is a script file
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.scene)
            continue;

        for (Entity entity : sceneProject.entities) {
            ScriptComponent* scriptComponent = sceneProject.scene->findComponent<ScriptComponent>(entity);
            if (!scriptComponent)
                continue;

            bool found = false;
            // Check all scripts in the component
            for (const auto& scriptEntry : scriptComponent->scripts) {
                bool isCppScript =
                    (scriptEntry.type == ScriptType::SUBCLASS ||
                    scriptEntry.type == ScriptType::SCRIPT_CLASS);

                bool isLuaScript =
                    (scriptEntry.type == ScriptType::SCRIPT_LUA);

                // For C++ scripts we compare headerPath, for Lua we compare .lua path
                // instance.filepath is already project-relative
                bool matchesFile = false;
                if (isCppScript && !scriptEntry.headerPath.empty()) {
                    matchesFile = (scriptEntry.headerPath == instance.filepath.string());
                } else if (isLuaScript && !scriptEntry.path.empty()) {
                    matchesFile = (scriptEntry.path == instance.filepath.string());
                }

                if (matchesFile) {
                    std::vector<ScriptEntry> newScripts = scriptComponent->scripts;

                    project->updateScriptProperties(&sceneProject, entity, newScripts);
                    PropertyCmd<std::vector<ScriptEntry>> propertyCmd(project, sceneProject.id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                    propertyCmd.execute();

                    found = true;
                    break; // Found matching script, no need to check others
                }
            }
            if (found)
            break; // No need to scan more entities in this scene
        }
    }
}

std::vector<fs::path> editor::CodeEditor::getOpenPaths() const{
    std::vector<fs::path> openPaths;
    for (auto it = editors.begin(); it != editors.end(); ++it) {
        const auto& instance = it->second;

        openPaths.push_back(instance.filepath);
    }

    return openPaths;
}

bool editor::CodeEditor::isFocused() const {
    return windowFocused;
}

bool editor::CodeEditor::save(EditorInstance& instance) {
    try {
        fs::path fullPath = resolveFilepath(instance.filepath);
        std::ofstream file(fullPath);
        if (!file.is_open()) {
            return false;
        }

        std::string text = instance.editor->GetText();
        file << text;
        file.close();

        instance.savedUndoIndex = instance.editor->GetUndoIndex();
        instance.isModified = false;
        instance.lastWriteTime = fs::last_write_time(fullPath);

        updateScriptProperties(instance);

        return true;
    } catch (const std::exception& e) {
        // Handle file save errors
        return false;
    }
}

void editor::CodeEditor::saveLastFocused(){
    if (lastFocused){
        save(*lastFocused);
    }
}

bool editor::CodeEditor::save(const std::string& filepath) {
    std::string key = toRelativePath(filepath);
    auto it = editors.find(key);
    if (it == editors.end()) {
        return false;
    }

    return save(it->second);
}

void editor::CodeEditor::saveAll() {
    for (auto& [filepath, instance] : editors) {
        if (instance.isModified) {
            save(instance);
        }
    }
}

void editor::CodeEditor::undoLastFocused() {
    if (lastFocused && lastFocused->editor) {
        lastFocused->editor->Undo();
        lastFocused->isModified = lastFocused->editor->GetUndoIndex() != lastFocused->savedUndoIndex;
    }
}

void editor::CodeEditor::redoLastFocused() {
    if (lastFocused && lastFocused->editor) {
        lastFocused->editor->Redo();
        lastFocused->isModified = lastFocused->editor->GetUndoIndex() != lastFocused->savedUndoIndex;
    }
}

bool editor::CodeEditor::canUndoLastFocused() const {
    return lastFocused && lastFocused->editor && lastFocused->editor->CanUndo();
}

bool editor::CodeEditor::canRedoLastFocused() const {
    return lastFocused && lastFocused->editor && lastFocused->editor->CanRedo();
}

bool editor::CodeEditor::hasUnsavedChanges() const {
    for (const auto& [filepath, instance] : editors) {
        if (instance.isModified) {
            return true;
        }
    }
    return false;
}

bool editor::CodeEditor::hasLastFocusedUnsavedChanges() const {
    if (lastFocused){
        return lastFocused->isModified;
    }
    return false;
}

void editor::CodeEditor::openFile(const std::string& filepath) {
    std::string key = toRelativePath(filepath);

    auto it = editors.find(key);
    if (it != editors.end()) {
        // File already open - set this window as focused for the next frame
        ImGui::SetWindowFocus(getWindowTitle(it->second).c_str());
        return;
    }

    auto& instance = editors[key];
    instance.filepath = key;
    instance.editor = std::make_unique<CustomTextEditor>();

    // Detect language from extension and filename
    std::string ext = instance.filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string filename = instance.filepath.filename().string();

    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
        ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h") {
        instance.languageType = SyntaxLanguage::Cpp;
        instance.editor->SetLanguage(SyntaxLanguage::Cpp);
    } else if (ext == ".lua") {
        instance.languageType = SyntaxLanguage::Lua;
        instance.editor->SetLanguage(SyntaxLanguage::Lua);
    } else if (ext == ".cmake" || filename == "CMakeLists.txt") {
        instance.languageType = SyntaxLanguage::CMake;
        instance.editor->SetLanguage(SyntaxLanguage::CMake);
    } else {
        // Default to plain text for all other files
        instance.languageType = SyntaxLanguage::None;
        instance.editor->SetLanguage(SyntaxLanguage::None);
    }

    instance.editor->SetTabSize(4);
    instance.editor->SetAutoIndent(true);
    instance.editor->SetHighlightCurrentLine(true);
    instance.editor->SetMatchBrackets(true);
    instance.editor->SetAutoComplete(true);

    // Load the file content
    if (!loadFileContent(instance)) {
        // If loading fails, remove the instance
        if (lastFocused == &instance) {
            lastFocused = nullptr;
        }
        editors.erase(key);
        return;
    }

    project->addTab(TabType::CODE_EDITOR, key);

    Backend::getApp().addNewCodeWindowToDock(instance.filepath);
}

void editor::CodeEditor::closeFile(const std::string& filepath) {
    std::string key = toRelativePath(filepath);
    if (auto it = editors.find(key); it != editors.end()) {
        if (lastFocused == &it->second) {
            lastFocused = nullptr;
        }

        project->removeTab(TabType::CODE_EDITOR, key);

        editors.erase(it);
    }
}

bool editor::CodeEditor::isFileOpen(const std::string& filepath) const {
    std::string key = toRelativePath(filepath);
    return editors.find(key) != editors.end();
}

void editor::CodeEditor::setText(const std::string& filepath, const std::string& text) {
    std::string key = toRelativePath(filepath);
    if (auto it = editors.find(key); it != editors.end()) {
        it->second.editor->SetText(text);
        it->second.savedUndoIndex = it->second.editor->GetUndoIndex();
        it->second.isModified = false;
        try {
            it->second.lastWriteTime = fs::last_write_time(resolveFilepath(key));
        } catch (const std::exception& e) {
            // Handle file access errors
        }
    }
}

std::string editor::CodeEditor::getText(const std::string& filepath) const {
    std::string key = toRelativePath(filepath);
    if (auto it = editors.find(key); it != editors.end()) {
        return it->second.editor->GetText();
    }
    return "";
}

bool editor::CodeEditor::handleFileRename(const fs::path& oldPath, const fs::path& newPath) {
    std::string oldKey = toRelativePath(oldPath.string());
    std::string newKey = toRelativePath(newPath.string());

    auto it = editors.find(oldKey);
    if (it == editors.end()) {
        return false;
    }

    // Create new instance with the same editor content
    EditorInstance newInstance;
    newInstance.editor = std::move(it->second.editor);
    newInstance.isOpen = it->second.isOpen;
    newInstance.filepath = newKey;
    newInstance.isModified = it->second.isModified;
    newInstance.lastCheckTime = it->second.lastCheckTime;
    newInstance.hasExternalChanges = it->second.hasExternalChanges;
    newInstance.savedUndoIndex = it->second.savedUndoIndex;

    try {
        newInstance.lastWriteTime = fs::last_write_time(resolveFilepath(newKey));
    } catch (const std::exception& e) {
        return false;
    }

    // Update lastFocused pointer if it was pointing to the old instance
    if (lastFocused == &it->second) {
        editors[newKey] = std::move(newInstance);
        lastFocused = &editors[newKey];
        editors.erase(it);
    } else {
        editors.erase(it);
        editors[newKey] = std::move(newInstance);
    }

    // Update any pending file changes
    auto changeIt = std::remove_if(changedFilesQueue.begin(), changedFilesQueue.end(),
        [&](const PendingFileChange& change) {
            return change.filepath == fs::path(oldKey);
        });
    changedFilesQueue.erase(changeIt, changedFilesQueue.end());

    project->removeTab(TabType::CODE_EDITOR, oldKey);
    project->addTab(TabType::CODE_EDITOR, newKey);

    Backend::getApp().addNewCodeWindowToDock(fs::path(newKey));

    return true;
}

void editor::CodeEditor::show() {
    // Get current time
    double currentTime = ImGui::GetTime();

    windowFocused = false;

    // Iterate through all open editors
    for (auto it = editors.begin(); it != editors.end();) {
        auto& instance = it->second;

        // Check for external file changes every second
        if (currentTime - instance.lastCheckTime >= 1.0) {
            checkFileChanges(instance);
            instance.lastCheckTime = currentTime;
        }

        // Create window title using filename
        std::string windowTitle = getWindowTitle(instance);

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(windowTitle.c_str(), &instance.isOpen)) {

            instance.isModified = instance.editor->GetUndoIndex() != instance.savedUndoIndex;

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                windowFocused = true;

                lastFocused = &instance;
            }

            int line, column;
            instance.editor->GetCursorPosition(line, column);

            ImGui::Text("%6d/%-6d %6d lines  | %s | %s", 
                line + 1, 
                column + 1,
                instance.editor->GetLineCount(),
                instance.editor->GetLanguageName(),
                instance.isModified ? "*" : " ");

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // font needs to be monospace
            instance.editor->Render("TextEditor");
            ImGui::PopFont();
        }
        ImGui::End();

        // If the window was closed, remove it from our editors map
        if (!instance.isOpen) {
            if (lastFocused == &instance) {
                lastFocused = nullptr;
            }

            project->removeTab(TabType::CODE_EDITOR, instance.filepath.string());

            it = editors.erase(it);
        } else {
            ++it;
        }
    }

    // Handle file change popup after all windows are rendered
    handleFileChangePopup();
}