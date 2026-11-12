// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LogicGraphCompiler.h"

#include "LogicNodes.h"
#include "util/ScriptEvents.h"

#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <set>

using namespace doriax;

namespace {

    using namespace doriax::editor;

    const char* indentUnit = "    ";

    bool isKeyword(const std::string& word) {
        static const std::set<std::string> keywords = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
            "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
        return keywords.count(word) > 0;
    }

    bool isIdentifier(const std::string& name) {
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) || isKeyword(name))
            return false;
        return std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    }

    std::string formatNumber(float value) {
        if (!std::isfinite(value))
            return value != value ? "(0/0)" : (value > 0.0f ? "math.huge" : "-math.huge");
        char buffer[32];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

    std::string quote(const std::string& text) {
        std::string quoted = "\"";
        for (char c : text) {
            switch (c) {
                case '"': quoted += "\\\""; break;
                case '\\': quoted += "\\\\"; break;
                case '\n': quoted += "\\n"; break;
                case '\r': quoted += "\\r"; break;
                case '\t': quoted += "\\t"; break;
                default: quoted += c;
            }
        }
        return quoted + "\"";
    }

    // speed as Speed, maxSpeed as Max Speed
    std::string displayName(const std::string& name) {
        std::string title;
        for (size_t i = 0; i < name.size(); i++) {
            const char c = name[i];
            if (c == '_') {
                title += ' ';
                continue;
            }
            if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(name[i - 1])))
                title += ' ';
            title += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        }
        return title;
    }

    // (a + b) as a + b, where nothing around it needs the parentheses
    std::string unwrapped(const std::string& text) {
        if (text.size() < 2 || text.front() != '(' || text.back() != ')')
            return text;
        int depth = 0;
        for (size_t i = 0; i + 1 < text.size(); i++) {
            if (text[i] == '"') {
                // a string may hold parentheses of its own
                for (i++; i + 1 < text.size() && text[i] != '"'; i++)
                    if (text[i] == '\\')
                        i++;
                continue;
            }
            depth += text[i] == '(' ? 1 : (text[i] == ')' ? -1 : 0);
            if (depth == 0)
                return text;
        }
        return text.substr(1, text.size() - 2);
    }

    int vectorSize(const std::string& type) {
        if (type == "Vector2")
            return 2;
        if (type == "Vector3")
            return 3;
        if (type == "Vector4")
            return 4;
        return 0;
    }

    class Compiler {
    public:
        Compiler(const GraphDocument& graph, const std::string& source, const std::string& module): graph(graph), source(source), module(module) {}

        GraphResult run() {
            if (graph.kind != "logic") {
                error(0, "Not a logic graph");
                return result;
            }
            if (!isIdentifier(module))
                error(0, "The file name gives the script its name, so it starts with a letter and has only letters, digits and _");

            checkVariables();
            for (const GraphNode& node : graph.nodes)
                if (!LogicNodes::get().find(node.type))
                    error(node.id, "Unknown node type " + node.type);
            if (!result.errors.empty())
                return result;

            // the events, in the order their nodes stand
            std::vector<const GraphNode*> events;
            for (const GraphNode& node : graph.nodes)
                if (LogicNodes::get().find(node.type)->flow == LogicFlow::EVENT)
                    events.push_back(&node);
            std::stable_sort(events.begin(), events.end(), [](const GraphNode* a, const GraphNode* b) {
                return a->position.y != b->position.y ? a->position.y < b->position.y : a->position.x < b->position.x;
            });

            std::vector<Function> functions;
            Function start;
            start.name = "init";
            std::set<std::string> taken = {"init"};
            for (const GraphNode* node : events) {
                const LogicNodeType& type = *LogicNodes::get().find(node->type);
                Function function;
                function.node = node->id;
                if (type.event < 0) {
                    function.name = "init";
                } else {
                    const ScriptEvent& event = ScriptEvents::getEvents()[type.event];
                    function.name = methodName(event);
                    for (const ScriptEventParam& param : event.params)
                        function.params.push_back(param.name);
                    function.registration = ScriptEvents::getLuaRegistration(event, function.name);
                }
                if (type.event >= 0 && !taken.insert(function.name).second) {
                    error(node->id, type.name + " is in the graph twice; a Sequence runs several things from one");
                    continue;
                }
                if (type.event < 0 && start.node) {
                    error(node->id, type.name + " is in the graph twice; a Sequence runs several things from one");
                    continue;
                }
                writeBody(*node, function);
                if (type.event < 0)
                    start = function;
                else
                    functions.push_back(function);
            }

            for (const GraphParameter& variable : graph.parameters)
                if (taken.count(variable.name))
                    error(0, "The variable " + variable.name + " has the name of an event of the graph");

            if (!result.errors.empty())
                return result;

            write(start, functions);
            result.success = checkSyntax();
            return result;
        }

    private:
        struct Line {
            std::string text;
            int node = 0;
        };

        struct Function {
            int node = 0;
            std::string name;
            std::vector<std::string> params;
            std::string registration;
            std::vector<Line> body;
            std::vector<std::string> locals;
        };

        const GraphDocument& graph;
        const std::string& source;
        const std::string& module;
        GraphResult result;

        // of the function being written
        Function* function = nullptr;
        std::set<int> ran;                   // the nodes written so far
        std::vector<int> running;            // the nodes whose statements are being written, outermost first
        std::vector<int> reading;            // the value nodes being read
        std::set<std::string> kept;          // its locals

        std::vector<std::string> state;      // lines of init: what Do Once and Flip Flop remember
        std::vector<std::string> objects;    // lines of init: the entity as the classes the graph uses
        std::vector<Line> helpers;           // the functions of Lua Expression and Lua Code nodes
        bool usesChoose = false;

        void error(int node, const std::string& text) {
            for (const GraphMessage& message : result.errors)
                if (message.node == node && message.text == text)
                    return;
            result.errors.push_back({node, text});
        }

        std::string methodName(const ScriptEvent& event) const {
            const std::string name = ScriptEvents::getLuaName(event);
            if (event.source == ScriptEventSource::Engine || event.source == ScriptEventSource::Input)
                return name;
            // onPress of a Button as onButtonPress
            std::string label;
            for (const ScriptEventSourceInfo& info : ScriptEvents::getSources())
                if (info.source == event.source)
                    label = info.label;
            label.erase(std::remove_if(label.begin(), label.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }), label.end());
            std::string action = name;
            if (action.size() > 2 && action.compare(0, 2, "on") == 0 && std::isupper(static_cast<unsigned char>(action[2])))
                action.erase(0, 2);
            else if (!action.empty())
                action[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(action[0])));
            return "on" + label + action;
        }

        void checkVariables() {
            static const std::set<std::string> reserved = {"scene", "entity", "properties", "init"};
            std::set<std::string> names;
            for (const GraphParameter& variable : graph.parameters) {
                if (!isIdentifier(variable.name))
                    error(0, "The variable name " + variable.name + " is not one Lua takes: letters, digits and _, not starting with a digit");
                else if (reserved.count(variable.name) || variable.name[0] == '_')
                    error(0, "The variable name " + variable.name + " is one the script keeps for itself");
                else if (!names.insert(variable.name).second)
                    error(0, "Two variables are named " + variable.name);
                const std::vector<std::string>& types = LogicNodes::getVariableTypes();
                if (std::find(types.begin(), types.end(), variable.type) == types.end())
                    error(0, "The variable " + variable.name + " has the unknown type " + variable.type);
            }
        }

        const GraphLink* linkFrom(int node, const std::string& pin) const {
            for (const GraphLink& link : graph.links)
                if (link.fromNode == node && link.fromPin == pin)
                    return &link;
            return nullptr;
        }

        // a constant as Lua writes it, by the type of the pin that takes it
        std::string literal(const std::string& type, const GraphValue& value) const {
            if (type == "bool")
                return value.text.empty() ? (value.number() != 0.0f ? "true" : "false") : (value.text == "true" ? "true" : "false");
            if (type == "number")
                return formatNumber(value.number());
            if (type == "string")
                return quote(value.text.empty() && !value.numbers.empty() ? formatNumber(value.number()) : value.text);
            if (int size = vectorSize(type)) {
                std::string text = type + "(";
                for (int i = 0; i < size; i++)
                    text += (i ? ", " : "") + formatNumber(value.number(i));
                return text + ")";
            }
            if (type == "Quaternion")
                return "Quaternion(" + formatNumber(value.number(0)) + ", " + formatNumber(value.number(1)) + ", " + formatNumber(value.number(2)) + ")";
            if (!LogicNodes::getEnumMembers(type).empty())
                return value.text.empty() ? formatNumber(value.number()) : type + "." + value.text;
            if (type == "any") {
                if (!value.text.empty())
                    return quote(value.text);
                if (!value.numbers.empty())
                    return formatNumber(value.number());
            }
            return "nil";
        }

        std::string variableDefault(const GraphParameter& variable, bool property) const {
            const std::string type = LogicNodes::getVariablePinType(variable.type);
            if (variable.type == "int")
                return std::to_string((int)std::lround(variable.value.number()));
            if (variable.type == "entity")
                return "nil";
            // the properties table lists a vector's numbers, a script holds the vector
            if (int size = vectorSize(type); size && property) {
                std::string text = "{";
                for (int i = 0; i < size; i++)
                    text += (i ? ", " : "") + formatNumber(variable.value.number(i));
                return text + "}";
            }
            return literal(type, variable.value);
        }

        // what an input reads: its link, else its constant
        std::string input(const GraphNode& node, const NodePin& pin) {
            if (const GraphLink* link = graph.findLinkInto(node.id, pin.name))
                return output(*link, node.id);
            auto constant = node.inputs.find(pin.name);
            const GraphValue& value = constant != node.inputs.end() ? constant->second : pin.value;
            if (pin.name == "self" && value.empty()) {
                // the script's own entity, or the script
                if (LogicNodes::isEntityClass(pin.type))
                    return selfObject(pin.type);
                if (pin.type == "any")
                    return "self";
                if (pin.type == "Scene")
                    return "self.scene";
                error(node.id, "Nothing is linked to " + pin.label + ", and the script's entity is not one");
                return "nil";
            }
            if (pin.type == "Scene" && value.empty())
                return "self.scene";
            if (pin.type == "Entity" && value.empty())
                return "self.entity";
            return literal(pin.type, value);
        }

        std::string output(const GraphLink& link, int reader) {
            if (link.fromPin.empty()) {
                const GraphParameter* variable = graph.findParameter(link.fromNode);
                if (!variable) {
                    error(reader, "Reads a variable that is gone");
                    return "nil";
                }
                return "self." + variable->name;
            }
            const GraphNode* node = graph.findNode(link.fromNode);
            const LogicNodeType* type = node ? LogicNodes::get().find(node->type) : nullptr;
            if (!type) {
                error(reader, "Reads a node that is gone");
                return "nil";
            }
            if (!type->runs())
                return value(*node, *type, link.fromPin);

            const bool inside = std::find(running.begin(), running.end(), node->id) != running.end();
            switch (type->flow) {
                case LogicFlow::EVENT:
                    if (!function || function->node != node->id) {
                        error(reader, "Reads " + link.fromPin + " of " + type->name + ", which only what that event runs can read");
                        return "nil";
                    }
                    return link.fromPin;
                case LogicFlow::FOR:
                case LogicFlow::FOR_EACH:
                    if (!inside) {
                        error(reader, "Reads " + link.fromPin + " of a " + type->name + " outside its body");
                        return "nil";
                    }
                    return (link.fromPin == "item" ? "item" : "index") + std::to_string(node->id);
                case LogicFlow::FLIP_FLOP:
                    return "self._flip" + std::to_string(node->id);
                default:
                    break;
            }
            if (node->type == "variable/set") {
                const GraphParameter* variable = namedVariable(*node);
                return variable ? "self." + variable->name : std::string("nil");
            }
            if (!ran.count(node->id)) {
                error(reader, type->name + " has not run when its value is read; link the execution through it first");
                return "nil";
            }
            return "v" + std::to_string(node->id);
        }

        // the script's entity as an engine class, made once when the script starts
        std::string selfObject(const std::string& type) {
            const std::string field = "self._" + type;
            const std::string made = field + " = " + type + "(self.scene, self.entity)";
            if (std::find(objects.begin(), objects.end(), made) == objects.end())
                objects.push_back(made);
            return field;
        }

        const GraphParameter* namedVariable(const GraphNode& node) const {
            auto setting = node.settings.find("variable");
            return setting != node.settings.end() ? graph.findParameter((int)setting->second.number()) : nullptr;
        }

        // {name} replaced by the node's inputs and settings
        std::string fill(const GraphNode& node, const LogicNodeType& type, const std::string& code) {
            std::vector<NodePin> inputs, outputs;
            LogicNodes::getPins(graph, node, type, inputs, outputs);

            std::string text;
            for (size_t i = 0; i < code.size(); i++) {
                const size_t close = code[i] == '{' ? code.find('}', i) : std::string::npos;
                if (close == std::string::npos) {
                    text += code[i];
                    continue;
                }
                const std::string name = code.substr(i + 1, close - i - 1);
                i = close;

                // an argument, or all that follows an =, stands without parentheses
                const bool opens = !text.empty() && (text.back() == '(' || (text.size() > 1 && text.compare(text.size() - 2, 2, ", ") == 0));
                const bool closes = close + 1 < code.size() && (code[close + 1] == ')' || code[close + 1] == ',');
                const bool assigned = text.size() > 2 && text.compare(text.size() - 3, 3, " = ") == 0 && close + 1 == code.size();
                bool found = false;
                for (const NodePin& pin : inputs) {
                    if (pin.name == name && pin.kind == PinKind::DATA) {
                        const std::string read = input(node, pin);
                        text += (opens && closes) || assigned ? unwrapped(read) : read;
                        found = true;
                    }
                }
                if (found)
                    continue;
                const NodeSetting* setting = type.findSetting(name);
                if (!setting) {
                    text += "{" + name + "}";
                    continue;
                }
                auto stored = node.settings.find(name);
                const GraphValue& value = stored != node.settings.end() ? stored->second : setting->value;
                if (setting->type == "parameter") {
                    const GraphParameter* variable = graph.findParameter((int)value.number());
                    if (!variable)
                        error(node.id, type.name + " names no variable");
                    text += variable ? variable->name : std::string("_");
                } else if (node.type == "value/string") {
                    text += quote(value.text);
                } else if (setting->type == "number") {
                    text += formatNumber(value.number());
                } else {
                    // a choice is one of the type's own words, a text names a class or a method
                    if (value.text.empty() && value.numbers.empty())
                        error(node.id, type.name + " needs its " + setting->label);
                    else if (setting->type != "choice" && !isIdentifier(value.text))
                        error(node.id, setting->label + " of " + type.name + " is a name: letters, digits and _");
                    text += value.text.empty() ? formatNumber(value.number()) : value.text;
                }
            }
            return text;
        }

        // the function a Lua Expression or Lua Code node becomes, and its call
        std::string helperCall(const GraphNode& node, const LogicNodeType& type, bool expression) {
            const char* setting = expression ? "code" : "body";
            auto stored = node.settings.find(setting);
            const std::string code = stored != node.settings.end() ? stored->second.text : type.findSetting(setting)->value.text;
            if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
                error(node.id, type.name + " has no code");
                return "nil";
            }
            const std::string name = (expression ? "expression" : "code") + std::to_string(node.id);
            const bool written = std::any_of(helpers.begin(), helpers.end(), [&](const Line& line) { return line.node == node.id; });
            if (!written) {
                helpers.push_back({"local function " + name + "(self, a, b, c, d)", node.id});
                if (expression) {
                    helpers.push_back({std::string(indentUnit) + "return " + code, node.id});
                } else {
                    size_t begin = 0;
                    while (begin <= code.size()) {
                        size_t end = code.find('\n', begin);
                        if (end == std::string::npos)
                            end = code.size();
                        std::string line = code.substr(begin, end - begin);
                        if (!line.empty() && line.back() == '\r')
                            line.pop_back();
                        helpers.push_back({line.empty() ? line : indentUnit + line, node.id});
                        begin = end + 1;
                    }
                }
                helpers.push_back({"end", node.id});
                helpers.push_back({"", 0});
            }
            std::string call = name + "(self";
            for (const NodePin& pin : type.inputs)
                if (pin.kind == PinKind::DATA)
                    call += ", " + input(node, pin);
            return call + ")";
        }

        // a value node, read where it is used
        std::string value(const GraphNode& node, const LogicNodeType& type, const std::string& pin) {
            if (std::find(reading.begin(), reading.end(), node.id) != reading.end()) {
                error(node.id, type.name + " reads its own value");
                return "nil";
            }
            reading.push_back(node.id);
            std::string text;
            if (node.type == "utility/expression") {
                text = helperCall(node, type, true);
            } else if (node.type == "self/object") {
                auto stored = node.settings.find("class");
                const std::string name = stored != node.settings.end() ? stored->second.text : type.findSetting("class")->value.text;
                if (!LogicNodes::isEntityClass(name))
                    error(node.id, name + " is not a class an entity can be read as, such as Object, Mesh or Sprite");
                text = selfObject(name);
            } else {
                size_t index = 0;
                for (size_t i = 0; i < type.outputs.size(); i++)
                    if (type.outputs[i].name == pin)
                        index = i;
                const std::string& code = index < type.code.size() ? type.code[index] : type.code.front();
                if (code.compare(0, 7, "choose(") == 0)
                    usesChoose = true;
                text = fill(node, type, code);
                if (!type.atomic)
                    text = "(" + text + ")";
            }
            reading.pop_back();
            return text;
        }

        void line(int indent, const std::string& text, int node) {
            std::string spaces;
            for (int i = 0; i < indent; i++)
                spaces += indentUnit;
            function->body.push_back({spaces + text, node});
        }

        void keep(const std::string& name) {
            if (kept.insert(name).second)
                function->locals.push_back(name);
        }

        // what an execution output leads to
        void follow(const GraphNode& node, const std::string& pin, int indent) {
            if (const GraphLink* link = linkFrom(node.id, pin))
                write(link->toNode, indent);
        }

        void write(int id, int indent) {
            const GraphNode* node = graph.findNode(id);
            const LogicNodeType* type = node ? LogicNodes::get().find(node->type) : nullptr;
            if (!type)
                return;
            if (std::find(running.begin(), running.end(), id) != running.end()) {
                error(id, type->name + " runs into itself; a loop node repeats what it runs");
                return;
            }
            if (running.size() > 512) {
                error(id, "Too many nodes run one after another");
                return;
            }
            running.push_back(id);
            const std::string tag = std::to_string(id);

            std::vector<NodePin> inputs, outputs;
            LogicNodes::getPins(graph, *node, *type, inputs, outputs);
            auto pin = [&](const char* name) -> const NodePin& {
                for (const NodePin& item : inputs)
                    if (item.name == name)
                        return item;
                return inputs.front();
            };

            switch (type->flow) {
                case LogicFlow::BRANCH: {
                    line(indent, "if " + unwrapped(input(*node, pin("condition"))) + " then", id);
                    follow(*node, "true", indent + 1);
                    if (linkFrom(id, "false")) {
                        line(indent, "else", id);
                        follow(*node, "false", indent + 1);
                    }
                    line(indent, "end", id);
                    break;
                }
                case LogicFlow::SEQUENCE: {
                    for (const NodePin& output : outputs)
                        follow(*node, output.name, indent);
                    break;
                }
                case LogicFlow::FOR: {
                    line(indent, "for index" + tag + " = " + unwrapped(input(*node, pin("first"))) + ", " + unwrapped(input(*node, pin("last"))) + " do", id);
                    follow(*node, "body", indent + 1);
                    line(indent, "end", id);
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::FOR_EACH: {
                    line(indent, "for index" + tag + ", item" + tag + " in ipairs(" + input(*node, pin("list")) + ") do", id);
                    follow(*node, "body", indent + 1);
                    line(indent, "end", id);
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::WHILE: {
                    line(indent, "while " + unwrapped(input(*node, pin("condition"))) + " do", id);
                    follow(*node, "body", indent + 1);
                    line(indent, "end", id);
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::ONCE: {
                    line(indent, "if not self._once" + tag + " then", id);
                    line(indent + 1, "self._once" + tag + " = true", id);
                    follow(*node, "out", indent + 1);
                    line(indent, "end", id);
                    break;
                }
                case LogicFlow::FLIP_FLOP: {
                    const std::string field = "self._flip" + tag;
                    if (std::find(state.begin(), state.end(), field + " = false") == state.end())
                        state.push_back(field + " = false");
                    line(indent, field + " = not " + field, id);
                    line(indent, "if " + field + " then", id);
                    follow(*node, "a", indent + 1);
                    if (linkFrom(id, "b")) {
                        line(indent, "else", id);
                        follow(*node, "b", indent + 1);
                    }
                    line(indent, "end", id);
                    break;
                }
                case LogicFlow::RETURN: {
                    // Lua takes a return only at the end of a block
                    line(indent, "do return end", id);
                    break;
                }
                default: {
                    const bool hasValue = std::any_of(outputs.begin(), outputs.end(), [](const NodePin& output) { return output.kind == PinKind::DATA; });
                    std::string statement;
                    if (node->type == "utility/statement")
                        statement = helperCall(*node, *type, false);
                    else
                        statement = type->code.empty() ? std::string() : fill(*node, *type, type->code.front());
                    if (hasValue && node->type != "variable/set") {
                        keep("v" + tag);
                        statement = "v" + tag + " = " + statement;
                    }
                    line(indent, statement, id);
                    ran.insert(id);
                    follow(*node, "out", indent);
                    break;
                }
            }
            running.pop_back();
        }

        void writeBody(const GraphNode& event, Function& target) {
            function = &target;
            ran.clear();
            running.clear();
            reading.clear();
            kept.clear();
            follow(event, "out", 1);
            function = nullptr;
        }

        // what Lua says of the script, which the code typed into nodes can break
        bool checkSyntax() {
            lua_State* L = luaL_newstate();
            if (!L)
                return true;
            const bool valid = luaL_loadbuffer(L, result.code.data(), result.code.size(), "script") == LUA_OK;
            if (!valid) {
                // [string "script"]:12: unexpected symbol near '+'
                std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
                int node = 0;
                const size_t at = message.find("]:");
                if (at != std::string::npos) {
                    const int line = std::atoi(message.c_str() + at + 2);
                    if (line >= 1 && line <= (int)result.lineNodes.size())
                        node = result.lineNodes[line - 1];
                    const size_t text = message.find(": ", at + 2);
                    if (text != std::string::npos)
                        message = message.substr(text + 2);
                }
                error(node, "Lua does not take this: " + message);
            }
            lua_close(L);
            return valid;
        }

        void emit(const std::string& text, int node = 0) {
            result.code += text + "\n";
            result.lineNodes.push_back(node);
        }

        void emitBody(const Function& item) {
            if (!item.locals.empty()) {
                std::string names;
                for (const std::string& name : item.locals)
                    names += (names.empty() ? "" : ", ") + name;
                emit(std::string(indentUnit) + "local " + names, item.node);
            }
            for (const Line& body : item.body)
                emit(body.text, body.node);
        }

        void write(const Function& start, const std::vector<Function>& functions) {
            emit("-- Written by the Doriax editor from " + source + ". Edit the graph: saving it writes this file again.");
            emit("");

            if (usesChoose) {
                emit("local function choose(condition, a, b)");
                emit(std::string(indentUnit) + "if condition then return a end");
                emit(std::string(indentUnit) + "return b");
                emit("end");
                emit("");
            }
            for (const Line& helper : helpers)
                emit(helper.text, helper.node);

            std::vector<const GraphParameter*> shown;
            for (const GraphParameter& variable : graph.parameters)
                if (!variable.runtime)
                    shown.push_back(&variable);

            if (shown.empty()) {
                emit("local " + module + " = {}");
            } else {
                emit("local " + module + " = {");
                emit(std::string(indentUnit) + "properties = {");
                for (size_t i = 0; i < shown.size(); i++) {
                    const GraphParameter& variable = *shown[i];
                    const std::string type = variable.type == "entity" ? "Object" : variable.type;
                    emit(std::string(indentUnit) + indentUnit + "{name = " + quote(variable.name) + ", displayName = " + quote(displayName(variable.name)) +
                         ", type = " + quote(type) + ", default = " + variableDefault(variable, true) + "}" + (i + 1 < shown.size() ? "," : ""));
                }
                emit(std::string(indentUnit) + "}");
                emit("}");
            }
            emit("");

            emit("function " + module + ":init()", start.node);
            for (const GraphParameter& variable : graph.parameters)
                if (variable.runtime)
                    emit(std::string(indentUnit) + "self." + variable.name + " = " + variableDefault(variable, false));
            for (const std::string& field : objects)
                emit(std::string(indentUnit) + field);
            for (const std::string& field : state)
                emit(std::string(indentUnit) + field);
            for (const Function& item : functions)
                emit(std::string(indentUnit) + item.registration, item.node);
            emitBody(start);
            emit("end", start.node);
            emit("");

            for (const Function& item : functions) {
                std::string params;
                for (const std::string& param : item.params)
                    params += (params.empty() ? "" : ", ") + param;
                emit("function " + module + ":" + item.name + "(" + params + ")", item.node);
                emitBody(item);
                emit("end", item.node);
                emit("");
            }

            emit("return " + module);
        }
    };

}

std::string editor::LogicGraphCompiler::getModuleName(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

editor::GraphResult editor::LogicGraphCompiler::compile(const GraphDocument& graph, const std::string& source, const std::string& module) {
    return Compiler(graph, source, module).run();
}
