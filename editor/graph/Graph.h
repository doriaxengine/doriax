// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "math/Vector2.h"
#include "math/Vector4.h"

#include <map>
#include <string>
#include <vector>

namespace doriax::editor {

    // a constant, a default or a node setting: numbers, or a word such as white
    struct GraphValue {
        std::vector<float> numbers;
        std::string text;

        GraphValue() = default;
        GraphValue(float value): numbers{value} {}
        GraphValue(std::vector<float> values): numbers(std::move(values)) {}
        GraphValue(const char* word): text(word) {}
        GraphValue(std::string word): text(std::move(word)) {}

        bool empty() const { return numbers.empty() && text.empty(); }
        float number(size_t index = 0) const { return index < numbers.size() ? numbers[index] : 0.0f; }

        bool operator==(const GraphValue& other) const { return numbers == other.numbers && text == other.text; }
        bool operator!=(const GraphValue& other) const { return !(*this == other); }
    };

    struct GraphParameter {
        int id = 0;
        std::string name;
        std::string type;            // float, vec2, vec3, vec4, int, color, texture2D, textureCube
        GraphValue value;            // the default; white, black or normal for a texture
        bool hasRange = false;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        std::string filter;          // a texture's: nearest, linear or trilinear
        std::string wrap;            // repeat, clamp or mirror
        bool runtime = false;        // set by scripts, not shown for editing
        std::string description;
    };

    struct GraphNode {
        int id = 0;
        std::string type;                             // stable, as math/multiply
        Vector2 position;
        std::map<std::string, GraphValue> settings;   // what its type declares, as set: 2
        std::map<std::string, GraphValue> inputs;     // constants of unconnected inputs
    };

    // from a node's output, or from a parameter when the pin is empty
    struct GraphLink {
        int fromNode = 0;
        std::string fromPin;
        int toNode = 0;
        std::string toPin;
    };

    struct GraphGroup {
        int id = 0;
        std::string title;
        Vector4 rect;                // x, y, width, height
        Vector4 color = Vector4(1.0f, 1.0f, 1.0f, 0.15f);
    };

    struct GraphComment {
        int id = 0;
        std::string text;
        Vector2 position;
    };

    // a graph file: the envelope every kind shares, then its graph
    class GraphDocument {
    public:
        static constexpr int formatVersion = 1;

        int version = formatVersion;
        std::string kind;            // shader or logic
        std::string type;            // what it fills, as mesh; script for a logic graph
        int nextId = 1;              // ids are shared by parameters, nodes, groups and comments, never reused
        std::vector<GraphParameter> parameters;
        std::vector<GraphNode> nodes;
        std::vector<GraphLink> links;
        std::vector<GraphGroup> groups;
        std::vector<GraphComment> comments;

        bool read(const std::string& text, std::string& error);
        // sorted, so the same graph always gives the same bytes
        std::string write() const;

        int takeId();

        // edits, as the canvas and the MCP tools make them
        int addNode(const std::string& nodeType, const Vector2& position);
        // with the links that touch them
        void removeNodes(const std::vector<int>& ids);
        void moveNodes(const std::vector<int>& ids, const Vector2& delta);
        // an input takes one link, so this replaces the one there
        void connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
        // execution leaves an output one way, and enters an input from any number
        void connectExecution(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
        void disconnect(int toNode, const std::string& toPin);
        void disconnect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
        int addParameter(const GraphParameter& parameter);
        // with its links and the nodes' settings that name it cleared
        void removeParameter(int id);

        GraphNode* findNode(int id);
        const GraphNode* findNode(int id) const;
        const GraphParameter* findParameter(int id) const;
        const GraphParameter* findParameter(const std::string& name) const;
        // what feeds an input, null when nothing does
        const GraphLink* findLinkInto(int node, const std::string& pin) const;
        // whether a node's outputs reach another, so a link back would close a loop
        bool feeds(int from, int to) const;
    };

}
