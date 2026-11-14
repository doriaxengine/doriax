// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/NodeRegistry.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doriax::editor {

    // how a node with execution pins is written
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

    // pin types: number, bool, string, any and the engine's classes
    struct LogicNodeType : NodeType {
        // Lua per output, or a statement; {name} is an input or setting
        std::vector<std::string> code;
        LogicFlow flow = LogicFlow::NONE;
        bool atomic = false;         // a call or a name, read without parentheses
        bool textInputs = false;     // an any input left unset reads as empty text
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

        // a node's pins, with variable types and Self's class
        static void getPins(const GraphDocument& graph, const GraphNode& node, const LogicNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs);
        static bool canLink(const std::string& fromType, const std::string& toType);

        // the pin type of a variable type, as float gives number
        static std::string getVariablePinType(const std::string& variableType);
        static const std::vector<std::string>& getVariableTypes();
        // what a new variable of a type holds
        static GraphValue getVariableDefault(const std::string& variableType);
        // numbers in a pin type, 1 for scalars
        static int getComponentCount(const std::string& pinType);
        // a method, property or constant of the engine API
        static bool isEngineType(const std::string& id);
        // setPosition as Set Position
        static std::string getTitle(const std::string& name);
        static bool isLuaKeyword(const std::string& word);
        // the members of an engine enum, empty for another type
        static const std::vector<std::string>& getEnumMembers(const std::string& type);
        // classes built as Object(self.scene, self.entity)
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
