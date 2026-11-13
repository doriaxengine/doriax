// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "GraphEditorWindow.h"

#include "App.h"
#include "Backend.h"
#include "Engine.h"
#include "Out.h"
#include "Project.h"
#include "Theme.h"
#include "Widgets.h"
#include "command/type/GraphEditCmd.h"
#include "graph/GraphNodes.h"
#include "external/IconsFontAwesome6.h"
#include "logic/LogicGraphCompiler.h"
#include "logic/LogicNodes.h"
#include "shader/ShaderBuilder.h"
#include "shader/graph/ShaderNodes.h"
#include "util/FileUtils.h"
#include "util/ProjectUtils.h"
#include "util/Util.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace doriax;

namespace {

    using namespace doriax::editor;

    // the scene builds a graph this long after its last edit, so a drag builds once
    constexpr double previewDelay = 0.35;
    constexpr float gridSnap = 10.0f;
    constexpr unsigned int previewSize = 256;

    const ImVec4 errorColor(0.9f, 0.45f, 0.4f, 1.0f);

    ImU32 categoryColor(const std::string& category) {
        static const std::map<std::string, ImU32> colors = {
            {"Output", IM_COL32(140, 50, 50, 255)}, {"Input", IM_COL32(40, 110, 110, 255)}, {"Parameter", IM_COL32(60, 120, 60, 255)},
            {"Math", IM_COL32(60, 75, 100, 255)}, {"Vector", IM_COL32(50, 80, 140, 255)}, {"Color", IM_COL32(110, 60, 130, 255)},
            {"UV", IM_COL32(150, 95, 40, 255)}, {"Texture", IM_COL32(120, 80, 50, 255)}, {"Noise", IM_COL32(100, 100, 50, 255)},
            {"Logic", IM_COL32(130, 60, 90, 255)}, {"Effect", IM_COL32(40, 120, 150, 255)}, {"Scene", IM_COL32(70, 60, 140, 255)},
            {"Utility", IM_COL32(85, 85, 85, 255)}};
        auto it = colors.find(category);
        return it != colors.end() ? it->second : IM_COL32(80, 80, 80, 255);
    }

    ImU32 logicNodeColor(const NodeType& type) {
        // the engine's classes, which may be named as a category of the graph's own
        if (LogicNodes::isEngineType(type.id))
            return IM_COL32(45, 95, 135, 255);
        const std::string& category = type.category;
        static const std::map<std::string, ImU32> colors = {
            {"Flow", IM_COL32(95, 95, 100, 255)}, {"Variables", IM_COL32(60, 120, 60, 255)}, {"Values", IM_COL32(50, 80, 140, 255)},
            {"Math", IM_COL32(60, 75, 100, 255)}, {"Logic", IM_COL32(130, 60, 90, 255)}, {"Text", IM_COL32(150, 95, 40, 255)},
            {"Self", IM_COL32(40, 110, 110, 255)}, {"Input", IM_COL32(40, 110, 110, 255)}, {"Enums", IM_COL32(100, 100, 50, 255)},
            {"Utility", IM_COL32(85, 85, 85, 255)}};
        if (category.rfind("Events", 0) == 0)
            return IM_COL32(140, 50, 50, 255);
        auto it = colors.find(category);
        return it != colors.end() ? it->second : IM_COL32(80, 80, 80, 255);
    }

    ImU32 typeColor(const std::string& type) {
        if (type == "vec2") return IM_COL32(120, 200, 120, 255);
        if (type == "vec3") return IM_COL32(220, 200, 90, 255);
        if (type == "vec4" || type == "color") return IM_COL32(190, 120, 220, 255);
        if (type == "int") return IM_COL32(90, 190, 210, 255);
        if (type == "bool") return IM_COL32(220, 90, 90, 255);
        if (type == "texture2D" || type == "textureCube") return IM_COL32(230, 140, 60, 255);
        return IM_COL32(170, 170, 170, 255);
    }

    ImU32 logicTypeColor(const std::string& type) {
        if (type == GraphNodes::executionType) return IM_COL32(240, 240, 240, 255);
        if (type == "number") return IM_COL32(120, 200, 120, 255);
        if (type == "bool") return IM_COL32(220, 90, 90, 255);
        if (type == "string") return IM_COL32(230, 120, 190, 255);
        if (type == "Vector2" || type == "Vector3" || type == "Vector4" || type == "Quaternion") return IM_COL32(220, 200, 90, 255);
        if (type == "any") return IM_COL32(170, 170, 170, 255);
        return IM_COL32(90, 170, 230, 255);
    }

    bool isTexture(const std::string& type) {
        return type == "texture2D" || type == "textureCube";
    }

    int componentCount(const std::string& type) {
        if (type == "vec2") return 2;
        if (type == "vec3") return 3;
        if (type == "vec4" || type == "color") return 4;
        return 1;
    }

    std::string formatNumber(float value) {
        return GraphValue::formatNumber(value);
    }

    std::string describeValue(const GraphValue& value) {
        // a context default reads as the field it names
        if (!value.text.empty())
            return value.text[0] == '@' ? value.text.substr(1) : value.text;
        std::string text;
        for (float number : value.numbers)
            text += (text.empty() ? "" : ", ") + formatNumber(number);
        return text;
    }

    Vector2 snapped(const ImVec2& point) {
        return Vector2(std::round(point.x / gridSnap) * gridSnap, std::round(point.y / gridSnap) * gridSnap);
    }

    const GraphParameter* namedParameter(const GraphDocument& document, const GraphNode& node, const std::string& setting) {
        auto it = node.settings.find(setting);
        return it != node.settings.end() && it->second.text.empty() ? document.findParameter((int)it->second.number()) : nullptr;
    }

    ImU32 pinColor(const GraphDocument& document, const std::string& type) {
        return GraphNodes::isLogic(document) ? logicTypeColor(type) : typeColor(type);
    }

    // an output a shader graph leaves to the shader when nothing is set
    bool isSlot(const GraphDocument& document, const NodeType& type, const std::string& pin) {
        return !GraphNodes::isLogic(document) && static_cast<const ShaderNodeType&>(type).slots.count(pin) > 0;
    }

    // a string typed into a buffer with room to grow, as Properties does
    std::vector<char> textBuffer(const std::string& text) {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.resize(std::max<size_t>(1024, text.size() + 256), '\0');
        return buffer;
    }

    bool editText(const char* label, std::string& text, const char* hint = nullptr, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr) {
        std::vector<char> buffer = textBuffer(text);
        const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size(), flags, callback)
                                  : ImGui::InputText(label, buffer.data(), buffer.size(), flags, callback);
        if (changed)
            text = buffer.data();
        return changed;
    }

    // rows of a name and its field, as Properties lays them out
    bool beginFields(const char* id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            return false;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, Theme::dpi(96.0f));
        ImGui::TableSetupColumn("Value");
        return true;
    }

    // the next row: its name, then the hidden label of the field that follows, which fills
    // the row but for what trails it
    const char* field(const std::string& label, float trailing = 0.0f) {
        static std::string id;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(trailing > 0.0f ? -trailing : -FLT_MIN);
        id = "##" + label;
        return id.c_str();
    }

    // the room an icon beside a field takes
    float iconWidth() {
        return ImGui::CalcTextSize(ICON_FA_ROTATE_LEFT).x + 6.0f;
    }

    // an icon beside a value, as Properties draws its reset arrow
    bool inlineIcon(const char* label) {
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        const bool pressed = ImGui::Button(label);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return pressed;
    }

    bool editTextMultiline(const char* label, std::string& text, const ImVec2& size, ImGuiInputTextFlags flags = 0) {
        std::vector<char> buffer = textBuffer(text);
        const bool changed = ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size, flags);
        if (changed)
            text = buffer.data();
        return changed;
    }

    int identifierFilter(ImGuiInputTextCallbackData* data) {
        const ImWchar c = data->EventChar;
        return (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? 0 : 1;
    }

    bool isOutputNode(const GraphNode& node) {
        return node.type.rfind("output/", 0) == 0;
    }

    bool isFunctionGraph(const std::filesystem::path& path) {
        if (path.extension() != ".shader")
            return false;
        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        GraphDocument document;
        std::string error;
        return document.read(text.str(), error) && document.kind == "shader" && document.type == "function";
    }

    // the function graphs of a project, by path, past hidden folders such as .doriax
    std::vector<std::string> findFunctionGraphs(const std::filesystem::path& root) {
        std::vector<std::string> found;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
                !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().filename().string().rfind(".", 0) == 0) {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
                continue;
            }
            if (isFunctionGraph(it->path()))
                found.push_back(std::filesystem::relative(it->path(), root, ec).generic_string());
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // where the preview of the selected node builds from, beside the graph it shows
    std::string nodePreviewKey(const std::filesystem::path& graph) {
        return graph.generic_string() + ".node.shader";
    }

    // a node, group or comment by its id
    bool exists(const GraphDocument& document, int id) {
        return document.findNode(id) ||
               std::any_of(document.groups.begin(), document.groups.end(), [&](const GraphGroup& group) { return group.id == id; }) ||
               std::any_of(document.comments.begin(), document.comments.end(), [&](const GraphComment& comment) { return comment.id == id; });
    }

    // the constant of a logic input nothing feeds, by its type
    bool drawLogicConstant(GraphNode& node, const NodePin& pin) {
        auto constant = node.inputs.find(pin.name);
        const GraphValue current = constant != node.inputs.end() ? constant->second : pin.value;
        const std::vector<std::string>& members = LogicNodes::getEnumMembers(pin.type);
        bool changed = false;
        const char* id = field(pin.label, constant != node.inputs.end() ? iconWidth() : 0.0f);

        if (pin.type == "bool") {
            bool on = current.text.empty() ? current.number() != 0.0f : current.text == "true";
            if (ImGui::Checkbox(id, &on)) {
                node.inputs[pin.name] = GraphValue(on ? 1.0f : 0.0f);
                changed = true;
            }
        } else if (pin.type == "number" || LogicNodes::getComponentCount(pin.type) > 1) {
            std::vector<float> numbers = current.numbers;
            numbers.resize(LogicNodes::getComponentCount(pin.type), 0.0f);
            if (ImGui::DragScalarN(id, ImGuiDataType_Float, numbers.data(), (int)numbers.size(), 0.01f)) {
                node.inputs[pin.name] = GraphValue(numbers);
                changed = true;
            }
        } else if (!members.empty()) {
            if (ImGui::BeginCombo(id, current.text.empty() ? "(none)" : current.text.c_str())) {
                for (const std::string& member : members) {
                    if (ImGui::Selectable(member.c_str(), member == current.text)) {
                        node.inputs[pin.name] = GraphValue(member);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (pin.type == "string" || pin.type == "any") {
            // a number typed into an any input stays a number
            std::string text = current.text.empty() && !current.numbers.empty() ? formatNumber(current.number()) : current.text;
            if (editText(id, text)) {
                float number = 0.0f;
                const bool numeric = pin.type == "any" && GraphValue::parseNumber(text, number);
                if (text.empty())
                    node.inputs.erase(pin.name);
                else
                    node.inputs[pin.name] = numeric ? GraphValue(number) : GraphValue(text);
                changed = true;
            }
        } else {
            // an engine object comes through a link
            const bool self = pin.name == "self" && LogicNodes::isEntityClass(pin.type);
            ImGui::TextDisabled("%s", self ? "the script's entity" : (pin.type == "Scene" ? "the script's scene" : "not linked"));
            return false;
        }
        if (!changed && constant != node.inputs.end()) {
            if (inlineIcon(ICON_FA_ROTATE_LEFT)) {
                node.inputs.erase(pin.name);
                changed = true;
            }
            ImGui::SetItemTooltip("Back to the default");
        }
        return changed;
    }

}

editor::GraphEditorWindow::GraphEditorWindow(Project* project): project(project) {
}

editor::GraphEditorWindow::~GraphEditorWindow() {
    closeAll();
}

std::string editor::GraphEditorWindow::getWindowId(const std::filesystem::path& relativePath) {
    return "###Graph:" + relativePath.lexically_normal().generic_string();
}

std::string editor::GraphEditorWindow::toRelativePath(const std::filesystem::path& path) const {
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.is_relative() || project->getProjectPath().empty())
        return normalized.generic_string();
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(normalized, project->getProjectPath(), ec);
    return (!ec && !relative.empty()) ? relative.lexically_normal().generic_string() : normalized.generic_string();
}

bool editor::GraphEditorWindow::load(Instance& instance) {
    const std::string key = instance.filepath.generic_string();
    const std::filesystem::path fullPath = project->getProjectPath() / instance.filepath;
    std::error_code ec;
    instance.writeTime = std::filesystem::last_write_time(fullPath, ec);
    instance.changedOnDisk = false;
    const bool reopened = instance.history != nullptr;
    instance.history = std::make_unique<CommandHistory>();
    instance.lastEdit = nullptr;
    ShaderBuilder::clearGraphSource(key);
    instance.compiled = ShaderGraphResult();

    std::ifstream file(fullPath, std::ios::binary);
    if (!file) {
        instance.loadError = "The file could not be opened.";
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::string error;
    instance.document = GraphDocument();
    if (!instance.document.read(text.str(), error)) {
        instance.loadError = error;
        return false;
    }
    const bool logicFile = Util::isLogicGraphFile(instance.filepath.string());
    if (instance.document.kind != (logicFile ? "logic" : "shader")) {
        instance.loadError = logicFile ? "This is not a logic graph." : "This is not a shader graph.";
        return false;
    }
    instance.loadError.clear();
    instance.text = instance.savedText = instance.document.write();
    compile(instance);
    instance.builtCode = instance.compiled.code;
    // what the scene built of the graph before is not what the file holds now
    if (reopened && !GraphNodes::isLogic(instance.document))
        project->invalidateCustomShader(key);
    return true;
}

void editor::GraphEditorWindow::compile(Instance& instance) {
    if (GraphNodes::isLogic(instance.document)) {
        const std::string name = instance.filepath.filename().string();
        static_cast<GraphResult&>(instance.compiled) = LogicGraphCompiler::compile(instance.document, name, LogicGraphCompiler::getModuleName(name));
        return;
    }
    Project* owner = project;
    instance.compiled = ShaderGraphCompiler::compile(instance.document, instance.filepath.generic_string(), [owner](const std::string& path, std::string& text) {
        return ShaderBuilder::readGraphSource(owner, path, text);
    });
    instance.compiledGeneration = ShaderBuilder::getCustomShaderGeneration();
}

bool editor::GraphEditorWindow::save(Instance& instance) {
    if (!instance.loadError.empty())
        return false;
    const std::filesystem::path fullPath = project->getProjectPath() / instance.filepath;
    if (!FileUtils::writeIfChanged(fullPath, instance.text)) {
        Out::error("Could not save %s", instance.filepath.generic_string().c_str());
        return false;
    }
    instance.savedText = instance.text;
    std::error_code ec;
    instance.writeTime = std::filesystem::last_write_time(fullPath, ec);
    instance.changedOnDisk = false;
    if (GraphNodes::isLogic(instance.document)) {
        writeScript(instance);
        return true;
    }
    // the file holds the edits now
    ShaderBuilder::clearGraphSource(instance.filepath.generic_string());
    instance.previewAt = -1.0;
    project->invalidateCustomShader(instance.filepath.generic_string());
    return true;
}

void editor::GraphEditorWindow::writeScript(Instance& instance) {
    if (!instance.compiled.success) {
        Out::warning("%s has errors, so its script keeps what it had", instance.filepath.generic_string().c_str());
        return;
    }
    std::string error;
    if (!ProjectUtils::writeLogicScript(project, project->getProjectPath() / instance.filepath, instance.savedText, instance.compiled.code, error))
        Out::error("%s", error.c_str());
}

void editor::GraphEditorWindow::documentChanged(Instance& instance, const std::string& text) {
    const std::string key = instance.filepath.generic_string();
    instance.text = text;
    compile(instance);
    // a script is written when the graph is saved
    if (GraphNodes::isLogic(instance.document))
        return;
    // the scene builds what the window shows, saved or not, and keeps the last that compiled
    if (!instance.compiled.success)
        return;
    if (text == instance.savedText)
        ShaderBuilder::clearGraphSource(key);
    else
        ShaderBuilder::setGraphSource(key, text);
    // moving nodes changes no code; a function's shows one output of several
    if (instance.document.type == "function" || instance.compiled.code != instance.builtCode) {
        instance.builtCode = instance.compiled.code;
        instance.previewAt = ImGui::GetTime() + previewDelay;
    }
}

void editor::GraphEditorWindow::applyText(Instance& instance, const std::string& text) {
    std::string error;
    instance.document = GraphDocument();
    instance.document.read(text, error);
    documentChanged(instance, text);
}

void editor::GraphEditorWindow::edit(Instance& instance, const std::string& mergeKey) {
    const std::string after = instance.document.write();
    if (after == instance.text)
        return;
    if (!(instance.lastEdit && instance.lastEdit->extend(mergeKey, after))) {
        Instance* target = &instance;
        GraphEditCmd* command = new GraphEditCmd([this, target](const std::string& text) { applyText(*target, text); }, instance.text, after, mergeKey);
        instance.history->addCommand(command);
        instance.lastEdit = mergeKey.empty() ? nullptr : command;
    }
    documentChanged(instance, after);
}

void editor::GraphEditorWindow::openFile(const std::filesystem::path& path, bool dockToCentral) {
    const std::string key = toRelativePath(path);
    if (key.empty())
        return;

    auto existing = instances.find(key);
    if (existing != instances.end()) {
        existing->second->focusRequested = dockToCentral;
        Backend::getApp().addGraphEditorWindowToDock(existing->second->filepath, dockToCentral);
        return;
    }

    auto instance = std::make_unique<Instance>();
    instance->filepath = std::filesystem::path(key);
    instance->focusRequested = dockToCentral;
    load(*instance);
    instances[key] = std::move(instance);

    project->addTab(TabType::GRAPH_EDITOR, key);
    project->saveWorkspaceFile();
    Backend::getApp().addGraphEditorWindowToDock(std::filesystem::path(key), dockToCentral);
}

// forgets an open graph; the scene goes back to its file
void editor::GraphEditorWindow::erase(const std::string& key) {
    auto it = instances.find(key);
    if (it == instances.end())
        return;
    Instance* instance = it->second.get();
    // a shader graph's unsaved edits were what the scene drew
    const bool edited = !GraphNodes::isLogic(instance->document) && instance->text != instance->savedText;
    ShaderBuilder::clearGraphSource(key);
    ShaderBuilder::clearGraphSource(nodePreviewKey(instance->filepath));
    if (instance->preview)
        retiredPreviews.push_back(std::move(instance->preview));
    if (focused == instance)
        focused = nullptr;
    if (lastFocused == instance)
        lastFocused = nullptr;
    instances.erase(it);
    if (edited)
        project->invalidateCustomShader(key);
}

void editor::GraphEditorWindow::close(const std::string& key) {
    erase(key);
    if (project->hasTab(TabType::GRAPH_EDITOR, key)) {
        project->removeTab(TabType::GRAPH_EDITOR, key);
        project->saveWorkspaceFile();
    }
}

void editor::GraphEditorWindow::closeFile(const std::filesystem::path& path) {
    close(toRelativePath(path));
}

void editor::GraphEditorWindow::closeAll() {
    for (auto& entry : instances) {
        ShaderBuilder::clearGraphSource(entry.first);
        ShaderBuilder::clearGraphSource(nodePreviewKey(entry.second->filepath));
        if (entry.second->preview)
            retiredPreviews.push_back(std::move(entry.second->preview));
    }
    instances.clear();
    focused = nullptr;
    lastFocused = nullptr;
}

bool editor::GraphEditorWindow::isFileOpen(const std::filesystem::path& path) const {
    return instances.count(toRelativePath(path)) > 0;
}

bool editor::GraphEditorWindow::isFileModified(const std::filesystem::path& path) const {
    auto it = instances.find(toRelativePath(path));
    return it != instances.end() && it->second->loadError.empty() && it->second->text != it->second->savedText;
}

void editor::GraphEditorWindow::remapEditedGraphs(const std::function<bool(GraphDocument&)>& remap) {
    for (auto& entry : instances) {
        Instance& instance = *entry.second;
        if (instance.loadError.empty() && instance.text != instance.savedText && remap(instance.document))
            edit(instance);
    }
}

bool editor::GraphEditorWindow::replaceFileText(const std::filesystem::path& path, const std::string& text) {
    auto it = instances.find(toRelativePath(path));
    if (it == instances.end() || !it->second->loadError.empty())
        return false;
    GraphDocument document;
    std::string error;
    if (!document.read(text, error))
        return false;
    Instance& instance = *it->second;
    instance.document = document;
    instance.lastEdit = nullptr;
    edit(instance);
    return save(instance);
}

bool editor::GraphEditorWindow::isFocused() const {
    return focused != nullptr;
}

bool editor::GraphEditorWindow::handleFileRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    const std::string oldKey = toRelativePath(oldPath);
    const std::string newKey = toRelativePath(newPath);
    auto it = instances.find(oldKey);
    if (it == instances.end() || newKey.empty())
        return false;

    std::unique_ptr<Instance> instance = std::move(it->second);
    instances.erase(it);
    erase(newKey);
    // unsaved edits follow the file
    ShaderBuilder::clearGraphSource(oldKey);
    ShaderBuilder::clearGraphSource(nodePreviewKey(oldKey));
    instance->nodePreviewOf.clear();
    instance->filepath = std::filesystem::path(newKey);
    instance->focusRequested = true;
    if (GraphNodes::isLogic(instance->document)) {
        // the file's name is the script's
        compile(*instance);
    } else if (instance->text != instance->savedText) {
        ShaderBuilder::setGraphSource(newKey, instance->text);
    }
    instances[newKey] = std::move(instance);

    project->removeTab(TabType::GRAPH_EDITOR, oldKey);
    project->addTab(TabType::GRAPH_EDITOR, newKey);
    project->saveWorkspaceFile();
    Backend::getApp().addGraphEditorWindowToDock(std::filesystem::path(newKey));
    return true;
}

bool editor::GraphEditorWindow::saveLastFocused() {
    return lastFocused && save(*lastFocused);
}

void editor::GraphEditorWindow::saveAll() {
    for (auto& entry : instances)
        if (entry.second->loadError.empty() && entry.second->text != entry.second->savedText)
            save(*entry.second);
}

bool editor::GraphEditorWindow::hasUnsavedChanges() const {
    for (const auto& entry : instances)
        if (entry.second->loadError.empty() && entry.second->text != entry.second->savedText)
            return true;
    return false;
}

bool editor::GraphEditorWindow::hasLastFocusedUnsavedChanges() const {
    return lastFocused && lastFocused->loadError.empty() && lastFocused->text != lastFocused->savedText;
}

bool editor::GraphEditorWindow::canUndoLastFocused() const {
    return lastFocused && lastFocused->history && lastFocused->history->canUndo();
}

bool editor::GraphEditorWindow::canRedoLastFocused() const {
    return lastFocused && lastFocused->history && lastFocused->history->canRedo();
}

void editor::GraphEditorWindow::undoLastFocused() {
    if (lastFocused && lastFocused->history) {
        lastFocused->lastEdit = nullptr;
        lastFocused->history->undo();
    }
}

void editor::GraphEditorWindow::redoLastFocused() {
    if (lastFocused && lastFocused->history) {
        lastFocused->lastEdit = nullptr;
        lastFocused->history->redo();
    }
}

editor::CanvasModel editor::GraphEditorWindow::buildModel(const Instance& instance) const {
    const GraphDocument& document = instance.document;
    CanvasModel model;

    std::map<int, std::string> errors;
    for (const GraphMessage& message : instance.compiled.errors)
        if (message.node)
            errors[message.node] += (errors[message.node].empty() ? "" : "\n") + message.text;

    for (const GraphNode& node : document.nodes) {
        CanvasNode item;
        item.id = node.id;
        item.position = ImVec2(node.position.x, node.position.y);
        if (auto error = errors.find(node.id); error != errors.end())
            item.error = error->second;

        std::vector<NodePin> inputs, outputs;
        const NodeType* type = nullptr;
        if (!GraphNodes::getPins(document, node, inputs, outputs, &type)) {
            item.title = node.type;
            item.error = "There is no " + node.type + " node.";
            model.nodes.push_back(item);
            continue;
        }
        item.title = type->name;
        if (type->id == "utility/subgraph") {
            auto file = node.settings.find("file");
            if (file != node.settings.end() && !file->second.text.empty())
                item.title = std::filesystem::path(file->second.text).stem().string();
        }
        item.color = GraphNodes::isLogic(document) ? logicNodeColor(*type) : categoryColor(type->category);

        // what tells nodes of one type apart
        for (const NodeSetting& setting : type->settings) {
            auto value = node.settings.find(setting.name);
            std::string shown;
            if (setting.type == "parameter") {
                const GraphParameter* parameter = namedParameter(document, node, setting.name);
                shown = parameter ? parameter->name : "(no " + setting.label + ")";
            } else if (setting.name != "body" && setting.name != "file") {
                shown = describeValue(value != node.settings.end() ? value->second : setting.value);
                // a line of code is enough to tell it by
                if (const size_t end = shown.find('\n'); end != std::string::npos)
                    shown = shown.substr(0, end);
            }
            if (!shown.empty())
                item.subtitle += (item.subtitle.empty() ? "" : ", ") + shown;
        }
        if (item.subtitle.size() > 48)
            item.subtitle = item.subtitle.substr(0, 45) + "...";

        for (const NodePin& pin : inputs) {
            CanvasPin canvasPin;
            canvasPin.name = pin.name;
            canvasPin.label = pin.label;
            canvasPin.color = pinColor(document, GraphNodes::getPinType(document, node, pin));
            canvasPin.execution = pin.kind == PinKind::EXECUTION;
            const GraphLink* link = document.findLinkInto(node.id, pin.name);
            canvasPin.connected = link && !link->fromPin.empty();
            if (link && link->fromPin.empty()) {
                const GraphParameter* parameter = document.findParameter(link->fromNode);
                canvasPin.value = "= " + (parameter ? parameter->name : std::string("?"));
            } else if (auto constant = node.inputs.find(pin.name); constant != node.inputs.end()) {
                canvasPin.value = describeValue(constant->second);
            } else if (!canvasPin.execution && !isSlot(document, *type, pin.name)) {
                canvasPin.value = describeValue(pin.value);
            }
            item.inputs.push_back(canvasPin);
        }
        for (const NodePin& pin : outputs) {
            CanvasPin canvasPin;
            canvasPin.name = pin.name;
            canvasPin.label = pin.label;
            canvasPin.color = pinColor(document, GraphNodes::getPinType(document, node, pin));
            canvasPin.execution = pin.kind == PinKind::EXECUTION;
            for (const GraphLink& link : document.links)
                if (link.fromNode == node.id && !link.fromPin.empty() && link.fromPin.substr(0, link.fromPin.find('.')) == pin.name)
                    canvasPin.connected = true;
            item.outputs.push_back(canvasPin);
        }
        model.nodes.push_back(item);
    }

    // a link whose pin is gone, after a signature or a function changed, is not drawn
    auto hasPin = [&](int id, const std::string& pin, bool output) {
        for (const CanvasNode& node : model.nodes) {
            if (node.id != id)
                continue;
            const std::string name = pin.substr(0, pin.find('.'));
            const std::vector<CanvasPin>& pins = output ? node.outputs : node.inputs;
            const bool named = std::any_of(pins.begin(), pins.end(), [&](const CanvasPin& item) { return item.name == name; });
            return named || (output && !pins.empty() && name.find_first_not_of("xyzwrgba") == std::string::npos);
        }
        return false;
    };
    for (const GraphLink& link : document.links) {
        if (link.fromPin.empty() || !hasPin(link.fromNode, link.fromPin, true) || !hasPin(link.toNode, link.toPin, false))
            continue;
        CanvasLink item;
        item.fromNode = link.fromNode;
        item.fromPin = link.fromPin;
        item.toNode = link.toNode;
        item.toPin = link.toPin;
        item.color = IM_COL32(170, 170, 170, 200);
        if (GraphNodes::isExecutionOutput(document, link.fromNode, link.fromPin)) {
            item.color = IM_COL32(235, 235, 235, 230);
            item.thickness = 3.0f;
        }
        model.links.push_back(item);
    }
    for (const GraphGroup& group : document.groups) {
        CanvasGroup item;
        item.id = group.id;
        item.title = group.title;
        item.position = ImVec2(group.rect.x, group.rect.y);
        item.size = ImVec2(group.rect.z, group.rect.w);
        item.color = ImGui::ColorConvertFloat4ToU32(ImVec4(group.color.x, group.color.y, group.color.z, group.color.w));
        model.groups.push_back(item);
    }
    for (const GraphComment& comment : document.comments) {
        CanvasComment item;
        item.id = comment.id;
        item.text = comment.text;
        item.position = ImVec2(comment.position.x, comment.position.y);
        model.comments.push_back(item);
    }
    return model;
}

void editor::GraphEditorWindow::addNode(Instance& instance, const NodeType& type) {
    GraphDocument& document = instance.document;
    const Vector2 at = snapped(instance.addPosition);
    int id = 0;
    if (type.id == "group") {
        GraphGroup group;
        group.id = id = document.takeId();
        group.title = "Group";
        group.rect = Vector4(at.x, at.y, 300.0f, 200.0f);
        document.groups.push_back(group);
    } else if (type.id == "comment") {
        GraphComment comment;
        comment.id = id = document.takeId();
        comment.text = "Comment";
        comment.position = at;
        document.comments.push_back(comment);
    } else {
        id = document.addNode(type.id, at);
        // the node takes the link that was dropped, on its first pin that fits
        std::vector<NodePin> inputs, outputs;
        const GraphNode& node = *document.findNode(id);
        if (instance.addFromNode && GraphNodes::getPins(document, node, inputs, outputs)) {
            for (const NodePin& pin : instance.addFromOutput ? inputs : outputs) {
                const std::string type = GraphNodes::getPinType(document, node, pin);
                if (instance.addFromOutput ? !GraphNodes::canLink(document, instance.addFromType, type) : !GraphNodes::canLink(document, type, instance.addFromType))
                    continue;
                if (instance.addFromOutput)
                    GraphNodes::connect(document, instance.addFromNode, instance.addFromPin, id, pin.name);
                else
                    GraphNodes::connect(document, id, pin.name, instance.addFromNode, instance.addFromPin);
                break;
            }
        }
    }
    instance.canvas.select({id});
    instance.inspected = id;
    edit(instance);
}

std::string editor::GraphEditorWindow::copySelection(const Instance& instance, const std::vector<int>& ids) const {
    GraphDocument copy;
    copy.kind = instance.document.kind;
    copy.type = instance.document.type;
    copy.nextId = instance.document.nextId;
    auto listed = [&](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    for (const GraphNode& node : instance.document.nodes)
        if (listed(node.id) && !isOutputNode(node))
            copy.nodes.push_back(node);
    for (const GraphLink& link : instance.document.links)
        if (listed(link.toNode) && (link.fromPin.empty() || listed(link.fromNode)))
            copy.links.push_back(link);
    for (const GraphComment& comment : instance.document.comments)
        if (listed(comment.id))
            copy.comments.push_back(comment);
    for (const GraphGroup& group : instance.document.groups)
        if (listed(group.id))
            copy.groups.push_back(group);
    if (copy.nodes.empty() && copy.comments.empty() && copy.groups.empty())
        return std::string();
    // the parameters go along, so another graph can take them
    copy.parameters = instance.document.parameters;
    return copy.write();
}

void editor::GraphEditorWindow::paste(Instance& instance, const std::string& text, const ImVec2& position) {
    GraphDocument copy;
    std::string error;
    if (text.empty() || !copy.read(text, error) || copy.kind != instance.document.kind)
        return;
    GraphDocument& document = instance.document;

    // the pasted part keeps its shape, its top left where asked
    Vector2 low(1e9f, 1e9f);
    for (const GraphNode& node : copy.nodes)
        low = Vector2(std::min(low.x, node.position.x), std::min(low.y, node.position.y));
    for (const GraphComment& comment : copy.comments)
        low = Vector2(std::min(low.x, comment.position.x), std::min(low.y, comment.position.y));
    for (const GraphGroup& group : copy.groups)
        low = Vector2(std::min(low.x, group.rect.x), std::min(low.y, group.rect.y));
    const Vector2 offset = snapped(position) - low;

    // a parameter is matched by name, and added when this graph has none by that name
    std::map<int, int> parameters;
    auto parameterFor = [&](int id) -> int {
        auto known = parameters.find(id);
        if (known != parameters.end())
            return known->second;
        const GraphParameter* source = copy.findParameter(id);
        int mapped = 0;
        if (source) {
            const GraphParameter* existing = document.findParameter(source->name);
            mapped = existing ? (existing->type == source->type ? existing->id : 0) : document.addParameter(*source);
        }
        return parameters[id] = mapped;
    };

    std::map<int, int> ids;
    std::set<int> pasted;
    for (GraphNode node : copy.nodes) {
        ids[node.id] = document.takeId();
        node.id = ids[node.id];
        node.position = node.position + offset;
        const NodeType* type = GraphNodes::getRegistry(document).findType(node.type);
        for (auto it = node.settings.begin(); it != node.settings.end();) {
            const NodeSetting* setting = type ? type->findSetting(it->first) : nullptr;
            if (setting && setting->type == "parameter" && it->second.text.empty()) {
                const int mapped = parameterFor((int)it->second.number());
                if (!mapped) {
                    it = node.settings.erase(it);
                    continue;
                }
                it->second = GraphValue((float)mapped);
            }
            ++it;
        }
        document.nodes.push_back(node);
        pasted.insert(node.id);
    }
    for (const GraphLink& link : copy.links) {
        if (!ids.count(link.toNode))
            continue;
        if (link.fromPin.empty()) {
            if (const int mapped = parameterFor(link.fromNode))
                document.connect(mapped, "", ids[link.toNode], link.toPin);
        } else if (ids.count(link.fromNode)) {
            GraphNodes::connect(document, ids[link.fromNode], link.fromPin, ids[link.toNode], link.toPin);
        }
    }
    for (GraphComment comment : copy.comments) {
        comment.id = document.takeId();
        comment.position = comment.position + offset;
        document.comments.push_back(comment);
        pasted.insert(comment.id);
    }
    for (GraphGroup group : copy.groups) {
        group.id = document.takeId();
        group.rect.x += offset.x;
        group.rect.y += offset.y;
        document.groups.push_back(group);
        pasted.insert(group.id);
    }
    instance.canvas.select(pasted);
}

void editor::GraphEditorWindow::applyCanvasEdits(Instance& instance, const std::vector<CanvasEdit>& edits) {
    GraphDocument& document = instance.document;
    auto removable = [&](const std::vector<int>& ids) {
        // the output node stays, a graph needs it
        std::vector<int> kept;
        for (int id : ids) {
            const GraphNode* node = document.findNode(id);
            if (!node || !isOutputNode(*node))
                kept.push_back(id);
        }
        return kept;
    };

    // what one gesture does is one step
    for (const CanvasEdit& change : edits) {
        switch (change.kind) {
            case CanvasEdit::Kind::MOVE: {
                document.moveNodes(change.ids, Vector2(change.delta.x, change.delta.y));
                // snapped, so a moved node is one changed line in the file
                auto moved = [&](int id) { return std::find(change.ids.begin(), change.ids.end(), id) != change.ids.end(); };
                for (GraphNode& node : document.nodes)
                    if (moved(node.id))
                        node.position = snapped(ImVec2(node.position.x, node.position.y));
                for (GraphComment& comment : document.comments)
                    if (moved(comment.id))
                        comment.position = snapped(ImVec2(comment.position.x, comment.position.y));
                for (GraphGroup& group : document.groups) {
                    if (moved(group.id)) {
                        const Vector2 corner = snapped(ImVec2(group.rect.x, group.rect.y));
                        group.rect.x = corner.x;
                        group.rect.y = corner.y;
                    }
                }
                break;
            }
            case CanvasEdit::Kind::CONNECT: {
                const GraphNode* from = document.findNode(change.fromNode);
                const GraphNode* to = document.findNode(change.toNode);
                std::vector<NodePin> fromInputs, fromOutputs, toInputs, toOutputs;
                if (!from || !to || !GraphNodes::getPins(document, *from, fromInputs, fromOutputs) || !GraphNodes::getPins(document, *to, toInputs, toOutputs))
                    break;
                std::string fromType, toType;
                for (const NodePin& pin : fromOutputs)
                    if (pin.name == change.fromPin.substr(0, change.fromPin.find('.')))
                        fromType = GraphNodes::getPinType(document, *from, pin);
                for (const NodePin& pin : toInputs)
                    if (pin.name == change.toPin)
                        toType = GraphNodes::getPinType(document, *to, pin);
                if (fromType.empty() || toType.empty() || !GraphNodes::canLink(document, fromType, toType)) {
                    Out::warning("%s does not go into %s %s input", fromType.c_str(), toType == GraphNodes::executionType ? "an" : "a", toType.c_str());
                    break;
                }
                if (document.feeds(change.toNode, change.fromNode)) {
                    Out::warning("That link would make the graph loop");
                    break;
                }
                GraphNodes::connect(document, change.fromNode, change.fromPin, change.toNode, change.toPin);
                break;
            }
            case CanvasEdit::Kind::DISCONNECT:
                document.disconnect(change.fromNode, change.fromPin, change.toNode, change.toPin);
                break;
            case CanvasEdit::Kind::REMOVE:
                document.removeNodes(removable(change.ids));
                break;
            case CanvasEdit::Kind::ADD: {
                instance.addRequested = true;
                instance.addPosition = change.position;
                instance.addFromNode = change.fromNode;
                instance.addFromPin = change.fromPin;
                instance.addFromOutput = change.fromOutput;
                instance.addFromType.clear();
                const GraphNode* node = document.findNode(change.fromNode);
                std::vector<NodePin> inputs, outputs;
                if (node && GraphNodes::getPins(document, *node, inputs, outputs))
                    for (const NodePin& pin : change.fromOutput ? outputs : inputs)
                        if (pin.name == change.fromPin.substr(0, change.fromPin.find('.')))
                            instance.addFromType = GraphNodes::getPinType(document, *node, pin);
                break;
            }
            case CanvasEdit::Kind::REROUTE: {
                // shader graphs have the node for it
                const GraphLink* link = GraphNodes::isLogic(document) ? nullptr : document.findLinkInto(change.toNode, change.toPin);
                if (!link)
                    break;
                const int fromNode = link->fromNode;
                const std::string fromPin = link->fromPin;
                const int reroute = document.addNode("utility/reroute", snapped(ImVec2(change.position.x - 10.0f, change.position.y - 30.0f)));
                document.connect(fromNode, fromPin, reroute, "in");
                document.connect(reroute, "out", change.toNode, change.toPin);
                break;
            }
            case CanvasEdit::Kind::COPY:
            case CanvasEdit::Kind::CUT: {
                const std::string copy = copySelection(instance, change.ids);
                if (copy.empty())
                    break;
                ImGui::SetClipboardText(copy.c_str());
                if (change.kind == CanvasEdit::Kind::CUT)
                    document.removeNodes(removable(change.ids));
                break;
            }
            case CanvasEdit::Kind::PASTE:
                if (const char* text = ImGui::GetClipboardText())
                    paste(instance, text, change.position);
                break;
            case CanvasEdit::Kind::CLONE: {
                Vector2 low(1e9f, 1e9f);
                for (int id : change.ids)
                    if (const GraphNode* node = document.findNode(id))
                        low = Vector2(std::min(low.x, node->position.x), std::min(low.y, node->position.y));
                if (low.x < 1e8f)
                    paste(instance, copySelection(instance, change.ids), ImVec2(low.x + 40.0f, low.y + 40.0f));
                break;
            }
            case CanvasEdit::Kind::RESIZE_GROUP:
                for (GraphGroup& group : document.groups) {
                    if (!change.ids.empty() && group.id == change.ids.front()) {
                        group.rect.z = std::round(change.size.x);
                        group.rect.w = std::round(change.size.y);
                    }
                }
                break;
            case CanvasEdit::Kind::OPEN: {
                const GraphNode* node = change.ids.empty() ? nullptr : document.findNode(change.ids.front());
                if (node && node->type == "utility/subgraph") {
                    auto file = node->settings.find("file");
                    if (file != node->settings.end() && !file->second.text.empty())
                        openRequested = project->getProjectPath() / file->second.text;
                }
                break;
            }
        }
    }
    if (!document.findNode(instance.inspected))
        instance.inspected = 0;
    instance.canvas.unframe();
    edit(instance);
}

void editor::GraphEditorWindow::drawAddPopup(Instance& instance) {
    if (instance.addRequested) {
        ImGui::OpenPopup("##addNode");
        instance.search.clear();
        instance.addRequested = false;
    }
    if (!ImGui::BeginPopup("##addNode"))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(Theme::dpi(260.0f));
    editText("##search", instance.search, "Search nodes");
    const bool enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));

    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return text;
    };
    const std::string search = lower(instance.search);
    std::vector<std::string> words;
    std::istringstream typed(search);
    for (std::string word; typed >> word;)
        words.push_back(word);
    // every word typed, in the name, the category or the id
    auto matches = [&](const NodeType& type) {
        if (words.empty())
            return true;
        const std::string text = lower(type.name + " " + type.category + " " + type.id);
        return std::all_of(words.begin(), words.end(), [&](const std::string& word) { return text.find(word) != std::string::npos; });
    };

    // what fits this graph, and the dropped link when there is one
    const GraphDocument& document = instance.document;
    const bool logic = GraphNodes::isLogic(document);
    // the engine's classes apart: some are named as a category of the graph's own, as Input
    std::map<std::string, std::vector<const NodeType*>> categories;
    std::map<std::string, std::vector<const NodeType*>> engineCategories;
    for (const NodeType* type : GraphNodes::getRegistry(document).getTypes()) {
        if (type->id.rfind("output/", 0) == 0 || !matches(*type))
            continue;
        if (!logic) {
            const ShaderNodeType* shaderType = static_cast<const ShaderNodeType*>(type);
            // a function takes any node, the graph calling it decides where it runs
            const bool function = document.type == "function";
            if (!shaderType->shaderTypes.empty() && !function &&
                std::find(shaderType->shaderTypes.begin(), shaderType->shaderTypes.end(), document.type) == shaderType->shaderTypes.end())
                continue;
        }
        if (instance.addFromNode && !instance.addFromType.empty() && type->id != "utility/customFunction") {
            const std::vector<NodePin>& pins = instance.addFromOutput ? type->inputs : type->outputs;
            const bool fits = std::any_of(pins.begin(), pins.end(), [&](const NodePin& pin) {
                const std::string kind = pin.kind == PinKind::EXECUTION ? std::string(GraphNodes::executionType) : pin.type;
                return instance.addFromOutput ? GraphNodes::canLink(document, instance.addFromType, kind) : GraphNodes::canLink(document, kind, instance.addFromType);
            });
            if (!fits)
                continue;
        }
        (logic && LogicNodes::isEngineType(type->id) ? engineCategories : categories)[type->category].push_back(type);
    }
    static const NodeType group = [] {
        NodeType type;
        type.id = "group";
        type.name = "Group";
        type.category = "Layout";
        type.description = "A frame that keeps nodes together.";
        return type;
    }();
    static const NodeType comment = [] {
        NodeType type;
        type.id = "comment";
        type.name = "Comment";
        type.category = "Layout";
        type.description = "A note on the canvas.";
        return type;
    }();
    if (!instance.addFromNode) {
        for (const NodeType* layout : {&group, &comment})
            if (matches(*layout))
                categories["Layout"].push_back(layout);
    }

    const NodeType* chosen = nullptr;
    const NodeType* first = nullptr;
    // a search lists what it finds, and no more than a person reads
    constexpr int searchLimit = 300;
    int listed = 0;
    auto list = [&](const std::vector<const NodeType*>& types, bool labelled) {
        for (const NodeType* type : types) {
            if (!words.empty() && ++listed > searchLimit)
                return;
            if (!first)
                first = type;
            // a property and a method can share a name, as Set Position
            const bool property = type->id.rfind("get/", 0) == 0 || type->id.rfind("set/", 0) == 0;
            const std::string label = labelled ? type->name + "  (" + type->category + (property ? " property" : "") + ")##" + type->id : type->name + "##" + type->id;
            if (ImGui::Selectable(label.c_str()))
                chosen = type;
            if (!type->description.empty())
                ImGui::SetItemTooltip("%s", type->description.c_str());
        }
    };
    ImGui::BeginChild("##nodes", Theme::dpi(ImVec2(logic ? 340.0f : 260.0f, 320.0f)));
    if (!words.empty()) {
        // the names that start with what was typed come first, the short ones before the long,
        // and a graph's own node before one of an engine class with its name
        struct Found {
            int rank;
            size_t length;
            bool engine;
            const NodeType* type;
        };
        std::vector<Found> found;
        for (const auto* group : {&categories, &engineCategories}) {
            for (const auto& [category, types] : *group) {
                for (const NodeType* type : types) {
                    const std::string name = lower(type->name);
                    const int rank = name == search ? 0 : (name.rfind(words.front(), 0) == 0 ? 1 : (name.find(words.front()) != std::string::npos ? 2 : 3));
                    found.push_back({rank, name.size(), group == &engineCategories, type});
                }
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
            return std::tie(a.rank, a.length, a.engine) < std::tie(b.rank, b.length, b.engine);
        });
        std::vector<const NodeType*> ordered;
        for (const Found& item : found)
            ordered.push_back(item.type);
        list(ordered, logic);
        if (listed > searchLimit)
            ImGui::TextDisabled("More match; type more to narrow them");
    } else {
        auto tree = [&](const std::map<std::string, std::vector<const NodeType*>>& group) {
            for (const auto& [category, types] : group) {
                if (ImGui::TreeNode(category.c_str())) {
                    list(types, false);
                    ImGui::TreePop();
                }
            }
        };
        tree(categories);
        // the engine's classes fold into one entry
        if (!engineCategories.empty() && ImGui::TreeNode("Engine")) {
            tree(engineCategories);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    if (enter && first)
        chosen = first;
    if (chosen) {
        addNode(instance, *chosen);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void editor::GraphEditorWindow::drawParameters(Instance& instance) {
    GraphDocument& document = instance.document;
    bool changed = false;

    const bool logic = GraphNodes::isLogic(document);
    if (!ImGui::CollapsingHeader(logic ? "Variables" : "Parameters", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (ImGui::Button(logic ? ICON_FA_PLUS " Variable" : ICON_FA_PLUS " Parameter"))
        ImGui::OpenPopup("##parameterTypes");
    ImGui::SetItemTooltip(logic ? "Add a variable" : "Add a parameter");

    if (ImGui::BeginPopup("##parameterTypes")) {
        static const std::vector<std::pair<const char*, const char*>> shaderTypes = {
            {"Float", "float"}, {"Vector 2", "vec2"}, {"Vector 3", "vec3"}, {"Vector 4", "vec4"}, {"Color", "color"},
            {"Integer", "int"}, {"Texture 2D", "texture2D"}, {"Cube Map", "textureCube"}};
        static const std::vector<std::pair<const char*, const char*>> logicTypes = {
            {"Number", "float"}, {"Integer", "int"}, {"Boolean", "bool"}, {"Text", "string"}, {"Vector 2", "vector2"}, {"Vector 3", "vector3"},
            {"Vector 4", "vector4"}, {"Color", "color3"}, {"Color with alpha", "color4"}, {"Entity", "entity"}};
        for (const auto& [label, type] : logic ? logicTypes : shaderTypes) {
            if (!ImGui::Selectable(label))
                continue;
            GraphParameter parameter;
            parameter.type = type;
            const std::string base = isTexture(type) ? "texture" : "value";
            for (int n = 1; parameter.name.empty(); n++)
                if (!document.findParameter(base + std::to_string(n)))
                    parameter.name = base + std::to_string(n);
            if (logic) {
                parameter.value = LogicNodes::getVariableDefault(parameter.type);
            } else if (isTexture(type)) {
                parameter.value = GraphValue("white");
                parameter.filter = "linear";
                parameter.wrap = "repeat";
            } else {
                parameter.value = GraphValue(std::vector<float>(componentCount(type), parameter.type == "color" ? 1.0f : 0.0f));
            }
            instance.parameter = document.addParameter(parameter);
            changed = true;
        }
        ImGui::EndPopup();
    }

    // each one folds like a script in Properties, so several can be open and set side by side
    int removed = 0;
    int edited = 0;
    for (GraphParameter& item : document.parameters) {
        GraphParameter* parameter = &item;
        ImGui::PushID(parameter->id);
        if (parameter->id == instance.parameter) {
            ImGui::SetNextItemOpen(true);
            instance.parameter = 0;
        }
        ImGui::PushStyleColor(ImGuiCol_Header, Theme::Colors::NestedHeader);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Colors::NestedHeaderHovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Colors::NestedHeaderActive);
        // the id stays while the name is typed
        const bool open = ImGui::CollapsingHeader((parameter->name + "  [" + parameter->type + "]###header").c_str(), ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopStyleColor(3);
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        // dropped on the canvas, it becomes a node
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("graph_parameter", &parameter->id, sizeof(int));
            ImGui::TextUnformatted(parameter->name.c_str());
            ImGui::EndDragDropSource();
        }

        // the trash can at the header's right, as Properties draws a script's
        const ImVec2 after = ImGui::GetCursorPos();
        const float padding = ImGui::GetStyle().FramePadding.x / 4.0f;
        const ImVec2 removeSize(ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + padding * 2.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(headerMax.x - removeSize.x, headerMin.y + (headerMax.y - headerMin.y - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##remove", removeSize))
            removed = parameter->id;
        ImGui::SetItemTooltip(logic ? "Remove the variable; the nodes that name it lose it" : "Remove the parameter, and the links from it");
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPos(after);

        if (open && beginFields("##fields")) {
            const bool before = changed;
            changed = false;
            // a name keeps to what GLSL and Lua allow, and is taken once typed
            const char* nameLabel = field("Name");
            const bool typing = ImGui::GetActiveID() == ImGui::GetID(nameLabel);
            std::string name = typing ? instance.parameterName : parameter->name;
            const bool typed = editText(nameLabel, name, nullptr, ImGuiInputTextFlags_CallbackCharFilter, identifierFilter);
            if (typed || ImGui::IsItemActivated())
                instance.parameterName = name;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const std::string& taken = instance.parameterName;
                if (!taken.empty() && !std::isdigit((unsigned char)taken[0]) && !document.findParameter(taken)) {
                    parameter->name = taken;
                    changed = true;
                }
            }

            if (logic) {
                const std::string valueType = LogicNodes::getVariablePinType(parameter->type);
                if (parameter->type == "string") {
                    std::string text = parameter->value.text;
                    if (editText(field("Default"), text)) {
                        parameter->value = text.empty() ? GraphValue() : GraphValue(text);
                        changed = true;
                    }
                } else if (parameter->type == "entity") {
                    field("Default");
                    ImGui::TextDisabled("Set in Properties");
                } else {
                    std::vector<float> values = parameter->value.numbers;
                    values.resize(LogicNodes::getComponentCount(valueType), 0.0f);
                    bool set = false;
                    if (parameter->type == "bool") {
                        bool on = values[0] != 0.0f;
                        if ((set = ImGui::Checkbox(field("Default"), &on)))
                            values[0] = on ? 1.0f : 0.0f;
                    } else if (parameter->type == "int") {
                        int value = (int)std::lround(values[0]);
                        if ((set = ImGui::DragInt(field("Default"), &value)))
                            values[0] = (float)value;
                    } else if (parameter->type == "color3") {
                        set = ImGui::ColorEdit3(field("Default"), values.data(), ImGuiColorEditFlags_Float);
                    } else if (parameter->type == "color4") {
                        set = ImGui::ColorEdit4(field("Default"), values.data(), ImGuiColorEditFlags_Float);
                    } else {
                        set = ImGui::DragScalarN(field("Default"), ImGuiDataType_Float, values.data(), (int)values.size(), 0.01f);
                    }
                    if (set) {
                        parameter->value = GraphValue(values);
                        changed = true;
                    }
                }
                if (parameter->type != "entity") {
                    changed |= ImGui::Checkbox(field("Private"), &parameter->runtime);
                    ImGui::SetItemTooltip("Kept by the script, not shown in Properties");
                }
            } else if (isTexture(parameter->type)) {
                auto combo = [&](const char* label, std::string& value, std::initializer_list<const char*> options) {
                    if (!ImGui::BeginCombo(label, value.c_str()))
                        return;
                    for (const char* option : options) {
                        if (ImGui::Selectable(option, value == option)) {
                            value = option;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                };
                std::string defaultTexture = parameter->value.text.empty() ? std::string("white") : parameter->value.text;
                combo(field("Default"), defaultTexture, {"white", "black", "normal"});
                if (defaultTexture != parameter->value.text)
                    parameter->value = GraphValue(defaultTexture);
                combo(field("Filter"), parameter->filter, {"nearest", "linear", "trilinear"});
                combo(field("Wrap"), parameter->wrap, {"repeat", "clamp", "mirror"});
            } else {
                const int count = componentCount(parameter->type);
                std::vector<float> values = parameter->value.numbers;
                values.resize(count, parameter->type == "color" ? 1.0f : 0.0f);
                bool set = false;
                if (parameter->type == "color") {
                    set = ImGui::ColorEdit4(field("Default"), values.data(), ImGuiColorEditFlags_Float);
                } else if (parameter->type == "int") {
                    int value = (int)std::lround(values[0]);
                    if ((set = ImGui::DragInt(field("Default"), &value)))
                        values[0] = (float)value;
                } else if (parameter->hasRange && count == 1) {
                    set = ImGui::SliderFloat(field("Default"), values.data(), parameter->minValue, parameter->maxValue);
                } else {
                    set = ImGui::DragScalarN(field("Default"), ImGuiDataType_Float, values.data(), count, 0.01f);
                }
                if (set) {
                    parameter->value = GraphValue(values);
                    changed = true;
                }
                if (parameter->type == "float" || parameter->type == "int") {
                    changed |= ImGui::Checkbox(field("Range"), &parameter->hasRange);
                    if (parameter->hasRange) {
                        float range[2] = {parameter->minValue, parameter->maxValue};
                        if (ImGui::DragFloat2(field("Limits"), range, 0.01f)) {
                            parameter->minValue = range[0];
                            parameter->maxValue = range[1];
                            changed = true;
                        }
                    }
                }
                changed |= ImGui::Checkbox(field("Set by scripts"), &parameter->runtime);
                ImGui::SetItemTooltip("Hidden in Properties; scripts set it");
            }
            changed |= editText(field("Description"), parameter->description);
            if (changed)
                edited = parameter->id;
            changed |= before;
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    if (document.parameters.empty())
        ImGui::TextDisabled(logic ? "None yet. They show on the script in Properties." : "None yet. They show in Properties.");
    if (removed) {
        document.removeParameter(removed);
        changed = true;
    }

    if (changed)
        edit(instance, edited ? "parameter" + std::to_string(edited) : std::string());
}

bool editor::GraphEditorWindow::drawLayoutInspector(Instance& instance) {
    GraphDocument& document = instance.document;
    bool shown = false;
    bool changed = false;
    for (GraphGroup& group : document.groups) {
        if (group.id != instance.inspected)
            continue;
        shown = true;
        ImGui::TextUnformatted("Group");
        ImGui::Separator();
        if (!beginFields("##group"))
            continue;
        changed |= editText(field("Title"), group.title);
        float color[4] = {group.color.x, group.color.y, group.color.z, group.color.w};
        if (ImGui::ColorEdit4(field("Color"), color, ImGuiColorEditFlags_AlphaBar)) {
            group.color = Vector4(color[0], color[1], color[2], color[3]);
            changed = true;
        }
        ImGui::EndTable();
    }
    for (GraphComment& comment : document.comments) {
        if (comment.id != instance.inspected)
            continue;
        shown = true;
        ImGui::TextUnformatted("Comment");
        ImGui::Separator();
        changed |= editTextMultiline("##text", comment.text, ImVec2(-FLT_MIN, Theme::dpi(100.0f)));
    }
    if (changed)
        edit(instance, "layout" + std::to_string(instance.inspected));
    return shown;
}

void editor::GraphEditorWindow::drawInspector(Instance& instance) {
    GraphDocument& document = instance.document;
    GraphNode* node = document.findNode(instance.inspected);
    if (!node) {
        if (!drawLayoutInspector(instance))
            ImGui::TextDisabled("Select a node to set its values.");
        return;
    }
    std::vector<NodePin> inputs, outputs;
    const NodeType* type = nullptr;
    if (!GraphNodes::getPins(document, *node, inputs, outputs, &type)) {
        ImGui::TextColored(errorColor, "There is no %s node.", node->type.c_str());
        return;
    }
    const bool logic = GraphNodes::isLogic(document);

    ImGui::TextUnformatted(type->name.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", type->description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    bool changed = false;
    if (!beginFields("##node"))
        return;
    ImGui::PushID(node->id);
    for (const NodeSetting& setting : type->settings) {
        auto stored = node->settings.find(setting.name);
        const GraphValue value = stored != node->settings.end() ? stored->second : setting.value;
        ImGui::PushID(setting.name.c_str());
        const char* id = field(setting.label);
        if (setting.type == "choice") {
            const std::string current = describeValue(value);
            if (ImGui::BeginCombo(id, current.c_str())) {
                for (const std::string& option : setting.options) {
                    if (ImGui::Selectable(option.c_str(), option == current)) {
                        // a number stays a number, as the file writes it
                        float number = 0.0f;
                        const bool numeric = GraphValue::parseNumber(option, number);
                        node->settings[setting.name] = numeric ? GraphValue(number) : GraphValue(option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (setting.type == "parameter") {
            const GraphParameter* current = namedParameter(document, *node, setting.name);
            if (ImGui::BeginCombo(id, current ? current->name.c_str() : "(none)")) {
                for (const GraphParameter& parameter : document.parameters) {
                    if (std::find(setting.options.begin(), setting.options.end(), parameter.type) == setting.options.end())
                        continue;
                    if (ImGui::Selectable(parameter.name.c_str(), current == &parameter)) {
                        node->settings[setting.name] = GraphValue((float)parameter.id);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (setting.type == "function") {
            const std::string current = value.text;
            const float openWidth = ImGui::CalcTextSize(ICON_FA_UP_RIGHT_FROM_SQUARE).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(-(openWidth + ImGui::GetStyle().ItemInnerSpacing.x));
            if (ImGui::BeginCombo(id, current.empty() ? "(none)" : current.c_str())) {
                if (ImGui::IsWindowAppearing())
                    functionGraphs = findFunctionGraphs(project->getProjectPath());
                if (functionGraphs.empty())
                    ImGui::TextDisabled("No function graph in the project");
                for (const std::string& path : functionGraphs) {
                    if (ImGui::Selectable(path.c_str(), path == current)) {
                        node->settings[setting.name] = GraphValue(path);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::BeginDisabled(current.empty());
            if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE))
                openRequested = project->getProjectPath() / current;
            ImGui::SetItemTooltip("Open the function");
            ImGui::EndDisabled();
        } else if (setting.type == "number" && logic) {
            float number = value.number();
            if (ImGui::DragFloat(id, &number, 0.01f)) {
                node->settings[setting.name] = GraphValue(number);
                changed = true;
            }
        } else if (setting.type == "number") {
            // a constant, by the type of what it gives
            const std::string outType = outputs.empty() ? "float" : outputs.front().type;
            std::vector<float> numbers = value.numbers;
            numbers.resize(componentCount(outType), outType == "color" ? 1.0f : 0.0f);
            bool set = false;
            if (outType == "color") {
                set = ImGui::ColorEdit4(id, numbers.data(), ImGuiColorEditFlags_Float);
            } else if (outType == "int") {
                int number = (int)std::lround(numbers[0]);
                if ((set = ImGui::DragInt(id, &number)))
                    numbers[0] = (float)number;
            } else {
                set = ImGui::DragScalarN(id, ImGuiDataType_Float, numbers.data(), (int)numbers.size(), 0.01f);
            }
            if (set) {
                node->settings[setting.name] = GraphValue(numbers);
                changed = true;
            }
        } else {
            std::string text = value.text;
            const bool set = setting.name == "body"
                ? editTextMultiline(id, text, ImVec2(-FLT_MIN, Theme::dpi(140.0f)), ImGuiInputTextFlags_AllowTabInput)
                : editText(id, text);
            if (set) {
                node->settings[setting.name] = GraphValue(text);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    // the values of inputs nothing feeds
    for (const NodePin& pin : inputs) {
        const GraphLink* link = document.findLinkInto(node->id, pin.name);
        if ((link && !link->fromPin.empty()) || isTexture(pin.type) || pin.kind == PinKind::EXECUTION)
            continue;
        ImGui::PushID(pin.name.c_str());
        auto constant = node->inputs.find(pin.name);
        const bool slot = isSlot(document, *type, pin.name);
        if (link) {
            // fed by a parameter straight
            const GraphParameter* parameter = document.findParameter(link->fromNode);
            field(pin.label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", parameter ? parameter->name.c_str() : "?");
            if (inlineIcon(ICON_FA_XMARK)) {
                document.disconnect(node->id, pin.name);
                changed = true;
            }
            ImGui::SetItemTooltip("Unlink the parameter");
        } else if (logic) {
            changed |= drawLogicConstant(*node, pin);
        } else if (constant == node->inputs.end() && (slot || !pin.value.text.empty())) {
            // an output left to the shader, or a context default
            field(pin.label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", slot ? "not set" : describeValue(pin.value).c_str());
            ImGui::SameLine();
            if (ImGui::Button("Set")) {
                const std::string valueType = pin.type == "dynamic" ? "float" : pin.type;
                const float start = (valueType == "color" || pin.value.text == "true") ? 1.0f : 0.0f;
                node->inputs[pin.name] = GraphValue(std::vector<float>(componentCount(valueType), start));
                changed = true;
            }
        } else {
            const GraphValue current = constant != node->inputs.end() ? constant->second : pin.value;
            std::vector<float> numbers = current.numbers;
            const int count = pin.type == "dynamic" ? std::max(1, (int)numbers.size()) : componentCount(pin.type);
            numbers.resize(count, pin.type == "color" ? 1.0f : 0.0f);
            const char* id = field(pin.label, constant != node->inputs.end() ? iconWidth() : 0.0f);
            bool set = false;
            if (pin.type == "color") {
                set = ImGui::ColorEdit4(id, numbers.data(), ImGuiColorEditFlags_Float);
            } else if (pin.type == "bool") {
                bool on = numbers[0] != 0.0f;
                if ((set = ImGui::Checkbox(id, &on)))
                    numbers[0] = on ? 1.0f : 0.0f;
            } else if (pin.type == "int") {
                int number = (int)std::lround(numbers[0]);
                if ((set = ImGui::DragInt(id, &number)))
                    numbers[0] = (float)number;
            } else {
                set = ImGui::DragScalarN(id, ImGuiDataType_Float, numbers.data(), count, 0.01f);
            }
            if (set) {
                node->inputs[pin.name] = GraphValue(numbers);
                changed = true;
            }
            if (constant != node->inputs.end()) {
                if (inlineIcon(ICON_FA_ROTATE_LEFT)) {
                    node->inputs.erase(pin.name);
                    changed = true;
                }
                ImGui::SetItemTooltip("Back to the default");
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::EndTable();

    if (changed)
        edit(instance, "node" + std::to_string(instance.inspected));
}

void editor::GraphEditorWindow::drawPreview(Instance& instance) {
    // the scene shows the other types where they draw; a function, its first output on a mesh
    const bool mesh = instance.document.type == "mesh" || (instance.document.type == "function" && instance.compiled.type == "mesh");
    if (!mesh || !ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!instance.preview) {
        instance.preview = std::make_unique<ShaderPreviewRender>(previewSize);
        instance.previewGeneration = ShaderBuilder::getCustomShaderGeneration();
    }
    ShaderPreviewRender& preview = *instance.preview;
    // the selected node's value, through a graph of its own
    std::string shader = instance.filepath.generic_string();
    GraphDocument nodeGraph;
    if (instance.previewSelection && ShaderGraphCompiler::previewNode(instance.document, instance.inspected, nodeGraph)) {
        shader = nodePreviewKey(instance.filepath);
        // built again once a drag ends, and only when its code changed
        const std::string of = std::to_string(instance.inspected) + "\n" + instance.text;
        if (of != instance.nodePreviewOf && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            instance.nodePreviewOf = of;
            Project* owner = project;
            const ShaderGraphResult built = ShaderGraphCompiler::compile(nodeGraph, shader, [owner](const std::string& path, std::string& text) {
                return ShaderBuilder::readGraphSource(owner, path, text);
            });
            if (built.code != instance.nodePreviewCode) {
                instance.nodePreviewCode = built.code;
                ShaderBuilder::setGraphSource(shader, nodeGraph.write());
                project->invalidateCustomShader(shader);
            }
        }
    }
    preview.setShader(shader);
    // the project's shaders were built anew, this one too
    const uint32_t generation = ShaderBuilder::getCustomShaderGeneration();
    if (generation != instance.previewGeneration) {
        instance.previewGeneration = generation;
        preview.reload();
    }
    preview.render();

    const float width = ImGui::GetContentRegionAvail().x;
    Widgets::image(Backend::getImGuiTexture(preview.getTexture().getRender()), ImVec2(width, width));
    // the shapes as the scene toolbar shows its modes: the one in use is disabled
    static const struct { const char* label; ShaderPreviewRender::ShapeType shape; const char* tooltip; } shapes[] = {
        {ICON_FA_CIRCLE "##sphere", ShaderPreviewRender::ShapeType::SPHERE, "Sphere"},
        {ICON_FA_CUBE "##cube", ShaderPreviewRender::ShapeType::CUBE, "Cube"},
        {ICON_FA_SQUARE "##plane", ShaderPreviewRender::ShapeType::PLANE, "Plane"}};
    for (const auto& item : shapes) {
        if (item.shape != ShaderPreviewRender::ShapeType::SPHERE)
            ImGui::SameLine();
        ImGui::BeginDisabled(preview.getShape() == item.shape);
        if (ImGui::Button(item.label))
            preview.setShape(item.shape);
        ImGui::SetItemTooltip("%s", item.tooltip);
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0.0f, Theme::dpi(10.0f));
    ImGui::Dummy(ImVec2(1.0f, Theme::dpi(20.0f)));
    ImGui::SameLine(0.0f, Theme::dpi(10.0f));
    const bool selection = instance.previewSelection;
    if (selection)
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ButtonActivated);
    if (ImGui::Button(ICON_FA_CROSSHAIRS "##node"))
        instance.previewSelection = !instance.previewSelection;
    if (selection)
        ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Draw the selected node's value instead of the graph");
    ImGui::Spacing();
}

void editor::GraphEditorWindow::drawErrors(Instance& instance) {
    if (instance.compiled.errors.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
    for (size_t i = 0; i < instance.compiled.errors.size(); i++) {
        const GraphMessage& message = instance.compiled.errors[i];
        ImGui::PushID((int)i);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopTextWrapPos();
        // a click shows the node, or the parameter
        if (message.node && ImGui::IsItemClicked()) {
            if (instance.document.findParameter(message.node)) {
                instance.parameter = message.node;
            } else {
                instance.canvas.select({message.node});
                instance.canvas.frame(true);
            }
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
}

void editor::GraphEditorWindow::convertToCode(Instance& instance) {
    compile(instance);
    if (!instance.compiled.success)
        return;
    std::filesystem::path code = instance.filepath;
    code.replace_extension(".glsl");
    const std::filesystem::path fullPath = project->getProjectPath() / code;
    std::error_code ec;
    if (std::filesystem::exists(fullPath, ec)) {
        Backend::getApp().registerAlert("Convert to Code", code.generic_string() + " already exists. Rename or remove it first.");
        return;
    }
    std::ofstream file(fullPath, std::ios::binary);
    if (!file) {
        Out::error("Could not write %s", code.generic_string().c_str());
        return;
    }
    // its own file now, not one generated
    const std::string& compiled = instance.compiled.code;
    file << "// " << code.filename().generic_string() << ", from the graph " << instance.filepath.filename().generic_string() << compiled.substr(compiled.find('\n'));
    file.close();
    project->remapShaderFilePath(instance.filepath, code);
    Out::info("%s is now code in %s, used where the graph was", instance.filepath.generic_string().c_str(), code.generic_string().c_str());
    if (ResourcesWindow* resources = Backend::getApp().getResourcesWindow())
        resources->refreshCurrentDirectory();
    if (CodeEditor* codeEditor = Backend::getApp().getCodeEditor())
        codeEditor->openFile(fullPath.string(), true);
}

void editor::GraphEditorWindow::drawToolbar(Instance& instance) {
    if (ImGui::Button(ICON_FA_PLUS " Node")) {
        instance.addRequested = true;
        instance.addPosition = instance.canvas.getViewCenter();
        instance.addFromNode = 0;
        instance.showCode = false;
    }
    ImGui::SetItemTooltip("Add a node (or right click the canvas)");
    ImGui::SameLine();
    // nothing to do while the view is as the last framing left it
    ImGui::BeginDisabled(instance.canvas.isFramed());
    if (ImGui::Button(ICON_FA_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER "##frame"))
        instance.canvas.frame();
    ImGui::SetItemTooltip("Frame the graph (F)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(instance.showCode ? ICON_FA_DIAGRAM_PROJECT " Graph" : ICON_FA_CODE " Code"))
        instance.showCode = !instance.showCode;
    const bool logic = GraphNodes::isLogic(instance.document);
    ImGui::SetItemTooltip(instance.showCode ? "Back to the graph" : (logic ? "The Lua script the graph makes" : "The shader code the graph makes"));
    ImGui::SameLine();
    const bool function = instance.document.type == "function";
    // a logic graph's script is kept by the editor
    if (!logic) {
        ImGui::BeginDisabled(!instance.compiled.success || function);
        if (ImGui::Button(ICON_FA_FILE_CODE " Convert"))
            convertToCode(instance);
        ImGui::SetItemTooltip(function ? "A function is used through Subgraph nodes" : "Write the code to a .glsl beside the graph, and use it where the graph is used");
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (instance.compiled.success && logic)
        ImGui::TextDisabled(ICON_FA_CHECK " script");
    else if (instance.compiled.success && function)
        ImGui::TextDisabled(ICON_FA_CHECK " function");
    else if (instance.compiled.success)
        ImGui::TextDisabled(ICON_FA_CHECK " %s shader", instance.document.type.c_str());
    else
        ImGui::TextColored(errorColor, ICON_FA_TRIANGLE_EXCLAMATION " %d error%s", (int)instance.compiled.errors.size(), instance.compiled.errors.size() == 1 ? "" : "s");
}

void editor::GraphEditorWindow::drawCanvas(Instance& instance) {
    if (instance.showCode) {
        std::string code = instance.compiled.success ? instance.compiled.code : instance.compiled.describeErrors();
        ImGui::PushFont(App::getCodeFont(), 0.0f);
        editTextMultiline("##code", code, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
        return;
    }

    // what an undo took away is no longer selected
    std::set<int> selected = instance.canvas.getSelection();
    for (auto it = selected.begin(); it != selected.end();)
        it = exists(instance.document, *it) ? std::next(it) : selected.erase(it);
    if (selected.size() != instance.canvas.getSelection().size())
        instance.canvas.select(selected);

    std::vector<CanvasEdit> edits;
    instance.canvas.draw(buildModel(instance), edits, "##graph");

    // a parameter dropped from the list
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("graph_parameter")) {
            const int id = *(const int*)payload->Data;
            if (const GraphParameter* parameter = instance.document.findParameter(id)) {
                std::string nodeType = "parameter";
                std::string setting = "parameter";
                if (GraphNodes::isLogic(instance.document)) {
                    nodeType = "variable/get";
                    setting = "variable";
                } else if (isTexture(parameter->type)) {
                    nodeType = parameter->type == "textureCube" ? "texture/sampleCube" : "texture/sample2D";
                    setting = "texture";
                }
                const int node = instance.document.addNode(nodeType, snapped(instance.canvas.getMousePosition()));
                instance.document.findNode(node)->settings[setting] = GraphValue((float)id);
                instance.canvas.select({node});
                edit(instance);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
            std::set<int> added;
            ImVec2 position = instance.canvas.getMousePosition();
            const char* data = (const char*)payload->Data;
            for (size_t offset = 0; offset < (size_t)payload->DataSize;) {
                const std::string path(data + offset);
                offset += path.size() + 1;
                if (GraphNodes::isLogic(instance.document) || !isFunctionGraph(path))
                    continue;
                const int node = instance.document.addNode("utility/subgraph", snapped(position));
                instance.document.findNode(node)->settings["file"] = GraphValue(toRelativePath(path));
                added.insert(node);
                position.y += 80.0f;
            }
            if (!added.empty()) {
                instance.canvas.select(added);
                edit(instance);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const std::set<int>& selection = instance.canvas.getSelection();
    instance.inspected = selection.size() == 1 ? *selection.begin() : 0;
    if (!edits.empty())
        applyCanvasEdits(instance, edits);
    drawAddPopup(instance);
}

void editor::GraphEditorWindow::show() {
    retiredPreviews.erase(std::remove_if(retiredPreviews.begin(), retiredPreviews.end(), [](const std::unique_ptr<ShaderPreviewRender>& preview) {
        return !Engine::isSceneRunning(preview->getScene());
    }), retiredPreviews.end());

    focused = nullptr;
    bool tabsChanged = false;

    for (auto it = instances.begin(); it != instances.end();) {
        Instance& instance = *it->second;

        // a drag or typing that ended closes its step
        if (instance.lastEdit && !ImGui::IsAnyItemActive()) {
            instance.lastEdit->setNoMerge();
            instance.lastEdit = nullptr;
        }

        // a function it calls changed: its errors and code are of the old one
        if (!instance.compiled.functions.empty() && instance.compiledGeneration != ShaderBuilder::getCustomShaderGeneration())
            compile(instance);

        // a change on disk is taken when nothing here is unsaved, looked for once a second
        std::error_code ec;
        const bool check = ImGui::GetTime() >= instance.checkAt;
        if (check)
            instance.checkAt = ImGui::GetTime() + 1.0;
        const auto writeTime = check ? std::filesystem::last_write_time(project->getProjectPath() / instance.filepath, ec) : instance.writeTime;
        if (!ec && writeTime != instance.writeTime) {
            instance.writeTime = writeTime;
            if (instance.text == instance.savedText || !instance.loadError.empty())
                load(instance);
            else
                instance.changedOnDisk = true;
        }

        if (instance.focusRequested) {
            ImGui::SetNextWindowFocus();
            instance.focusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);

        const bool modified = instance.loadError.empty() && instance.text != instance.savedText;
        const std::string title = instance.filepath.filename().string() + getWindowId(instance.filepath);
        const bool visible = ImGui::Begin(title.c_str(), &instance.isOpen, modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            focused = lastFocused = &instance;

        if (visible && !instance.loadError.empty()) {
            ImGui::TextColored(errorColor, "%s could not be read: %s", instance.filepath.filename().string().c_str(), instance.loadError.c_str());
            if (ImGui::Button("Retry"))
                load(instance);
        } else if (visible) {
            if (instance.changedOnDisk) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " The file changed on disk.");
                ImGui::SameLine();
                if (ImGui::Button("Reload"))
                    load(instance);
                ImGui::SameLine();
                if (ImGui::Button("Keep mine"))
                    instance.changedOnDisk = false;
            }
            drawToolbar(instance);

            // room between the panel's fields and its edges, where the resize line is
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(8.0f, 8.0f)));
            ImGui::BeginChild("##side", ImVec2(Theme::dpi(290.0f), 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            drawPreview(instance);
            drawParameters(instance);
            ImGui::Spacing();
            ImGui::Separator();
            drawInspector(instance);
            drawErrors(instance);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            drawCanvas(instance);
            ImGui::EndChild();
        }
        ImGui::End();

        // the scene builds the edits a moment after the last one
        if (instance.previewAt >= 0.0 && ImGui::GetTime() >= instance.previewAt) {
            instance.previewAt = -1.0;
            project->invalidateCustomShader(instance.filepath.generic_string());
        }

        if (instance.isOpen) {
            ++it;
            continue;
        }

        if (!modified) {
            const std::string key = it->first;
            ++it;
            erase(key);
            project->removeTab(TabType::GRAPH_EDITOR, key);
            tabsChanged = true;
            continue;
        }

        // kept open until the alert is answered
        instance.isOpen = true;
        const std::string key = it->first;
        const std::string windowId = getWindowId(instance.filepath);
        Backend::getApp().registerThreeButtonAlert(
            "Unsaved Changes",
            "\"" + instance.filepath.filename().string() + "\" has unsaved changes. Do you want to save before closing?",
            [this, key]() {
                auto found = instances.find(key);
                if (found != instances.end() && save(*found->second))
                    close(key);
            },
            [this, key]() { close(key); },
            [windowId]() { ImGui::SetWindowFocus(windowId.c_str()); });
        ++it;
    }

    if (tabsChanged)
        project->saveWorkspaceFile();
    // opened once the loop over the open graphs is done
    if (!openRequested.empty()) {
        openFile(openRequested);
        openRequested.clear();
    }
}
