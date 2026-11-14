// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "imgui.h"

#include <set>
#include <string>
#include <vector>

namespace doriax::editor {

    struct CanvasPin {
        std::string name;
        std::string label;
        ImU32 color = IM_COL32(200, 200, 200, 255);
        bool connected = false;
        bool execution = false;      // drawn as an arrow
        std::string value;           // shown beside an input nothing feeds
    };

    struct CanvasNode {
        int id = 0;
        std::string title;
        std::string subtitle;
        ImU32 color = IM_COL32(80, 80, 80, 255);
        ImVec2 position;             // graph space
        std::vector<CanvasPin> inputs;
        std::vector<CanvasPin> outputs;
        std::string error;
    };

    struct CanvasLink {
        int fromNode = 0;
        std::string fromPin;
        int toNode = 0;
        std::string toPin;
        ImU32 color = IM_COL32(200, 200, 200, 255);
        float thickness = 2.0f;
    };

    struct CanvasGroup {
        int id = 0;
        std::string title;
        ImVec2 position;
        ImVec2 size;
        ImU32 color = IM_COL32(255, 255, 255, 40);
    };

    struct CanvasComment {
        int id = 0;
        std::string text;
        ImVec2 position;
    };

    // what the canvas draws, rebuilt each frame
    struct CanvasModel {
        std::vector<CanvasNode> nodes;
        std::vector<CanvasLink> links;
        std::vector<CanvasGroup> groups;
        std::vector<CanvasComment> comments;
    };

    // a canvas action, for the window to apply
    struct CanvasEdit {
        enum class Kind {
            MOVE,            // ids by delta
            CONNECT,         // from node.pin to node.pin
            DISCONNECT,      // the link from node.pin to node.pin
            REMOVE,          // ids, with the links they hold
            ADD,             // a node wanted at position; from node.pin when a link was dropped on nothing
            REROUTE,         // a reroute node on the link into toNode.toPin, at position
            COPY,
            CUT,
            PASTE,           // at position
            CLONE,
            RESIZE_GROUP,    // id to size
            OPEN             // a double click on node id
        };

        Kind kind = Kind::MOVE;
        std::vector<int> ids;
        ImVec2 delta;
        ImVec2 position;
        ImVec2 size;
        int fromNode = 0;
        std::string fromPin;
        bool fromOutput = true;      // of an ADD from a dropped link: which side it left
        int toNode = 0;
        std::string toPin;
    };

    // pan, zoom, selection and the gesture in progress of one open graph
    class NodeCanvas {
    public:
        void draw(const CanvasModel& model, std::vector<CanvasEdit>& edits, const char* id);

        const std::set<int>& getSelection() const { return selection; }
        void select(const std::set<int>& ids) { selection = ids; }
        // the whole graph, or the selection, in view
        void frame(bool selectionOnly = false);
        // whether the view still shows the whole graph as frame() left it
        bool isFramed() const { return framed; }
        // the graph changed shape under the view
        void unframe() { framed = false; }
        ImVec2 getViewCenter() const;
        ImVec2 getMousePosition() const;

    private:
        enum class Gesture {
            NONE,
            PAN,
            MOVE,
            CONNECT,
            BOX,
            RESIZE
        };

        // a link by its ends; none while toNode is 0
        struct LinkEnds {
            int fromNode = 0;
            std::string fromPin;
            int toNode = 0;
            std::string toPin;

            bool empty() const { return toNode == 0; }
            void clear() { *this = LinkEnds(); }
            void set(const CanvasLink& link) {
                fromNode = link.fromNode;
                fromPin = link.fromPin;
                toNode = link.toNode;
                toPin = link.toPin;
            }
            bool is(const CanvasLink& link) const {
                return toNode == link.toNode && toPin == link.toPin && fromNode == link.fromNode && fromPin == link.fromPin;
            }
            CanvasEdit disconnect() const {
                CanvasEdit edit;
                edit.kind = CanvasEdit::Kind::DISCONNECT;
                edit.fromNode = fromNode;
                edit.fromPin = fromPin;
                edit.toNode = toNode;
                edit.toPin = toPin;
                return edit;
            }
        };

        struct NodeLayout {
            int id = 0;
            ImVec2 min;              // graph space
            ImVec2 max;
            std::vector<ImVec2> inputs;
            std::vector<ImVec2> outputs;
        };

        ImVec2 pan = ImVec2(0.0f, 0.0f);
        float zoom = 1.0f;
        ImVec2 origin;               // the canvas's top left on screen
        ImVec2 canvasSize;
        bool framePending = true;
        bool framed = false;
        bool frameSelectionOnly = false;
        ImVec2 framedSize;           // framing goes on while the window settles its size

        std::set<int> selection;
        LinkEnds selectedLink;

        Gesture gesture = Gesture::NONE;
        ImVec2 gestureStart;         // graph space
        ImVec2 moved;
        std::set<int> moving;        // the selection and what its groups hold
        int pinNode = 0;
        std::string pinName;
        bool pinOutput = true;
        LinkEnds detachedLink;       // the link taken off an input to be dropped elsewhere
        int resizeGroup = 0;
        ImVec2 resizeSize;
        bool rightDragged = false;

        std::vector<NodeLayout> layouts;

        ImVec2 toScreen(const ImVec2& point) const;
        ImVec2 toGraph(const ImVec2& point) const;
        void layoutNodes(const CanvasModel& model);
        const NodeLayout* findLayout(int id) const;
        // the pin under the cursor, false when none
        bool pinAt(const CanvasModel& model, const ImVec2& graphPoint, int& node, std::string& pin, bool& output) const;
        int nodeAt(const ImVec2& graphPoint) const;
        int commentAt(const CanvasModel& model, const ImVec2& graphPoint) const;
        int groupTitleAt(const CanvasModel& model, const ImVec2& graphPoint) const;
        bool linkAt(const CanvasModel& model, const ImVec2& graphPoint, const CanvasLink*& link) const;
        ImVec2 pinPosition(const CanvasModel& model, int node, const std::string& pin, bool output) const;
        void startMove(const CanvasModel& model, const ImVec2& graphPoint);

        void drawGrid(ImDrawList* drawList) const;
        void drawLink(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) const;
        void drawNode(ImDrawList* drawList, const CanvasNode& node, const NodeLayout& layout) const;
    };

}
