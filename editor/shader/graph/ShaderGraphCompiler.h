// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/Graph.h"

#include <string>
#include <vector>

namespace doriax::editor {

    struct ShaderGraphMessage {
        int node = 0;                // 0 for the graph as a whole
        std::string text;
    };

    struct ShaderGraphResult {
        bool success = false;
        std::string code;                        // a shader file, the same one a person would write
        std::vector<ShaderGraphMessage> errors;
        std::vector<int> lineNodes;              // the node each line of code came from, 0 for none

        std::string describeErrors() const;
    };

    // a .shader graph to the .glsl shader file it stands for
    class ShaderGraphCompiler {
    public:
        // source names the graph in the first line of the code
        static ShaderGraphResult compile(const GraphDocument& graph, const std::string& source);
    };

}
