// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/Graph.h"
#include "graph/GraphResult.h"
#include "graph/NodeRegistry.h"

#include <functional>
#include <string>
#include <vector>

namespace doriax::editor {

    // code holds a shader file, as one written by hand
    struct ShaderGraphResult : GraphResult {
        std::string type;                        // what the code fills, as mesh; a function's preview names its own
        std::vector<std::string> functions;      // the function graphs its Subgraph nodes read
    };

    // reads the graph file a Subgraph node names, by its path in the project
    using ShaderGraphReader = std::function<bool(const std::string& path, std::string& text)>;

    // a .shader graph to the .glsl shader file it stands for
    class ShaderGraphCompiler {
    public:
        // source names the graph in the first line of the code; a function graph compiles to a
        // preview of its outputs, drawn in the first shader type all its nodes fit
        static ShaderGraphResult compile(const GraphDocument& graph, const std::string& source, const ShaderGraphReader& reader = nullptr);

        // the pins of a Subgraph node calling this function, in the order its nodes stand
        static bool getFunctionPins(const GraphDocument& function, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error = nullptr);

        // a mesh graph that draws one node's first output in place of what it draws, as Final
        // Color or, from nodes that work per vertex, Vertex Offset; false for a node with none
        static bool previewNode(const GraphDocument& graph, int node, GraphDocument& preview);
    };

}
