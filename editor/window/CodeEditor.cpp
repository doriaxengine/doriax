// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "CodeEditor.h"

#include "Backend.h"
#include "AppSettings.h"
#include "util/ProjectUtils.h"
#include "util/ScriptParser.h"
#include "util/Util.h"
#include "widget/SemanticSuggestions.h"
#include "command/type/PropertyCmd.h"
#include "Out.h"
#include "external/IconsFontAwesome6.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>

using namespace doriax;

namespace {

using ProjectSymbol = editor::CustomTextEditor::ProjectSymbol;

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();

    return true;
}

size_t skipSpaces(const std::string& line, size_t pos) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    return pos;
}

size_t scanWord(const std::string& line, size_t pos) {
    while (pos < line.size() && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) pos++;
    return pos;
}

size_t scanQualifiedName(const std::string& line, size_t pos) {
    while (pos < line.size() && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_' ||
                                 line[pos] == '.' || line[pos] == ':')) pos++;
    return pos;
}

bool startsWithIdentifier(const std::string& word) {
    return !word.empty() && (std::isalpha(static_cast<unsigned char>(word[0])) || word[0] == '_');
}

std::string stripNamespace(const std::string& type) {
    size_t lastColon = type.rfind(':');
    return (lastColon != std::string::npos) ? type.substr(lastColon + 1) : type;
}

void parseLuaSymbols(const std::string& content, std::unordered_set<std::string>& seen, std::vector<ProjectSymbol>& out) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        size_t pos = line.find("function ");
        if (pos != std::string::npos) {
            size_t start = skipSpaces(line, pos + 9);
            size_t end = scanQualifiedName(line, start);
            std::string name = line.substr(start, end - start);

            if (!name.empty() && name != "(" && seen.insert(name).second) {
                // "function Player:update()" is a method of Player, so it only shows up
                // in member completion (player:upd...)
                size_t sep = name.find_first_of(".:");
                if (sep != std::string::npos && sep > 0 && sep + 1 < name.size()) {
                    std::string owner = name.substr(0, sep);
                    std::string method = name.substr(sep + 1);
                    out.push_back({method, editor::SuggestionKind::Method, owner + ":" + method + "()", owner, ""});
                } else {
                    out.push_back({name, editor::SuggestionKind::Function, "project function", "", ""});
                }
            }
        }

        pos = line.find("local ");
        if (pos != std::string::npos && line.find("function", pos) == std::string::npos) {
            size_t start = skipSpaces(line, pos + 6);
            size_t end = scanWord(line, start);
            std::string name = line.substr(start, end - start);

            if (!name.empty() && seen.insert(name).second) {
                out.push_back({name, editor::SuggestionKind::Variable, "local variable", "", ""});
            }
        }
    }
}

// Member declared inside a class body: [const] [namespace::]Type[<...>] [*&] name [= ...];
void parseCppMember(const std::string& line, const std::string& currentClass, std::unordered_set<std::string>& seen, std::vector<ProjectSymbol>& out) {
    static const char* skipTokens[] = {"(", "public", "private", "protected", "friend", "#",
                                       "DPROPERTY", "REGISTER", "using ", "typedef ", "virtual", "static"};
    for (const char* token : skipTokens) {
        if (line.find(token) != std::string::npos) return;
    }

    std::string decl = line.substr(skipSpaces(line, 0));
    if (decl.empty() || decl[0] == '{' || decl[0] == '}' || decl.rfind("//", 0) == 0) return;
    if (decl.rfind("const ", 0) == 0) decl = decl.substr(6);

    size_t typeEnd = 0;
    while (typeEnd < decl.size() && (std::isalnum(static_cast<unsigned char>(decl[typeEnd])) || decl[typeEnd] == '_' || decl[typeEnd] == ':')) typeEnd++;
    if (typeEnd == 0 || typeEnd >= decl.size()) return;

    std::string type = decl.substr(0, typeEnd);
    if (!startsWithIdentifier(type)) return;

    static const std::unordered_set<std::string> builtinTypes = {
        "return", "void", "bool", "int", "float", "double", "char", "unsigned", "signed"
    };
    if (builtinTypes.count(type)) return;

    size_t pos = typeEnd;
    if (pos < decl.size() && decl[pos] == '<') {
        int depth = 1;
        pos++;
        while (pos < decl.size() && depth > 0) {
            if (decl[pos] == '<') depth++;
            else if (decl[pos] == '>') depth--;
            pos++;
        }
    }
    while (pos < decl.size() && (decl[pos] == '*' || decl[pos] == '&' || decl[pos] == ' ')) pos++;

    size_t nameEnd = scanWord(decl, pos);
    if (nameEnd == pos) return;
    std::string name = decl.substr(pos, nameEnd - pos);

    // Only a real declaration ends with '=' or ';', anything else is stray text
    size_t rest = skipSpaces(decl, nameEnd);
    if (rest >= decl.size() || (decl[rest] != '=' && decl[rest] != ';')) return;

    if (seen.insert(currentClass + "::" + name).second) {
        out.push_back({name, editor::SuggestionKind::Field, type, currentClass, stripNamespace(type)});
    }
}

void parseCppSymbols(const std::string& content, std::unordered_set<std::string>& seen, std::vector<ProjectSymbol>& out) {
    std::istringstream stream(content);
    std::string line;
    std::string currentClass;
    int braceDepth = 0;
    int classStartDepth = -1;

    while (std::getline(stream, line)) {
        for (char ch : line) {
            if (ch == '{') {
                braceDepth++;
            } else if (ch == '}') {
                braceDepth--;
                if (classStartDepth >= 0 && braceDepth <= classStartDepth) {
                    currentClass.clear();
                    classStartDepth = -1;
                }
            }
        }

        for (const char* keyword : {"class ", "struct "}) {
            size_t pos = line.find(keyword);
            if (pos == std::string::npos) continue;

            size_t start = skipSpaces(line, pos + std::strlen(keyword));
            size_t end = scanWord(line, start);
            if (end == start) continue;

            std::string name = line.substr(start, end - start);
            if (name != "{" && seen.insert(name).second) {
                out.push_back({name, editor::SuggestionKind::Class, "project type", "", ""});
            }

            currentClass = name;
            classStartDepth = (line.find('{') != std::string::npos) ? braceDepth - 1 : braceDepth;
        }

        if (!currentClass.empty() && braceDepth > classStartDepth) {
            parseCppMember(line, currentClass, seen, out);
        }

        // Function names are the word right before a '('
        size_t paren = line.find('(');
        if (paren == std::string::npos || paren == 0) continue;

        size_t end = paren;
        while (end > 0 && line[end - 1] == ' ') end--;
        size_t start = end;
        while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[start - 1])) || line[start - 1] == '_')) start--;
        if (end == start) continue;

        static const std::unordered_set<std::string> statementKeywords = {
            "if", "for", "while", "switch", "catch", "return", "sizeof", "new", "delete"
        };
        std::string name = line.substr(start, end - start);
        if (startsWithIdentifier(name) && !statementKeywords.count(name) && seen.insert(name).second) {
            out.push_back({name, editor::SuggestionKind::Function, "project function", currentClass, ""});
        }
    }
}

editor::SyntaxLanguage languageForPath(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".lua") return editor::SyntaxLanguage::Lua;
    // GLSL is close enough to reuse the C++ highlighter
    if (ext == ".c" || editor::Util::isSourceFile(ext) || editor::Util::isHeaderFile(ext) || editor::Util::isShaderFile(ext))
        return editor::SyntaxLanguage::Cpp;
    if (ext == ".cmake" || path.filename() == "CMakeLists.txt") return editor::SyntaxLanguage::CMake;

    return editor::SyntaxLanguage::None;
}

// Reads every project script that is not already open in the editor
void collectProjectSources(const fs::path& projectPath, const std::unordered_set<std::string>& openFiles,
                           std::vector<std::string>& luaContents, std::vector<std::string>& cppContents) {
    if (projectPath.empty() || !fs::exists(projectPath)) return;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(projectPath, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;

        // '.doriax' holds the engine copy and the generated resources, 'build' the artifacts
        const std::string filename = entry.path().filename().string();
        if (filename.rfind('.', 0) == 0 || filename == "build") {
            if (entry.is_directory(ec)) it.disable_recursion_pending();
            continue;
        }

        const std::string path = entry.path().string();
        if (!entry.is_regular_file() || !editor::Util::isScriptFile(path)) continue;

        std::error_code relEc;
        fs::path relPath = fs::relative(entry.path(), projectPath, relEc);
        if (relEc || relPath.empty() || openFiles.count(relPath.string())) continue;

        std::string content;
        if (!readFile(entry.path(), content)) continue;

        if (editor::Util::isLuaFile(path)) {
            luaContents.push_back(std::move(content));
        } else {
            cppContents.push_back(std::move(content));
        }
    }
}

}

editor::CodeEditor::CodeEditor(Project* project) : lastScriptWatchTime(0.0), isFileChangePopupOpen(false), windowFocused(false), lastFocused(nullptr) {
    this->project = project;
}

editor::CodeEditor::~CodeEditor() {
    if (symbolParseThread.joinable()) {
        symbolParseThread.join();
    }
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

        std::string content;
        if (!readFile(fullPath, content)) return false;

        instance.editor->SetText(content);
        instance.savedUndoIndex = instance.editor->GetUndoIndex();
        instance.lastWriteTime = fs::last_write_time(fullPath);
        instance.isModified = false;

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void editor::CodeEditor::updateAllProjectSymbols() {
    if (isParsingSymbols) return;

    // Copy what the worker needs so it never reaches into the editors
    std::vector<std::string> luaContents;
    std::vector<std::string> cppContents;
    std::unordered_set<std::string> openFiles;

    for (const auto& [key, instance] : editors) {
        openFiles.insert(key);
        if (!instance.editor) continue;

        if (instance.languageType == SyntaxLanguage::Lua) {
            luaContents.push_back(instance.editor->GetText());
        } else if (instance.languageType == SyntaxLanguage::Cpp) {
            cppContents.push_back(instance.editor->GetText());
        }
    }

    fs::path projectPath = project->getProjectPath();

    isParsingSymbols = true;
    if (symbolParseThread.joinable()) {
        symbolParseThread.join();
    }

    symbolParseThread = std::thread([this, projectPath, openFiles = std::move(openFiles),
                                     luaContents = std::move(luaContents), cppContents = std::move(cppContents)]() mutable {
        collectProjectSources(projectPath, openFiles, luaContents, cppContents);

        std::vector<ProjectSymbol> parsedLua;
        std::vector<ProjectSymbol> parsedCpp;
        std::unordered_set<std::string> seenLua;
        std::unordered_set<std::string> seenCpp;

        for (const std::string& content : luaContents) {
            parseLuaSymbols(content, seenLua, parsedLua);
        }
        for (const std::string& content : cppContents) {
            parseCppSymbols(content, seenCpp, parsedCpp);
        }

        {
            std::lock_guard<std::mutex> lock(parsedSymbolsMutex);
            newLuaSymbols = std::move(parsedLua);
            newCppSymbols = std::move(parsedCpp);
            newSymbolsReady = true;
        }
        isParsingSymbols = false;
    });
}

void editor::CodeEditor::applyParsedProjectSymbols() {
    if (!newSymbolsReady) return;

    std::vector<ProjectSymbol> luaSymbols;
    std::vector<ProjectSymbol> cppSymbols;

    {
        std::lock_guard<std::mutex> lock(parsedSymbolsMutex);
        luaSymbols = std::move(newLuaSymbols);
        cppSymbols = std::move(newCppSymbols);
        newSymbolsReady = false;
    }

    for (auto& [key, instance] : editors) {
        if (!instance.editor) continue;

        if (instance.languageType == SyntaxLanguage::Lua) {
            instance.editor->UpdateProjectSymbols(luaSymbols);
        } else if (instance.languageType == SyntaxLanguage::Cpp) {
            instance.editor->UpdateProjectSymbols(cppSymbols);
        }
    }
}

void editor::CodeEditor::invalidateShadersForFile(const EditorInstance& instance) {
    // The build cache resolves sources through an in-memory snapshot, so without this
    // an external edit would never reach the renderer
    if (Util::isShaderFile(instance.filepath.string())) {
        project->invalidateCustomShaders();
    }
}

void editor::CodeEditor::checkFileChanges(EditorInstance& instance) {
    try {
        fs::path fullPath = resolveFilepath(instance.filepath);
        auto currentWriteTime = fs::last_write_time(fullPath);
        if (currentWriteTime == instance.lastWriteTime) return;

        std::string content;
        if (readFile(fullPath, content) && content == instance.editor->GetText()) {
            // Same content, this is our own save
            instance.lastWriteTime = currentWriteTime;
            return;
        }

        if (!instance.isModified) {
            loadFileContent(instance);
            updateScriptProperties(instance);
            invalidateShadersForFile(instance);
            return;
        }

        auto it = std::find_if(changedFilesQueue.begin(), changedFilesQueue.end(),
            [&](const PendingFileChange& change) {
                return change.filepath == instance.filepath;
            });

        if (it == changedFilesQueue.end()) {
            changedFilesQueue.push_back({instance.filepath, currentWriteTime});
        }
    } catch (const std::exception&) {
        // File gone or unreadable this tick
    }
}

void editor::CodeEditor::checkExternalScriptChanges() {
    // Play mutates the live scene in-process and restores it from a snapshot on stop, so a
    // background property refresh would fight it. External edits are picked up after stop.
    for (const auto& sceneProject : project->getScenes()) {
        if (sceneProject.playState != ScenePlayState::STOPPED)
            return;
    }

    std::unordered_set<std::string> referenced;
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.scene)
            continue;

        auto scriptsArray = sceneProject.scene->getComponentArray<ScriptComponent>();
        for (size_t i = 0; i < scriptsArray->size(); i++) {
            const ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
            for (const auto& scriptEntry : scriptComponent.scripts) {
                if (scriptEntry.type == ScriptType::CPP && !scriptEntry.headerPath.empty()) {
                    referenced.insert(scriptEntry.headerPath);
                } else if (scriptEntry.type == ScriptType::LUA && !scriptEntry.path.empty()) {
                    // Stored relative to the Lua root, the watch list is project-relative
                    referenced.insert(toRelativePath(project->resolveLuaPath(scriptEntry.path).string()));
                }
            }
        }
    }

    for (const std::string& relPath : referenced) {
        fs::file_time_type currentWriteTime;
        try {
            currentWriteTime = fs::last_write_time(resolveFilepath(relPath));
        } catch (const std::exception&) {
            continue;
        }

        auto it = watchedScriptFiles.find(relPath);
        if (it == watchedScriptFiles.end()) {
            // First sighting, record the baseline without refreshing
            watchedScriptFiles[relPath] = currentWriteTime;
            continue;
        }
        if (it->second == currentWriteTime)
            continue;

        // checkFileChanges owns open files, including the unsaved-edit conflict prompt
        it->second = currentWriteTime;
        if (editors.find(relPath) != editors.end())
            continue;

        updateScriptPropertiesForPath(relPath);
    }

    // Forget unreferenced files so a re-added one gets a fresh baseline
    for (auto it = watchedScriptFiles.begin(); it != watchedScriptFiles.end();) {
        if (referenced.find(it->first) == referenced.end())
            it = watchedScriptFiles.erase(it);
        else
            ++it;
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

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Files Changed###FilesChanged", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (changedFilesQueue.size() == 1) {
            ImGui::Text("The file '%s' has been modified externally.",
                changedFilesQueue[0].filepath.filename().string().c_str());
        } else {
            ImGui::Text("%zu files have been modified externally:", changedFilesQueue.size());

            const size_t maxListed = std::min<size_t>(changedFilesQueue.size(), 10);
            for (size_t i = 0; i < maxListed; i++) {
                ImGui::BulletText("%s", changedFilesQueue[i].filepath.filename().string().c_str());
            }
            if (changedFilesQueue.size() > maxListed) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "And %zu more files...", changedFilesQueue.size() - maxListed);
            }
        }

        ImGui::Text("Do you want to reload the modified files?");
        ImGui::Text("Warning: You have unsaved changes that will be lost.");
        ImGui::Separator();

        float buttonWidth = 120.0f;
        float windowWidth = ImGui::GetWindowSize().x;

        ImGui::SetCursorPosX((windowWidth - buttonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f);

        if (ImGui::Button("Yes", ImVec2(buttonWidth, 0))) {
            for (const auto& change : changedFilesQueue) {
                auto it = editors.find(change.filepath.string());
                if (it != editors.end()) {
                    loadFileContent(it->second);
                    updateScriptProperties(it->second);
                    invalidateShadersForFile(it->second);
                }
            }
            changedFilesQueue.clear();
            isFileChangePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(buttonWidth, 0))) {
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

void editor::CodeEditor::updateScriptProperties(const EditorInstance& instance, const std::string& inMemoryContent){
    updateScriptPropertiesForPath(instance.filepath, inMemoryContent);
}

void editor::CodeEditor::updateScriptPropertiesForPath(const fs::path& relFilepath, const std::string& inMemoryContent){
    const std::string relPathStr = relFilepath.string();
    const fs::path fullPath = resolveFilepath(relFilepath);

    // Several entities can share the same script, so every one of them is refreshed
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.scene)
            continue;

        for (Entity entity : sceneProject.entities) {
            ScriptComponent* scriptComponent = sceneProject.scene->findComponent<ScriptComponent>(entity);
            if (!scriptComponent)
                continue;

            for (const auto& scriptEntry : scriptComponent->scripts) {
                bool matchesFile = (scriptEntry.type == ScriptType::CPP) ? (scriptEntry.headerPath == relPathStr)
                                                                        : (scriptEntry.path == relPathStr);
                if (!matchesFile)
                    continue;

                std::vector<ScriptEntry> newScripts = scriptComponent->scripts;

                // Ignore edits that do not change script properties
                if (project->updateScriptProperties(&sceneProject, entity, newScripts, inMemoryContent, fullPath.string())) {
                    PropertyCmd<std::vector<ScriptEntry>> propertyCmd(project, sceneProject.id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                    propertyCmd.execute();
                }

                break;
            }
        }
    }
}

std::string editor::CodeEditor::toCamelCase(const std::string& name) {
    std::string result;
    bool nextUpper = false;

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            if (!result.empty()) nextUpper = true;
            continue;
        }

        if (result.empty()) {
            result += (char)std::tolower(static_cast<unsigned char>(c));
        } else if (nextUpper) {
            result += (char)std::toupper(static_cast<unsigned char>(c));
            nextUpper = false;
        } else {
            result += c;
        }
    }

    return result.empty() ? "entity" : result;
}

std::string editor::CodeEditor::toDisplayName(const std::string& camelCase) {
    std::string result;

    for (size_t i = 0; i < camelCase.size(); i++) {
        if (i == 0) {
            result += (char)std::toupper(static_cast<unsigned char>(camelCase[i]));
        } else if (std::isupper(static_cast<unsigned char>(camelCase[i]))) {
            result += ' ';
            result += camelCase[i];
        } else {
            result += camelCase[i];
        }
    }

    return result;
}

void editor::CodeEditor::applyFontZoom(int delta) {
    float fontSize = AppSettings::getCodeEditorFontSize();
    if (delta == 0) {
        fontSize = AppSettings::defaultCodeEditorFontSize;
    } else {
        fontSize += delta;
    }
    AppSettings::setCodeEditorFontSize(fontSize);
    AppSettings::saveSettings();
}

void editor::CodeEditor::showSettingsButton() {
    if (ImGui::Button(ICON_FA_GEAR)) {
        ImGui::OpenPopup("CodeEditorSettingsPopup");
    }
    ImGui::SetItemTooltip("Editor settings");

    if (ImGui::BeginPopup("CodeEditorSettingsPopup")) {
        if (ImGui::BeginTable("code_editor_settings_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);

            float fontSize = AppSettings::getCodeEditorFontSize();
            bool defChanged = std::fabs(fontSize - AppSettings::defaultCodeEditorFontSize) > 1e-4f;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", ICON_FA_FONT " Font size");
            ImGui::SameLine();

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (defChanged) {
                if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetCodeFontSize")) {
                    applyFontZoom(0);
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##CodeFontSize", &fontSize, 0.2f, AppSettings::minCodeEditorFontSize, AppSettings::maxCodeEditorFontSize, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
                AppSettings::setCodeEditorFontSize(fontSize);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                AppSettings::saveSettings();
            }

            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
}

void editor::CodeEditor::offsetToLineCol(const std::string& text, size_t offset, int& line, int& col) {
    line = 0;
    col = 0;
    for (size_t i = 0; i < offset && i < text.size(); i++) {
        if (text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
}

void editor::CodeEditor::insertLuaEntityProperty(EditorInstance& instance, Entity entity, uint32_t entitySceneId) {
    SceneProject* sourceSceneProject = project->getScene(entitySceneId);
    if (!sourceSceneProject || !sourceSceneProject->scene) {
        sourceSceneProject = project->getSelectedScene();
    }
    if (!sourceSceneProject || !sourceSceneProject->scene) return;

    Scene* scene = sourceSceneProject->scene;
    if (!scene->isEntityCreated(entity)) return;

    std::string entityType = editor::ProjectUtils::getEntityTypeName(scene, entity);

    // A Lua script class is a more specific type than the entity type
    ScriptComponent* entityScriptComp = scene->findComponent<ScriptComponent>(entity);
    if (entityScriptComp) {
        for (const auto& scriptEntry : entityScriptComp->scripts) {
            if (scriptEntry.type == ScriptType::LUA && scriptEntry.enabled && !scriptEntry.className.empty()) {
                entityType = scriptEntry.className;
                break;
            }
        }
    }

    std::string text = instance.editor->GetText();
    std::string varName = toCamelCase(scene->getEntityName(entity));

    std::string baseName = varName;
    int suffix = 2;
    while (text.find("name = \"" + varName + "\"") != std::string::npos) {
        varName = baseName + std::to_string(suffix++);
    }

    std::string propEntry =
        "        {\n"
        "            name = \"" + varName + "\",\n"
        "            displayName = \"" + toDisplayName(varName) + "\",\n"
        "            type = \"" + entityType + "\",\n"
        "            default = nil\n"
        "        }";

    std::smatch match;
    if (!std::regex_search(text, match, std::regex(R"(properties\s*=\s*\{)"))) {
        Out::error("Could not find 'properties = {' table in Lua script");
        return;
    }

    size_t openPos = match.position() + match.length();
    int depth = 1;
    size_t closePos = std::string::npos;
    for (size_t i = openPos; i < text.size(); i++) {
        if (text[i] == '{') {
            depth++;
        } else if (text[i] == '}') {
            depth--;
            if (depth == 0) {
                closePos = i;
                break;
            }
        }
    }
    if (closePos == std::string::npos) {
        Out::error("Could not find closing '}' for properties table");
        return;
    }

    bool hasEntries = text.find('{', openPos) < closePos;
    std::string insertion = hasEntries ? (",\n" + propEntry) : ("\n" + propEntry + "\n    ");

    // Inserting through the cursor keeps the undo history
    int insertLine, insertCol;
    offsetToLineCol(text, closePos, insertLine, insertCol);
    instance.editor->SetCursorPosition(insertLine, insertCol);
    instance.editor->InsertText(insertion, false);

    // Parse the properties from the current text, without overwriting the file
    instance.isModified = true;
    instance.propertyInsertUndoIndex = instance.editor->GetUndoIndex();
    updateScriptProperties(instance, instance.editor->GetText());

    SceneProject* selectedScene = project->getSelectedScene();
    if (!selectedScene || !selectedScene->scene) return;

    uint32_t storedSceneId = (entitySceneId != selectedScene->id) ? entitySceneId : 0;

    for (Entity sceneEntity : selectedScene->entities) {
        ScriptComponent* scriptComp = selectedScene->scene->findComponent<ScriptComponent>(sceneEntity);
        if (!scriptComp) continue;

        for (size_t si = 0; si < scriptComp->scripts.size(); si++) {
            const ScriptEntry& scriptEntry = scriptComp->scripts[si];
            if (scriptEntry.type != ScriptType::LUA) continue;
            if (project->resolveLuaPath(scriptEntry.path) != resolveFilepath(instance.filepath)) continue;

            // A same-named property of another type is not the one just inserted
            for (size_t pi = 0; pi < scriptEntry.properties.size(); pi++) {
                if (scriptEntry.properties[pi].name != varName ||
                    scriptEntry.properties[pi].type != ScriptPropertyType::EntityReference) continue;

                std::vector<ScriptEntry> newScripts = scriptComp->scripts;
                newScripts[si].properties[pi].value = EntityReference{entity, storedSceneId};

                PropertyCmd<std::vector<ScriptEntry>> propCmd(
                    project, selectedScene->id, sceneEntity,
                    ComponentType::ScriptComponent, "scripts", newScripts);
                propCmd.execute();
                return;
            }
        }
    }
}

void editor::CodeEditor::insertCppEntityProperty(EditorInstance& instance, Entity entity, uint32_t entitySceneId) {
    SceneProject* sourceSceneProject = project->getScene(entitySceneId);
    if (!sourceSceneProject || !sourceSceneProject->scene) {
        sourceSceneProject = project->getSelectedScene();
    }
    if (!sourceSceneProject || !sourceSceneProject->scene) return;

    Scene* scene = sourceSceneProject->scene;
    if (!scene->isEntityCreated(entity)) return;

    std::string entityType = editor::ProjectUtils::getEntityTypeName(scene, entity);

    // A C++ script that does not inherit ScriptBase is an entity subclass, so its class
    // is the more specific entity-reference type
    bool isSubclassType = false;
    std::string subclassHeaderFile;
    ScriptComponent* entityScriptComp = scene->findComponent<ScriptComponent>(entity);
    if (entityScriptComp) {
        for (const auto& scriptEntry : entityScriptComp->scripts) {
            if (scriptEntry.type != ScriptType::CPP || scriptEntry.className.empty() || scriptEntry.headerPath.empty())
                continue;

            std::optional<bool> inheritsScriptBase;
            auto openHeader = editors.find(toRelativePath(scriptEntry.headerPath));
            if (openHeader != editors.end()) {
                inheritsScriptBase = ScriptParser::inheritsScriptBaseFromString(
                    openHeader->second.editor->GetText(), scriptEntry.className);
            } else {
                inheritsScriptBase = ScriptParser::inheritsScriptBase(
                    resolveFilepath(scriptEntry.headerPath), scriptEntry.className);
            }

            if (inheritsScriptBase && !*inheritsScriptBase) {
                entityType = scriptEntry.className;
                isSubclassType = true;
                subclassHeaderFile = fs::path(scriptEntry.headerPath).filename().string();
                break;
            }
        }
    }

    std::string varName = toCamelCase(scene->getEntityName(entity));

    fs::path headerPath = instance.filepath;
    if (!Util::isHeaderFile(headerPath.string())) {
        headerPath.replace_extension(".h");
    }

    std::string headerText;
    EditorInstance* headerInstance = nullptr;
    auto headerIt = editors.find(headerPath.string());
    if (headerIt != editors.end()) {
        headerInstance = &headerIt->second;
        headerText = headerInstance->editor->GetText();
    } else if (!readFile(resolveFilepath(headerPath), headerText)) {
        Out::error("Could not open header file: %s", headerPath.string().c_str());
        return;
    }

    // Any member or method already using the name would make the entity reference land
    // on the wrong one
    auto nameTaken = [&headerText](const std::string& name) {
        return std::regex_search(headerText, std::regex(R"([\s*])" + name + R"(\s*(?:=|;|\{|\[|\())"));
    };
    std::string baseName = varName;
    int suffix = 2;
    while (nameTaken(varName)) {
        varName = baseName + std::to_string(suffix++);
    }

    std::string typeDecl = isSubclassType ? entityType : ("doriax::" + entityType);
    std::string propCode =
        "\n"
        "    DPROPERTY(\"" + toDisplayName(varName) + "\")\n"
        "    " + typeDecl + "* " + varName + " = nullptr;\n";

    // The member type must be declared: a subclass type needs its own script header,
    // an engine type needs the engine header
    bool needsInclude = false;
    size_t includeInsertPos = std::string::npos;
    std::string typeHeaderFile = isSubclassType ? subclassHeaderFile : (entityType + ".h");
    std::string includeDirective = "#include \"" + typeHeaderFile + "\"";
    if (!typeHeaderFile.empty() && headerText.find(includeDirective) == std::string::npos) {
        needsInclude = true;

        size_t lastInclude = std::string::npos;
        size_t searchPos = 0;
        while ((searchPos = headerText.find("#include", searchPos)) != std::string::npos) {
            lastInclude = searchPos;
            searchPos++;
        }
        if (lastInclude != std::string::npos) {
            size_t endOfLine = headerText.find('\n', lastInclude);
            if (endOfLine != std::string::npos) {
                includeInsertPos = endOfLine + 1;
            }
        }
    }

    // Insertion points are found on the original text so they match the editor content
    size_t insertPos = std::string::npos;

    // The declaration after DPROPERTY ends with ';'. Type is [\w:<>]+ and the pointer '*' or
    // whitespace separator is [\s*]+, so "T* v" and "T *v" both match. The initializer is optional.
    std::regex propertyRegex(R"(DPROPERTY\s*\([^)]*\)\s*\n\s*[\w:<>]+[\s*]+\w+\s*(?:=[^;]*)?;)");
    size_t lastPropertyEnd = std::string::npos;
    for (std::sregex_iterator it(headerText.begin(), headerText.end(), propertyRegex), endIt; it != endIt; ++it) {
        lastPropertyEnd = it->position() + it->length();
    }

    if (lastPropertyEnd != std::string::npos) {
        size_t nextLine = headerText.find('\n', lastPropertyEnd);
        insertPos = (nextLine != std::string::npos) ? nextLine + 1 : lastPropertyEnd;
    } else {
        // No property yet, fall back to the line above the constructor
        std::smatch ctorMatch;
        if (std::regex_search(headerText, ctorMatch, std::regex(R"(\n(\s+)\w+\s*\()"))) {
            insertPos = ctorMatch.position() + 1;
        }
    }

    if (insertPos == std::string::npos) {
        Out::error("Could not find insertion point in header file");
        return;
    }

    if (headerInstance) {
        // The include goes in first, so the DPROPERTY position shifts by its line
        if (needsInclude && includeInsertPos != std::string::npos) {
            int includeLine, includeCol;
            offsetToLineCol(headerText, includeInsertPos, includeLine, includeCol);
            headerInstance->editor->SetCursorPosition(includeLine, includeCol);
            headerInstance->editor->InsertText(includeDirective + "\n", false);
        }

        int insertLine, insertCol;
        offsetToLineCol(headerText, insertPos, insertLine, insertCol);
        if (needsInclude && includeInsertPos != std::string::npos && includeInsertPos <= insertPos) {
            insertLine++;
        }
        headerInstance->editor->SetCursorPosition(insertLine, insertCol);
        headerInstance->editor->InsertText(propCode, false);

        // Parse the properties from the current text, without overwriting the file
        headerInstance->isModified = true;
        headerInstance->propertyInsertUndoIndex = headerInstance->editor->GetUndoIndex();
    } else {
        if (needsInclude && includeInsertPos != std::string::npos) {
            std::string incText = includeDirective + "\n";
            headerText.insert(includeInsertPos, incText);
            if (includeInsertPos <= insertPos) {
                insertPos += incText.size();
            }
        }

        std::ofstream file(resolveFilepath(headerPath), std::ios::trunc);
        if (!file.is_open()) {
            Out::error("Could not write header file: %s", headerPath.string().c_str());
            return;
        }
        file << headerText.substr(0, insertPos) + propCode + headerText.substr(insertPos);
    }

    SceneProject* selectedScene = project->getSelectedScene();
    if (!selectedScene || !selectedScene->scene) return;

    uint32_t storedSceneId = (entitySceneId != selectedScene->id) ? entitySceneId : 0;
    std::string inMemoryContent = headerInstance ? headerInstance->editor->GetText() : "";

    // Refresh every entity referencing this header and link the dropped one
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.scene) continue;

        for (Entity sceneEntity : sceneProject.entities) {
            ScriptComponent* scriptComp = sceneProject.scene->findComponent<ScriptComponent>(sceneEntity);
            if (!scriptComp) continue;

            for (size_t si = 0; si < scriptComp->scripts.size(); si++) {
                if (scriptComp->scripts[si].type != ScriptType::CPP) continue;
                if (scriptComp->scripts[si].headerPath != headerPath.string()) continue;

                std::vector<ScriptEntry> newScripts = scriptComp->scripts;
                project->updateScriptProperties(&sceneProject, sceneEntity, newScripts, inMemoryContent, resolveFilepath(headerPath).string());

                for (ScriptEntry& newScript : newScripts) {
                    if (newScript.headerPath != headerPath.string()) continue;

                    for (ScriptProperty& property : newScript.properties) {
                        if (property.name == varName && property.type == ScriptPropertyType::EntityReference) {
                            property.value = EntityReference{entity, storedSceneId};
                            break;
                        }
                    }
                }

                PropertyCmd<std::vector<ScriptEntry>> propCmd(
                    project, sceneProject.id, sceneEntity,
                    ComponentType::ScriptComponent, "scripts", newScripts);
                propCmd.execute();
            }
        }
    }
}

std::vector<fs::path> editor::CodeEditor::getOpenPaths() const{
    std::vector<fs::path> openPaths;
    for (const auto& [key, instance] : editors) {
        openPaths.push_back(instance.filepath);
    }

    return openPaths;
}

bool editor::CodeEditor::isFocused() const {
    return windowFocused;
}

void editor::CodeEditor::closeAll() {
    if (symbolParseThread.joinable()) {
        symbolParseThread.join();
    }
    editors.clear();
    changedFilesQueue.clear();
    lastFocused = nullptr;
}

bool editor::CodeEditor::save(EditorInstance& instance) {
    try {
        fs::path fullPath = resolveFilepath(instance.filepath);
        std::ofstream file(fullPath);
        if (!file.is_open()) {
            return false;
        }

        file << instance.editor->GetText();
        file.close();

        instance.savedUndoIndex = instance.editor->GetUndoIndex();
        instance.isModified = false;
        instance.propertyInsertUndoIndex = -1;
        instance.lastWriteTime = fs::last_write_time(fullPath);

        updateScriptProperties(instance);
        updateAllProjectSymbols();
        invalidateShadersForFile(instance);

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void editor::CodeEditor::saveLastFocused(){
    if (lastFocused){
        save(*lastFocused);
    }
}

bool editor::CodeEditor::save(const std::string& filepath) {
    auto it = editors.find(toRelativePath(filepath));
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

void editor::CodeEditor::openFile(const std::string& filepath, bool dockToCentral) {
    std::string key = toRelativePath(filepath);

    auto it = editors.find(key);
    if (it != editors.end()) {
        if (dockToCentral) {
            Backend::getApp().addNewCodeWindowToDock(it->second.filepath, true);
        }
        ImGui::SetWindowFocus(getWindowTitle(it->second).c_str());
        return;
    }

    auto& instance = editors[key];
    instance.filepath = key;
    instance.editor = std::make_unique<CustomTextEditor>();

    instance.languageType = languageForPath(instance.filepath);
    instance.editor->SetLanguage(instance.languageType);

    instance.editor->SetTabSize(4);
    instance.editor->SetAutoIndent(true);
    instance.editor->SetHighlightCurrentLine(true);
    instance.editor->SetMatchBrackets(true);
    instance.editor->SetAutoComplete(true);
    instance.editor->SetFontZoomCallback([](int delta) { applyFontZoom(delta); });
    // VSCode line height is 1.35x the em size, the pushed ImGui size is em * codeFontEmScale
    instance.editor->SetLineHeightFactor(1.35f / App::codeFontEmScale);

    if (!loadFileContent(instance)) {
        if (lastFocused == &instance) {
            lastFocused = nullptr;
        }
        editors.erase(key);
        return;
    }

    project->addTab(TabType::CODE_EDITOR, key);
    project->saveProjectFile();

    Backend::getApp().addNewCodeWindowToDock(instance.filepath, dockToCentral);

    updateAllProjectSymbols();
}

void editor::CodeEditor::closeFile(const std::string& filepath) {
    std::string key = toRelativePath(filepath);
    if (auto it = editors.find(key); it != editors.end()) {
        if (lastFocused == &it->second) {
            lastFocused = nullptr;
        }

        project->removeTab(TabType::CODE_EDITOR, key);
        project->saveProjectFile();

        editors.erase(it);
    }
}

bool editor::CodeEditor::isFileOpen(const std::string& filepath) const {
    return editors.find(toRelativePath(filepath)) != editors.end();
}

bool editor::CodeEditor::isFileModified(const std::string& filepath) const {
    auto it = editors.find(toRelativePath(filepath));
    return it != editors.end() && it->second.isModified;
}

void editor::CodeEditor::setText(const std::string& filepath, const std::string& text) {
    std::string key = toRelativePath(filepath);
    if (auto it = editors.find(key); it != editors.end()) {
        it->second.editor->SetText(text);
        it->second.savedUndoIndex = it->second.editor->GetUndoIndex();
        it->second.isModified = false;
        try {
            it->second.lastWriteTime = fs::last_write_time(resolveFilepath(key));
        } catch (const std::exception&) {
            // Keeps the previous timestamp, the watcher reloads on the next tick
        }
    }
}

std::string editor::CodeEditor::getText(const std::string& filepath) const {
    if (auto it = editors.find(toRelativePath(filepath)); it != editors.end()) {
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

    fs::file_time_type newWriteTime;
    try {
        newWriteTime = fs::last_write_time(resolveFilepath(newKey));
    } catch (const std::exception&) {
        return false;
    }

    bool wasFocused = (lastFocused == &it->second);

    EditorInstance instance = std::move(it->second);
    instance.filepath = newKey;
    instance.lastWriteTime = newWriteTime;
    editors.erase(it);

    EditorInstance& renamed = editors[newKey];
    renamed = std::move(instance);
    if (wasFocused) {
        lastFocused = &renamed;
    }

    // A new extension needs its own highlighter, and SetLanguage drops the project
    // symbols the suggestions engine had for this buffer
    SyntaxLanguage language = languageForPath(newKey);
    if (language != renamed.languageType) {
        renamed.languageType = language;
        renamed.editor->SetLanguage(language);
        updateAllProjectSymbols();
    }

    auto changeIt = std::remove_if(changedFilesQueue.begin(), changedFilesQueue.end(),
        [&](const PendingFileChange& change) {
            return change.filepath == fs::path(oldKey);
        });
    changedFilesQueue.erase(changeIt, changedFilesQueue.end());

    project->removeTab(TabType::CODE_EDITOR, oldKey);
    project->addTab(TabType::CODE_EDITOR, newKey);
    project->saveProjectFile();

    Backend::getApp().addNewCodeWindowToDock(fs::path(newKey));

    return true;
}

void editor::CodeEditor::show() {
    applyParsedProjectSymbols();

    double currentTime = ImGui::GetTime();

    windowFocused = false;

    if (currentTime - lastScriptWatchTime >= 1.0) {
        checkExternalScriptChanges();
        lastScriptWatchTime = currentTime;
    }

    for (auto it = editors.begin(); it != editors.end();) {
        auto& instance = it->second;

        if (currentTime - instance.lastCheckTime >= 1.0) {
            checkFileChanges(instance);
            instance.lastCheckTime = currentTime;
        }

        std::string windowTitle = getWindowTitle(instance);

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(windowTitle.c_str(), &instance.isOpen)) {

            instance.isModified = instance.editor->GetUndoIndex() != instance.savedUndoIndex;

            // Undone past a drag-drop property insertion, re-parse from the current content
            if (instance.propertyInsertUndoIndex >= 0 &&
                instance.editor->GetUndoIndex() < instance.propertyInsertUndoIndex) {
                instance.propertyInsertUndoIndex = -1;
                updateScriptProperties(instance, instance.editor->GetText());
            }

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                windowFocused = true;
                lastFocused = &instance;
            }

            int line, column;
            instance.editor->GetCursorDisplayPosition(line, column);

            float statusRowY = ImGui::GetCursorPosY();

            ImGui::Text("%6d/%-6d %6d lines  | %s | %s",
                line + 1,
                column + 1,
                instance.editor->GetLineCount(),
                instance.editor->GetLanguageName(),
                instance.isModified ? "*" : " ");

            // Full-height button vertically centered on the status row, overlapping the
            // spacing above and below so the row keeps its text-only height
            float settingsButtonWidth = ImGui::CalcTextSize(ICON_FA_GEAR).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - settingsButtonWidth);
            ImGui::SetCursorPosY(statusRowY - (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
            showSettingsButton();
            ImGui::SetCursorPosY(statusRowY + ImGui::GetTextLineHeightWithSpacing());

            // Font size is em-based like VSCode, ImGui sizes by the line box
            ImGui::PushFont(App::getCodeFont(), AppSettings::getCodeEditorFontSize() * App::codeFontEmScale);
            instance.editor->Render("TextEditor");
            ImGui::PopFont();

            if ((instance.languageType == SyntaxLanguage::Lua || instance.languageType == SyntaxLanguage::Cpp) && ImGui::BeginDragDropTarget()) {
                bool isCppSource = (instance.languageType == SyntaxLanguage::Cpp) && !Util::isHeaderFile(instance.filepath.string());

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity")) {
                    if (isCppSource) {
                        Backend::getApp().registerAlert("Drop Entity", "Entity properties must be added to the header file (.h), not the source file (.cpp).");
                    } else if (payload->DataSize >= sizeof(EntityPayload)) {
                        const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                        if (instance.languageType == SyntaxLanguage::Lua)
                            insertLuaEntityProperty(instance, entityPayload->entity, entityPayload->entitySceneId);
                        else
                            insertCppEntityProperty(instance, entityPayload->entity, entityPayload->entitySceneId);
                    }
                }
                ImGui::EndDragDropTarget();
                ImGui::SetWindowFocus();
                instance.editor->RequestFocus();
            }
        }
        ImGui::End();

        if (instance.isOpen) {
            ++it;
            continue;
        }

        if (!instance.isModified) {
            if (lastFocused == &instance) {
                lastFocused = nullptr;
            }
            project->removeTab(TabType::CODE_EDITOR, instance.filepath.string());
            project->saveProjectFile();
            it = editors.erase(it);
            continue;
        }

        // Keep the window alive and selected while the confirmation dialog is up
        instance.isOpen = true;
        std::string closeKey = it->first;
        std::string windowId = "###" + instance.filepath.string();
        ImGui::SetWindowFocus(windowId.c_str());

        auto closeInstance = [this, closeKey](bool saveFirst) {
            auto fit = editors.find(closeKey);
            if (fit == editors.end()) return;

            if (saveFirst) {
                save(fit->second);
            } else {
                updateScriptProperties(fit->second);
            }
            if (lastFocused == &fit->second) lastFocused = nullptr;
            project->removeTab(TabType::CODE_EDITOR, fit->second.filepath.string());
            project->saveProjectFile();
            editors.erase(fit);
        };

        Backend::getApp().registerThreeButtonAlert(
            "Unsaved Changes",
            "\"" + instance.filepath.filename().string() + "\" has unsaved changes. Do you want to save before closing?",
            [closeInstance]() { closeInstance(true); },
            [closeInstance]() { closeInstance(false); },
            [windowId]() { ImGui::SetWindowFocus(windowId.c_str()); }
        );
        ++it;
    }

    handleFileChangePopup();
}
