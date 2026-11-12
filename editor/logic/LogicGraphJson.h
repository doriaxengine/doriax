// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "LogicNodes.h"
#include "shader/graph/ShaderGraphJson.h"

namespace doriax::editor {

    // a node type as the CLI, the MCP tools and the docs list it
    inline nlohmann::json logicNodeTypeJson(const LogicNodeType& type) {
        nlohmann::json node = {{"id", type.id}, {"name", type.name}, {"category", type.category}, {"description", type.description},
                               {"runs", type.runs()}};
        node["inputs"] = nlohmann::json::array();
        for (const NodePin& pin : type.inputs) {
            if (pin.kind == PinKind::EXECUTION)
                node["inputs"].push_back({{"name", pin.name}, {"label", pin.label}, {"execution", true}});
            else
                node["inputs"].push_back({{"name", pin.name}, {"label", pin.label}, {"type", pin.type}, {"default", graphValueJson(pin.value)}});
        }
        node["outputs"] = nlohmann::json::array();
        for (const NodePin& pin : type.outputs) {
            if (pin.kind == PinKind::EXECUTION)
                node["outputs"].push_back({{"name", pin.name}, {"label", pin.label}, {"execution", true}});
            else
                node["outputs"].push_back({{"name", pin.name}, {"label", pin.label}, {"type", pin.type}});
        }
        node["settings"] = nlohmann::json::array();
        for (const NodeSetting& setting : type.settings)
            node["settings"].push_back({{"name", setting.name}, {"label", setting.label}, {"type", setting.type},
                                        {"options", setting.options}, {"default", graphValueJson(setting.value)}});
        return node;
    }

}
