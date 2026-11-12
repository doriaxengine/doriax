// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Graph.h"
#include "NodeRegistry.h"

#include <string>
#include <vector>

namespace doriax::editor {

    // what differs between the kinds of graph, for the window and the tools that edit them all
    class GraphNodes {
    public:
        // the type of a pin that orders what runs
        static constexpr const char* executionType = "execution";

        static bool isLogic(const GraphDocument& graph);
        static const NodeRegistry& getRegistry(const GraphDocument& graph);
        // the pins a node has in this graph; false for an unknown type
        static bool getPins(const GraphDocument& graph, const GraphNode& node, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, const NodeType** found = nullptr);
        static std::string getPinType(const GraphDocument& graph, const GraphNode& node, const NodePin& pin);
        static bool canLink(const GraphDocument& graph, const std::string& fromType, const std::string& toType);
        static bool isExecutionOutput(const GraphDocument& graph, int node, const std::string& pin);
        // a link as its output takes it: execution leaves one way, a value input takes one link
        static void connect(GraphDocument& graph, int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
    };

}
