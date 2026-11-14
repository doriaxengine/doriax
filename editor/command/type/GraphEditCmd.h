// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "command/Command.h"
#include <functional>
#include <string>

namespace doriax::editor{

    // an edit of an open graph, kept as its text before and after
    class GraphEditCmd: public Command{

    private:
        std::function<void(const std::string&)> apply;
        std::string before;
        std::string after;
        std::string key;
        bool firstRun;

    public:
        // the document already holds after, so the first execute leaves it
        GraphEditCmd(std::function<void(const std::string&)> apply, std::string before, std::string after, std::string key);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
        bool affectsStructure() const override { return false; }

        // a drag or typing goes on in the same step
        bool extend(const std::string& key, const std::string& after);

        // both texts through a change every state of the graph takes, as a function's new path
        void remapTexts(const std::function<std::string(const std::string&)>& remap);
    };

}
