// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/NodeRegistry.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doriax::editor {

    // where a node's GLSL can run
    enum class ShaderNodeStage {
        ANY,
        VERTEX,
        FRAGMENT
    };

    // what an input of an output node fills in the shader file
    struct ShaderOutputSlot {
        std::string hook;            // getSurface, getColor...
        std::string target;          // surface.baseColor, or empty when the hook returns it
        bool vertex = false;
        bool depthPasses = false;    // read by the depth and shadow passes too
    };

    // a GLSL helper, written once per stage after its needs
    struct ShaderFunction {
        std::vector<std::string> needs;
        std::string code;
    };

    // pin types: float, vecN, int, bool, color and dynamic
    struct ShaderNodeType : NodeType {
        // GLSL per output: {input}, {T}, @context field, %hook value
        std::vector<std::string> code;
        // code by the value of the first setting, as a Position node's space
        std::map<std::string, std::vector<std::string>> variants;
        ShaderNodeStage stage = ShaderNodeStage::ANY;
        std::vector<std::string> functions;          // helpers it calls, by name
        std::string include;                         // an engine library it calls into
        std::vector<std::string> shaderTypes;        // the files it fits; empty for all
        bool inlined = false;                        // read in place, never kept in a variable
        std::map<std::string, ShaderOutputSlot> slots; // of an output node, by input
    };

    class ShaderNodes : public NodeRegistry {
    public:
        // a function graph's pins, by path
        using FunctionPins = std::function<bool(const std::string& path, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs)>;

        static const ShaderNodes& get();
        // the editor answers it from the project's files
        static void setFunctionPins(FunctionPins lookup);

        const NodeType* findType(const std::string& id) const override;
        std::vector<const NodeType*> getTypes() const override;

        const ShaderNodeType* find(const std::string& id) const;
        const ShaderFunction* findFunction(const std::string& name) const;

        // a node's pins; false on a bad signature or function
        static bool getPins(const GraphNode& node, const ShaderNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error = nullptr);
        // the type a pin carries in this node: a parameter's own, a mask's size
        static std::string getPinType(const GraphDocument& graph, const GraphNode& node, const NodePin& pin);
        // numbers convert; textures and bools only to themselves
        static bool canLink(const std::string& fromType, const std::string& toType);

    private:
        ShaderNodes();

        ShaderNodeType& add(const std::string& id, const std::string& name, const std::string& category, const std::string& description,
                            std::vector<NodePin> inputs, std::vector<NodePin> outputs, std::vector<std::string> code);

        std::vector<std::unique_ptr<ShaderNodeType>> types;
        std::map<std::string, const ShaderNodeType*> byId;
        std::map<std::string, ShaderFunction> functions;
    };

}
