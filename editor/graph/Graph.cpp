// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Graph.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>
#include <set>

using namespace doriax;

namespace {

    std::string formatNumber(float value) {
        return editor::GraphValue::formatNumber(value);
    }

    editor::GraphValue readValue(const YAML::Node& node) {
        editor::GraphValue value;
        if (node.IsSequence()) {
            for (const YAML::Node& item : node)
                value.numbers.push_back(item.as<float>());
        } else if (node.IsScalar()) {
            // a quoted scalar is text, whatever it reads as
            float number = 0.0f;
            if (node.Tag() != "!" && YAML::convert<float>::decode(node, number))
                value.numbers.push_back(number);
            else
                value.text = node.Scalar();
        }
        return value;
    }

    // whether a text would be read back as a number, so it is written quoted
    bool readsAsNumber(const std::string& text) {
        float number = 0.0f;
        return YAML::convert<float>::decode(YAML::Node(text), number);
    }

    void writeValue(YAML::Emitter& out, const editor::GraphValue& value) {
        if (!value.text.empty()) {
            if (readsAsNumber(value.text))
                out << YAML::DoubleQuoted;
            out << value.text;
        } else if (value.numbers.size() == 1) {
            out << formatNumber(value.numbers[0]);
        } else {
            out << YAML::Flow << YAML::BeginSeq;
            for (float number : value.numbers)
                out << formatNumber(number);
            out << YAML::EndSeq;
        }
    }

    void writeNumbers(YAML::Emitter& out, std::initializer_list<float> numbers) {
        out << YAML::Flow << YAML::BeginSeq;
        for (float number : numbers)
            out << formatNumber(number);
        out << YAML::EndSeq;
    }

    // "5.uv" names an output or input of node 5, "2" a parameter
    bool readEnd(const std::string& text, int& node, std::string& pin) {
        const size_t dot = text.find('.');
        const std::string id = text.substr(0, dot);
        if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; }))
            return false;
        if (std::from_chars(id.data(), id.data() + id.size(), node).ec != std::errc())
            return false;
        pin = (dot == std::string::npos) ? std::string() : text.substr(dot + 1);
        return dot == std::string::npos || !pin.empty();
    }

    std::string writeEnd(int node, const std::string& pin) {
        return pin.empty() ? std::to_string(node) : std::to_string(node) + "." + pin;
    }

    template<typename T>
    std::vector<const T*> sortedById(const std::vector<T>& items) {
        std::vector<const T*> sorted;
        for (const T& item : items)
            sorted.push_back(&item);
        std::sort(sorted.begin(), sorted.end(), [](const T* a, const T* b) { return a->id < b->id; });
        return sorted;
    }

}

std::string editor::GraphValue::formatNumber(float value) {
    if (value == 0.0f)
        return "0";
    // std::to_chars for floats is missing from the older systems the editor runs on:
    // the fewest digits that read back the same, then the shorter of 10 and 1e+01
    char scientific[48];
    int digits = 1;
    for (; digits <= 9; digits++) {
        snprintf(scientific, sizeof(scientific), "%.*e", digits - 1, value);
        if (strtof(scientific, nullptr) == value)
            break;
    }
    std::string text = scientific;
    if (const char* exponent = strchr(scientific, 'e')) {
        char fixed[64];
        const int decimals = std::max(0, std::min(digits, 9) - 1 - atoi(exponent + 1));
        const int length = snprintf(fixed, sizeof(fixed), "%.*f", decimals, value);
        if (length > 0 && length < (int)sizeof(fixed) && (size_t)length <= text.size() && strtof(fixed, nullptr) == value)
            text = fixed;
    }
    std::replace(text.begin(), text.end(), ',', '.');
    return text;
}

bool editor::GraphValue::parseNumber(const std::string& text, float& value) {
    if (text.empty() || !(std::isdigit((unsigned char)text[0]) || text[0] == '-' || text[0] == '.'))
        return false;
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> value;
    return !stream.fail() && stream.peek() == std::char_traits<char>::eof();
}

bool editor::GraphDocument::read(const std::string& text, std::string& error) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        error = std::string("not valid YAML: ") + e.what();
        return false;
    }
    if (!root.IsMap()) {
        error = "not a graph file";
        return false;
    }

    *this = GraphDocument();
    try {
        version = root["version"] ? root["version"].as<int>() : formatVersion;
        if (version > formatVersion) {
            error = "made by a newer editor (format " + std::to_string(version) + ")";
            return false;
        }
        kind = root["kind"] ? root["kind"].as<std::string>() : std::string();
        type = root["type"] ? root["type"].as<std::string>() : std::string();
        nextId = root["nextId"] ? root["nextId"].as<int>() : 1;

        for (const YAML::Node& item : root["parameters"]) {
            GraphParameter parameter;
            parameter.id = item["id"].as<int>();
            parameter.name = item["name"].as<std::string>();
            parameter.type = item["type"].as<std::string>();
            if (item["default"])
                parameter.value = readValue(item["default"]);
            if (item["range"] && item["range"].size() == 2) {
                parameter.hasRange = true;
                parameter.minValue = item["range"][0].as<float>();
                parameter.maxValue = item["range"][1].as<float>();
            }
            if (item["filter"])
                parameter.filter = item["filter"].as<std::string>();
            if (item["wrap"])
                parameter.wrap = item["wrap"].as<std::string>();
            if (item["runtime"])
                parameter.runtime = item["runtime"].as<bool>();
            if (item["description"])
                parameter.description = item["description"].as<std::string>();
            parameters.push_back(parameter);
        }

        for (const YAML::Node& item : root["nodes"]) {
            GraphNode node;
            for (const auto& field : item) {
                const std::string key = field.first.as<std::string>();
                if (key == "id") {
                    node.id = field.second.as<int>();
                } else if (key == "type") {
                    node.type = field.second.as<std::string>();
                } else if (key == "pos") {
                    node.position = Vector2(field.second[0].as<float>(), field.second[1].as<float>());
                } else if (key == "in") {
                    for (const auto& input : field.second)
                        node.inputs[input.first.as<std::string>()] = readValue(input.second);
                } else {
                    node.settings[key] = readValue(field.second);
                }
            }
            if (node.id <= 0 || node.type.empty()) {
                error = "a node without id or type";
                return false;
            }
            nodes.push_back(node);
        }

        for (const YAML::Node& item : root["links"]) {
            GraphLink link;
            if (!item.IsSequence() || item.size() != 2 ||
                    !readEnd(item[0].as<std::string>(), link.fromNode, link.fromPin) ||
                    !readEnd(item[1].as<std::string>(), link.toNode, link.toPin) || link.toPin.empty()) {
                error = "a link that is not [from, node.pin]";
                return false;
            }
            links.push_back(link);
        }

        for (const YAML::Node& item : root["groups"]) {
            GraphGroup group;
            group.id = item["id"].as<int>();
            if (item["title"])
                group.title = item["title"].as<std::string>();
            if (item["rect"] && item["rect"].size() == 4)
                group.rect = Vector4(item["rect"][0].as<float>(), item["rect"][1].as<float>(), item["rect"][2].as<float>(), item["rect"][3].as<float>());
            if (item["color"] && item["color"].size() == 4)
                group.color = Vector4(item["color"][0].as<float>(), item["color"][1].as<float>(), item["color"][2].as<float>(), item["color"][3].as<float>());
            groups.push_back(group);
        }

        for (const YAML::Node& item : root["comments"]) {
            GraphComment comment;
            comment.id = item["id"].as<int>();
            if (item["text"])
                comment.text = item["text"].as<std::string>();
            if (item["pos"] && item["pos"].size() == 2)
                comment.position = Vector2(item["pos"][0].as<float>(), item["pos"][1].as<float>());
            comments.push_back(comment);
        }
    } catch (const YAML::Exception& e) {
        error = std::string("malformed graph: ") + e.what();
        return false;
    }

    // ids are never reused, whatever the file says
    std::set<int> ids;
    auto take = [&](int id) {
        // 0 is no id, and the next one has to fit
        if (id <= 0 || id == std::numeric_limits<int>::max()) {
            error = "id " + std::to_string(id) + " is not one a graph can hold";
            return false;
        }
        if (!ids.insert(id).second) {
            error = "id " + std::to_string(id) + " is used twice";
            return false;
        }
        nextId = std::max(nextId, id + 1);
        return true;
    };
    for (const GraphParameter& parameter : parameters)
        if (!take(parameter.id)) return false;
    for (const GraphNode& node : nodes)
        if (!take(node.id)) return false;
    for (const GraphGroup& group : groups)
        if (!take(group.id)) return false;
    for (const GraphComment& comment : comments)
        if (!take(comment.id)) return false;

    return true;
}

std::string editor::GraphDocument::write() const {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << version;
    out << YAML::Key << "kind" << YAML::Value << kind;
    out << YAML::Key << "type" << YAML::Value << type;
    out << YAML::Key << "nextId" << YAML::Value << nextId;

    if (!parameters.empty()) {
        out << YAML::Key << "parameters" << YAML::Value << YAML::BeginSeq;
        for (const GraphParameter* parameter : sortedById(parameters)) {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << parameter->id;
            out << YAML::Key << "name" << YAML::Value << parameter->name;
            out << YAML::Key << "type" << YAML::Value << parameter->type;
            if (!parameter->value.empty()) {
                out << YAML::Key << "default" << YAML::Value;
                writeValue(out, parameter->value);
            }
            if (parameter->hasRange) {
                out << YAML::Key << "range" << YAML::Value;
                writeNumbers(out, {parameter->minValue, parameter->maxValue});
            }
            if (!parameter->filter.empty())
                out << YAML::Key << "filter" << YAML::Value << parameter->filter;
            if (!parameter->wrap.empty())
                out << YAML::Key << "wrap" << YAML::Value << parameter->wrap;
            if (parameter->runtime)
                out << YAML::Key << "runtime" << YAML::Value << true;
            if (!parameter->description.empty())
                out << YAML::Key << "description" << YAML::Value << parameter->description;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    if (!nodes.empty()) {
        out << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq;
        for (const GraphNode* node : sortedById(nodes)) {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << node->id;
            out << YAML::Key << "type" << YAML::Value << node->type;
            out << YAML::Key << "pos" << YAML::Value;
            writeNumbers(out, {std::round(node->position.x), std::round(node->position.y)});
            for (const auto& [name, value] : node->settings) {
                if (value.empty())
                    continue;
                out << YAML::Key << name << YAML::Value;
                writeValue(out, value);
            }
            if (!node->inputs.empty()) {
                out << YAML::Key << "in" << YAML::Value << YAML::Flow << YAML::BeginMap;
                for (const auto& [name, value] : node->inputs) {
                    out << YAML::Key << name << YAML::Value;
                    writeValue(out, value);
                }
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    if (!links.empty()) {
        std::vector<const GraphLink*> sorted;
        for (const GraphLink& link : links)
            sorted.push_back(&link);
        // by target, an input takes one link
        std::sort(sorted.begin(), sorted.end(), [](const GraphLink* a, const GraphLink* b) {
            if (a->toNode != b->toNode)
                return a->toNode < b->toNode;
            if (a->toPin != b->toPin)
                return a->toPin < b->toPin;
            if (a->fromNode != b->fromNode)
                return a->fromNode < b->fromNode;
            return a->fromPin < b->fromPin;
        });
        out << YAML::Key << "links" << YAML::Value << YAML::BeginSeq;
        for (const GraphLink* link : sorted) {
            out << YAML::Flow << YAML::BeginSeq << writeEnd(link->fromNode, link->fromPin) << writeEnd(link->toNode, link->toPin) << YAML::EndSeq;
        }
        out << YAML::EndSeq;
    }

    if (!groups.empty()) {
        out << YAML::Key << "groups" << YAML::Value << YAML::BeginSeq;
        for (const GraphGroup* group : sortedById(groups)) {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << group->id;
            out << YAML::Key << "title" << YAML::Value << group->title;
            out << YAML::Key << "rect" << YAML::Value;
            writeNumbers(out, {std::round(group->rect.x), std::round(group->rect.y), std::round(group->rect.z), std::round(group->rect.w)});
            out << YAML::Key << "color" << YAML::Value;
            writeNumbers(out, {group->color.x, group->color.y, group->color.z, group->color.w});
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    if (!comments.empty()) {
        out << YAML::Key << "comments" << YAML::Value << YAML::BeginSeq;
        for (const GraphComment* comment : sortedById(comments)) {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << comment->id;
            out << YAML::Key << "text" << YAML::Value << comment->text;
            out << YAML::Key << "pos" << YAML::Value;
            writeNumbers(out, {std::round(comment->position.x), std::round(comment->position.y)});
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

int editor::GraphDocument::takeId() {
    return nextId++;
}

int editor::GraphDocument::addNode(const std::string& nodeType, const Vector2& position) {
    GraphNode node;
    node.id = takeId();
    node.type = nodeType;
    node.position = position;
    nodes.push_back(node);
    return node.id;
}

void editor::GraphDocument::removeNodes(const std::vector<int>& ids) {
    auto listed = [&](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const GraphNode& node) { return listed(node.id); }), nodes.end());
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& link) {
        return listed(link.toNode) || (!link.fromPin.empty() && listed(link.fromNode));
    }), links.end());
    groups.erase(std::remove_if(groups.begin(), groups.end(), [&](const GraphGroup& group) { return listed(group.id); }), groups.end());
    comments.erase(std::remove_if(comments.begin(), comments.end(), [&](const GraphComment& comment) { return listed(comment.id); }), comments.end());
}

void editor::GraphDocument::moveNodes(const std::vector<int>& ids, const Vector2& delta) {
    for (GraphNode& node : nodes)
        if (std::find(ids.begin(), ids.end(), node.id) != ids.end())
            node.position = node.position + delta;
    for (GraphComment& comment : comments)
        if (std::find(ids.begin(), ids.end(), comment.id) != ids.end())
            comment.position = comment.position + delta;
    for (GraphGroup& group : groups)
        if (std::find(ids.begin(), ids.end(), group.id) != ids.end()) {
            group.rect.x += delta.x;
            group.rect.y += delta.y;
        }
}

void editor::GraphDocument::connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    disconnect(toNode, toPin);
    links.push_back({fromNode, fromPin, toNode, toPin});
}

void editor::GraphDocument::connectExecution(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& link) {
        return link.fromNode == fromNode && link.fromPin == fromPin;
    }), links.end());
    links.push_back({fromNode, fromPin, toNode, toPin});
}

void editor::GraphDocument::disconnect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& link) {
        return link.fromNode == fromNode && link.fromPin == fromPin && link.toNode == toNode && link.toPin == toPin;
    }), links.end());
}

void editor::GraphDocument::disconnect(int toNode, const std::string& toPin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& link) {
        return link.toNode == toNode && link.toPin == toPin;
    }), links.end());
}

int editor::GraphDocument::addParameter(const GraphParameter& parameter) {
    GraphParameter added = parameter;
    added.id = takeId();
    parameters.push_back(added);
    return added.id;
}

void editor::GraphDocument::removeParameter(int id) {
    parameters.erase(std::remove_if(parameters.begin(), parameters.end(), [&](const GraphParameter& parameter) { return parameter.id == id; }), parameters.end());
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& link) { return link.fromPin.empty() && link.fromNode == id; }), links.end());
    for (GraphNode& node : nodes)
        for (auto it = node.settings.begin(); it != node.settings.end();) {
            const bool named = (it->first == "parameter" || it->first == "texture" || it->first == "variable") && it->second.text.empty() && (int)it->second.number() == id;
            it = named ? node.settings.erase(it) : std::next(it);
        }
}

editor::GraphNode* editor::GraphDocument::findNode(int id) {
    for (GraphNode& node : nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}

const editor::GraphNode* editor::GraphDocument::findNode(int id) const {
    for (const GraphNode& node : nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}

const editor::GraphParameter* editor::GraphDocument::findParameter(int id) const {
    for (const GraphParameter& parameter : parameters)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

const editor::GraphParameter* editor::GraphDocument::findParameter(const std::string& name) const {
    for (const GraphParameter& parameter : parameters)
        if (parameter.name == name)
            return &parameter;
    return nullptr;
}

bool editor::GraphDocument::feeds(int from, int to) const {
    std::vector<int> open = {from};
    std::set<int> seen;
    while (!open.empty()) {
        const int node = open.back();
        open.pop_back();
        if (node == to)
            return true;
        if (!seen.insert(node).second)
            continue;
        for (const GraphLink& link : links)
            if (!link.fromPin.empty() && link.fromNode == node)
                open.push_back(link.toNode);
    }
    return false;
}

const editor::GraphLink* editor::GraphDocument::findLinkInto(int node, const std::string& pin) const {
    for (const GraphLink& link : links)
        if (link.toNode == node && link.toPin == pin)
            return &link;
    return nullptr;
}
