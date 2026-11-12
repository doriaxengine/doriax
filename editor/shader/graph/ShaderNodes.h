// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/NodeRegistry.h"

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

    // a GLSL helper some nodes call, written once a stage after the ones it needs
    struct ShaderFunction {
        std::vector<std::string> needs;
        std::string code;
    };

    // Pin types: float, vec2, vec3, vec4, int, bool, color (a vec4 that converts with w = 1), and
    // dynamic, a float or vector that takes the smallest vector size linked to the node's dynamic inputs
    struct ShaderNodeType : NodeType {
        // the GLSL of each output, in their order: {name} is an input or a setting, {T} the dynamic
        // type, @field a field of the hook's context, %name the hook's own value (surface, color)
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
        static const ShaderNodes& get();

        const NodeType* findType(const std::string& id) const override;
        std::vector<const NodeType*> getTypes() const override;

        const ShaderNodeType* find(const std::string& id) const;
        const ShaderFunction* findFunction(const std::string& name) const;

        // the pins of one node, which a Custom Function takes from its signature; false on a bad signature
        static bool getPins(const GraphNode& node, const ShaderNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error = nullptr);
        // the type a pin carries in this node: a parameter's own, a mask's size
        static std::string getPinType(const GraphDocument& graph, const GraphNode& node, const NodePin& pin);
        // numbers convert into each other; textures and bools only into their own type
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
