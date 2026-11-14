// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace doriax::editor {

    struct GraphMessage {
        int node = 0;                // 0 for the graph as a whole
        std::string text;
    };

    // what a graph compiles to: the text a person would write by hand
    struct GraphResult {
        bool success = false;
        std::string code;
        std::vector<GraphMessage> errors;
        std::vector<int> lineNodes;  // the node each line of code came from, 0 for none

        std::string describeErrors() const {
            std::string text;
            for (const GraphMessage& message : errors)
                text += (text.empty() ? "" : "\n") + (message.node ? "node " + std::to_string(message.node) + ": " : std::string()) + message.text;
            return text;
        }
    };

}
