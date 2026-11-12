// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderGraphCompiler.h"

#include "ShaderNodes.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace doriax;

namespace {

    using namespace doriax::editor;

    // a GLSL value; color is a vec4 that turns to and from vec3 with w = 1
    struct Type {
        std::string name;    // float, vec2, vec3, vec4, int, bool, texture2D, textureCube
        bool color = false;

        int size() const {
            if (name == "float" || name == "int" || name == "bool") return 1;
            if (name == "vec2") return 2;
            if (name == "vec3") return 3;
            if (name == "vec4") return 4;
            return 0;
        }
        bool vector() const { return name == "float" || name == "vec2" || name == "vec3" || name == "vec4"; }
        bool operator==(const Type& other) const { return name == other.name; }
    };

    Type vectorType(int size) {
        static const char* names[] = {"float", "float", "vec2", "vec3", "vec4"};
        return Type{names[std::clamp(size, 1, 4)]};
    }

    Type pinType(const std::string& name) {
        if (name == "color")
            return Type{"vec4", true};
        return Type{name};
    }

    // a float GLSL reads back the same, with a point; negative ones bracketed where they stand alone
    std::string glslFloat(float value, bool alone) {
        if (!std::isfinite(value))
            value = 0.0f;
        char buffer[32];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        std::string text(buffer, result.ptr);
        if (text.find_first_of(".e") == std::string::npos)
            text += ".0";
        return (alone && value < 0.0f) ? "(" + text + ")" : text;
    }

    std::string literal(const GraphValue& value, const Type& type) {
        if (type.name == "bool")
            return value.number() != 0.0f ? "true" : "false";
        if (type.name == "int")
            return std::to_string((int)std::lround(value.number()));
        const int size = type.size();
        if (size == 1)
            return glslFloat(value.number(), true);
        // one number fills every component
        if (value.numbers.size() <= 1)
            return type.name + "(" + glslFloat(value.number(), false) + ")";
        std::string text = type.name + "(";
        for (int i = 0; i < size; i++) {
            const float number = i < (int)value.numbers.size() ? value.numbers[i] : (type.color && i == 3 ? 1.0f : 0.0f);
            text += (i ? ", " : "") + glslFloat(number, false);
        }
        return text + ")";
    }

    bool isIdentifier(const std::string& name) {
        if (name.empty() || !(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
            return false;
        return std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '_'; });
    }

    // a setting's text, the type's default when the node leaves it
    std::string settingText(const GraphNode& node, const char* name) {
        auto it = node.settings.find(name);
        if (it != node.settings.end())
            return it->second.text;
        const ShaderNodeType* type = ShaderNodes::get().find(node.type);
        const NodeSetting* declared = type ? type->findSetting(name) : nullptr;
        return declared ? declared->value.text : std::string();
    }

    // a function's Function Inputs and Outputs as the pins of a Subgraph node calling it, top to bottom
    bool checkFunction(const GraphDocument& function, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::vector<GraphMessage>& errors) {
        if (function.kind != "shader" || function.type != "function") {
            errors.push_back({0, "not a function graph (type: " + function.type + ")"});
            return false;
        }
        std::vector<const GraphNode*> ordered;
        for (const GraphNode& node : function.nodes) {
            if (node.type == "function/input" || node.type == "function/output")
                ordered.push_back(&node);
            else if (node.type.rfind("output/", 0) == 0)
                errors.push_back({node.id, "a function graph gives its values through Function Output nodes"});
        }
        std::sort(ordered.begin(), ordered.end(), [](const GraphNode* a, const GraphNode* b) {
            if (a->position.y != b->position.y)
                return a->position.y < b->position.y;
            return a->position.x != b->position.x ? a->position.x < b->position.x : a->id < b->id;
        });
        static const std::set<std::string> valueTypes = {"float", "vec2", "vec3", "vec4", "color", "int", "bool"};
        std::set<std::string> inputNames, outputNames;
        for (const GraphNode* node : ordered) {
            const bool input = node->type == "function/input";
            const std::string label = input ? "Function Input" : "Function Output";
            const std::string name = settingText(*node, "name");
            const std::string type = settingText(*node, "valueType");
            if (!isIdentifier(name)) {
                errors.push_back({node->id, label + " name " + name + " is not a name of letters, digits and _"});
            } else if (!valueTypes.count(type)) {
                errors.push_back({node->id, label + " has an unknown type " + type});
            } else if (!(input ? inputNames : outputNames).insert(name).second) {
                errors.push_back({node->id, "two " + label + "s are called " + name});
            } else if (input) {
                NodePin pin{name, name, type, GraphValue(0.0f), PinKind::DATA};
                if (auto value = node->inputs.find("default"); value != node->inputs.end())
                    pin.value = value->second;
                inputs.push_back(pin);
            } else {
                outputs.push_back(NodePin{name, name, type, GraphValue(), PinKind::DATA});
            }
        }
        if (outputs.empty() && errors.empty())
            errors.push_back({0, "a function graph needs a Function Output"});
        return errors.empty();
    }

    // the first shader type all of a function's nodes fit, which its preview draws in
    std::string previewType(const GraphDocument& function) {
        for (const char* candidate : {"mesh", "ui", "points", "lines", "sky", "water", "postprocess"}) {
            const bool fits = std::all_of(function.nodes.begin(), function.nodes.end(), [&](const GraphNode& node) {
                const ShaderNodeType* type = ShaderNodes::get().find(node.type);
                if (!type || type->shaderTypes.empty() || node.type.rfind("function/", 0) == 0)
                    return true;
                return std::find(type->shaderTypes.begin(), type->shaderTypes.end(), candidate) != type->shaderTypes.end();
            });
            if (fits)
                return candidate;
        }
        return "mesh";
    }

    // whether a node that works per vertex feeds this one
    bool fedPerVertex(const GraphDocument& function, int node, std::set<int>& seen) {
        if (!seen.insert(node).second)
            return false;
        for (const GraphLink& link : function.links) {
            if (link.toNode != node || link.fromPin.empty())
                continue;
            const GraphNode* from = function.findNode(link.fromNode);
            const ShaderNodeType* type = from ? ShaderNodes::get().find(from->type) : nullptr;
            if ((type && type->stage == ShaderNodeStage::VERTEX) || fedPerVertex(function, link.fromNode, seen))
                return true;
        }
        return false;
    }

    // "rgba.xyz" is a swizzle of output rgba, "xyz" one of the first output
    void splitSwizzle(const std::string& pin, std::string& output, std::string& swizzle) {
        const size_t dot = pin.find('.');
        output = dot == std::string::npos ? pin : pin.substr(0, dot);
        swizzle = dot == std::string::npos ? std::string() : pin.substr(dot + 1);
    }

    bool isSwizzle(const std::string& pin) {
        if (pin.empty() || pin.size() > 4)
            return false;
        const bool xyzw = pin.find_first_not_of("xyzw") == std::string::npos;
        const bool rgba = pin.find_first_not_of("rgba") == std::string::npos;
        return xyzw || rgba;
    }

    int swizzleReach(const std::string& pin) {
        int reach = 0;
        for (char c : pin)
            reach = std::max(reach, (int)std::string(c == 'x' || c == 'y' || c == 'z' || c == 'w' ? "xyzw" : "rgba").find(c) + 1);
        return reach;
    }

    // texture(t, uv) as textureLod(t, uv, 0.0): vertices have no screen derivatives
    std::string fixedLevel(const std::string& code) {
        std::string out;
        size_t at = 0;
        for (size_t found; (found = code.find("texture(", at)) != std::string::npos;) {
            if (found > 0 && (std::isalnum((unsigned char)code[found - 1]) || code[found - 1] == '_')) {
                out += code.substr(at, found + 8 - at);
                at = found + 8;
                continue;
            }
            int depth = 0;
            size_t close = found + 7;
            for (; close < code.size(); close++) {
                if (code[close] == '(') depth++;
                else if (code[close] == ')' && --depth == 0) break;
            }
            out += code.substr(at, found - at) + "textureLod(" + code.substr(found + 8, close - found - 8) + ", 0.0)";
            at = close + 1;
        }
        return out + code.substr(std::min(at, code.size()));
    }

    // the words a template leaves in the code: comments go before macros expand, and
    // {input}, @field and %value are replaced
    void collectIdentifiers(const std::string& code, std::set<std::string>& names) {
        for (size_t i = 0; i < code.size();) {
            if (code.compare(i, 2, "//") == 0) {
                i = std::min(code.find('\n', i), code.size());
            } else if (code.compare(i, 2, "/*") == 0) {
                const size_t end = code.find("*/", i + 2);
                i = end == std::string::npos ? code.size() : end + 2;
            } else if (code[i] == '{' && code.find('}', i) != std::string::npos && code.find('\n', i) > code.find('}', i)) {
                i = code.find('}', i) + 1;
            } else if (code[i] == '@' || code[i] == '%') {
                i++;
                while (i < code.size() && (std::isalnum((unsigned char)code[i]) || code[i] == '_'))
                    i++;
            } else if (std::isalpha((unsigned char)code[i]) || code[i] == '_') {
                size_t end = i;
                while (end < code.size() && (std::isalnum((unsigned char)code[end]) || code[end] == '_'))
                    end++;
                names.insert(code.substr(i, end - i));
                i = end;
            } else {
                i++;
            }
        }
    }

    // a parameter is a member and a texture a define, so neither can take a GLSL word
    bool isKeyword(const std::string& name) {
        static const std::set<std::string> keywords = {
            "attribute", "const", "uniform", "varying", "buffer", "shared", "centroid", "flat", "smooth", "layout", "break",
            "continue", "do", "for", "while", "switch", "case", "default", "if", "else", "in", "out", "inout", "true", "false",
            "invariant", "precise", "discard", "return", "struct", "void", "bool", "int", "uint", "float", "double", "highp",
            "mediump", "lowp", "precision", "sampler", "texture", "main", "params", "context", "surface", "color", "varyings",
            "Engine", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
            "mat2", "mat3", "mat4", "texture2D", "textureCube", "sampler2D", "samplerCube", "light", "Surface", "FragmentContext",
            "VertexContext", "UnderwaterContext", "LightContext", "Varyings"};
        return keywords.count(name) > 0;
    }

    struct Field {
        std::string code;
        Type type;
    };

    // what the context of each hook holds, by the names nodes use
    std::map<std::string, Field> contextFields(const std::string& shaderType, const std::string& hook) {
        std::map<std::string, Field> fields;
        auto add = [&](const std::string& name, const std::string& type, const std::string& code = std::string()) {
            fields[name] = Field{code.empty() ? "context." + name : code, Type{type}};
        };
        if (hook == "getVertexOffset" || hook == "getVaryings") {
            add("localPosition", "vec3"); add("worldPosition", "vec3"); add("normal", "vec3");
            add("uv1", "vec2"); add("uv2", "vec2"); add("color", "vec4"); add("instance", "vec4");
            if (shaderType == "mesh") {
                add("objectPosition", "vec3"); add("objectScale", "vec3");
            }
        } else if (shaderType == "mesh") {
            add("worldPosition", "vec3"); add("normal", "vec3"); add("tangent", "vec3"); add("bitangent", "vec3");
            add("uv1", "vec2"); add("uv2", "vec2"); add("color", "vec4"); add("instance", "vec4");
            add("view", "vec3"); add("eye", "vec3"); add("forward", "vec3"); add("perspective", "bool");
            add("pixel", "vec2"); add("screenUV", "vec2"); add("frontFacing", "bool");
            add("nearPlane", "float"); add("farPlane", "float"); add("worldToView", "mat3");
        } else if (shaderType == "ui" || shaderType == "points" || shaderType == "lines") {
            add("worldPosition", "vec3"); add("uv1", "vec2"); add("color", "vec4");
            add("textureColor", "vec4"); add("tint", "vec4"); add("pixel", "vec2");
        } else if (shaderType == "sky") {
            add("direction", "vec3"); add("textureColor", "vec4"); add("tint", "vec4"); add("pixel", "vec2");
        } else if (shaderType == "water" && hook == "getUnderwaterColor") {
            add("screenUV", "vec2", "context.uv"); add("sceneColor", "vec4"); add("pathLength", "float"); add("pixel", "vec2");
        } else if (shaderType == "water") {
            add("worldPosition", "vec3"); add("normal", "vec3"); add("view", "vec3"); add("pixel", "vec2");
        } else if (shaderType == "postprocess") {
            add("screenUV", "vec2", "context.uv"); add("pixel", "vec2");
        }
        return fields;
    }

    struct Hook {
        const char* name;
        const char* signature;
        bool vertex;
    };

    const Hook hooks[] = {
        {"getVertexOffset", "vec3 getVertexOffset(VertexContext context)", true},
        {"getVaryings", "void getVaryings(VertexContext context, out Varyings varyings)", true},
        {"getCoverage", "float getCoverage(FragmentContext context)", false},
        {"getSurface", "void getSurface(FragmentContext context, inout Surface surface)", false},
        {"getLight", "vec3 getLight(FragmentContext context, Surface surface, LightContext light)", false},
        {"getDepthOffset", "float getDepthOffset(FragmentContext context)", false},
        {"getColor", "vec4 getColor(FragmentContext context, vec4 color)", false},
        {"getUnderwaterColor", "vec4 getUnderwaterColor(UnderwaterContext context, vec4 color)", false},
    };

    struct Value {
        std::string code;
        Type type;
    };

    // what a stage needs declared ahead of its hooks
    struct Stage {
        std::set<int> parameters;
        std::set<std::string> shared;        // #pragma scene and view lines
        std::set<std::string> includes;
        std::set<std::string> functions;
        std::map<int, std::string> custom;   // Custom Function bodies, by node
    };

    struct Varying {
        std::string name;
        Type type;
        std::string code;                    // written in getVaryings
        int node = 0;
    };

    struct Line {
        std::string text;
        int node = 0;
    };

    class Compiler {
    public:
        Compiler(const GraphDocument& graph, const std::string& source, const ShaderGraphReader& reader): graph(graph), source(source), reader(reader) {}
        // a function's preview: a graph of that type calling it once, with one output into slot
        Compiler(const GraphDocument& function, const std::string& source, const ShaderGraphReader& reader,
                 const std::string& type, const std::string& output, const std::string& slot);

        ShaderGraphResult run();

    private:
        // one hook being written
        struct Body {
            const Hook* hook = nullptr;
            std::map<std::string, Field> fields;
            std::vector<Line> lines;
            std::vector<Line> assignments;          // last, after every read of the hook's own values
            std::map<std::string, Value> values;     // node outputs written so far, by "id.pin"
            std::set<int> visiting;
        };

        GraphDocument graph;                      // with every Subgraph node replaced by its function
        const std::string source;
        const ShaderGraphReader& reader;
        const GraphDocument* previewed = nullptr;   // the function a preview calls, as edited
        int previewCall = 0;
        std::map<int, int> origin;                // what came in through a Subgraph, to what its errors point at
        std::map<int, std::string> originFile;    // and the function it came from
        std::map<int, std::vector<std::string>> callers;    // the functions a Subgraph node sits in
        std::set<std::string> functions;
        const ShaderNodes& library = ShaderNodes::get();
        ShaderGraphResult result;
        Stage vertex;
        Stage fragment;
        std::vector<Varying> varyings;
        Body varyingsBody;
        std::map<std::string, std::vector<const GraphLink*>> linksInto;
        std::set<std::string> reported;

        void error(int node, const std::string& text) {
            const std::string key = std::to_string(node) + ":" + text;
            if (reported.insert(key).second)
                result.errors.push_back({node, text});
        }

        Stage& stageOf(const Body& body) { return body.hook->vertex ? vertex : fragment; }

        void inlineSubgraphs();
        bool inlineCall(const GraphNode& call);
        void build();
        void reportOrigins();
        bool checkGraph(const GraphNode*& output);
        bool evaluate(Body& body, int nodeId, const std::string& pin, Value& value);
        bool inputValue(Body& body, const GraphNode& node, const ShaderNodeType& type, const NodePin& pin, Value& value);
        bool expand(Body& body, const GraphNode& node, const ShaderNodeType& type, const std::string& code,
                    const std::map<std::string, std::string>& inputs, const Type& dynamic, std::string& out);
        bool convert(const Value& from, const Type& to, Value& out, int node, const std::string& pin);
        bool parameterValue(Body& body, const GraphParameter& parameter, int node, Value& value);
        std::string varying(const std::string& name, const Type& type, const std::string& code, int node);
        void declare(Stage& stage, const ShaderNodeType& type);
        void writeStage(std::vector<Line>& out, const Stage& stage, const Stage& other, bool vertexStage);
        void writeFunctions(std::vector<Line>& out, const std::set<std::string>& names, const std::function<bool(const std::string&)>& here);
        std::string parameterLine(const GraphParameter& parameter);
    };

    Compiler::Compiler(const GraphDocument& function, const std::string& source, const ShaderGraphReader& reader,
                       const std::string& type, const std::string& output, const std::string& slot): source(source), reader(reader), previewed(&function) {
        graph.kind = "shader";
        graph.type = type;
        const int draw = graph.addNode("output/" + type, Vector2());
        previewCall = graph.addNode("utility/subgraph", Vector2());
        graph.findNode(previewCall)->settings["file"] = GraphValue(source);
        graph.connect(previewCall, output, draw, slot);
    }

    ShaderGraphResult Compiler::run() {
        inlineSubgraphs();
        build();
        reportOrigins();
        result.type = graph.type;
        result.functions.assign(functions.begin(), functions.end());
        return result;
    }

    void Compiler::inlineSubgraphs() {
        auto isCall = [](const GraphNode& node) { return node.type == "utility/subgraph"; };
        for (auto call = std::find_if(graph.nodes.begin(), graph.nodes.end(), isCall); call != graph.nodes.end();
                call = std::find_if(graph.nodes.begin(), graph.nodes.end(), isCall)) {
            const GraphNode node = *call;
            // a call that failed leaves its error and nothing else
            if (!inlineCall(node))
                graph.removeNodes({node.id});
            if (graph.nodes.size() > 4096) {
                error(node.id, "Subgraph functions call each other too many times");
                graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), isCall), graph.nodes.end());
            }
        }
    }

    // a copy of the function takes the call's place: what fed the call feeds its Function
    // Inputs, and what read the call reads its Function Outputs
    bool Compiler::inlineCall(const GraphNode& call) {
        const bool preview = call.id == previewCall;
        const std::string file = settingText(call, "file");
        const std::vector<std::string> outer = callers[call.id];
        if (file.empty()) {
            error(call.id, "Subgraph has no function set");
            return false;
        }
        if (std::find(outer.begin(), outer.end(), file) != outer.end()) {
            error(call.id, "Subgraph " + file + " calls itself");
            return false;
        }

        GraphDocument function;
        if (preview) {
            function = *previewed;
        } else {
            std::string text, readError;
            if (!reader || !reader(file, text)) {
                error(call.id, "Subgraph cannot read " + file);
                return false;
            }
            if (!function.read(text, readError)) {
                error(call.id, file + ": " + readError);
                return false;
            }
            functions.insert(file);
        }
        std::vector<NodePin> inputs, outputs;
        std::vector<GraphMessage> problems;
        if (!checkFunction(function, inputs, outputs, problems)) {
            for (const GraphMessage& problem : problems)
                error(call.id, file + ": " + problem.text);
            return false;
        }
        for (const GraphLink& link : graph.links) {
            if (link.toNode == call.id && std::none_of(inputs.begin(), inputs.end(), [&](const NodePin& pin) { return pin.name == link.toPin; })) {
                error(call.id, file + " has no input " + link.toPin);
                return false;
            }
        }

        // a preview reports on the function's own nodes, a call on the Subgraph node
        const int target = origin.count(call.id) ? origin[call.id] : call.id;
        auto cameFrom = [&](int id, int own) {
            origin[id] = preview ? own : target;
            originFile[id] = preview ? std::string() : file;
        };

        // new ids; parameters join the graph's of the same name
        std::map<int, int> ids;
        for (const GraphParameter& parameter : function.parameters) {
            if (const GraphParameter* existing = graph.findParameter(parameter.name)) {
                if (existing->type != parameter.type) {
                    error(call.id, file + " has a " + parameter.type + " parameter " + parameter.name + ", the graph a " + existing->type + " one");
                    return false;
                }
                ids[parameter.id] = existing->id;
                continue;
            }
            GraphParameter copy = parameter;
            copy.id = graph.takeId();
            ids[parameter.id] = copy.id;
            cameFrom(copy.id, parameter.id);
            graph.parameters.push_back(copy);
        }
        for (const GraphNode& node : function.nodes)
            ids[node.id] = graph.takeId();
        for (GraphNode node : function.nodes) {
            const int own = node.id;
            node.id = ids[own];
            if (const ShaderNodeType* type = library.find(node.type)) {
                for (const NodeSetting& declared : type->settings) {
                    auto value = node.settings.find(declared.name);
                    if (declared.type == "parameter" && value != node.settings.end() && value->second.text.empty()) {
                        auto mapped = ids.find((int)value->second.number());
                        value->second = GraphValue((float)(mapped != ids.end() ? mapped->second : 0));
                    }
                }
            }
            if (node.type == "utility/subgraph") {
                callers[node.id] = outer;
                callers[node.id].push_back(file);
            }
            cameFrom(node.id, own);
            graph.nodes.push_back(node);
        }
        for (GraphLink link : function.links) {
            auto from = ids.find(link.fromNode);
            auto to = ids.find(link.toNode);
            if (from == ids.end() || to == ids.end())
                continue;
            link.fromNode = from->second;
            link.toNode = to->second;
            graph.links.push_back(link);
        }

        for (const GraphNode& node : function.nodes) {
            if (node.type != "function/input")
                continue;
            const std::string name = settingText(node, "name");
            const int input = ids[node.id];
            const GraphLink* fed = graph.findLinkInto(call.id, name);
            auto constant = call.inputs.find(name);
            if (!fed && constant == call.inputs.end())
                continue;
            const GraphLink link = fed ? *fed : GraphLink();
            graph.disconnect(input, "default");
            GraphNode* copy = graph.findNode(input);
            copy->inputs.erase("default");
            if (fed)
                graph.links.push_back({link.fromNode, link.fromPin, input, "default"});
            else
                copy->inputs["default"] = constant->second;
        }

        // each output passes through an input node that turns its value to the output's type
        std::set<int> sinks;
        for (const GraphNode& node : function.nodes) {
            if (node.type != "function/output")
                continue;
            const std::string name = settingText(node, "name");
            const int sink = ids[node.id];
            sinks.insert(sink);
            GraphNode relay;
            relay.id = graph.takeId();
            relay.type = "function/input";
            relay.settings["name"] = GraphValue(name);
            relay.settings["valueType"] = GraphValue(settingText(node, "valueType"));
            if (auto value = node.inputs.find("value"); value != node.inputs.end())
                relay.inputs["default"] = value->second;
            origin[relay.id] = origin[sink];
            originFile[relay.id] = originFile[sink];
            for (GraphLink& link : graph.links) {
                if (link.toNode == sink) {
                    link.toNode = relay.id;
                    link.toPin = "default";
                }
                if (link.fromNode != call.id || link.fromPin.empty())
                    continue;
                std::string pin, swizzle;
                splitSwizzle(link.fromPin, pin, swizzle);
                // a bare swizzle reads the first output
                const bool named = std::any_of(outputs.begin(), outputs.end(), [&](const NodePin& output) { return output.name == pin; });
                if (!named && swizzle.empty() && isSwizzle(pin) && name == outputs.front().name) {
                    swizzle = pin;
                    pin = name;
                }
                if (pin != name)
                    continue;
                link.fromNode = relay.id;
                link.fromPin = swizzle.empty() ? "out" : "out." + swizzle;
            }
            graph.nodes.push_back(relay);
        }
        for (const GraphLink& link : graph.links) {
            if (link.fromNode == call.id && !link.fromPin.empty()) {
                error(call.id, file + " has no output " + link.fromPin);
                return false;
            }
        }

        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [&](const GraphLink& link) { return link.toNode == call.id; }), graph.links.end());
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [&](const GraphNode& node) {
            return node.id == call.id || sinks.count(node.id);
        }), graph.nodes.end());
        return true;
    }

    void Compiler::reportOrigins() {
        std::vector<GraphMessage> errors;
        std::set<std::string> seen;
        for (GraphMessage message : result.errors) {
            if (auto it = origin.find(message.node); it != origin.end()) {
                const std::string& file = originFile[message.node];
                if (!file.empty())
                    message.text = file + ": " + message.text;
                message.node = it->second;
            } else if (previewed) {
                // the preview's own output and call
                message.node = 0;
            }
            if (seen.insert(std::to_string(message.node) + ":" + message.text).second)
                errors.push_back(message);
        }
        result.errors = errors;
        for (int& node : result.lineNodes) {
            auto it = origin.find(node);
            node = it != origin.end() ? it->second : (previewed ? 0 : node);
        }
    }

    bool Compiler::checkGraph(const GraphNode*& output) {
        static const std::set<std::string> fileTypes = {"mesh", "ui", "points", "lines", "sky", "water", "postprocess"};
        if (graph.kind != "shader")
            error(0, "not a shader graph (kind: " + graph.kind + ")");
        if (!fileTypes.count(graph.type))
            error(0, "type " + graph.type + " is not one of mesh, ui, points, lines, sky, water, postprocess");

        // every word the code can hold, which a texture's name would replace
        std::set<std::string> used;
        const bool textures = std::any_of(graph.parameters.begin(), graph.parameters.end(), [](const GraphParameter& parameter) {
            return parameter.type == "texture2D" || parameter.type == "textureCube";
        });
        for (const NodeType* base : textures ? library.getTypes() : std::vector<const NodeType*>()) {
            const ShaderNodeType* type = static_cast<const ShaderNodeType*>(base);
            for (const std::string& code : type->code)
                collectIdentifiers(code, used);
            for (const auto& variant : type->variants)
                for (const std::string& code : variant.second)
                    collectIdentifiers(code, used);
            for (const NodePin& pin : type->inputs)
                collectIdentifiers(pin.value.text, used);
            for (const std::string& function : type->functions)
                if (const ShaderFunction* helper = library.findFunction(function))
                    collectIdentifiers(helper->code, used);
        }
        for (const char* hook : {"getVertexOffset", "getVaryings", "getCoverage", "getSurface", "getLight", "getDepthOffset", "getColor", "getUnderwaterColor"})
            for (const auto& field : contextFields(graph.type, hook))
                used.insert(field.first);

        std::set<std::string> names;
        for (const GraphParameter& parameter : graph.parameters) {
            const bool texture = parameter.type == "texture2D" || parameter.type == "textureCube";
            if (!isIdentifier(parameter.name))
                error(parameter.id, "parameter name " + parameter.name + " is not a GLSL name");
            else if (isKeyword(parameter.name) || (texture && used.count(parameter.name)))
                error(parameter.id, "parameter name " + parameter.name + " is a word the shader code uses, rename it");
            else if (!names.insert(parameter.name).second)
                error(parameter.id, "two parameters are called " + parameter.name);
            static const std::set<std::string> types = {"float", "vec2", "vec3", "vec4", "int", "color", "texture2D", "textureCube"};
            if (!types.count(parameter.type))
                error(parameter.id, "parameter " + parameter.name + " has an unknown type " + parameter.type);
        }

        for (const GraphNode& node : graph.nodes) {
            const ShaderNodeType* type = library.find(node.type);
            if (!type) {
                error(node.id, "unknown node type " + node.type);
                continue;
            }
            std::vector<NodePin> inputs, outputs;
            std::string pinError;
            if (!ShaderNodes::getPins(node, *type, inputs, outputs, &pinError))
                error(node.id, type->name + ": " + pinError);
            if (type->id.rfind("function/", 0) == 0 && !origin.count(node.id))
                error(node.id, type->name + " only works in a function graph");
            if (type->id.rfind("output/", 0) == 0) {
                if (type->id != "output/" + graph.type)
                    error(node.id, type->name + " does not fit a " + graph.type + " shader");
                else if (output)
                    error(node.id, "a graph has one output node");
                else
                    output = &node;
            }
        }
        if (!output && result.errors.empty())
            error(0, "the graph has no " + graph.type + " output node");

        for (const GraphLink& link : graph.links) {
            const GraphNode* to = graph.findNode(link.toNode);
            const ShaderNodeType* toType = to ? library.find(to->type) : nullptr;
            std::vector<NodePin> toInputs, toOutputs;
            if (to && toType && !ShaderNodes::getPins(*to, *toType, toInputs, toOutputs))
                continue;
            if (!to || (toType && std::none_of(toInputs.begin(), toInputs.end(), [&](const NodePin& pin) { return pin.name == link.toPin; }))) {
                error(link.toNode, "a link ends at a missing input " + std::to_string(link.toNode) + "." + link.toPin);
                continue;
            }
            const std::string key = std::to_string(link.toNode) + "." + link.toPin;
            linksInto[key].push_back(&link);
            if (linksInto[key].size() > 1)
                error(link.toNode, "input " + link.toPin + " takes one link");
            if (link.fromPin.empty()) {
                if (!graph.findParameter(link.fromNode))
                    error(link.toNode, "a link starts at missing parameter " + std::to_string(link.fromNode));
                continue;
            }
            const GraphNode* from = graph.findNode(link.fromNode);
            const ShaderNodeType* fromType = from ? library.find(from->type) : nullptr;
            std::vector<NodePin> fromInputs, fromOutputs;
            if (from && fromType && !ShaderNodes::getPins(*from, *fromType, fromInputs, fromOutputs))
                continue;
            std::string fromOutput, fromSwizzle;
            splitSwizzle(link.fromPin, fromOutput, fromSwizzle);
            const bool named = std::any_of(fromOutputs.begin(), fromOutputs.end(), [&](const NodePin& pin) { return pin.name == fromOutput; });
            if (!from || (fromType && !(named && (fromSwizzle.empty() || isSwizzle(fromSwizzle))) &&
                          !(fromSwizzle.empty() && isSwizzle(fromOutput) && !fromOutputs.empty())))
                error(link.toNode, "a link starts at a missing output " + std::to_string(link.fromNode) + "." + link.fromPin);
        }
        return result.errors.empty();
    }

    bool Compiler::convert(const Value& from, const Type& to, Value& out, int node, const std::string& pin) {
        out.type = to;
        if (from.type.name == to.name) {
            out.code = from.code;
            return true;
        }
        if (from.type.name == "texture2D" || from.type.name == "textureCube" || to.name == "texture2D" || to.name == "textureCube") {
            error(node, pin + " takes a " + to.name + ", not a " + from.type.name);
            return false;
        }
        if (to.name == "bool") {
            error(node, pin + " takes a bool; compare the value first");
            return false;
        }
        if (to.name == "int") {
            if (from.type.name == "float") {
                out.code = "int(" + from.code + ")";
                return true;
            }
            error(node, pin + " takes an int, not a " + from.type.name);
            return false;
        }
        std::string scalar = from.code;
        Type source = from.type;
        if (source.name == "int" || source.name == "bool") {
            scalar = "float(" + from.code + ")";
            source = Type{"float"};
        }
        const int fromSize = source.size();
        const int toSize = to.size();
        if (fromSize == toSize) {
            out.code = scalar;
        } else if (fromSize == 1) {
            out.code = to.name + "(" + scalar + ")";
        } else if (fromSize > toSize && toSize > 1) {
            out.code = scalar + (toSize == 2 ? ".xy" : ".xyz");
        } else if (fromSize == 3 && toSize == 4) {
            out.code = "vec4(" + scalar + ", " + (to.color || source.color ? "1.0" : "0.0") + ")";
        } else {
            error(node, pin + " takes a " + to.name + ", not a " + from.type.name + "; split or combine it");
            return false;
        }
        return true;
    }

    std::string Compiler::parameterLine(const GraphParameter& parameter) {
        std::ostringstream line;
        if (parameter.type == "texture2D" || parameter.type == "textureCube") {
            line << "#pragma param " << parameter.type << " " << parameter.name << " = "
                 << (parameter.value.text.empty() ? "white" : parameter.value.text) << " "
                 << (parameter.filter.empty() ? "linear" : parameter.filter) << " "
                 << (parameter.wrap.empty() ? "repeat" : parameter.wrap);
        } else {
            const bool color = parameter.type == "color";
            const std::string type = color ? "vec4" : parameter.type;
            const int size = Type{type}.size();
            line << "#pragma param " << type << " " << parameter.name << " = ";
            for (int i = 0; i < size; i++) {
                float number = i < (int)parameter.value.numbers.size() ? parameter.value.numbers[i]
                                                                       : (parameter.value.numbers.size() == 1 ? parameter.value.number() : (color ? 1.0f : 0.0f));
                char buffer[32];
                auto r = std::to_chars(buffer, buffer + sizeof(buffer), number);
                line << (i ? ", " : "") << std::string(buffer, r.ptr);
            }
            if (parameter.hasRange) {
                char low[32], high[32];
                auto a = std::to_chars(low, low + sizeof(low), parameter.minValue);
                auto b = std::to_chars(high, high + sizeof(high), parameter.maxValue);
                line << " [" << std::string(low, a.ptr) << ", " << std::string(high, b.ptr) << "]";
            }
            if (color)
                line << " color";
        }
        if (parameter.runtime)
            line << " runtime";
        if (!parameter.description.empty()) {
            std::string description = parameter.description;
            description.erase(std::remove(description.begin(), description.end(), '"'), description.end());
            line << " \"" << description << "\"";
        }
        return line.str();
    }

    bool Compiler::parameterValue(Body& body, const GraphParameter& parameter, int node, Value& value) {
        if (parameter.type == "texture2D" || parameter.type == "textureCube") {
            error(node, "texture " + parameter.name + " is read through a Sample node, set as its Texture");
            return false;
        }
        stageOf(body).parameters.insert(parameter.id);
        value.code = "params." + parameter.name;
        value.type = pinType(parameter.type);
        return true;
    }

    std::string Compiler::varying(const std::string& name, const Type& type, const std::string& code, int node) {
        for (const Varying& existing : varyings)
            if (existing.name == name)
                return "varyings." + name;
        varyings.push_back({name, type, code, node});
        return "varyings." + name;
    }

    void Compiler::declare(Stage& stage, const ShaderNodeType& type) {
        if (!type.include.empty())
            stage.includes.insert(type.include);
        std::function<void(const std::string&)> need = [&](const std::string& name) {
            if (stage.functions.count(name))
                return;
            if (const ShaderFunction* function = library.findFunction(name)) {
                for (const std::string& dependency : function->needs)
                    need(dependency);
            }
            stage.functions.insert(name);
        };
        for (const std::string& function : type.functions)
            need(function);
    }

    bool Compiler::expand(Body& body, const GraphNode& node, const ShaderNodeType& type, const std::string& code,
                          const std::map<std::string, std::string>& inputs, const Type& dynamic, std::string& out) {
        out.clear();
        for (size_t i = 0; i < code.size();) {
            const char c = code[i];
            if (c == '{') {
                const size_t end = code.find('}', i);
                if (end == std::string::npos) {
                    error(node.id, type.name + " has a { without its }");
                    return false;
                }
                const std::string name = code.substr(i + 1, end - i - 1);
                if (name == "T") {
                    out += dynamic.name;
                } else if (auto it = inputs.find(name); it != inputs.end()) {
                    out += it->second;
                } else if (name == "texture") {
                    // a texture parameter named by the node's setting
                    auto setting = node.settings.find("texture");
                    const GraphParameter* parameter = setting != node.settings.end() ? graph.findParameter((int)setting->second.number()) : nullptr;
                    if (!parameter || (parameter->type != "texture2D" && parameter->type != "textureCube")) {
                        error(node.id, type.name + " has no texture parameter set");
                        return false;
                    }
                    const NodeSetting* declared = type.findSetting("texture");
                    if (declared && std::find(declared->options.begin(), declared->options.end(), parameter->type) == declared->options.end()) {
                        error(node.id, type.name + " takes a " + (parameter->type == "texture2D" ? "cube map" : "2D texture") + ", " + parameter->name + " is not one");
                        return false;
                    }
                    stageOf(body).parameters.insert(parameter->id);
                    out += parameter->name;
                } else {
                    auto setting = node.settings.find(name);
                    const NodeSetting* declared = type.findSetting(name);
                    const GraphValue value = setting != node.settings.end() ? setting->second : (declared ? declared->value : GraphValue());
                    out += value.text.empty() ? glslFloat(value.number(), true) : value.text;
                }
                i = end + 1;
            } else if (c == '@' || c == '%') {
                size_t end = i + 1;
                while (end < code.size() && (std::isalnum((unsigned char)code[end]) || code[end] == '_' || (c == '%' && code[end] == '.')))
                    end++;
                const std::string name = code.substr(i + 1, end - i - 1);
                i = end;
                if (c == '@') {
                    if (auto field = body.fields.find(name); field != body.fields.end()) {
                        out += field->second.code;
                    } else if (graph.type == "mesh" && !body.hook->vertex) {
                        // a vertex value the fragments get interpolated
                        auto fields = contextFields("mesh", "getVaryings");
                        auto field = fields.find(name);
                        if (field == fields.end()) {
                            error(node.id, type.name + " reads " + name + ", which " + graph.type + " shaders do not have");
                            return false;
                        }
                        out += varying(name, field->second.type, field->second.code, node.id);
                    } else {
                        error(node.id, type.name + " reads " + name + ", which " + std::string(body.hook->name) + " does not have");
                        return false;
                    }
                } else if (name.rfind("surface.", 0) == 0) {
                    if (std::string(body.hook->name) != "getSurface" && std::string(body.hook->name) != "getLight") {
                        error(node.id, type.name + " only feeds the surface outputs and Light");
                        return false;
                    }
                    out += name;
                } else if (name.rfind("light.", 0) == 0) {
                    if (std::string(body.hook->name) != "getLight") {
                        error(node.id, type.name + " only feeds Light");
                        return false;
                    }
                    out += name;
                } else if (name == "color") {
                    if (std::string(body.hook->name) != "getColor" && std::string(body.hook->name) != "getUnderwaterColor") {
                        error(node.id, type.name + " only feeds the color outputs");
                        return false;
                    }
                    out += "color";
                } else {
                    error(node.id, "unknown code token %" + name);
                    return false;
                }
            } else {
                out += c;
                i++;
            }
        }
        return true;
    }

    bool Compiler::inputValue(Body& body, const GraphNode& node, const ShaderNodeType& type, const NodePin& pin, Value& value) {
        auto links = linksInto.find(std::to_string(node.id) + "." + pin.name);
        if (links != linksInto.end()) {
            const GraphLink& link = *links->second.front();
            if (link.fromPin.empty()) {
                const GraphParameter* parameter = graph.findParameter(link.fromNode);
                return parameter && parameterValue(body, *parameter, node.id, value);
            }
            return evaluate(body, link.fromNode, link.fromPin, value);
        }

        // a constant, else the pin's default
        GraphValue constant = pin.value;
        if (auto it = node.inputs.find(pin.name); it != node.inputs.end())
            constant = it->second;
        const Type wanted = pin.type == "dynamic" ? vectorType((int)std::max<size_t>(1, constant.numbers.size())) : pinType(pin.type);
        if (!constant.text.empty() && constant.numbers.empty()) {
            // code, as @uv1 or Engine.time
            std::string code;
            if (!expand(body, node, type, constant.text, {}, wanted, code))
                return false;
            value.code = code;
            if (constant.text[0] == '@') {
                const std::string name = constant.text.substr(1);
                auto field = body.fields.find(name);
                const std::map<std::string, Field> vertexFields = contextFields("mesh", "getVaryings");
                auto vertexField = vertexFields.find(name);
                value.type = field != body.fields.end() ? field->second.type
                           : (vertexField != vertexFields.end() ? vertexField->second.type : wanted);
            } else {
                value.type = (constant.text == "true" || constant.text == "false") ? Type{"bool"} : (pin.type == "dynamic" ? Type{"float"} : wanted);
            }
            return true;
        }
        value.code = literal(constant, wanted);
        value.type = wanted;
        return true;
    }

    bool Compiler::evaluate(Body& body, int nodeId, const std::string& pin, Value& value) {
        const std::string key = std::to_string(nodeId) + "." + pin;
        if (auto done = body.values.find(key); done != body.values.end()) {
            value = done->second;
            return true;
        }

        const GraphNode* node = graph.findNode(nodeId);
        const ShaderNodeType* type = node ? library.find(node->type) : nullptr;
        if (!node || !type) {
            error(nodeId, "missing node " + std::to_string(nodeId));
            return false;
        }

        std::vector<NodePin> pinsIn, pinsOut;
        if (!ShaderNodes::getPins(*node, *type, pinsIn, pinsOut))
            return false;

        // a swizzle of a named output, or of the first one
        std::string outputName, swizzle;
        splitSwizzle(pin, outputName, swizzle);
        const NodePin* output = nullptr;
        for (const NodePin& candidate : pinsOut)
            if (candidate.name == outputName)
                output = &candidate;
        if (!output && swizzle.empty() && isSwizzle(pin) && !pinsOut.empty()) {
            output = &pinsOut.front();
            swizzle = pin;
        }
        if (output && !swizzle.empty() && !isSwizzle(swizzle)) {
            error(nodeId, swizzle + " is not a swizzle of xyzw or rgba");
            return false;
        }
        if (!output) {
            error(nodeId, type->name + " has no output " + pin);
            return false;
        }
        if (!swizzle.empty()) {
            Value base;
            if (!evaluate(body, nodeId, output->name, base))
                return false;
            if (!base.type.vector() || swizzleReach(swizzle) > base.type.size()) {
                error(nodeId, "a " + base.type.name + " has no ." + swizzle);
                return false;
            }
            value.code = base.code + "." + swizzle;
            value.type = vectorType((int)swizzle.size());
            body.values[key] = value;
            return true;
        }

        if (!type->shaderTypes.empty() && type->id.rfind("function/", 0) != 0 &&
                std::find(type->shaderTypes.begin(), type->shaderTypes.end(), graph.type) == type->shaderTypes.end()) {
            error(nodeId, type->name + " does not work in a " + graph.type + " shader");
            return false;
        }
        if (type->stage == ShaderNodeStage::FRAGMENT && body.hook->vertex) {
            error(nodeId, type->name + " works per fragment, it cannot feed a vertex output");
            return false;
        }
        if (type->stage == ShaderNodeStage::VERTEX && !body.hook->vertex) {
            error(nodeId, type->name + " works per vertex, it can only feed Vertex Offset");
            return false;
        }
        if (!body.visiting.insert(nodeId).second) {
            error(nodeId, type->name + " feeds itself through a loop of links");
            return false;
        }

        // worked out per vertex, read per fragment
        if (type->id == "utility/vertexToFragment" && !body.hook->vertex) {
            body.visiting.erase(nodeId);
            Value input;
            if (!inputValue(varyingsBody, *node, *type, pinsIn.front(), input))
                return false;
            if (!input.type.vector()) {
                error(nodeId, "a " + input.type.name + " cannot pass to the fragments");
                return false;
            }
            value.code = varying("n" + std::to_string(nodeId), input.type, input.code, nodeId);
            value.type = input.type;
            body.values[key] = value;
            return true;
        }

        // inputs, then the size the dynamic ones settle on: the smallest vector linked, floats aside
        std::map<std::string, Value> raw;
        int dynamicSize = 0;
        for (const NodePin& input : pinsIn) {
            Value inputValueOut;
            if (!inputValue(body, *node, *type, input, inputValueOut)) {
                body.visiting.erase(nodeId);
                return false;
            }
            raw[input.name] = inputValueOut;
            if (input.type == "dynamic" && inputValueOut.type.vector() && inputValueOut.type.size() > 1)
                dynamicSize = dynamicSize ? std::min(dynamicSize, inputValueOut.type.size()) : inputValueOut.type.size();
        }
        const Type dynamic = vectorType(std::max(dynamicSize, 1));

        std::map<std::string, std::string> inputs;
        for (const NodePin& input : pinsIn) {
            Value converted;
            if (!convert(raw[input.name], input.type == "dynamic" ? dynamic : pinType(input.type), converted, nodeId, input.label)) {
                body.visiting.erase(nodeId);
                return false;
            }
            inputs[input.name] = converted.code;
        }

        // the code of this output: by the node's first setting, per vertex, or the plain one
        const size_t index = output - &pinsOut.front();
        const std::vector<std::string>* code = &type->code;
        if (!type->variants.empty() && !type->settings.empty()) {
            auto setting = node->settings.find(type->settings.front().name);
            const std::string chosen = setting != node->settings.end()
                ? (setting->second.text.empty() ? std::to_string((int)setting->second.number()) : setting->second.text)
                : type->settings.front().value.text;
            auto variant = type->variants.find(chosen);
            if (variant == type->variants.end()) {
                error(nodeId, type->name + " has no " + type->settings.front().label + " " + chosen);
                body.visiting.erase(nodeId);
                return false;
            }
            code = &variant->second;
        }
        if (index >= code->size()) {
            error(nodeId, type->name + " has no code for " + output->name);
            body.visiting.erase(nodeId);
            return false;
        }

        // the type of the output
        Type outType = output->type == "dynamic" ? dynamic : pinType(output->type);
        if (output->type == "mask") {
            auto mask = node->settings.find("mask");
            const std::string picked = mask != node->settings.end() ? mask->second.text : type->settings.front().value.text;
            if (!isSwizzle(picked)) {
                error(nodeId, "a mask is up to four of xyzw or rgba");
                body.visiting.erase(nodeId);
                return false;
            }
            outType = vectorType((int)picked.size());
        }
        std::string expression;
        if ((*code)[index] == "%custom") {
            auto text = [&](const char* name) {
                auto it = node->settings.find(name);
                return it != node->settings.end() ? it->second.text : type->findSetting(name)->value.text;
            };
            std::string arguments;
            for (const NodePin& input : pinsIn)
                arguments += (arguments.empty() ? "" : ", ") + inputs[input.name];
            const std::string file = text("file");
            if (!file.empty()) {
                // the function is the signature's, in a file of the project
                std::istringstream words(text("signature"));
                std::string returned, name;
                words >> returned >> name;
                name = name.substr(0, name.find('('));
                stageOf(body).includes.insert(file);
                expression = name + "(" + arguments + ")";
            } else {
                std::string signature = text("signature");
                const size_t open = signature.find('(');
                if (open == std::string::npos) {
                    error(nodeId, "a signature reads like vec3 shade(vec3 color, float amount)");
                    body.visiting.erase(nodeId);
                    return false;
                }
                std::istringstream words(signature.substr(0, open));
                std::string returned;
                words >> returned;
                const std::string function = "graphCustom" + std::to_string(nodeId);
                stageOf(body).custom[nodeId] = returned + " " + function + signature.substr(open) + "{\n" + text("body") + "\n}";
                expression = function + "(" + arguments + ")";
            }
        } else if ((*code)[index] == "%parameter") {
            auto setting = node->settings.find("parameter");
            const GraphParameter* parameter = setting != node->settings.end() ? graph.findParameter((int)setting->second.number()) : nullptr;
            if (!parameter) {
                error(nodeId, "Parameter has no parameter set");
                body.visiting.erase(nodeId);
                return false;
            }
            Value parameterOut;
            if (!parameterValue(body, *parameter, nodeId, parameterOut)) {
                body.visiting.erase(nodeId);
                return false;
            }
            expression = parameterOut.code;
            outType = parameterOut.type;
        } else if ((*code)[index] == "%shared") {
            auto text = [&](const char* name) {
                auto it = node->settings.find(name);
                return it != node->settings.end() ? it->second.text : std::string();
            };
            const std::string collection = text("collection");
            const std::string name = text("name");
            std::string valueType = text("valueType");
            if (valueType.empty())
                valueType = "float";
            if (!isIdentifier(collection) || !isIdentifier(name)) {
                error(nodeId, type->name + " needs a collection and a name, as Wind and velocity");
                body.visiting.erase(nodeId);
                return false;
            }
            const bool scene = type->id == "parameter/scene";
            stageOf(body).shared.insert(std::string("#pragma ") + (scene ? "scene " : "view ") + valueType + " " + collection + "." + name);
            expression = collection + "." + name;
            outType = Type{valueType};
        } else if ((*code)[index] == "%value") {
            auto setting = node->settings.find("value");
            const GraphValue constant = setting != node->settings.end() ? setting->second : type->settings.front().value;
            expression = literal(constant, outType);
        } else if (!expand(body, *node, *type, body.hook->vertex ? fixedLevel((*code)[index]) : (*code)[index], inputs, dynamic, expression)) {
            body.visiting.erase(nodeId);
            return false;
        }
        declare(stageOf(body), *type);
        body.visiting.erase(nodeId);

        if (type->inlined) {
            value.code = expression;
        } else {
            const std::string variable = "n" + std::to_string(nodeId) + (pinsOut.size() > 1 ? "_" + output->name : "");
            body.lines.push_back({"    " + outType.name + " " + variable + " = " + expression + ";", nodeId});
            value.code = variable;
        }
        value.type = outType;
        body.values[key] = value;
        return true;
    }

    void Compiler::writeStage(std::vector<Line>& out, const Stage& stage, const Stage& other, bool vertexStage) {
        for (const GraphParameter& parameter : graph.parameters)
            if (stage.parameters.count(parameter.id) && !other.parameters.count(parameter.id))
                out.push_back({parameterLine(parameter), parameter.id});
        for (const std::string& line : stage.shared)
            if (!other.shared.count(line))
                out.push_back({line});
        if (vertexStage)
            for (const Varying& v : varyings)
                out.push_back({"#pragma varying " + v.type.name + " " + v.name, v.node});
        for (const std::string& include : stage.includes)
            if (!other.includes.count(include))
                out.push_back({"#include \"" + include + "\""});
        writeFunctions(out, stage.functions, [&](const std::string& name) { return !other.functions.count(name); });
        for (const auto& [node, code] : stage.custom) {
            out.push_back({"", node});
            std::istringstream lines(code);
            for (std::string line; std::getline(lines, line);)
                out.push_back({line, node});
        }
    }

    void Compiler::writeFunctions(std::vector<Line>& out, const std::set<std::string>& names, const std::function<bool(const std::string&)>& here) {
        // in the order they need each other
        std::vector<std::string> ordered;
        std::function<void(const std::string&)> visit = [&](const std::string& name) {
            if (std::find(ordered.begin(), ordered.end(), name) != ordered.end())
                return;
            if (const ShaderFunction* function = library.findFunction(name))
                for (const std::string& dependency : function->needs)
                    visit(dependency);
            ordered.push_back(name);
        };
        for (const std::string& name : names)
            visit(name);
        for (const std::string& name : ordered) {
            if (!here(name))
                continue;
            out.push_back({""});
            std::istringstream lines(library.findFunction(name)->code);
            for (std::string line; std::getline(lines, line);)
                out.push_back({line});
        }
    }

    void Compiler::build() {
        const GraphNode* output = nullptr;
        if (!checkGraph(output))
            return;

        const ShaderNodeType* outputType = library.find(output->type);
        varyingsBody.hook = &hooks[1];
        varyingsBody.fields = contextFields(graph.type, "getVaryings");
        std::map<std::string, Body> bodies;
        for (const NodePin& pin : outputType->inputs) {
            const bool linked = linksInto.count(std::to_string(output->id) + "." + pin.name) > 0;
            if (!linked && !output->inputs.count(pin.name))
                continue;
            const ShaderOutputSlot& slot = outputType->slots.at(pin.name);
            Body& body = bodies[slot.hook];
            if (!body.hook) {
                for (const Hook& hook : hooks)
                    if (slot.hook == hook.name)
                        body.hook = &hook;
                body.fields = contextFields(graph.type, slot.hook);
            }
            Value value;
            if (!inputValue(body, *output, *outputType, pin, value))
                continue;
            Value converted;
            if (!convert(value, pinType(pin.type), converted, output->id, pin.label))
                continue;
            if (slot.target.empty()) {
                body.assignments.push_back({"    return " + converted.code + ";", output->id});
            } else if (slot.depthPasses) {
                body.assignments.push_back({"    " + slot.target + " = " + converted.code + ";", output->id});
            } else {
                // the depth passes read only the surface's alpha
                body.assignments.push_back({"    #ifndef IN_DEPTH_PASS", output->id});
                body.assignments.push_back({"        " + slot.target + " = " + converted.code + ";", output->id});
                body.assignments.push_back({"    #endif", output->id});
            }
        }
        for (const Varying& v : varyings)
            varyingsBody.assignments.push_back({"    varyings." + v.name + " = " + v.code + ";", v.node});
        if (varyings.size() > 4)
            error(varyings.back().node, "a graph passes at most 4 values from the vertices to the fragments, this one " + std::to_string(varyings.size()));
        if (!result.errors.empty())
            return;
        // with nothing set it draws as the type does by default, through a color hook that keeps it
        if (bodies.empty() && varyings.empty()) {
            Body& body = bodies["getColor"];
            for (const Hook& hook : hooks)
                if (std::string(hook.name) == "getColor")
                    body.hook = &hook;
            body.assignments.push_back({"    return color;", output->id});
        }

        // the file, in the order one written by hand has
        std::vector<Line> lines;
        lines.push_back({"// generated from " + source + ", edit the graph instead"});
        lines.push_back({"#pragma shader " + graph.type});

        // what both stages read is declared once, before them
        std::vector<Line> shared;
        for (const GraphParameter& parameter : graph.parameters)
            if (vertex.parameters.count(parameter.id) && fragment.parameters.count(parameter.id))
                shared.push_back({parameterLine(parameter), parameter.id});
        for (const std::string& line : vertex.shared)
            if (fragment.shared.count(line))
                shared.push_back({line});
        for (const std::string& include : vertex.includes)
            if (fragment.includes.count(include))
                shared.push_back({"#include \"" + include + "\""});
        writeFunctions(shared, vertex.functions, [&](const std::string& name) { return fragment.functions.count(name) > 0; });
        if (!shared.empty()) {
            lines.push_back({""});
            lines.insert(lines.end(), shared.begin(), shared.end());
        }

        for (bool vertexStage : {true, false}) {
            bool any = vertexStage && !varyings.empty();
            for (const Hook& hook : hooks)
                any = any || (hook.vertex == vertexStage && bodies.count(hook.name));
            if (!any)
                continue;
            lines.push_back({""});
            lines.push_back({vertexStage ? "#pragma stage vertex" : "#pragma stage fragment"});
            writeStage(lines, vertexStage ? vertex : fragment, vertexStage ? fragment : vertex, vertexStage);
            for (const Hook& hook : hooks) {
                if (hook.vertex != vertexStage)
                    continue;
                const bool isVaryings = std::string(hook.name) == "getVaryings";
                if (isVaryings ? varyings.empty() : !bodies.count(hook.name))
                    continue;
                const Body& body = isVaryings ? varyingsBody : bodies[hook.name];
                lines.push_back({""});
                lines.push_back({std::string(hook.signature) + "{"});
                lines.insert(lines.end(), body.lines.begin(), body.lines.end());
                lines.insert(lines.end(), body.assignments.begin(), body.assignments.end());
                lines.push_back({"}"});
            }
        }

        std::string code;
        for (const Line& line : lines) {
            code += line.text + "\n";
            result.lineNodes.push_back(line.node);
        }
        result.code = code;
        result.success = true;
    }

}

editor::ShaderGraphResult editor::ShaderGraphCompiler::compile(const GraphDocument& graph, const std::string& source, const ShaderGraphReader& reader) {
    if (graph.kind != "shader" || graph.type != "function")
        return Compiler(graph, source, reader).run();

    // a function draws each output in turn, and shows the first
    ShaderGraphResult result;
    std::vector<NodePin> inputs, outputs;
    if (!checkFunction(graph, inputs, outputs, result.errors))
        return result;
    const std::string type = previewType(graph);
    const bool vertexSlot = ShaderNodes::get().find("output/" + type)->findInput("vertexOffset") != nullptr;
    // what only one hook has decides where the preview draws
    auto holds = [&](const char* nodeType) {
        return std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const GraphNode& node) { return node.type == nodeType; });
    };
    const char* fragmentSlot = holds("input/light") ? "light" : (holds("input/material") ? "emissive" : "color");
    for (size_t i = 0; i < outputs.size(); i++) {
        bool perVertex = false;
        for (const GraphNode& node : graph.nodes) {
            std::set<int> seen;
            if (node.type == "function/output" && settingText(node, "name") == outputs[i].name)
                perVertex = vertexSlot && fedPerVertex(graph, node.id, seen);
        }
        ShaderGraphResult drawn = Compiler(graph, source, reader, type, outputs[i].name, perVertex ? "vertexOffset" : fragmentSlot).run();
        if (i == 0) {
            result.type = drawn.type;
            result.code = drawn.code;
            result.lineNodes = drawn.lineNodes;
        }
        for (const GraphMessage& message : drawn.errors) {
            if (std::none_of(result.errors.begin(), result.errors.end(), [&](const GraphMessage& known) { return known.node == message.node && known.text == message.text; }))
                result.errors.push_back(message);
        }
        for (const std::string& file : drawn.functions) {
            if (std::find(result.functions.begin(), result.functions.end(), file) == result.functions.end())
                result.functions.push_back(file);
        }
    }
    result.success = result.errors.empty();
    return result;
}

bool editor::ShaderGraphCompiler::getFunctionPins(const GraphDocument& function, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error) {
    std::vector<GraphMessage> problems;
    inputs.clear();
    outputs.clear();
    if (checkFunction(function, inputs, outputs, problems))
        return true;
    if (error)
        *error = problems.front().text;
    return false;
}

bool editor::ShaderGraphCompiler::previewNode(const GraphDocument& graph, int node, GraphDocument& preview) {
    const GraphNode* shown = graph.findNode(node);
    const ShaderNodeType* type = shown ? ShaderNodes::get().find(shown->type) : nullptr;
    if (graph.type != "mesh" || !type || type->id.rfind("output/", 0) == 0)
        return false;
    std::vector<NodePin> inputs, outputs;
    if (!ShaderNodes::getPins(*shown, *type, inputs, outputs) || outputs.empty())
        return false;
    preview = graph;
    auto output = std::find_if(preview.nodes.begin(), preview.nodes.end(), [](const GraphNode& item) { return item.type == "output/mesh"; });
    if (output == preview.nodes.end())
        return false;
    output->inputs.clear();
    const int draw = output->id;
    preview.links.erase(std::remove_if(preview.links.begin(), preview.links.end(), [&](const GraphLink& link) { return link.toNode == draw; }), preview.links.end());
    std::set<int> seen;
    const bool perVertex = type->stage == ShaderNodeStage::VERTEX || fedPerVertex(graph, node, seen);
    preview.connect(node, outputs.front().name, draw, perVertex ? "vertexOffset" : "color");
    return true;
}
