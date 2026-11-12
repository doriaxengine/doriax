// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Graph.h"
#include "GraphResult.h"
#include "NodeRegistry.h"
#include "json.hpp"

#include <functional>

namespace doriax::editor {

    // a value as JSON: a number, a list of numbers, or a word
    inline nlohmann::json graphValueJson(const GraphValue& value) {
        if (!value.text.empty())
            return value.text;
        if (value.numbers.size() == 1)
            return value.numbers[0];
        if (value.numbers.empty())
            return nullptr;
        return value.numbers;
    }

    // the pins that are a node's own, false for a node with its type's
    using GraphNodePins = std::function<bool(const GraphNode& node, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs)>;

    // a graph and what it compiles to; links name their ends as node.pin, or a parameter by name
    inline nlohmann::json graphJson(const GraphDocument& graph, const GraphResult& compiled, const GraphNodePins& ownPins = nullptr) {
        nlohmann::json out = {{"kind", graph.kind}, {"type", graph.type}};
        out["parameters"] = nlohmann::json::array();
        for (const GraphParameter& parameter : graph.parameters) {
            nlohmann::json item = {{"id", parameter.id}, {"name", parameter.name}, {"type", parameter.type}, {"default", graphValueJson(parameter.value)}};
            if (parameter.hasRange)
                item["range"] = {parameter.minValue, parameter.maxValue};
            if (!parameter.filter.empty())
                item["filter"] = parameter.filter;
            if (!parameter.wrap.empty())
                item["wrap"] = parameter.wrap;
            if (parameter.runtime)
                item["runtime"] = true;
            if (!parameter.description.empty())
                item["description"] = parameter.description;
            out["parameters"].push_back(item);
        }
        out["nodes"] = nlohmann::json::array();
        for (const GraphNode& node : graph.nodes) {
            nlohmann::json item = {{"id", node.id}, {"type", node.type}, {"position", {node.position.x, node.position.y}}};
            for (const auto& [name, value] : node.settings)
                item["settings"][name] = graphValueJson(value);
            for (const auto& [name, value] : node.inputs)
                item["inputs"][name] = graphValueJson(value);
            std::vector<NodePin> inputs, outputs;
            if (ownPins && ownPins(node, inputs, outputs)) {
                item["pins"] = {{"inputs", nlohmann::json::array()}, {"outputs", nlohmann::json::array()}};
                for (const NodePin& pin : inputs)
                    item["pins"]["inputs"].push_back({{"name", pin.name}, {"type", pin.type}});
                for (const NodePin& pin : outputs)
                    item["pins"]["outputs"].push_back({{"name", pin.name}, {"type", pin.type}});
            }
            out["nodes"].push_back(item);
        }
        out["links"] = nlohmann::json::array();
        for (const GraphLink& link : graph.links) {
            const std::string to = std::to_string(link.toNode) + "." + link.toPin;
            if (link.fromPin.empty()) {
                const GraphParameter* parameter = graph.findParameter(link.fromNode);
                out["links"].push_back({{"parameter", parameter ? parameter->name : std::string()}, {"to", to}});
            } else {
                out["links"].push_back({{"from", std::to_string(link.fromNode) + "." + link.fromPin}, {"to", to}});
            }
        }
        out["groups"] = nlohmann::json::array();
        for (const GraphGroup& group : graph.groups)
            out["groups"].push_back({{"id", group.id}, {"title", group.title}, {"rect", {group.rect.x, group.rect.y, group.rect.z, group.rect.w}}});
        out["comments"] = nlohmann::json::array();
        for (const GraphComment& comment : graph.comments)
            out["comments"].push_back({{"id", comment.id}, {"text", comment.text}, {"position", {comment.position.x, comment.position.y}}});
        out["valid"] = compiled.success;
        out["errors"] = nlohmann::json::array();
        for (const GraphMessage& message : compiled.errors)
            out["errors"].push_back(message.node ? nlohmann::json{{"node", message.node}, {"message", message.text}} : nlohmann::json{{"message", message.text}});
        return out;
    }

}
