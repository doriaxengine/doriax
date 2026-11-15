// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LogicGraphCompiler.h"

#include "LogicNodes.h"
#include "util/ScriptEvents.h"

#include "engine_api_suggestions.h"
#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>

using namespace doriax;

namespace {

    using namespace doriax::editor;

    const char* indentUnit = "    ";
    const std::string scriptHeader = "-- Written by the Doriax editor from ";

    bool isIdentifier(const std::string& name) {
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) || LogicNodes::isLuaKeyword(name))
            return false;
        return std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    }

    std::string formatNumber(float value) {
        if (!std::isfinite(value))
            return value != value ? "(0/0)" : (value > 0.0f ? "math.huge" : "-math.huge");
        return GraphValue::formatNumber(value);
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

    // a text setting, which a hand-written file may give as a number
    std::string settingText(const GraphValue& value) {
        return value.text.empty() && !value.numbers.empty() ? formatNumber(value.number()) : value.text;
    }

    int vectorSize(const std::string& type) {
        return type == "Quaternion" ? 0 : (LogicNodes::getComponentCount(type) > 1 ? LogicNodes::getComponentCount(type) : 0);
    }

    // a number where it stands beside an operator: -1 as (-1)
    std::string operand(float value) {
        const std::string text = formatNumber(value);
        return text[0] == '-' ? "(" + text + ")" : text;
    }

    // a line of C++ without its // comment
    std::string uncommented(const std::string& line) {
        char quote = 0;
        for (size_t i = 0; i + 1 < line.size(); i++) {
            if (quote && line[i] == '\\')
                i++;
            else if (quote)
                quote = line[i] == quote ? 0 : quote;
            else if (line[i] == '"' || line[i] == '\'')
                quote = line[i];
            else if (line[i] == '/' && line[i + 1] == '/')
                return line.substr(0, i);
        }
        return line;
    }

    std::string cppNumber(float value) {
        if (!std::isfinite(value))
            return value != value ? "NAN" : (value > 0.0f ? "INFINITY" : "-INFINITY");
        std::string text = GraphValue::formatNumber(value);
        if (text.find_first_of(".e") == std::string::npos)
            text += ".0";
        return text + "f";
    }

    // the type of a variable in the class's header
    std::string cppType(const std::string& variableType) {
        if (variableType == "float" || variableType == "int" || variableType == "bool")
            return variableType;
        if (variableType == "string")
            return "std::string";
        if (variableType == "entity")
            return "doriax::Object*";
        return "doriax::" + LogicNodes::getVariablePinType(variableType);
    }

    // what some C++ nodes call, as nodes:: above the script
    struct CppHelper {
        const char* name;
        std::vector<const char*> needs;
        std::vector<const char*> lines;
    };

    const std::vector<CppHelper>& cppHelpers() {
        static const std::vector<CppHelper> list = {
            {"text", {}, {
                "template<typename T>",
                "std::string text(const T& value) {",
                "    if constexpr (std::is_null_pointer_v<T>) {",
                "        return \"nil\";",
                "    } else if constexpr (std::is_same_v<T, bool>) {",
                "        return value ? \"true\" : \"false\";",
                "    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {",
                "        char buffer[40];",
                "        snprintf(buffer, sizeof(buffer), std::is_same_v<T, float> ? \"%.7g\" : \"%.14g\", (double)value);",
                "        return buffer;",
                "    } else if constexpr (std::is_convertible_v<T, std::string>) {",
                "        return value;",
                "    } else {",
                "        return value.toString();",
                "    }",
                "}"}},
            {"number", {}, {
                "template<typename T>",
                "double number(const T& value) {",
                "    if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)",
                "        return (double)value;",
                "    else if constexpr (std::is_convertible_v<T, std::string>)",
                "        return std::strtod(std::string(value).c_str(), nullptr);",
                "    else",
                "        return 0.0;",
                "}"}},
            {"toNumber", {"number"}, {
                "template<typename T>",
                "float toNumber(const T& value) {",
                "    return (float)number(value);",
                "}"}},
            {"length", {}, {
                "template<typename T>",
                "float length(const T& value) {",
                "    if constexpr (std::is_convertible_v<T, std::string>)",
                "        return (float)std::string(value).size();",
                "    else",
                "        return (float)value.size();",
                "}"}},
            {"format", {"text", "number"}, {
                "template<typename... Args>",
                "std::string format(const std::string& pattern, const Args&... args) {",
                "    const std::string texts[] = {text(args)..., std::string()};",
                "    const double numbers[] = {number(args)..., 0.0};",
                "    std::string result;",
                "    size_t next = 0;",
                "    for (size_t i = 0; i < pattern.size(); i++) {",
                "        const size_t end = pattern[i] == '%' ? pattern.find_first_of(\"sdiuxXcfgeE%\", i + 1) : std::string::npos;",
                "        if (end == std::string::npos) {",
                "            result += pattern[i];",
                "            continue;",
                "        }",
                "        std::string spec = pattern.substr(i, end - i + 1);",
                "        const char kind = pattern[end];",
                "        i = end;",
                "        if (kind == '%' || next >= sizeof...(Args)) {",
                "            result += kind == '%' ? \"%\" : \"\";",
                "            continue;",
                "        }",
                "        char buffer[128];",
                "        if (kind == 's') {",
                "            result += texts[next++];",
                "            continue;",
                "        } else if (kind == 'c') {",
                "            snprintf(buffer, sizeof(buffer), spec.c_str(), (int)numbers[next++]);",
                "        } else if (std::strchr(\"diuxX\", kind)) {",
                "            spec.insert(spec.size() - 1, \"ll\");",
                "            snprintf(buffer, sizeof(buffer), spec.c_str(), (long long)numbers[next++]);",
                "        } else {",
                "            snprintf(buffer, sizeof(buffer), spec.c_str(), numbers[next++]);",
                "        }",
                "        result += buffer;",
                "    }",
                "    return result;",
                "}"}},
            {"divide", {}, {
                "template<typename A, typename B>",
                "auto divide(const A& a, const B& b) {",
                "    if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)",
                "        return (float)a / (float)b;",
                "    else",
                "        return a / b;",
                "}"}},
            {"modulo", {}, {
                "inline float modulo(float a, float b) {",
                "    return a - std::floor(a / b) * b;",
                "}"}},
            {"lerp", {}, {
                "template<typename A, typename B>",
                "auto lerp(const A& a, const B& b, float t) {",
                "    return a + (b - a) * t;",
                "}"}},
            {"random", {}, {
                "inline float random() {",
                "    return (float)std::rand() / ((float)RAND_MAX + 1.0f);",
                "}"}},
            {"randomRange", {"random"}, {
                "inline float randomRange(float min, float max) {",
                "    return min + random() * (max - min);",
                "}"}}};
        return list;
    }

    // the functions some nodes call, written once above the script
    const std::vector<std::pair<std::string, std::vector<std::string>>>& helperFunctions() {
        static const std::vector<std::pair<std::string, std::vector<std::string>>> list = {
            {"choose", {"local function choose(condition, a, b)", "    if condition then return a end", "    return b", "end"}},
            {"lerp", {"local function lerp(a, b, t)", "    return a + (b - a) * t", "end"}},
            {"randomRange", {"local function randomRange(min, max)", "    return min + math.random() * (max - min)", "end"}}};
        return list;
    }

    class Compiler {
    public:
        Compiler(const GraphDocument& graph, const std::string& source, const std::string& name, bool cpp):
            graph(graph), source(source), module(tableName(name)), cpp(cpp) {}

        GraphResult run() {
            if (graph.kind != "logic") {
                error(0, "Not a logic graph");
                return result;
            }
            if (!isIdentifier(module) || (cpp && LogicNodes::isCppKeyword(module)))
                error(0, "The file name gives the script its name, so it starts with a letter and has only letters, digits and _");

            checkVariables();
            for (const GraphNode& node : graph.nodes)
                if (!LogicNodes::get().find(node.type))
                    error(node.id, "Unknown node type " + node.type);
            if (!result.errors.empty())
                return result;

            for (const GraphLink& link : graph.links) {
                if (link.fromPin.empty())
                    continue;
                readers[link.fromNode]++;
                if (pinKind(link.fromNode, link.fromPin) == PinKind::DATA)
                    valueRead.insert(link.fromNode);
            }

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
                    function.registration = cpp ? ScriptEvents::getCppRegistration(event, function.name) : ScriptEvents::getLuaRegistration(event, function.name);
                    function.event = type.event;
                    function.returnsBool = event.returnsBool;
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

            if (cpp) {
                writeCpp(start, functions);
                result.success = true;
                return result;
            }
            write(start, functions);
            result.success = checkSyntax();
            return result;
        }

        // the file name, or Camera as CameraScript on a clash
        static std::string tableName(const std::string& name) {
            static const std::set<std::string> globals = {
                "math", "string", "table", "os", "io", "coroutine", "utf8", "debug", "package", "tostring", "tonumber", "ipairs", "pairs",
                "select", "type", "print", "require", "error", "assert", "pcall", "next", "self", "RegisterEvent", "RegisterEngineEvent",
                "_ENV", "_G", "nodes"};
            static const std::unordered_set<std::string> types = getEngineTypeNames();
            static const std::unordered_set<std::string> builtins = getEngineBuiltinNames();
            const auto& helpers = helperFunctions();
            const bool helper = std::any_of(helpers.begin(), helpers.end(), [&](const auto& item) { return item.first == name; });
            const bool taken = globals.count(name) || types.count(name) || builtins.count(name) || helper;
            return taken ? name + "Script" : name;
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
            int event = -1;
            bool returnsBool = false;    // an engine query, true unless the script says otherwise
        };

        const GraphDocument& graph;
        const std::string& source;
        const std::string module;            // the local the script's table is kept in, or the C++ class
        const bool cpp;
        GraphResult result;

        // of the function being written
        Function* function = nullptr;
        std::set<int> ran;                   // the nodes written so far
        std::vector<int> running;            // nodes being written, outermost first
        std::vector<int> bodies;             // the loops whose body is being written
        std::map<std::string, std::string> shared; // the values the statement being written reads more than once
        std::map<int, int> readers;          // how many inputs each node feeds
        std::set<int> valueRead;             // the nodes with a value output linked
        std::vector<std::set<std::string>> scopes; // C++ locals declared, innermost last
        int statementIndent = 1;
        int sharedCount = 0;
        bool sharing = true;                 // off where a value is read again, as a loop condition
        bool overflow = false;               // the function grew past what a graph should make
        std::vector<int> reading;            // the value nodes being read
        std::set<std::string> kept;          // its locals

        std::vector<std::string> state;      // lines of init: what Flip Flop starts as
        std::vector<std::string> objects;    // lines of init: the entity as the classes the graph uses
        std::vector<Line> helpers;           // the functions of Code Expression and Code nodes
        std::set<std::string> usedHelpers;

        PinKind pinKind(int id, const std::string& pin) const {
            const GraphNode* node = graph.findNode(id);
            const LogicNodeType* type = node ? LogicNodes::get().find(node->type) : nullptr;
            const NodePin* output = type ? type->findOutput(pin) : nullptr;
            return output ? output->kind : PinKind::DATA;
        }

        const std::vector<std::string>& codes(const LogicNodeType& type) const {
            return cpp ? type.cpp : type.code;
        }

        // a node only Lua has, in a graph written as C++
        bool luaOnly(const GraphNode& node, const LogicNodeType& type) {
            if (!cpp || !type.cpp.empty() || type.flow != LogicFlow::NONE || node.type == "utility/expression" || node.type == "utility/statement")
                return false;
            error(node.id, type.name + " has no C++ form; set the graph's output to Lua to use it");
            return true;
        }

        // a member a parameter or a local of the function would hide
        std::string member(const std::string& name) const {
            if (!cpp)
                return "self." + name;
            static const char* locals[] = {"v", "t", "index", "item", "last", "code"};
            bool hidden = function && std::find(function->params.begin(), function->params.end(), name) != function->params.end();
            for (const char* local : locals) {
                const size_t length = std::char_traits<char>::length(local);
                hidden |= name.size() > length && name.compare(0, length, local) == 0 && std::isdigit(static_cast<unsigned char>(name[length]));
            }
            return hidden ? "this->" + name : name;
        }

        std::string number(float value) const {
            return cpp ? cppNumber(value) : formatNumber(value);
        }

        // the first time in the scopes open, when C++ declares it
        bool declare(const std::string& name) {
            for (const std::set<std::string>& scope : scopes)
                if (scope.count(name))
                    return false;
            scopes.back().insert(name);
            return true;
        }

        // what a link gives, as the pin it enters takes it in C++
        std::string converted(const std::string& read, const GraphLink& link, const std::string& to) const {
            std::string from;
            if (link.fromPin.empty()) {
                const GraphParameter* variable = graph.findParameter(link.fromNode);
                from = variable ? LogicNodes::getVariablePinType(variable->type) : std::string();
            } else if (const GraphNode* node = graph.findNode(link.fromNode)) {
                if (const LogicNodeType* type = LogicNodes::get().find(node->type)) {
                    std::vector<NodePin> inputs, outputs;
                    LogicNodes::getPins(graph, *node, *type, inputs, outputs);
                    for (const NodePin& pin : outputs)
                        if (pin.name == link.fromPin)
                            from = pin.type;
                }
            }
            // an enum and a number are not each other in C++
            const std::vector<std::string>& members = LogicNodes::getEnumMembers(to);
            if (from == "number" && !members.empty()) {
                const std::string* first = LogicNodes::findCppSymbol("enum/" + to + "." + members.front());
                return first ? "static_cast<decltype(" + *first + ")>(" + unwrapped(read) + ")" : read;
            }
            if (to == "number" && !LogicNodes::getEnumMembers(from).empty())
                return "static_cast<int>(" + unwrapped(read) + ")";
            return read;
        }

        void error(int node, const std::string& text) {
            for (const GraphMessage& message : result.errors)
                if (message.node == node && message.text == text)
                    return;
            result.errors.push_back({node, text});
        }

        std::string methodName(const ScriptEvent& event) const {
            // the C++ macro registers an engine event by its own name
            const std::string name = cpp ? std::string(event.name) : ScriptEvents::getLuaName(event);
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
                else if (cpp && LogicNodes::isCppKeyword(variable.name))
                    error(0, "The variable name " + variable.name + " is a word of C++");
                else if (cpp && variable.runtime && variable.type == "entity")
                    error(0, "The entity variable " + variable.name + " is private, and C++ takes an entity only from Properties");
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
                return beside(value.number());
            if (type == "string")
                return quote(value.text.empty() && !value.numbers.empty() ? formatNumber(value.number()) : value.text);
            if (int size = vectorSize(type)) {
                std::string text = type + "(";
                for (int i = 0; i < size; i++)
                    text += (i ? ", " : "") + number(value.number(i));
                return text + ")";
            }
            if (type == "Quaternion")
                return "Quaternion(" + number(value.number(0)) + ", " + number(value.number(1)) + ", " + number(value.number(2)) + ")";
            if (!LogicNodes::getEnumMembers(type).empty())
                return enumLiteral(type, value);
            if (type == "any") {
                // a std::string, so it joins and compares as text
                if (!value.text.empty())
                    return cpp ? "std::string(" + quote(value.text) + ")" : quote(value.text);
                if (!value.numbers.empty())
                    return beside(value.number());
            }
            return none();
        }

        // a number where it stands beside an operator: -1 as (-1)
        std::string beside(float value) const {
            if (!cpp)
                return operand(value);
            const std::string text = cppNumber(value);
            return text[0] == '-' ? "(" + text + ")" : text;
        }

        std::string enumLiteral(const std::string& type, const GraphValue& value) const {
            if (!cpp)
                return value.text.empty() ? formatNumber(value.number()) : type + "." + value.text;
            const std::vector<std::string>& members = LogicNodes::getEnumMembers(type);
            const std::string* named = LogicNodes::findCppSymbol("enum/" + type + "." + (value.text.empty() ? members.front() : value.text));
            if (!named)
                return "0";
            if (!value.text.empty())
                return *named;
            return "static_cast<decltype(" + *named + ")>(" + std::to_string((int)std::lround(value.number())) + ")";
        }

        std::string variableDefault(const GraphParameter& variable, bool property) const {
            const std::string type = LogicNodes::getVariablePinType(variable.type);
            if (variable.type == "int")
                return std::to_string((int)std::lround(variable.value.number()));
            if (variable.type == "entity")
                return cpp ? "nullptr" : "nil";
            if (cpp && variable.type == "float")
                return cppNumber(variable.value.number());
            // properties list a vector's numbers, scripts hold the vector
            GraphValue value = variable.value;
            // a color given without alpha is opaque
            if (variable.type == "color4" && value.numbers.size() == 3)
                value.numbers.push_back(1.0f);
            if (int size = vectorSize(type); size && property) {
                std::string text = "{";
                for (int i = 0; i < size; i++)
                    text += (i ? ", " : "") + formatNumber(value.number(i));
                return text + "}";
            }
            return literal(type, value);
        }

        // what an input reads: its link, else its constant
        std::string input(const GraphNode& node, const NodePin& pin) {
            if (const GraphLink* link = graph.findLinkInto(node.id, pin.name)) {
                // Is Nil asks an entity variable's pointer, not its entity
                const GraphParameter* variable = cpp && node.type == "logic/isNil" ? linkedVariable(*link) : nullptr;
                if (variable && variable->type == "entity")
                    return member(variable->name);
                const std::string read = output(*link, node.id);
                return cpp ? converted(read, *link, pin.type) : read;
            }
            auto constant = node.inputs.find(pin.name);
            const GraphValue& value = constant != node.inputs.end() ? constant->second : pin.value;
            const LogicNodeType* type = LogicNodes::get().find(node.type);
            if (value.empty() && pin.type == "any" && type && type->textInputs)
                return cpp ? "std::string()" : "\"\"";
            if (pin.name == "self" && value.empty()) {
                // the script's own entity, or the script
                if (LogicNodes::isEntityClass(pin.type))
                    return selfObject(pin.type);
                if (pin.type == "any")
                    return cpp ? "this" : "self";
                if (pin.type == "Scene")
                    return cpp ? "scene" : "self.scene";
                error(node.id, "Nothing is linked to " + pin.label + ", and the script's entity is not one");
                return none();
            }
            if (pin.type == "Scene" && value.empty())
                return cpp ? "scene" : "self.scene";
            if (pin.type == "Entity" && value.empty())
                return cpp ? "entity" : "self.entity";
            // an unset input of an engine call is default-made
            if (cpp && value.empty() && LogicNodes::isEngineType(node.type) && pin.type != "number" && pin.type != "bool" && pin.type != "string")
                return "{}";
            return literal(pin.type, value);
        }

        std::string none() const {
            return cpp ? "nullptr" : "nil";
        }

        std::string output(const GraphLink& link, int reader) {
            if (link.fromPin.empty()) {
                const GraphParameter* variable = graph.findParameter(link.fromNode);
                if (!variable) {
                    error(reader, "Reads a variable that is gone");
                    return none();
                }
                return variableRead(*variable);
            }
            const GraphNode* node = graph.findNode(link.fromNode);
            const LogicNodeType* type = node ? LogicNodes::get().find(node->type) : nullptr;
            if (!type) {
                error(reader, "Reads a node that is gone");
                return "nil";
            }
            if (!type->runs())
                return value(*node, *type, link.fromPin);

            const bool inside = std::find(bodies.begin(), bodies.end(), node->id) != bodies.end();
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
                    return flipField(node->id);
                default:
                    break;
            }
            if (node->type == "variable/set") {
                const GraphParameter* variable = namedVariable(*node);
                return variable ? variableRead(*variable) : none();
            }
            if (!ran.count(node->id)) {
                error(reader, type->name + " has not run when its value is read; link the execution through it first");
                return "nil";
            }
            return "v" + std::to_string(node->id);
        }

        // the state of a Flip Flop, false until it runs
        std::string flipField(int id) {
            const std::string field = member("_flip" + std::to_string(id));
            const std::string kept = cpp ? "_flip" + std::to_string(id) : field + " = false";
            if (std::find(state.begin(), state.end(), kept) == state.end())
                state.push_back(kept);
            return field;
        }

        // an entity variable is a pointer in C++
        std::string variableRead(const GraphParameter& variable) const {
            return cpp && variable.type == "entity" ? "(*" + member(variable.name) + ")" : member(variable.name);
        }

        // the entity as an engine class, made in init
        std::string selfObject(const std::string& type) {
            if (cpp) {
                if (std::find(objects.begin(), objects.end(), type) == objects.end())
                    objects.push_back(type);
                return "_as" + type;
            }
            const std::string field = "self._" + type;
            const std::string made = field + " = " + type + "(self.scene, self.entity)";
            if (std::find(objects.begin(), objects.end(), made) == objects.end())
                objects.push_back(made);
            return field;
        }

        // a pointer in C++, as Get Framebuffer of a Camera gives
        bool readsPointer(const GraphNode& node, const NodePin& pin) const {
            const GraphLink* link = graph.findLinkInto(node.id, pin.name);
            const GraphNode* from = link && !link->fromPin.empty() ? graph.findNode(link->fromNode) : nullptr;
            const LogicNodeType* type = from ? LogicNodes::get().find(from->type) : nullptr;
            return type && type->result == CppResult::POINTER;
        }

        // the variable a link reads, straight or through Get Variable
        const GraphParameter* linkedVariable(const GraphLink& link) const {
            if (link.fromPin.empty())
                return graph.findParameter(link.fromNode);
            const GraphNode* node = graph.findNode(link.fromNode);
            return node && node->type == "variable/get" ? namedVariable(*node) : nullptr;
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
                        // C++ reaches a member of a pointer with ->
                        if (cpp && close + 1 < code.size() && code[close + 1] == '.' && (LogicNodes::isPointerClass(pin.type) || readsPointer(node, pin))) {
                            text += "->";
                            i++;
                        }
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
                    const bool set = node.type == "variable/set";
                    if (!variable)
                        error(node.id, type.name + " names no variable");
                    else if (cpp && set && variable->type == "entity")
                        error(node.id, "An entity variable is set in Properties; C++ keeps it as a pointer");
                    if (!variable)
                        text += "_";
                    else if (cpp)
                        text += set ? member(variable->name) : variableRead(*variable);
                    else
                        text += variable->name;
                } else if (node.type == "value/string") {
                    // a number written unquoted in the file is the text of its digits
                    text += cpp ? "std::string(" + quote(settingText(value)) + ")" : quote(settingText(value));
                } else if (setting->type == "number") {
                    text += beside(value.number());
                } else if (setting->type == "choice") {
                    const std::string chosen = value.text.empty() ? formatNumber(value.number()) : value.text;
                    if (std::find(setting->options.begin(), setting->options.end(), chosen) == setting->options.end())
                        error(node.id, setting->label + " of " + type.name + " is not one of its choices");
                    text += chosen;
                } else {
                    // a text names a class or a method
                    if (value.text.empty() && value.numbers.empty())
                        error(node.id, type.name + " needs its " + setting->label);
                    else if (!isIdentifier(settingText(value)))
                        error(node.id, setting->label + " of " + type.name + " is a name: letters, digits and _");
                    text += settingText(value);
                }
            }
            // what the filled name is in C++, as an enum's member
            if (cpp && !text.empty() && text[0] == '@') {
                const std::string* symbol = LogicNodes::findCppSymbol(text.substr(1));
                if (!symbol)
                    error(node.id, type.name + " has no C++ form for " + text.substr(text.find('/') + 1));
                return symbol ? *symbol : none();
            }
            return text;
        }

        // the function of a Code Expression or Code node, and its call
        std::string helperCall(const GraphNode& node, const LogicNodeType& type, bool expression) {
            const char* setting = expression ? "code" : "body";
            auto stored = node.settings.find(setting);
            const std::string code = settingText(stored != node.settings.end() ? stored->second : type.findSetting(setting)->value);
            if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
                error(node.id, type.name + " has no code");
                return "nil";
            }
            if (cpp)
                return lambdaCall(node, type, code, expression);
            const std::string name = std::string("nodes.") + (expression ? "expression" : "code") + std::to_string(node.id);
            const bool written = std::any_of(helpers.begin(), helpers.end(), [&](const Line& line) { return line.node == node.id; });
            if (!written) {
                helpers.push_back({"function " + name + "(self, a, b, c, d)", node.id});
                // one entry for each line, so an error names the node it is in
                const std::string body = expression ? "return " + code : code;
                for (size_t begin = 0; begin <= body.size();) {
                    size_t end = body.find('\n', begin);
                    if (end == std::string::npos)
                        end = body.size();
                    std::string text = body.substr(begin, end - begin);
                    if (!text.empty() && text.back() == '\r')
                        text.pop_back();
                    helpers.push_back({text.empty() ? text : indentUnit + text, node.id});
                    begin = end + 1;
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

        // the lambda of a Code Expression or Code node, and its call
        std::string lambdaCall(const GraphNode& node, const LogicNodeType& type, const std::string& code, bool expression) {
            const std::string head = "[&](auto&& a, auto&& b, auto&& c, auto&& d)";
            std::string arguments;
            for (const NodePin& pin : type.inputs)
                if (pin.kind == PinKind::DATA)
                    arguments += (arguments.empty() ? "" : ", ") + input(node, pin);

            std::vector<std::string> lines;
            for (size_t begin = 0; begin <= code.size();) {
                size_t end = code.find('\n', begin);
                if (end == std::string::npos)
                    end = code.size();
                lines.push_back(code.substr(begin, end - begin));
                if (!lines.back().empty() && lines.back().back() == '\r')
                    lines.back().pop_back();
                begin = end + 1;
            }
            if (expression) {
                // on one line, where a // comment would take the rest
                std::string text;
                for (const std::string& item : lines)
                    text += (text.empty() ? "" : " ") + uncommented(item);
                return "(" + head + " -> decltype(auto) { return (" + text + "); })(" + arguments + ")";
            }
            const std::string name = "code" + std::to_string(node.id);
            if (declare(name)) {
                line(statementIndent, "auto " + name + " = " + head + " {", node.id);
                for (const std::string& item : lines)
                    line(item.empty() ? 0 : statementIndent + 1, item, node.id);
                line(statementIndent, "};", node.id);
            }
            return name + "(" + arguments + ")";
        }

        // a value node, read where it is used
        std::string value(const GraphNode& node, const LogicNodeType& type, const std::string& pin) {
            if (std::find(reading.begin(), reading.end(), node.id) != reading.end()) {
                error(node.id, type.name + " reads its own value");
                return "nil";
            }
            if (reading.size() > 120) {
                error(node.id, "Too many values feed one another in a row; keep one in a variable on the way");
                return "nil";
            }
            // shared values once per statement, names and constants inline
            const bool plain = type.atomic && type.inputs.empty() && (codes(type).empty() || codes(type).front().find('(') == std::string::npos);
            const bool once = sharing && sharedCount < 150 && readers[node.id] > 1 && !plain;
            const std::string key = std::to_string(node.id) + "." + pin;
            if (once) {
                auto held = shared.find(key);
                if (held != shared.end())
                    return held->second;
            }
            reading.push_back(node.id);
            std::string text;
            if (luaOnly(node, type)) {
                text = none();
            } else if (node.type == "utility/expression") {
                text = helperCall(node, type, true);
            } else if (node.type == "self/object") {
                auto stored = node.settings.find("class");
                const std::string name = settingText(stored != node.settings.end() ? stored->second : type.findSetting("class")->value);
                if (!LogicNodes::isEntityClass(name))
                    error(node.id, name + " is not a class an entity can be read as, such as Object, Mesh or Sprite");
                text = selfObject(name);
            } else {
                size_t index = 0;
                for (size_t i = 0; i < type.outputs.size(); i++)
                    if (type.outputs[i].name == pin)
                        index = i;
                const std::vector<std::string>& list = codes(type);
                const std::string& code = index < list.size() ? list[index] : list.front();
                for (const auto& helper : helperFunctions())
                    if (!cpp && code.compare(0, helper.first.size() + 1, helper.first + "(") == 0)
                        usedHelpers.insert(helper.first);
                text = fill(node, type, code);
                if (!type.atomic)
                    text = "(" + text + ")";
            }
            reading.pop_back();
            if (once && function) {
                size_t index = 0;
                for (size_t i = 0; i < type.outputs.size(); i++)
                    if (type.outputs[i].name == pin)
                        index = i;
                std::string name = "t" + std::to_string(node.id) + (index ? "_" + std::to_string(index) : std::string());
                if (cpp && type.result == CppResult::REFERENCE) {
                    // a reference reaches the component, and each statement declares its own
                    const std::string base = name;
                    for (int count = 2; !declare(name); count++)
                        name = base + "_" + std::to_string(count);
                    line(statementIndent, "auto&& " + name + " = " + unwrapped(text) + ";", node.id);
                } else if (cpp) {
                    line(statementIndent, (declare(name) ? "auto " : "") + name + " = " + unwrapped(text) + ";", node.id);
                } else {
                    line(statementIndent, "local " + name + " = " + unwrapped(text), node.id);
                }
                sharedCount++;
                shared[key] = name;
                return name;
            }
            return text;
        }

        void line(int indent, const std::string& text, int node) {
            // joined paths repeat, doubling at each join
            constexpr size_t limit = 5000;
            if (function->body.size() >= limit) {
                if (!overflow)
                    error(function->node, "Too much runs from this event: where paths meet, what follows is written for each of them. Let them meet less, or move the shared part into a Code node");
                overflow = true;
                return;
            }
            std::string spaces;
            for (int i = 0; i < indent; i++)
                spaces += indentUnit;
            function->body.push_back({spaces + text, node});
        }

        void keep(const std::string& name) {
            if (!kept.insert(name).second)
                return;
            function->locals.push_back(name);
            // Lua holds 200 locals in a function
            if (function->locals.size() == 180)
                error(function->node, "Too many nodes that give a value run from this event; split it across events or Code nodes");
        }

        // what an execution output leads to
        void follow(const GraphNode& node, const std::string& pin, int indent) {
            if (const GraphLink* link = linkFrom(node.id, pin))
                write(link->toNode, indent);
        }

        // a branch that may not run keeps nothing after it
        void block(const GraphNode& node, const std::string& pin, int indent) {
            const std::set<int> before = ran;
            scopes.emplace_back();
            follow(node, pin, indent);
            scopes.pop_back();
            ran = before;
        }

        // the body of a loop, where its index and item are read
        void loop(const GraphNode& node, int indent) {
            bodies.push_back(node.id);
            block(node, "body", indent);
            bodies.pop_back();
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
            if (overflow)
                return;
            running.push_back(id);
            const std::string tag = std::to_string(id);
            const std::string end = cpp ? ";" : "";
            const std::string close = cpp ? "}" : "end";
            shared.clear();
            statementIndent = indent;

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
                    const std::string condition = unwrapped(input(*node, pin("condition")));
                    line(indent, cpp ? "if (" + condition + ") {" : "if " + condition + " then", id);
                    block(*node, "true", indent + 1);
                    if (linkFrom(id, "false")) {
                        line(indent, cpp ? "} else {" : "else", id);
                        block(*node, "false", indent + 1);
                    }
                    line(indent, close, id);
                    break;
                }
                case LogicFlow::SEQUENCE: {
                    for (const NodePin& output : outputs)
                        follow(*node, output.name, indent);
                    break;
                }
                case LogicFlow::FOR: {
                    const std::string first = unwrapped(input(*node, pin("first")));
                    const std::string last = unwrapped(input(*node, pin("last")));
                    // Last is read once, as Lua reads it
                    if (cpp)
                        line(indent, "for (int index" + tag + " = (int)(" + first + "), last" + tag + " = (int)(" + last + "); index" + tag + " <= last" + tag + "; index" + tag + "++) {", id);
                    else
                        line(indent, "for index" + tag + " = " + first + ", " + last + " do", id);
                    loop(*node, indent + 1);
                    line(indent, close, id);
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::FOR_EACH: {
                    if (!graph.findLinkInto(id, "list"))
                        error(id, "For Each has no list linked");
                    const std::string list = unwrapped(input(*node, pin("list")));
                    if (cpp) {
                        // Index counts from 1 in both
                        line(indent, "{", id);
                        line(indent + 1, "int index" + tag + " = 0;", id);
                        line(indent + 1, "for (auto& item" + tag + " : " + list + ") {", id);
                        line(indent + 2, "index" + tag + "++;", id);
                        loop(*node, indent + 2);
                        line(indent + 1, "}", id);
                        line(indent, "}", id);
                    } else {
                        line(indent, "for index" + tag + ", item" + tag + " in ipairs(" + list + ") do", id);
                        loop(*node, indent + 1);
                        line(indent, "end", id);
                    }
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::WHILE: {
                    // read again each time round
                    sharing = false;
                    const std::string condition = unwrapped(input(*node, pin("condition")));
                    sharing = true;
                    line(indent, cpp ? "while (" + condition + ") {" : "while " + condition + " do", id);
                    loop(*node, indent + 1);
                    line(indent, close, id);
                    follow(*node, "out", indent);
                    break;
                }
                case LogicFlow::ONCE: {
                    const std::string field = member("_once" + tag);
                    if (cpp && std::find(state.begin(), state.end(), "_once" + tag) == state.end())
                        state.push_back("_once" + tag);
                    line(indent, cpp ? "if (!" + field + ") {" : "if not " + field + " then", id);
                    line(indent + 1, field + " = true" + end, id);
                    block(*node, "out", indent + 1);
                    line(indent, close, id);
                    break;
                }
                case LogicFlow::FLIP_FLOP: {
                    const std::string field = flipField(id);
                    line(indent, field + (cpp ? " = !" : " = not ") + field + end, id);
                    line(indent, cpp ? "if (" + field + ") {" : "if " + field + " then", id);
                    block(*node, "a", indent + 1);
                    if (linkFrom(id, "b")) {
                        line(indent, cpp ? "} else {" : "else", id);
                        block(*node, "b", indent + 1);
                    }
                    line(indent, close, id);
                    break;
                }
                case LogicFlow::RETURN: {
                    if (cpp) {
                        line(indent, function->returnsBool ? "return " + unwrapped(input(*node, pin("value"))) + ";" : "return;", id);
                        break;
                    }
                    // Lua takes a return only at the end of a block
                    line(indent, function->returnsBool ? "do return " + unwrapped(input(*node, pin("value"))) + " end" : "do return end", id);
                    break;
                }
                default: {
                    const bool hasValue = std::any_of(outputs.begin(), outputs.end(), [](const NodePin& output) { return output.kind == PinKind::DATA; });
                    std::string statement;
                    if (luaOnly(*node, *type))
                        statement.clear();
                    else if (node->type == "utility/statement")
                        statement = helperCall(*node, *type, false);
                    else
                        statement = codes(*type).empty() ? std::string() : fill(*node, *type, codes(*type).front());
                    if (hasValue && node->type != "variable/set") {
                        if (!cpp) {
                            keep("v" + tag);
                            statement = "v" + tag + " = " + statement;
                        } else if (valueRead.count(id)) {
                            // kept only when read: a call may give nothing in C++
                            statement = (declare("v" + tag) ? "auto v" : "v") + tag + " = " + statement;
                        }
                    }
                    line(indent, statement + end, id);
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
            bodies.clear();
            shared.clear();
            sharedCount = 0;
            statementIndent = 1;
            overflow = false;
            reading.clear();
            kept.clear();
            scopes.assign(1, {});
            follow(event, "out", 1);
            function = nullptr;
        }

        // typed Lua code can break the script
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
            if (item.returnsBool)
                emit(std::string(indentUnit) + "return true", item.node);
        }

        void write(const Function& start, const std::vector<Function>& functions) {
            emit(scriptHeader + source + ". Edit the graph: saving it writes this file again.");
            emit("");

            for (const auto& helper : helperFunctions()) {
                if (!usedHelpers.count(helper.first))
                    continue;
                for (const std::string& text : helper.second)
                    emit(text);
                emit("");
            }
            if (!helpers.empty()) {
                emit("local nodes = {}");
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
                    emit(std::string(indentUnit) + indentUnit + "{name = " + quote(variable.name) + ", displayName = " + quote(LogicNodes::getTitle(variable.name)) +
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

        std::string cppDefault(const GraphParameter& variable) const {
            const std::string value = variableDefault(variable, false);
            return vectorSize(LogicNodes::getVariablePinType(variable.type)) ? "doriax::" + value : value;
        }

        std::set<std::string> headersOf(const std::set<std::string>& types) const {
            std::set<std::string> files;
            for (const std::string& type : types)
                if (const std::string* file = LogicNodes::findHeader(type))
                    files.insert(*file);
            return files;
        }

        // the header, then the source of the C++ class
        void writeCpp(const Function& start, const std::vector<Function>& functions) {
            const std::string note = "// Written by the Doriax editor from " + source + ". Edit the graph: saving it writes this file again.";
            const std::string unit = indentUnit;
            std::string& header = result.header;
            auto head = [&](const std::string& text) { header += text + "\n"; };

            std::set<std::string> classes(objects.begin(), objects.end());
            std::vector<const GraphParameter*> shown, hidden;
            for (const GraphParameter& variable : graph.parameters) {
                (variable.runtime ? hidden : shown).push_back(&variable);
                if (variable.type == "entity")
                    classes.insert("Object");
            }

            head(note);
            head("#pragma once");
            head("");
            head("#include \"ScriptBase.h\"");
            head("#include \"ScriptProperty.h\"");
            for (const std::string& file : headersOf(classes))
                head("#include \"" + file + "\"");
            head("");
            head("class " + module + " : public doriax::ScriptBase {");
            head("public:");
            for (const GraphParameter* variable : shown) {
                const bool color = variable->type.rfind("color", 0) == 0;
                head(unit + "DPROPERTY(" + quote(LogicNodes::getTitle(variable->name)) + (color ? (variable->type == "color3" ? ", Color3" : ", Color4") : "") + ")");
                head(unit + cppType(variable->type) + " " + variable->name + " = " + cppDefault(*variable) + ";");
                head("");
            }
            head(unit + module + "(doriax::Scene* scene, doriax::Entity entity);");
            if (!functions.empty())
                head("");
            for (const Function& item : functions)
                head(unit + ScriptEvents::getCppSignature(ScriptEvents::getEvents()[item.event], item.name, true) + ";");
            if (!hidden.empty() || !objects.empty() || !state.empty() || start.node) {
                head("");
                head("private:");
                for (const GraphParameter* variable : hidden)
                    head(unit + cppType(variable->type) + " " + variable->name + " = " + cppDefault(*variable) + ";");
                for (const std::string& name : objects)
                    head(unit + "doriax::" + name + " _as" + name + ";");
                for (const std::string& field : state)
                    head(unit + "bool " + field + " = false;");
                if (start.node) {
                    if (!hidden.empty() || !objects.empty() || !state.empty())
                        head("");
                    head(unit + "void _start();");
                }
            }
            head("};");

            // what the bodies call says what the source includes
            std::string all;
            for (const Line& body : start.body)
                all += body.text + "\n";
            for (const Function& item : functions)
                for (const Line& body : item.body)
                    all += body.text + "\n";
            std::set<std::string> used;
            for (const CppHelper& helper : cppHelpers())
                if (all.find(std::string("nodes::") + helper.name + "(") != std::string::npos)
                    used.insert(helper.name);
            for (const CppHelper& helper : cppHelpers())
                if (used.count(helper.name))
                    used.insert(helper.needs.begin(), helper.needs.end());

            // the engine classes the nodes name
            std::set<std::string> named;
            for (const GraphNode& node : graph.nodes) {
                const LogicNodeType* type = LogicNodes::get().find(node.type);
                if (!type)
                    continue;
                const size_t slash = node.type.find('/');
                named.insert(node.type.substr(slash + 1, node.type.find('.', slash) - slash - 1));
                std::vector<NodePin> inputs, outputs;
                LogicNodes::getPins(graph, node, *type, inputs, outputs);
                for (const NodePin& pin : inputs)
                    named.insert(pin.type);
                for (const NodePin& pin : outputs)
                    named.insert(pin.type);
            }
            for (const std::string& name : classes)
                named.erase(name);

            emit(note);
            emit("#include \"" + module + ".h\"");
            emit("");
            emit("#include \"Doriax.h\"");
            for (const std::string& file : headersOf(named))
                emit("#include \"" + file + "\"");
            if (all.find("std::") != std::string::npos || all.find("NAN") != std::string::npos || all.find("INFINITY") != std::string::npos || !used.empty()) {
                emit("");
                if (all.find("std::min<") != std::string::npos || all.find("std::max<") != std::string::npos)
                    emit("#include <algorithm>");
                emit("#include <cmath>");
                if (!used.empty()) {
                    emit("#include <cstdio>");
                    emit("#include <cstdlib>");
                    emit("#include <cstring>");
                    emit("#include <string>");
                    emit("#include <type_traits>");
                }
            }
            emit("");
            emit("using namespace doriax;");
            emit("");

            if (!used.empty()) {
                emit("namespace {");
                emit("namespace nodes {");
                emit("");
                for (const CppHelper& helper : cppHelpers()) {
                    if (!used.count(helper.name))
                        continue;
                    for (const char* text : helper.lines)
                        emit(text);
                    emit("");
                }
                emit("}");
                emit("}");
                emit("");
            }

            std::string made = "ScriptBase(scene, entity)";
            for (const std::string& name : objects)
                made += ", _as" + name + "(scene, entity)";
            // the updates that run On Start stay registered after it
            auto starting = [](const Function& item) {
                const ScriptEvent& event = ScriptEvents::getEvents()[item.event];
                const std::string name = event.name;
                return event.source == ScriptEventSource::Engine && (name == "onUpdate" || name == "onFixedUpdate");
            };

            emit(module + "::" + module + "(Scene* scene, Entity entity): " + made + " {", start.node);
            // On Start waits for the first update or fixed update, with properties set
            if (start.node) {
                emit(unit + "REGISTER_EVENT(Engine::onFixedUpdate, _start);", start.node);
                emit(unit + "REGISTER_EVENT(Engine::onUpdate, _start);", start.node);
            }
            for (const Function& item : functions)
                if (!start.node || starting(item))
                    emit(unit + item.registration, item.node);
            emit("}", start.node);
            emit("");

            if (start.node) {
                emit("void " + module + "::_start() {", start.node);
                emit(unit + "UNREGISTER_EVENT(Engine::onFixedUpdate, _start);", start.node);
                emit(unit + "UNREGISTER_EVENT(Engine::onUpdate, _start);", start.node);
                // the other events wait for it; a Return can end its body
                for (const Function& item : functions)
                    if (!starting(item))
                        emit(unit + item.registration, item.node);
                for (const Line& body : start.body)
                    emit(body.text, body.node);
                emit("}", start.node);
                emit("");
            }

            for (size_t i = 0; i < functions.size(); i++) {
                const Function& item = functions[i];
                emit(ScriptEvents::getCppSignature(ScriptEvents::getEvents()[item.event], module + "::" + item.name, false) + " {", item.node);
                for (const Line& body : item.body)
                    emit(body.text, body.node);
                if (item.returnsBool)
                    emit(unit + "return true;", item.node);
                emit("}", item.node);
                if (i + 1 < functions.size())
                    emit("");
            }
        }
    };

}

bool editor::LogicGraphCompiler::isGenerated(const std::string& firstLine) {
    return firstLine.rfind(scriptHeader, 0) == 0;
}

std::string editor::LogicGraphCompiler::getModuleName(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::string editor::LogicGraphCompiler::getLanguageName(LogicLanguage language) {
    return language == LogicLanguage::CPP ? "cpp" : "lua";
}

bool editor::LogicGraphCompiler::findLanguage(const std::string& name, LogicLanguage& language) {
    if (name != "lua" && name != "cpp")
        return false;
    language = name == "cpp" ? LogicLanguage::CPP : LogicLanguage::LUA;
    return true;
}

std::string editor::LogicGraphCompiler::getClassName(const std::string& module) {
    return Compiler::tableName(module);
}

editor::GraphResult editor::LogicGraphCompiler::compile(const GraphDocument& graph, const std::string& source, const std::string& module, LogicLanguage language) {
    return Compiler(graph, source, module, language == LogicLanguage::CPP).run();
}
