// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "ShaderGraphCompiler.h"
#include "ShaderNodes.h"
#include "graph/GraphJson.h"

namespace doriax::editor {

    // a node type as the CLI, the MCP tools and the docs list it
    inline nlohmann::json shaderNodeTypeJson(const ShaderNodeType& type) {
        static const char* stages[] = {"any", "vertex", "fragment"};
        nlohmann::json node = {{"id", type.id}, {"name", type.name}, {"category", type.category}, {"description", type.description},
                               {"stage", stages[(int)type.stage]}, {"shaderTypes", type.shaderTypes}};
        node["inputs"] = nlohmann::json::array();
        for (const NodePin& pin : type.inputs) {
            nlohmann::json item = {{"name", pin.name}, {"label", pin.label}, {"type", pin.type}, {"default", graphValueJson(pin.value)}};
            if (auto slot = type.slots.find(pin.name); slot != type.slots.end())
                item["fills"] = {{"hook", slot->second.hook}, {"target", slot->second.target}, {"vertex", slot->second.vertex}};
            node["inputs"].push_back(item);
        }
        node["outputs"] = nlohmann::json::array();
        for (const NodePin& pin : type.outputs)
            node["outputs"].push_back({{"name", pin.name}, {"label", pin.label}, {"type", pin.type}});
        node["settings"] = nlohmann::json::array();
        for (const NodeSetting& setting : type.settings)
            node["settings"].push_back({{"name", setting.name}, {"label", setting.label}, {"type", setting.type},
                                        {"options", setting.options}, {"default", graphValueJson(setting.value)}});
        return node;
    }

    // a shader graph and its result, with each node's own pins
    inline nlohmann::json shaderGraphJson(const GraphDocument& graph, const GraphResult& compiled) {
        return graphJson(graph, compiled, [](const GraphNode& node, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs) {
            const ShaderNodeType* type = ShaderNodes::get().find(node.type);
            const bool own = node.type == "utility/subgraph" || node.type == "utility/customFunction" || node.type.rfind("function/", 0) == 0;
            return own && type && ShaderNodes::getPins(node, *type, inputs, outputs);
        });
    }

}
