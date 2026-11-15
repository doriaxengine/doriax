// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/Graph.h"
#include "graph/GraphResult.h"

#include <string>
#include <vector>

namespace doriax::editor {

    enum class LogicLanguage {
        LUA,
        CPP
    };

    // a .logic graph to the Lua or C++ script it stands for
    class LogicGraphCompiler {
    public:
        // source names the graph, module the script
        static GraphResult compile(const GraphDocument& graph, const std::string& source, const std::string& module,
                                   LogicLanguage language = LogicLanguage::LUA);

        // whether a script's first line says a graph wrote it
        static bool isGenerated(const std::string& firstLine);
        // the name a graph's script has, from its file name
        static std::string getModuleName(const std::string& path);
        // the C++ class: the name, or Camera as CameraScript
        static std::string getClassName(const std::string& module);

        // lua or cpp, as files and settings name a language
        static std::string getLanguageName(LogicLanguage language);
        static bool findLanguage(const std::string& name, LogicLanguage& language);
    };

}
