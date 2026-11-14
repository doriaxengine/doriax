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
        std::string type;                        // what the code fills, as mesh, or a function preview's type
        std::vector<std::string> functions;      // the function graphs its Subgraph nodes read
        // pin numbers a live compile left out, by parameter
        std::vector<std::pair<std::string, Vector4>> liveValues;
    };

    // reads a Subgraph's graph file by project path
    using ShaderGraphReader = std::function<bool(const std::string& path, std::string& text)>;

    // a .shader graph to the .glsl shader file it stands for
    class ShaderGraphCompiler {
    public:
        // source names the graph; live is the pin numbers' collection
        static ShaderGraphResult compile(const GraphDocument& graph, const std::string& source, const ShaderGraphReader& reader = nullptr,
                                         const std::string& live = "");

        // a graph's live collection, from its path
        static std::string getLiveCollection(const std::string& source);
        static bool isLiveCollection(const std::string& collection);

        // the pins of a Subgraph calling this function
        static bool getFunctionPins(const GraphDocument& function, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error = nullptr);

        // a mesh graph drawing one node's first output; false for none
        static bool previewNode(const GraphDocument& graph, int node, GraphDocument& preview, const ShaderGraphReader& reader = nullptr);
    };

}
