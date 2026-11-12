// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/NodeRegistry.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doriax::editor {

    // how a node that runs is written; the others are one statement, or a value
    enum class LogicFlow {
        NONE,
        EVENT,
        BRANCH,
        SEQUENCE,
        FOR,
        FOR_EACH,
        WHILE,
        ONCE,
        FLIP_FLOP,
        RETURN
    };

    // Pin types: number, bool, string, any, and the engine's own as Lua names them (Vector3, Object,
    // an enum). A class goes into a pin of a class it derives from, and any takes everything.
    struct LogicNodeType : NodeType {
        // the Lua of each output of a value node, in their order, or the statement of a node that
        // runs, whose value its first output keeps: {name} is an input or a setting
        std::vector<std::string> code;
        LogicFlow flow = LogicFlow::NONE;
        bool atomic = false;         // a call or a name, read without parentheses
        int event = -1;              // of the ScriptEvents catalogue, -1 for the script's start

        // whether it runs in the order its execution pins are linked
        bool runs() const;
    };

    class LogicNodes : public NodeRegistry {
    public:
        static const LogicNodes& get();

        const NodeType* findType(const std::string& id) const override;
        std::vector<const NodeType*> getTypes() const override;

        const LogicNodeType* find(const std::string& id) const;

        // the pins of one node: a variable's type, the class a Self node stands for
        static void getPins(const GraphDocument& graph, const GraphNode& node, const LogicNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs);
        static bool canLink(const std::string& fromType, const std::string& toType);

        // the pin type of a variable type, as float gives number
        static std::string getVariablePinType(const std::string& variableType);
        static const std::vector<std::string>& getVariableTypes();
        // the members of an engine enum, empty for another type
        static const std::vector<std::string>& getEnumMembers(const std::string& type);
        // whether a class is built from a scene and an entity, as Object(self.scene, self.entity)
        static bool isEntityClass(const std::string& type);

    private:
        LogicNodes();

        LogicNodeType& add(const std::string& id, const std::string& name, const std::string& category, const std::string& description,
                           std::vector<NodePin> inputs, std::vector<NodePin> outputs, std::vector<std::string> code);
        void addEvents();
        void addEngineApi();

        std::vector<std::unique_ptr<LogicNodeType>> types;
        std::map<std::string, const LogicNodeType*> byId;
    };

}
