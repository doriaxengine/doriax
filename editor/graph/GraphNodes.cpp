// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "GraphNodes.h"

#include "logic/LogicNodes.h"
#include "shader/graph/ShaderNodes.h"

using namespace doriax;

bool editor::GraphNodes::isLogic(const GraphDocument& graph) {
    return graph.kind == "logic";
}

const editor::NodeRegistry& editor::GraphNodes::getRegistry(const GraphDocument& graph) {
    if (isLogic(graph))
        return LogicNodes::get();
    return ShaderNodes::get();
}

bool editor::GraphNodes::getPins(const GraphDocument& graph, const GraphNode& node, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, const NodeType** found) {
    const NodeType* type = getRegistry(graph).findType(node.type);
    if (found)
        *found = type;
    if (!type)
        return false;
    if (isLogic(graph)) {
        LogicNodes::getPins(graph, node, *static_cast<const LogicNodeType*>(type), inputs, outputs);
        return true;
    }
    // a bad signature leaves the type's own pins
    if (!ShaderNodes::getPins(node, *static_cast<const ShaderNodeType*>(type), inputs, outputs)) {
        inputs = type->inputs;
        outputs = type->outputs;
    }
    return true;
}

std::string editor::GraphNodes::getPinType(const GraphDocument& graph, const GraphNode& node, const NodePin& pin) {
    if (pin.kind == PinKind::EXECUTION)
        return executionType;
    return isLogic(graph) ? pin.type : ShaderNodes::getPinType(graph, node, pin);
}

bool editor::GraphNodes::canLink(const GraphDocument& graph, const std::string& fromType, const std::string& toType) {
    if (fromType == executionType || toType == executionType)
        return fromType == toType;
    return isLogic(graph) ? LogicNodes::canLink(fromType, toType) : ShaderNodes::canLink(fromType, toType);
}

bool editor::GraphNodes::isExecutionOutput(const GraphDocument& graph, int node, const std::string& pin) {
    const GraphNode* from = graph.findNode(node);
    const NodeType* type = from ? getRegistry(graph).findType(from->type) : nullptr;
    const NodePin* output = type ? type->findOutput(pin) : nullptr;
    return output && output->kind == PinKind::EXECUTION;
}

void editor::GraphNodes::connect(GraphDocument& graph, int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    if (isExecutionOutput(graph, fromNode, fromPin))
        graph.connectExecution(fromNode, fromPin, toNode, toPin);
    else
        graph.connect(fromNode, fromPin, toNode, toPin);
}
