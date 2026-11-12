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
#include "external/IconsFontAwesome6.h"
#include "shader/ShaderBuilder.h"
#include "shader/graph/ShaderNodes.h"

#include "imgui_internal.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

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

    ImU32 typeColor(const std::string& type) {
        if (type == "vec2") return IM_COL32(120, 200, 120, 255);
        if (type == "vec3") return IM_COL32(220, 200, 90, 255);
        if (type == "vec4" || type == "color") return IM_COL32(190, 120, 220, 255);
        if (type == "int") return IM_COL32(90, 190, 210, 255);
        if (type == "bool") return IM_COL32(220, 90, 90, 255);
        if (type == "texture2D" || type == "textureCube") return IM_COL32(230, 140, 60, 255);
        return IM_COL32(170, 170, 170, 255);
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

    // whether a value of one type reaches a pin of another; numbers convert, the rest does not
    bool compatible(const std::string& from, const std::string& to) {
        if (isTexture(from) || isTexture(to))
            return from == to;
        if (from == "bool" || to == "bool")
            return from == to;
        return true;
    }

    std::string formatNumber(float value) {
        char buffer[32];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
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

    // the type a pin carries in this node: a parameter's own, a mask's size
    std::string pinType(const GraphDocument& document, const GraphNode& node, const NodePin& pin) {
        if (pin.type == "parameter") {
            if (const GraphParameter* parameter = namedParameter(document, node, "parameter"))
                return parameter->type;
            auto valueType = node.settings.find("valueType");
            return valueType != node.settings.end() ? valueType->second.text : "float";
        }
        if (pin.type == "mask") {
            auto mask = node.settings.find("mask");
            static const char* types[] = {"float", "float", "vec2", "vec3", "vec4"};
            return types[std::min<size_t>(mask != node.settings.end() ? mask->second.text.size() : 3, 4)];
        }
        return pin.type;
    }

    bool nodePins(const GraphNode& node, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, const ShaderNodeType** found = nullptr) {
        const ShaderNodeType* type = ShaderNodes::get().find(node.type);
        if (found)
            *found = type;
        if (!type)
            return false;
        // a bad signature leaves the type's own pins
        if (!ShaderNodes::getPins(node, *type, inputs, outputs)) {
            inputs = type->inputs;
            outputs = type->outputs;
        }
        return true;
    }

    // whether from feeds to, so a link from to into from would close a loop
    bool feeds(const GraphDocument& document, int from, int to) {
        std::vector<int> open = {from};
        std::set<int> seen;
        while (!open.empty()) {
            const int node = open.back();
            open.pop_back();
            if (node == to)
                return true;
            if (!seen.insert(node).second)
                continue;
            for (const GraphLink& link : document.links)
                if (!link.fromPin.empty() && link.fromNode == node)
                    open.push_back(link.toNode);
        }
        return false;
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

    // a node, group or comment by its id
    bool exists(const GraphDocument& document, int id) {
        return document.findNode(id) ||
               std::any_of(document.groups.begin(), document.groups.end(), [&](const GraphGroup& group) { return group.id == id; }) ||
               std::any_of(document.comments.begin(), document.comments.end(), [&](const GraphComment& comment) { return comment.id == id; });
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
    instance.history = std::make_unique<CommandHistory>();
    instance.lastEdit = nullptr;
    ShaderBuilder::clearGraphSource(key);

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
    if (instance.document.kind != "shader") {
        instance.loadError = "This is not a shader graph.";
        return false;
    }
    instance.loadError.clear();
    instance.text = instance.savedText = instance.document.write();
    instance.compiled = ShaderGraphCompiler::compile(instance.document, key);
    return true;
}

bool editor::GraphEditorWindow::save(Instance& instance) {
    if (!instance.loadError.empty())
        return false;
    const std::filesystem::path fullPath = project->getProjectPath() / instance.filepath;
    std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        Out::error("Could not save %s", instance.filepath.generic_string().c_str());
        return false;
    }
    file << instance.text;
    file.close();
    instance.savedText = instance.text;
    std::error_code ec;
    instance.writeTime = std::filesystem::last_write_time(fullPath, ec);
    instance.changedOnDisk = false;
    // the file holds the edits now
    ShaderBuilder::clearGraphSource(instance.filepath.generic_string());
    instance.previewAt = -1.0;
    project->invalidateCustomShaders();
    return true;
}

void editor::GraphEditorWindow::documentChanged(Instance& instance, const std::string& text) {
    const std::string key = instance.filepath.generic_string();
    instance.text = text;
    instance.compiled = ShaderGraphCompiler::compile(instance.document, key);
    // the scene builds what the window shows, saved or not, and keeps the last that compiled
    if (!instance.compiled.success)
        return;
    if (text == instance.savedText)
        ShaderBuilder::clearGraphSource(key);
    else
        ShaderBuilder::setGraphSource(key, text);
    instance.previewAt = ImGui::GetTime() + previewDelay;
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
    const bool edited = instance->text != instance->savedText;
    ShaderBuilder::clearGraphSource(key);
    if (instance->preview)
        retiredPreviews.push_back(std::move(instance->preview));
    if (focused == instance)
        focused = nullptr;
    if (lastFocused == instance)
        lastFocused = nullptr;
    instances.erase(it);
    if (edited)
        project->invalidateCustomShaders();
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
    instance->filepath = std::filesystem::path(newKey);
    instance->focusRequested = true;
    if (instance->text != instance->savedText)
        ShaderBuilder::setGraphSource(newKey, instance->text);
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
    for (const ShaderGraphMessage& message : instance.compiled.errors)
        if (message.node)
            errors[message.node] += (errors[message.node].empty() ? "" : "\n") + message.text;

    for (const GraphNode& node : document.nodes) {
        CanvasNode item;
        item.id = node.id;
        item.position = ImVec2(node.position.x, node.position.y);
        if (auto error = errors.find(node.id); error != errors.end())
            item.error = error->second;

        std::vector<NodePin> inputs, outputs;
        const ShaderNodeType* type = nullptr;
        if (!nodePins(node, inputs, outputs, &type)) {
            item.title = node.type;
            item.error = "There is no " + node.type + " node.";
            model.nodes.push_back(item);
            continue;
        }
        item.title = type->name;
        item.color = categoryColor(type->category);

        // what tells nodes of one type apart
        for (const NodeSetting& setting : type->settings) {
            auto value = node.settings.find(setting.name);
            std::string shown;
            if (setting.type == "parameter") {
                const GraphParameter* parameter = namedParameter(document, node, setting.name);
                shown = parameter ? parameter->name : "(no " + setting.label + ")";
            } else if (setting.name != "body" && setting.name != "file") {
                shown = describeValue(value != node.settings.end() ? value->second : setting.value);
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
            canvasPin.color = typeColor(pinType(document, node, pin));
            const GraphLink* link = document.findLinkInto(node.id, pin.name);
            canvasPin.connected = link && !link->fromPin.empty();
            if (link && link->fromPin.empty()) {
                const GraphParameter* parameter = document.findParameter(link->fromNode);
                canvasPin.value = "= " + (parameter ? parameter->name : std::string("?"));
            } else if (auto constant = node.inputs.find(pin.name); constant != node.inputs.end()) {
                canvasPin.value = describeValue(constant->second);
            } else if (!type->slots.count(pin.name)) {
                canvasPin.value = describeValue(pin.value);
            }
            item.inputs.push_back(canvasPin);
        }
        for (const NodePin& pin : outputs) {
            CanvasPin canvasPin;
            canvasPin.name = pin.name;
            canvasPin.label = pin.label;
            canvasPin.color = typeColor(pinType(document, node, pin));
            for (const GraphLink& link : document.links)
                if (link.fromNode == node.id && !link.fromPin.empty() && link.fromPin.substr(0, link.fromPin.find('.')) == pin.name)
                    canvasPin.connected = true;
            item.outputs.push_back(canvasPin);
        }
        model.nodes.push_back(item);
    }

    for (const GraphLink& link : document.links) {
        if (link.fromPin.empty())
            continue;
        CanvasLink item;
        item.fromNode = link.fromNode;
        item.fromPin = link.fromPin;
        item.toNode = link.toNode;
        item.toPin = link.toPin;
        item.color = IM_COL32(170, 170, 170, 200);
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
        if (instance.addFromNode && nodePins(node, inputs, outputs)) {
            for (const NodePin& pin : instance.addFromOutput ? inputs : outputs) {
                const std::string pinKind = pinType(document, node, pin);
                if (instance.addFromOutput ? !compatible(instance.addFromType, pinKind) : !compatible(pinKind, instance.addFromType))
                    continue;
                if (instance.addFromOutput)
                    document.connect(instance.addFromNode, instance.addFromPin, id, pin.name);
                else
                    document.connect(id, pin.name, instance.addFromNode, instance.addFromPin);
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
        const NodeType* type = ShaderNodes::get().findType(node.type);
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
            document.connect(ids[link.fromNode], link.fromPin, ids[link.toNode], link.toPin);
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
                if (!from || !to || !nodePins(*from, fromInputs, fromOutputs) || !nodePins(*to, toInputs, toOutputs))
                    break;
                std::string fromType, toType;
                for (const NodePin& pin : fromOutputs)
                    if (pin.name == change.fromPin.substr(0, change.fromPin.find('.')))
                        fromType = pinType(document, *from, pin);
                for (const NodePin& pin : toInputs)
                    if (pin.name == change.toPin)
                        toType = pinType(document, *to, pin);
                if (fromType.empty() || toType.empty() || !compatible(fromType, toType)) {
                    Out::warning("A %s output does not go into a %s input", fromType.c_str(), toType.c_str());
                    break;
                }
                if (feeds(document, change.toNode, change.fromNode)) {
                    Out::warning("That link would make the graph loop");
                    break;
                }
                document.connect(change.fromNode, change.fromPin, change.toNode, change.toPin);
                break;
            }
            case CanvasEdit::Kind::DISCONNECT:
                document.disconnect(change.toNode, change.toPin);
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
                if (node && nodePins(*node, inputs, outputs))
                    for (const NodePin& pin : change.fromOutput ? outputs : inputs)
                        if (pin.name == change.fromPin.substr(0, change.fromPin.find('.')))
                            instance.addFromType = pinType(document, *node, pin);
                break;
            }
            case CanvasEdit::Kind::REROUTE: {
                const GraphLink* link = document.findLinkInto(change.toNode, change.toPin);
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
            case CanvasEdit::Kind::DUPLICATE: {
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
        }
    }
    if (!document.findNode(instance.inspected))
        instance.inspected = 0;
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
    auto matches = [&](const NodeType& type) {
        return search.empty() || lower(type.name + " " + type.category + " " + type.id).find(search) != std::string::npos;
    };

    // what fits this graph, and the dropped link when there is one
    std::map<std::string, std::vector<const NodeType*>> categories;
    for (const NodeType* base : ShaderNodes::get().getTypes()) {
        const ShaderNodeType* type = static_cast<const ShaderNodeType*>(base);
        if (type->id.rfind("output/", 0) == 0 || !matches(*type))
            continue;
        if (!type->shaderTypes.empty() && std::find(type->shaderTypes.begin(), type->shaderTypes.end(), instance.document.type) == type->shaderTypes.end())
            continue;
        if (instance.addFromNode && !instance.addFromType.empty() && type->id != "utility/customFunction") {
            const std::vector<NodePin>& pins = instance.addFromOutput ? type->inputs : type->outputs;
            const bool fits = std::any_of(pins.begin(), pins.end(), [&](const NodePin& pin) {
                return instance.addFromOutput ? compatible(instance.addFromType, pin.type) : compatible(pin.type, instance.addFromType);
            });
            if (!fits)
                continue;
        }
        categories[type->category].push_back(type);
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
    ImGui::BeginChild("##nodes", Theme::dpi(ImVec2(260.0f, 320.0f)));
    for (const auto& [category, list] : categories) {
        const bool open = !search.empty() || ImGui::TreeNode(category.c_str());
        if (!open)
            continue;
        for (const NodeType* type : list) {
            if (!first)
                first = type;
            if (ImGui::Selectable((type->name + "##" + type->id).c_str()))
                chosen = type;
            if (!type->description.empty())
                ImGui::SetItemTooltip("%s", type->description.c_str());
        }
        if (search.empty())
            ImGui::TreePop();
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

    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float buttons = ImGui::CalcTextSize(ICON_FA_PLUS).x + ImGui::CalcTextSize(ICON_FA_MINUS).x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::TextDisabled("Parameters");
    ImGui::SameLine(right - buttons);
    if (ImGui::SmallButton(ICON_FA_PLUS "##addParameter"))
        ImGui::OpenPopup("##parameterTypes");
    ImGui::SetItemTooltip("Add a parameter");
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.findParameter(instance.parameter));
    if (ImGui::SmallButton(ICON_FA_MINUS "##removeParameter")) {
        document.removeParameter(instance.parameter);
        instance.parameter = 0;
        changed = true;
    }
    ImGui::SetItemTooltip("Remove the parameter, and the links from it");
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("##parameterTypes")) {
        static const std::pair<const char*, const char*> types[] = {
            {"Float", "float"}, {"Vector 2", "vec2"}, {"Vector 3", "vec3"}, {"Vector 4", "vec4"}, {"Color", "color"},
            {"Integer", "int"}, {"Texture 2D", "texture2D"}, {"Cube Map", "textureCube"}};
        for (const auto& [label, type] : types) {
            if (!ImGui::Selectable(label))
                continue;
            GraphParameter parameter;
            parameter.type = type;
            const std::string base = isTexture(type) ? "texture" : "value";
            for (int n = 1; parameter.name.empty(); n++)
                if (!document.findParameter(base + std::to_string(n)))
                    parameter.name = base + std::to_string(n);
            if (isTexture(type)) {
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

    for (const GraphParameter& parameter : document.parameters) {
        ImGui::PushID(parameter.id);
        if (ImGui::Selectable(parameter.name.c_str(), parameter.id == instance.parameter))
            instance.parameter = parameter.id;
        // dropped on the canvas, it becomes a node
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("graph_parameter", &parameter.id, sizeof(int));
            ImGui::TextUnformatted(parameter.name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(parameter.type.c_str()).x);
        ImGui::TextDisabled("%s", parameter.type.c_str());
        ImGui::PopID();
    }
    if (document.parameters.empty())
        ImGui::TextDisabled("None yet. They show in Properties.");

    GraphParameter* parameter = nullptr;
    for (GraphParameter& item : document.parameters)
        if (item.id == instance.parameter)
            parameter = &item;

    if (parameter) {
        ImGui::Separator();
        ImGui::PushID(parameter->id);

        // a name keeps to what GLSL allows, and is taken once typed
        const ImGuiID nameId = ImGui::GetID("##name");
        if (ImGui::GetActiveID() != nameId)
            instance.parameterName = parameter->name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        editText("##name", instance.parameterName, nullptr, ImGuiInputTextFlags_CallbackCharFilter, identifierFilter);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string& name = instance.parameterName;
            if (!name.empty() && !std::isdigit((unsigned char)name[0]) && !document.findParameter(name)) {
                parameter->name = name;
                changed = true;
            }
        }

        if (isTexture(parameter->type)) {
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
            combo("Default", defaultTexture, {"white", "black", "normal"});
            if (defaultTexture != parameter->value.text)
                parameter->value = GraphValue(defaultTexture);
            combo("Filter", parameter->filter, {"nearest", "linear", "trilinear"});
            combo("Wrap", parameter->wrap, {"repeat", "clamp", "mirror"});
        } else {
            const int count = componentCount(parameter->type);
            std::vector<float> values = parameter->value.numbers;
            values.resize(count, parameter->type == "color" ? 1.0f : 0.0f);
            bool set = false;
            if (parameter->type == "color") {
                set = ImGui::ColorEdit4("Default", values.data(), ImGuiColorEditFlags_Float);
            } else if (parameter->type == "int") {
                int value = (int)std::lround(values[0]);
                if ((set = ImGui::DragInt("Default", &value)))
                    values[0] = (float)value;
            } else if (parameter->hasRange && count == 1) {
                set = ImGui::SliderFloat("Default", values.data(), parameter->minValue, parameter->maxValue);
            } else {
                set = ImGui::DragScalarN("Default", ImGuiDataType_Float, values.data(), count, 0.01f);
            }
            if (set) {
                parameter->value = GraphValue(values);
                changed = true;
            }
            if (parameter->type == "float" || parameter->type == "int") {
                changed |= ImGui::Checkbox("Range", &parameter->hasRange);
                if (parameter->hasRange) {
                    float range[2] = {parameter->minValue, parameter->maxValue};
                    if (ImGui::DragFloat2("##range", range, 0.01f)) {
                        parameter->minValue = range[0];
                        parameter->maxValue = range[1];
                        changed = true;
                    }
                }
            }
            changed |= ImGui::Checkbox("Set by scripts", &parameter->runtime);
            ImGui::SetItemTooltip("Hidden in Properties; scripts set it");
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= editText("##description", parameter->description, "Description");
        ImGui::PopID();
    }

    if (changed)
        edit(instance, "parameter" + std::to_string(instance.parameter));
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
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= editText("##title", group.title);
        float color[4] = {group.color.x, group.color.y, group.color.z, group.color.w};
        if (ImGui::ColorEdit4("Color", color, ImGuiColorEditFlags_AlphaBar)) {
            group.color = Vector4(color[0], color[1], color[2], color[3]);
            changed = true;
        }
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
    const ShaderNodeType* type = nullptr;
    if (!nodePins(*node, inputs, outputs, &type)) {
        ImGui::TextColored(errorColor, "There is no %s node.", node->type.c_str());
        return;
    }

    ImGui::TextUnformatted(type->name.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", type->description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    bool changed = false;
    ImGui::PushID(node->id);
    for (const NodeSetting& setting : type->settings) {
        auto stored = node->settings.find(setting.name);
        const GraphValue value = stored != node->settings.end() ? stored->second : setting.value;
        ImGui::PushID(setting.name.c_str());
        if (setting.type == "choice") {
            const std::string current = describeValue(value);
            if (ImGui::BeginCombo(setting.label.c_str(), current.c_str())) {
                for (const std::string& option : setting.options) {
                    if (ImGui::Selectable(option.c_str(), option == current)) {
                        // a number stays a number, as the file writes it
                        float number = 0.0f;
                        auto parsed = std::from_chars(option.data(), option.data() + option.size(), number);
                        const bool numeric = parsed.ec == std::errc() && parsed.ptr == option.data() + option.size();
                        node->settings[setting.name] = numeric ? GraphValue(number) : GraphValue(option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (setting.type == "parameter") {
            const GraphParameter* current = namedParameter(document, *node, setting.name);
            if (ImGui::BeginCombo(setting.label.c_str(), current ? current->name.c_str() : "(none)")) {
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
        } else if (setting.type == "number") {
            // a constant, by the type of what it gives
            const std::string outType = outputs.empty() ? "float" : outputs.front().type;
            std::vector<float> numbers = value.numbers;
            numbers.resize(componentCount(outType), outType == "color" ? 1.0f : 0.0f);
            bool set = false;
            if (outType == "color") {
                set = ImGui::ColorEdit4(setting.label.c_str(), numbers.data(), ImGuiColorEditFlags_Float);
            } else if (outType == "int") {
                int number = (int)std::lround(numbers[0]);
                if ((set = ImGui::DragInt(setting.label.c_str(), &number)))
                    numbers[0] = (float)number;
            } else {
                set = ImGui::DragScalarN(setting.label.c_str(), ImGuiDataType_Float, numbers.data(), (int)numbers.size(), 0.01f);
            }
            if (set) {
                node->settings[setting.name] = GraphValue(numbers);
                changed = true;
            }
        } else {
            std::string text = value.text;
            const bool set = setting.name == "body"
                ? editTextMultiline(setting.label.c_str(), text, ImVec2(-FLT_MIN, Theme::dpi(140.0f)), ImGuiInputTextFlags_AllowTabInput)
                : editText(setting.label.c_str(), text);
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
        if ((link && !link->fromPin.empty()) || isTexture(pin.type))
            continue;
        ImGui::PushID(pin.name.c_str());
        auto constant = node->inputs.find(pin.name);
        const bool slot = type->slots.count(pin.name) > 0;
        if (link) {
            // fed by a parameter straight
            const GraphParameter* parameter = document.findParameter(link->fromNode);
            ImGui::TextDisabled("%s: %s", pin.label.c_str(), parameter ? parameter->name.c_str() : "?");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) {
                document.disconnect(node->id, pin.name);
                changed = true;
            }
            ImGui::SetItemTooltip("Unlink the parameter");
        } else if (constant == node->inputs.end() && (slot || !pin.value.text.empty())) {
            // an output left to the shader, or a context default
            ImGui::TextDisabled("%s: %s", pin.label.c_str(), slot ? "not set" : describeValue(pin.value).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Set")) {
                const std::string valueType = pin.type == "dynamic" ? "float" : pin.type;
                node->inputs[pin.name] = GraphValue(std::vector<float>(componentCount(valueType), valueType == "color" ? 1.0f : 0.0f));
                changed = true;
            }
        } else {
            const GraphValue current = constant != node->inputs.end() ? constant->second : pin.value;
            std::vector<float> numbers = current.numbers;
            const int count = pin.type == "dynamic" ? std::max(1, (int)numbers.size()) : componentCount(pin.type);
            numbers.resize(count, pin.type == "color" ? 1.0f : 0.0f);
            bool set = false;
            if (pin.type == "color") {
                set = ImGui::ColorEdit4(pin.label.c_str(), numbers.data(), ImGuiColorEditFlags_Float);
            } else if (pin.type == "bool") {
                bool on = numbers[0] != 0.0f;
                if ((set = ImGui::Checkbox(pin.label.c_str(), &on)))
                    numbers[0] = on ? 1.0f : 0.0f;
            } else if (pin.type == "int") {
                int number = (int)std::lround(numbers[0]);
                if ((set = ImGui::DragInt(pin.label.c_str(), &number)))
                    numbers[0] = (float)number;
            } else {
                set = ImGui::DragScalarN(pin.label.c_str(), ImGuiDataType_Float, numbers.data(), count, 0.01f);
            }
            if (set) {
                node->inputs[pin.name] = GraphValue(numbers);
                changed = true;
            }
            if (constant != node->inputs.end()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT)) {
                    node->inputs.erase(pin.name);
                    changed = true;
                }
                ImGui::SetItemTooltip("Back to the default");
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    if (changed)
        edit(instance, "node" + std::to_string(instance.inspected));
}

void editor::GraphEditorWindow::drawPreview(Instance& instance) {
    // the scene shows the other types where they draw
    if (instance.document.type != "mesh" || !ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!instance.preview) {
        instance.preview = std::make_unique<ShaderPreviewRender>(previewSize);
        instance.previewGeneration = ShaderBuilder::getCustomShaderGeneration();
    }
    ShaderPreviewRender& preview = *instance.preview;
    preview.setShader(instance.filepath.generic_string());
    // the project's shaders were built anew, this one too
    const uint32_t generation = ShaderBuilder::getCustomShaderGeneration();
    if (generation != instance.previewGeneration) {
        instance.previewGeneration = generation;
        preview.reload();
    }
    preview.render();

    const float width = ImGui::GetContentRegionAvail().x;
    Widgets::image(Backend::getImGuiTexture(preview.getTexture().getRender()), ImVec2(width, width));
    static const std::pair<const char*, ShaderPreviewRender::ShapeType> shapes[] = {
        {ICON_FA_CIRCLE "##sphere", ShaderPreviewRender::ShapeType::SPHERE},
        {ICON_FA_CUBE "##cube", ShaderPreviewRender::ShapeType::CUBE},
        {ICON_FA_SQUARE "##plane", ShaderPreviewRender::ShapeType::PLANE}};
    for (const auto& [label, shape] : shapes) {
        if (shape != ShaderPreviewRender::ShapeType::SPHERE)
            ImGui::SameLine();
        const bool current = preview.getShape() == shape;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(label))
            preview.setShape(shape);
        if (current)
            ImGui::PopStyleColor();
    }
    ImGui::Spacing();
}

void editor::GraphEditorWindow::drawErrors(Instance& instance) {
    if (instance.compiled.errors.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
    for (size_t i = 0; i < instance.compiled.errors.size(); i++) {
        const ShaderGraphMessage& message = instance.compiled.errors[i];
        ImGui::PushID((int)i);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopTextWrapPos();
        // a click shows the node
        if (message.node && ImGui::IsItemClicked()) {
            instance.canvas.select({message.node});
            instance.canvas.frame(true);
            instance.inspected = message.node;
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
}

void editor::GraphEditorWindow::convertToCode(Instance& instance) {
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
    const bool modified = instance.text != instance.savedText;
    ImGui::BeginDisabled(!modified);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "##save"))
        save(instance);
    ImGui::SetItemTooltip("Save (Ctrl+S)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " Node")) {
        instance.addRequested = true;
        instance.addPosition = instance.canvas.getViewCenter();
        instance.addFromNode = 0;
        instance.showCode = false;
    }
    ImGui::SetItemTooltip("Add a node (or right click the canvas)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER "##frame"))
        instance.canvas.frame();
    ImGui::SetItemTooltip("Frame the graph (F)");
    ImGui::SameLine();
    if (ImGui::Button(instance.showCode ? ICON_FA_DIAGRAM_PROJECT " Graph" : ICON_FA_CODE " Code"))
        instance.showCode = !instance.showCode;
    ImGui::SetItemTooltip(instance.showCode ? "Back to the graph" : "The shader code the graph makes");
    ImGui::SameLine();
    ImGui::BeginDisabled(!instance.compiled.success);
    if (ImGui::Button(ICON_FA_FILE_CODE " Convert"))
        convertToCode(instance);
    ImGui::SetItemTooltip("Write the code to a .glsl beside the graph, and use it where the graph is used");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (instance.compiled.success)
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
                if (isTexture(parameter->type)) {
                    nodeType = parameter->type == "textureCube" ? "texture/sampleCube" : "texture/sample2D";
                    setting = "texture";
                }
                const int node = instance.document.addNode(nodeType, snapped(instance.canvas.getMousePosition()));
                instance.document.findNode(node)->settings[setting] = GraphValue((float)id);
                instance.canvas.select({node});
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

        // a change on disk is taken when nothing here is unsaved
        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(project->getProjectPath() / instance.filepath, ec);
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
                if (ImGui::SmallButton("Reload"))
                    load(instance);
                ImGui::SameLine();
                if (ImGui::SmallButton("Keep mine"))
                    instance.changedOnDisk = false;
            }
            drawToolbar(instance);

            ImGui::BeginChild("##side", ImVec2(Theme::dpi(270.0f), 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
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
            project->invalidateCustomShaders();
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

        // kept open while the person decides
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
}
