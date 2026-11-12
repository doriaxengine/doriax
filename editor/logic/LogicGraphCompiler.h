// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/Graph.h"
#include "graph/GraphResult.h"

#include <string>
#include <vector>

namespace doriax::editor {

    // a .logic graph to the Lua script it stands for
    class LogicGraphCompiler {
    public:
        // source names the graph in the first line of the code, module the script's table
        static GraphResult compile(const GraphDocument& graph, const std::string& source, const std::string& module);

        // the table a graph's script returns, from its file name
        static std::string getModuleName(const std::string& path);
    };

}
