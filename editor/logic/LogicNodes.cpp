// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LogicNodes.h"

#include "util/ScriptEvents.h"
#include "engine_api_suggestions.h"

#include <algorithm>
#include <cctype>
#include <set>

using namespace doriax;

namespace {

    editor::NodePin in(const char* name, const char* label, const std::string& type, editor::GraphValue value = editor::GraphValue()) {
        editor::NodePin pin;
        pin.name = name;
        pin.label = label;
        pin.type = type;
        pin.value = value;
        return pin;
    }

    editor::NodePin out(const char* name, const char* label, const std::string& type) {
        editor::NodePin pin;
        pin.name = name;
        pin.label = label;
        pin.type = type;
        return pin;
    }

    editor::NodePin execIn(const char* name = "in", const char* label = "") {
        editor::NodePin pin;
        pin.name = name;
        pin.label = label;
        pin.kind = editor::PinKind::EXECUTION;
        return pin;
    }

    editor::NodePin execOut(const char* name = "out", const char* label = "") {
        return execIn(name, label);
    }

    editor::NodeSetting choice(const char* name, const char* label, std::vector<std::string> options) {
        editor::NodeSetting setting;
        setting.name = name;
        setting.label = label;
        setting.type = "choice";
        setting.value = options.front();
        setting.options = std::move(options);
        return setting;
    }

    editor::NodeSetting setting(const char* name, const char* label, const char* type, editor::GraphValue value = editor::GraphValue()) {
        editor::NodeSetting item;
        item.name = name;
        item.label = label;
        item.type = type;
        item.value = value;
        return item;
    }

    std::string trim(const std::string& text) {
        size_t begin = text.find_first_not_of(" \t");
        size_t end = text.find_last_not_of(" \t");
        return begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
    }

    // a type of the API table as a pin type
    std::string pinType(std::string type) {
        // a default follows the name, a note the type
        type = trim(type.substr(0, type.find_first_of("=(")));
        for (const char* word : {"const ", "std::", "doriax::", "&", "*"}) {
            size_t at;
            while ((at = type.find(word)) != std::string::npos)
                type.erase(at, std::char_traits<char>::length(word));
        }
        type = trim(type);
        // what a preprocessor line left before it
        if (size_t space = type.rfind(' '); space != std::string::npos && type.compare(0, 2, "if") == 0)
            type = type.substr(space + 1);
        else if (type.compare(0, 6, "endif ") == 0)
            type = type.substr(6);

        static const std::set<std::string> numbers = {
            "float", "double", "int", "unsigned int", "unsigned", "long", "unsigned long", "unsigned char", "short", "size_t",
            "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "wchar_t",
            "long long", "unsigned long long"};
        if (numbers.count(type))
            return "number";
        if (type == "string" || type == "char")
            return "string";
        if (type.empty() || type == "void")
            return std::string();
        if (type.find_first_of("<> ") != std::string::npos)
            return "any";
        return type;
    }

    bool isConstantName(const std::string& name) {
        bool letters = false;
        for (char c : name) {
            if (std::islower(static_cast<unsigned char>(c)))
                return false;
            letters |= std::isupper(static_cast<unsigned char>(c)) != 0;
        }
        return letters;
    }

    bool startsWithWord(const std::string& name, const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        return name.size() > length && name.compare(0, length, word) == 0 && std::isupper(static_cast<unsigned char>(name[length]));
    }

    struct ApiClasses {
        std::map<std::string, std::string> base;                    // by class
        std::map<std::string, std::vector<std::string>> members;    // by enum
    };

    const ApiClasses& apiClasses() {
        static const ApiClasses classes = [] {
            ApiClasses list;
            for (const editor::EngineAPISymbol& symbol : editor::getEngineAPISymbols()) {
                const std::string kind = symbol.kind;
                const std::string detail = symbol.detail;
                if (kind == "Class") {
                    // listed again without its base by the C++ headers
                    const size_t colon = detail.find(" : ");
                    std::string& base = list.base[symbol.name];
                    if (colon != std::string::npos)
                        base = trim(detail.substr(colon + 3));
                } else if (kind == "EnumMember") {
                    list.members[symbol.parent].push_back(symbol.name);
                }
            }
            return list;
        }();
        return classes;
    }

    bool derives(const std::string& type, const std::string& base) {
        const ApiClasses& classes = apiClasses();
        std::string current = type;
        // a chain is a few classes long
        for (int depth = 0; depth < 16 && !current.empty(); depth++) {
            if (current == base)
                return true;
            auto it = classes.base.find(current);
            if (it == classes.base.end())
                return false;
            current = it->second;
        }
        return false;
    }

}

std::string editor::LogicNodes::getTitle(const std::string& name) {
    std::string title;
    for (size_t i = 0; i < name.size(); i++) {
        const char c = name[i];
        if (c == '_') {
            title += ' ';
            continue;
        }
        const bool upper = std::isupper(static_cast<unsigned char>(c));
        const bool afterLower = i > 0 && std::islower(static_cast<unsigned char>(name[i - 1]));
        if (i > 0 && upper && afterLower)
            title += ' ';
        title += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
    return title;
}

bool editor::LogicNodes::isLuaKeyword(const std::string& word) {
    static const std::set<std::string> keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
        "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    return keywords.count(word) > 0;
}

bool editor::LogicNodes::isEngineType(const std::string& id) {
    const std::string group = id.substr(0, id.find('/'));
    return group == "call" || group == "get" || group == "set" || group == "const";
}

int editor::LogicNodes::getComponentCount(const std::string& pinType) {
    if (pinType == "Vector2")
        return 2;
    if (pinType == "Vector3" || pinType == "Quaternion")
        return 3;
    if (pinType == "Vector4")
        return 4;
    return 1;
}

editor::GraphValue editor::LogicNodes::getVariableDefault(const std::string& variableType) {
    if (variableType == "string" || variableType == "entity")
        return GraphValue();
    // a color starts white, the rest at zero
    const bool color = variableType.rfind("color", 0) == 0;
    return GraphValue(std::vector<float>(getComponentCount(getVariablePinType(variableType)), color ? 1.0f : 0.0f));
}

bool editor::LogicNodeType::runs() const {
    for (const NodePin& pin : inputs)
        if (pin.kind == PinKind::EXECUTION)
            return true;
    return flow == LogicFlow::EVENT;
}

const editor::LogicNodes& editor::LogicNodes::get() {
    static LogicNodes instance;
    return instance;
}

const editor::NodeType* editor::LogicNodes::findType(const std::string& id) const {
    return find(id);
}

std::vector<const editor::NodeType*> editor::LogicNodes::getTypes() const {
    std::vector<const NodeType*> list;
    for (const auto& type : types)
        list.push_back(type.get());
    return list;
}

const editor::LogicNodeType* editor::LogicNodes::find(const std::string& id) const {
    auto it = byId.find(id);
    return it != byId.end() ? it->second : nullptr;
}

const std::vector<std::string>& editor::LogicNodes::getVariableTypes() {
    static const std::vector<std::string> list = {"float", "int", "bool", "string", "vector2", "vector3", "vector4", "color3", "color4", "entity"};
    return list;
}

std::string editor::LogicNodes::getVariablePinType(const std::string& variableType) {
    if (variableType == "float" || variableType == "int")
        return "number";
    if (variableType == "bool" || variableType == "string")
        return variableType;
    if (variableType == "vector2")
        return "Vector2";
    if (variableType == "vector3" || variableType == "color3")
        return "Vector3";
    if (variableType == "vector4" || variableType == "color4")
        return "Vector4";
    if (variableType == "entity")
        return "Object";
    return "any";
}

const std::vector<std::string>& editor::LogicNodes::getEnumMembers(const std::string& type) {
    static const std::vector<std::string> none;
    const ApiClasses& classes = apiClasses();
    auto it = classes.members.find(type);
    return it != classes.members.end() ? it->second : none;
}

bool editor::LogicNodes::isEntityClass(const std::string& type) {
    return type != "EntityHandle" && derives(type, "EntityHandle");
}

bool editor::LogicNodes::canLink(const std::string& fromType, const std::string& toType) {
    if (fromType == toType || fromType == "any" || toType == "any")
        return true;
    // an enum is a number in Lua
    if (toType == "number" && !getEnumMembers(fromType).empty())
        return true;
    if (fromType == "number" && !getEnumMembers(toType).empty())
        return true;
    return derives(fromType, toType);
}

void editor::LogicNodes::getPins(const GraphDocument& graph, const GraphNode& node, const LogicNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs) {
    inputs = type.inputs;
    outputs = type.outputs;

    if (node.type == "variable/get" || node.type == "variable/set") {
        auto setting = node.settings.find("variable");
        const GraphParameter* variable = setting != node.settings.end() ? graph.findParameter((int)setting->second.number()) : nullptr;
        const std::string valueType = variable ? getVariablePinType(variable->type) : std::string("any");
        for (NodePin& pin : inputs)
            if (pin.name == "value")
                pin.type = valueType;
        for (NodePin& pin : outputs)
            if (pin.name == "value")
                pin.type = valueType;
    } else if (node.type == "self/object") {
        auto setting = node.settings.find("class");
        if (setting != node.settings.end() && !setting->second.text.empty())
            outputs[0].type = setting->second.text;
    }
}

editor::LogicNodeType& editor::LogicNodes::add(const std::string& id, const std::string& name, const std::string& category, const std::string& description,
                                               std::vector<NodePin> inputs, std::vector<NodePin> outputs, std::vector<std::string> code) {
    auto type = std::make_unique<LogicNodeType>();
    type->id = id;
    type->name = name;
    type->category = category;
    type->description = description;
    type->inputs = std::move(inputs);
    type->outputs = std::move(outputs);
    type->code = std::move(code);
    LogicNodeType& added = *type;
    byId[id] = type.get();
    types.push_back(std::move(type));
    return added;
}

void editor::LogicNodes::addEvents() {
    LogicNodeType& start = add("event/start", "On Start", "Events", "Once, when the script starts on its entity.", {}, {execOut()}, {});
    start.flow = LogicFlow::EVENT;

    const std::vector<ScriptEvent>& events = ScriptEvents::getEvents();
    for (size_t i = 0; i < events.size(); i++) {
        const ScriptEvent& event = events[i];
        const ScriptEventSourceInfo* source = nullptr;
        for (const ScriptEventSourceInfo& info : ScriptEvents::getSources())
            if (info.source == event.source)
                source = &info;
        if (!source)
            continue;

        std::string sourceId = source->label;
        sourceId.erase(std::remove_if(sourceId.begin(), sourceId.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }), sourceId.end());

        std::vector<NodePin> outputs = {execOut()};
        for (const ScriptEventParam& param : event.params) {
            NodePin pin;
            pin.name = param.name;
            pin.label = editor::LogicNodes::getTitle(param.name);
            pin.type = pinType(param.type);
            outputs.push_back(pin);
        }
        LogicNodeType& type = add("event/" + sourceId + "." + event.name, editor::LogicNodes::getTitle(event.name), std::string("Events: ") + source->label,
                                  std::string(event.description) + ".", {}, std::move(outputs), {});
        type.flow = LogicFlow::EVENT;
        type.event = static_cast<int>(i);
    }
}

void editor::LogicNodes::addEngineApi() {
    const ApiClasses& classes = apiClasses();
    // what their functions return is a value, with nothing to run
    static const std::set<std::string> valueClasses = {"Vector2", "Vector3", "Vector4", "Quaternion", "Matrix2", "Matrix3", "Matrix4",
                                                       "Rect", "AABB", "OBB", "Ray", "Plane", "Sphere", "Angle", "Color", "Base64"};

    std::vector<std::string> keys;

    for (const EngineAPISymbol& symbol : getEngineAPISymbols()) {
        const std::string kind = symbol.kind;
        const std::string name = symbol.name;
        const std::string parent = symbol.parent;
        const std::string detail = symbol.detail;
        if (parent.empty() || name.empty() || name.compare(0, 2, "__") == 0)
            continue;

        if (kind == "Method" || kind == "Function") {
            const bool method = kind == "Method";
            const size_t open = detail.find('(');
            const size_t close = detail.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close < open)
                continue;
            const size_t arrow = detail.find("->", close);
            const std::string result = arrow == std::string::npos ? std::string() : pinType(detail.substr(arrow + 2));

            const bool reads = startsWithWord(name, "get") || startsWithWord(name, "is") || startsWithWord(name, "has") ||
                               startsWithWord(name, "can") || startsWithWord(name, "num") || startsWithWord(name, "find");
            // a getter the table has no type for still gives something
            const std::string given = result.empty() && reads ? std::string("any") : result;
            const bool value = !given.empty() && (reads || (!method && valueClasses.count(parent)));

            std::vector<NodePin> inputs;
            if (!value)
                inputs.push_back(execIn());
            std::string call = method ? "{self}:" + name + "(" : parent + "." + name + "(";
            if (method)
                inputs.push_back(in("self", parent.c_str(), parent));

            std::string list = detail.substr(open + 1, close - open - 1);
            int count = 0;
            size_t begin = 0;
            // templates keep their commas
            int depth = 0;
            for (size_t i = 0; i <= list.size(); i++) {
                if (i < list.size() && list[i] == '<')
                    depth++;
                if (i < list.size() && list[i] == '>')
                    depth--;
                if (i < list.size() && (list[i] != ',' || depth > 0))
                    continue;
                std::string param = trim(list.substr(begin, i - begin));
                begin = i + 1;
                // what follows a format's text is not a pin
                if (param.empty() || param.find("...") != std::string::npos)
                    continue;
                param = trim(param.substr(0, param.find('=')));
                const size_t space = param.rfind(' ');
                const std::string paramName = space == std::string::npos ? "value" + std::to_string(count) : param.substr(space + 1);
                const std::string paramType = space == std::string::npos ? pinType(param) : pinType(param.substr(0, space));
                NodePin pin;
                pin.name = paramName;
                pin.label = editor::LogicNodes::getTitle(paramName);
                pin.type = paramType.empty() ? "any" : paramType;
                inputs.push_back(pin);
                call += std::string(count ? ", " : "") + "{" + paramName + "}";
                count++;
            }
            call += ")";

            std::vector<NodePin> outputs;
            if (!value)
                outputs.push_back(execOut());
            if (!given.empty())
                outputs.push_back(out("result", "", given));

            LogicNodeType& type = add("call/" + parent + "." + name, editor::LogicNodes::getTitle(name), parent, detail, std::move(inputs), std::move(outputs), {call});
            type.atomic = true;
        } else if (kind == "Property") {
            // a type a class declares, which the table lists with its members
            if (std::isupper(static_cast<unsigned char>(name[0])) && !isConstantName(name))
                continue;
            const size_t arrow = detail.find("->");
            const std::string type = arrow == std::string::npos ? std::string("any") : pinType(detail.substr(arrow + 2));
            const std::string valueType = type.empty() ? std::string("any") : type;
            static const std::unordered_set<std::string> statics = getEngineStaticProperties();
            const bool shared = statics.count(parent + "." + name) > 0;

            if (isConstantName(name)) {
                if (parent == "Input" && name.compare(0, 4, "KEY_") == 0) {
                    keys.push_back(name.substr(4));
                    continue;
                }
                LogicNodeType& constant = add("const/" + parent + "." + name, parent + "." + name, parent, detail, {}, {out("value", "", "number")}, {parent + "." + name});
                constant.atomic = true;
                continue;
            }

            std::vector<NodePin> getInputs;
            std::vector<NodePin> setInputs = {execIn()};
            if (!shared) {
                getInputs.push_back(in("self", parent.c_str(), parent));
                setInputs.push_back(in("self", parent.c_str(), parent));
            }
            setInputs.push_back(in("value", "Value", valueType));
            // function is a word of Lua, so it is read as a key
            const std::string access = (shared ? parent : std::string("{self}")) + (isLuaKeyword(name) ? "[\"" + name + "\"]" : "." + name);

            LogicNodeType& get = add("get/" + parent + "." + name, "Get " + editor::LogicNodes::getTitle(name), parent, "Reads " + parent + "." + name + ".",
                                     std::move(getInputs), {out("value", "", valueType)}, {access});
            get.atomic = true;
            add("set/" + parent + "." + name, "Set " + editor::LogicNodes::getTitle(name), parent, "Sets " + parent + "." + name + ".",
                std::move(setInputs), {execOut()}, {access + " = {value}"});
        } else if (kind == "Constant") {
            LogicNodeType& constant = add("const/" + parent + "." + name, parent + "." + name, parent, detail, {}, {out("value", "", parent)}, {parent + "." + name});
            constant.atomic = true;
        }
    }

    for (const auto& [name, members] : classes.members) {
        LogicNodeType& type = add("enum/" + name, name, "Enums", "A value of " + name + ".", {}, {out("value", "", name)}, {name + ".{value}"});
        type.settings = {choice("value", "Value", members)};
        type.atomic = true;
    }

    if (!keys.empty()) {
        LogicNodeType& key = add("input/key", "Key", "Input", "A keyboard key, as key events and Is Key Pressed name it.", {}, {out("value", "", "number")}, {"Input.KEY_{key}"});
        key.settings = {choice("key", "Key", keys)};
        key.settings[0].value = "SPACE";
        key.atomic = true;
    }
}

editor::LogicNodes::LogicNodes() {
    addEvents();

    // flow
    {
        LogicNodeType& branch = add("flow/branch", "Branch", "Flow", "Runs one side or the other.",
            {execIn(), in("condition", "Condition", "bool", 1.0f)}, {execOut("true", "True"), execOut("false", "False")}, {});
        branch.flow = LogicFlow::BRANCH;

        LogicNodeType& sequence = add("flow/sequence", "Sequence", "Flow", "Runs each output in turn.",
            {execIn()}, {execOut("then0", "1"), execOut("then1", "2"), execOut("then2", "3"), execOut("then3", "4")}, {});
        sequence.flow = LogicFlow::SEQUENCE;

        LogicNodeType& loop = add("flow/for", "For Loop", "Flow", "Runs the body once for each whole number from First to Last.",
            {execIn(), in("first", "First", "number", 1.0f), in("last", "Last", "number", 10.0f)},
            {execOut("body", "Body"), out("index", "Index", "number"), execOut("out", "Completed")}, {});
        loop.flow = LogicFlow::FOR;

        LogicNodeType& each = add("flow/forEach", "For Each", "Flow", "Runs the body once for each item of a list.",
            {execIn(), in("list", "List", "any")},
            {execOut("body", "Body"), out("index", "Index", "number"), out("item", "Item", "any"), execOut("out", "Completed")}, {});
        each.flow = LogicFlow::FOR_EACH;

        LogicNodeType& whileLoop = add("flow/while", "While Loop", "Flow", "Runs the body while the condition holds.",
            {execIn(), in("condition", "Condition", "bool")}, {execOut("body", "Body"), execOut("out", "Completed")}, {});
        whileLoop.flow = LogicFlow::WHILE;

        LogicNodeType& once = add("flow/once", "Do Once", "Flow", "Lets the first run through and stops the next ones.",
            {execIn()}, {execOut()}, {});
        once.flow = LogicFlow::ONCE;

        LogicNodeType& flip = add("flow/flipFlop", "Flip Flop", "Flow", "Runs A and B in turns, A first.",
            {execIn()}, {execOut("a", "A"), execOut("b", "B"), out("isA", "Is A", "bool")}, {});
        flip.flow = LogicFlow::FLIP_FLOP;

        LogicNodeType& stop = add("flow/return", "Return", "Flow", "Ends the event here. Result answers an event that asks, as Should Collide.",
            {execIn(), in("value", "Result", "bool", 1.0f)}, {}, {});
        stop.flow = LogicFlow::RETURN;
    }

    // variables
    {
        LogicNodeType& get = add("variable/get", "Get Variable", "Variables", "Reads a variable of the graph.", {}, {out("value", "", "any")}, {"self.{variable}"});
        get.settings = {setting("variable", "Variable", "parameter")};
        get.settings[0].options = getVariableTypes();
        get.atomic = true;

        LogicNodeType& set = add("variable/set", "Set Variable", "Variables", "Sets a variable of the graph.",
            {execIn(), in("value", "Value", "any")}, {execOut(), out("value", "", "any")}, {"self.{variable} = {value}"});
        set.settings = {setting("variable", "Variable", "parameter")};
        set.settings[0].options = getVariableTypes();
    }

    // the script's own entity
    {
        LogicNodeType& entity = add("self/entity", "Self Entity", "Self", "The entity the script is on.", {}, {out("value", "", "Entity")}, {"self.entity"});
        entity.atomic = true;
        LogicNodeType& scene = add("self/scene", "Self Scene", "Self", "The scene of the script's entity.", {}, {out("value", "", "Scene")}, {"self.scene"});
        scene.atomic = true;
        LogicNodeType& object = add("self/object", "Self", "Self", "The script's entity as an engine class, as Object, Mesh or Sprite.",
            {}, {out("value", "", "Object")}, {"self._{class}"});
        object.settings = {setting("class", "Class", "text", "Object")};
        object.atomic = true;
        LogicNodeType& other = add("self/entityObject", "Entity As", "Self", "Another entity of the scene as an engine class.",
            {in("entity", "Entity", "Entity")}, {out("value", "", "any")}, {"{class}(self.scene, {entity})"});
        other.settings = {setting("class", "Class", "text", "Object")};
        other.atomic = true;
    }

    // values
    {
        LogicNodeType& number = add("value/number", "Number", "Values", "A number.", {}, {out("value", "", "number")}, {"{value}"});
        number.settings = {setting("value", "Value", "number", 0.0f)};
        number.atomic = true;
        LogicNodeType& boolean = add("value/bool", "Boolean", "Values", "True or false.", {}, {out("value", "", "bool")}, {"{value}"});
        boolean.settings = {choice("value", "Value", {"true", "false"})};
        boolean.atomic = true;
        LogicNodeType& text = add("value/string", "Text", "Values", "A piece of text.", {}, {out("value", "", "string")}, {"{value}"});
        text.settings = {setting("value", "Value", "text")};
        text.atomic = true;
        LogicNodeType& nil = add("value/nil", "Nil", "Values", "No value.", {}, {out("value", "", "any")}, {"nil"});
        nil.atomic = true;

        add("value/vector2", "Vector2", "Values", "A vector of two numbers.",
            {in("x", "X", "number", 0.0f), in("y", "Y", "number", 0.0f)}, {out("value", "", "Vector2")}, {"Vector2({x}, {y})"}).atomic = true;
        add("value/vector3", "Vector3", "Values", "A vector of three numbers.",
            {in("x", "X", "number", 0.0f), in("y", "Y", "number", 0.0f), in("z", "Z", "number", 0.0f)}, {out("value", "", "Vector3")}, {"Vector3({x}, {y}, {z})"}).atomic = true;
        add("value/vector4", "Vector4", "Values", "A vector of four numbers.",
            {in("x", "X", "number", 0.0f), in("y", "Y", "number", 0.0f), in("z", "Z", "number", 0.0f), in("w", "W", "number", 0.0f)},
            {out("value", "", "Vector4")}, {"Vector4({x}, {y}, {z}, {w})"}).atomic = true;
        add("value/quaternion", "Quaternion", "Values", "A rotation from angles around X, Y and Z.",
            {in("x", "X", "number", 0.0f), in("y", "Y", "number", 0.0f), in("z", "Z", "number", 0.0f)}, {out("value", "", "Quaternion")}, {"Quaternion({x}, {y}, {z})"}).atomic = true;
        add("value/splitVector2", "Split Vector2", "Values", "The numbers of a vector.",
            {in("vector", "Vector", "Vector2")}, {out("x", "X", "number"), out("y", "Y", "number")}, {"{vector}.x", "{vector}.y"}).atomic = true;
        add("value/splitVector3", "Split Vector3", "Values", "The numbers of a vector.",
            {in("vector", "Vector", "Vector3")}, {out("x", "X", "number"), out("y", "Y", "number"), out("z", "Z", "number")},
            {"{vector}.x", "{vector}.y", "{vector}.z"}).atomic = true;
        add("value/splitVector4", "Split Vector4", "Values", "The numbers of a vector.",
            {in("vector", "Vector", "Vector4")}, {out("x", "X", "number"), out("y", "Y", "number"), out("z", "Z", "number"), out("w", "W", "number")},
            {"{vector}.x", "{vector}.y", "{vector}.z", "{vector}.w"}).atomic = true;
    }

    // math, on numbers and on what the engine gives operators, as vectors
    {
        auto binary = [&](const char* id, const char* name, const char* description, const char* op, float b) {
            add(std::string("math/") + id, name, "Math", description, {in("a", "A", "any", 0.0f), in("b", "B", "any", b)}, {out("value", "", "any")},
                {std::string("{a} ") + op + " {b}"});
        };
        binary("add", "Add", "A plus B.", "+", 0.0f);
        binary("subtract", "Subtract", "A minus B.", "-", 0.0f);
        binary("multiply", "Multiply", "A times B.", "*", 1.0f);
        binary("divide", "Divide", "A divided by B.", "/", 1.0f);
        add("math/modulo", "Modulo", "Math", "What is left of A divided by B.", {in("a", "A", "number", 0.0f), in("b", "B", "number", 1.0f)}, {out("value", "", "number")}, {"{a} % {b}"});
        add("math/power", "Power", "Math", "A raised to B.", {in("a", "A", "number", 0.0f), in("b", "B", "number", 2.0f)}, {out("value", "", "number")}, {"{a} ^ {b}"});
        add("math/negate", "Negate", "Math", "The value with its sign changed.", {in("a", "A", "any", 0.0f)}, {out("value", "", "any")}, {"-{a}"});

        auto function = [&](const char* id, const char* name, const char* description, const char* call, std::vector<NodePin> inputs) {
            add(std::string("math/") + id, name, "Math", description, std::move(inputs), {out("value", "", "number")}, {call}).atomic = true;
        };
        function("abs", "Absolute", "The value without its sign.", "math.abs({a})", {in("a", "A", "number", 0.0f)});
        function("floor", "Floor", "The whole number below.", "math.floor({a})", {in("a", "A", "number", 0.0f)});
        function("ceil", "Ceil", "The whole number above.", "math.ceil({a})", {in("a", "A", "number", 0.0f)});
        function("sqrt", "Square Root", "The square root.", "math.sqrt({a})", {in("a", "A", "number", 0.0f)});
        function("sin", "Sine", "The sine of an angle in radians.", "math.sin({a})", {in("a", "A", "number", 0.0f)});
        function("cos", "Cosine", "The cosine of an angle in radians.", "math.cos({a})", {in("a", "A", "number", 0.0f)});
        function("atan2", "Arctangent 2", "The angle of the point X, Y, in radians.", "math.atan({y}, {x})", {in("y", "Y", "number", 0.0f), in("x", "X", "number", 1.0f)});
        function("min", "Minimum", "The smaller of two numbers.", "math.min({a}, {b})", {in("a", "A", "number", 0.0f), in("b", "B", "number", 0.0f)});
        function("max", "Maximum", "The larger of two numbers.", "math.max({a}, {b})", {in("a", "A", "number", 0.0f), in("b", "B", "number", 0.0f)});
        function("clamp", "Clamp", "The value kept between Min and Max.", "math.max({min}, math.min({max}, {a}))",
                 {in("a", "A", "number", 0.0f), in("min", "Min", "number", 0.0f), in("max", "Max", "number", 1.0f)});
        function("random", "Random", "A random number from 0 up to 1.", "math.random()", {});
        function("randomRange", "Random Range", "A random number between Min and Max.", "randomRange({min}, {max})",
                 {in("min", "Min", "number", 0.0f), in("max", "Max", "number", 1.0f)});
        add("math/lerp", "Lerp", "Math", "A at 0, B at 1, and between them in between.",
            {in("a", "A", "any", 0.0f), in("b", "B", "any", 1.0f), in("t", "T", "number", 0.5f)}, {out("value", "", "any")}, {"lerp({a}, {b}, {t})"}).atomic = true;
        function("pi", "Pi", "The number pi.", "math.pi", {});
    }

    // comparisons and logic
    {
        auto compare = [&](const char* id, const char* name, const char* description, const char* op) {
            add(std::string("logic/") + id, name, "Logic", description, {in("a", "A", "any", 0.0f), in("b", "B", "any", 0.0f)}, {out("value", "", "bool")},
                {std::string("{a} ") + op + " {b}"});
        };
        compare("equal", "Equal", "Whether A is B.", "==");
        compare("notEqual", "Not Equal", "Whether A is not B.", "~=");
        compare("less", "Less", "Whether A is below B.", "<");
        compare("lessEqual", "Less or Equal", "Whether A is B or below it.", "<=");
        compare("greater", "Greater", "Whether A is above B.", ">");
        compare("greaterEqual", "Greater or Equal", "Whether A is B or above it.", ">=");
        add("logic/and", "And", "Logic", "Whether both hold.", {in("a", "A", "bool", 1.0f), in("b", "B", "bool", 1.0f)}, {out("value", "", "bool")}, {"{a} and {b}"});
        add("logic/or", "Or", "Logic", "Whether one of them holds.", {in("a", "A", "bool"), in("b", "B", "bool")}, {out("value", "", "bool")}, {"{a} or {b}"});
        add("logic/not", "Not", "Logic", "The opposite.", {in("a", "A", "bool")}, {out("value", "", "bool")}, {"not {a}"});
        add("logic/select", "Select", "Logic", "A when the condition holds, else B.",
            {in("condition", "Condition", "bool", 1.0f), in("a", "A", "any", 0.0f), in("b", "B", "any", 0.0f)}, {out("value", "", "any")},
            {"choose({condition}, {a}, {b})"}).atomic = true;
        add("logic/isNil", "Is Nil", "Logic", "Whether there is no value.", {in("a", "A", "any")}, {out("value", "", "bool")}, {"{a} == nil"});
    }

    // text
    {
        add("text/join", "Join", "Text", "A followed by B.", {in("a", "A", "any", ""), in("b", "B", "any", "")}, {out("value", "", "string")},
            {"tostring({a}) .. tostring({b})"}).textInputs = true;
        add("text/toString", "To Text", "Text", "A value as text.", {in("a", "A", "any", 0.0f)}, {out("value", "", "string")}, {"tostring({a})"}).atomic = true;
        add("text/toNumber", "To Number", "Text", "A text as a number, 0 when it is none.", {in("a", "A", "string", "")}, {out("value", "", "number")},
            {"(tonumber({a}) or 0)"}).atomic = true;
        add("text/length", "Length", "Text", "How many characters a text has, or items a list.", {in("a", "A", "any", "")}, {out("value", "", "number")}, {"#{a}"}).textInputs = true;
        LogicNodeType& format = add("text/format", "Format", "Text", "A text with %s, %d or %.2f replaced by the values, as string.format.",
            {in("format", "Format", "string", "%s"), in("a", "A", "any", ""), in("b", "B", "any", ""), in("c", "C", "any", "")}, {out("value", "", "string")},
            {"string.format({format}, {a}, {b}, {c})"});
        format.atomic = true;
        format.textInputs = true;
    }

    // utility
    {
        add("utility/print", "Print", "Utility", "Writes a line to the log.", {execIn(), in("text", "Text", "any", "")}, {execOut()}, {"Log.print(tostring({text}))"}).textInputs = true;

        LogicNodeType& expression = add("utility/expression", "Lua Expression", "Utility",
            "A value written in Lua. It reads the inputs as a, b, c and d, and the script as self.",
            {in("a", "A", "any"), in("b", "B", "any"), in("c", "C", "any"), in("d", "D", "any")}, {out("value", "", "any")}, {});
        expression.settings = {setting("code", "Code", "text", "a")};

        LogicNodeType& statement = add("utility/statement", "Lua Code", "Utility",
            "Lines written in Lua. They read the inputs as a, b, c and d, the script as self, and give a value with return.",
            {execIn(), in("a", "A", "any"), in("b", "B", "any"), in("c", "C", "any"), in("d", "D", "any")}, {execOut(), out("value", "", "any")}, {});
        statement.settings = {setting("body", "Code", "text")};

        add("utility/call", "Call Method", "Utility", "Calls a method of the script by name, one written in a Lua Code node or by another script.",
            {execIn(), in("self", "Target", "any"), in("a", "A", "any"), in("b", "B", "any")}, {execOut(), out("result", "", "any")},
            {"{self}:{method}({a}, {b})"}).settings = {setting("method", "Method", "text")};
    }

    addEngineApi();
}
