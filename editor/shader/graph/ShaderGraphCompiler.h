// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/Graph.h"
#include "graph/NodeRegistry.h"

#include <functional>
#include <string>
#include <vector>

namespace doriax::editor {

    struct ShaderGraphMessage {
        int node = 0;                // 0 for the graph as a whole
        std::string text;
    };

    struct ShaderGraphResult {
        bool success = false;
        std::string type;                        // what the code fills, as mesh; a function's preview names its own
        std::string code;                        // a shader file, the same one a person would write
        std::vector<ShaderGraphMessage> errors;
        std::vector<int> lineNodes;              // the node each line of code came from, 0 for none
        std::vector<std::string> functions;      // the function graphs its Subgraph nodes read

        std::string describeErrors() const;
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
    };

}
