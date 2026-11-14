// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Graph.h"

#include <string>
#include <vector>

namespace doriax::editor {

    // data flows along links; execution order is for logic graphs
    enum class PinKind {
        DATA,
        EXECUTION
    };

    struct NodePin {
        std::string name;            // what links and constants name
        std::string label;           // shown on the node
        std::string type;            // a data type of the graph kind
        GraphValue value;            // used when nothing is linked and no constant is set
        PinKind kind = PinKind::DATA;
    };

    struct NodeSetting {
        std::string name;
        std::string label;
        std::string type;                    // choice, number, parameter or text
        std::vector<std::string> options;    // of a choice
        GraphValue value;
    };

    struct NodeType {
        std::string id;              // stable, as math/multiply; a new shape gets a new id
        std::string name;
        std::string category;
        std::string description;
        std::vector<NodePin> inputs;
        std::vector<NodePin> outputs;
        std::vector<NodeSetting> settings;

        const NodePin* findInput(const std::string& name) const;
        const NodePin* findOutput(const std::string& name) const;
        const NodeSetting* findSetting(const std::string& name) const;
    };

    // the node types of one graph kind
    class NodeRegistry {
    public:
        virtual ~NodeRegistry() = default;

        virtual const NodeType* findType(const std::string& id) const = 0;
        virtual std::vector<const NodeType*> getTypes() const = 0;
    };

    inline const NodePin* NodeType::findInput(const std::string& pin) const {
        for (const NodePin& input : inputs)
            if (input.name == pin)
                return &input;
        return nullptr;
    }

    inline const NodePin* NodeType::findOutput(const std::string& pin) const {
        for (const NodePin& output : outputs)
            if (output.name == pin)
                return &output;
        return nullptr;
    }

    inline const NodeSetting* NodeType::findSetting(const std::string& setting) const {
        for (const NodeSetting& item : settings)
            if (item.name == setting)
                return &item;
        return nullptr;
    }

}
