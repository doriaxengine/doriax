// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NodeCanvas.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>

using namespace doriax;

namespace {

    // graph units: pixels at zoom 1 on a screen of scale 1
    constexpr float headerHeight = 24.0f;
    constexpr float rowHeight = 20.0f;
    constexpr float padding = 9.0f;
    constexpr float pinRadius = 4.5f;
    constexpr float minWidth = 110.0f;
    constexpr float pinGap = 22.0f;
    constexpr float groupTitle = 22.0f;
    constexpr float minZoom = 0.2f;
    constexpr float maxZoom = 2.5f;

    const ImU32 highlight = IM_COL32(255, 190, 70, 255);

    float distanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        const ImVec2 ab(b.x - a.x, b.y - a.y);
        const float length = ab.x * ab.x + ab.y * ab.y;
        float t = length > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
        return std::sqrt((p.x - closest.x) * (p.x - closest.x) + (p.y - closest.y) * (p.y - closest.y));
    }

    // the same curve drawLink draws, for hit tests
    ImVec2 bezier(const ImVec2& from, const ImVec2& to, float t) {
        const float bend = std::max(std::abs(to.x - from.x) * 0.5f, 30.0f);
        const ImVec2 c1(from.x + bend, from.y);
        const ImVec2 c2(to.x - bend, to.y);
        const float u = 1.0f - t;
        return ImVec2(u * u * u * from.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * to.x,
                      u * u * u * from.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * to.y);
    }

    // in graph units, measured at the unzoomed size
    ImVec2 textSize(const std::string& text) {
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float scale = editor::Theme::dpiScale();
        return ImVec2(size.x / scale, size.y / scale);
    }

    float textWidth(const std::string& text) {
        return textSize(text).x;
    }

    bool inside(const ImVec2& point, const ImVec2& min, const ImVec2& max) {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

}

ImVec2 editor::NodeCanvas::toScreen(const ImVec2& point) const {
    const float scale = zoom * Theme::dpiScale();
    return ImVec2(origin.x + pan.x + point.x * scale, origin.y + pan.y + point.y * scale);
}

ImVec2 editor::NodeCanvas::toGraph(const ImVec2& point) const {
    const float scale = zoom * Theme::dpiScale();
    return ImVec2((point.x - origin.x - pan.x) / scale, (point.y - origin.y - pan.y) / scale);
}

ImVec2 editor::NodeCanvas::getViewCenter() const {
    return toGraph(ImVec2(origin.x + canvasSize.x * 0.5f, origin.y + canvasSize.y * 0.5f));
}

ImVec2 editor::NodeCanvas::getMousePosition() const {
    return toGraph(ImGui::GetIO().MousePos);
}

void editor::NodeCanvas::frame(bool selectionOnly) {
    framePending = true;
    frameSelectionOnly = selectionOnly;
    framedSize = ImVec2(0.0f, 0.0f);
}

void editor::NodeCanvas::layoutNodes(const CanvasModel& model) {
    layouts.clear();
    for (const CanvasNode& node : model.nodes) {
        float inputWidth = 0.0f;
        for (const CanvasPin& pin : node.inputs)
            inputWidth = std::max(inputWidth, textWidth(pin.label) + (pin.connected || pin.value.empty() ? 0.0f : textWidth(pin.value) + 8.0f));
        float outputWidth = 0.0f;
        for (const CanvasPin& pin : node.outputs)
            outputWidth = std::max(outputWidth, textWidth(pin.label));
        const float width = std::max({minWidth, textWidth(node.title) + padding * 2.0f, textWidth(node.subtitle) + padding * 2.0f,
                                      inputWidth + outputWidth + pinGap + padding * 2.0f});
        const float subtitle = node.subtitle.empty() ? 0.0f : rowHeight * 0.8f;
        const size_t rows = std::max(node.inputs.size(), node.outputs.size());

        NodeLayout layout;
        layout.id = node.id;
        layout.min = node.position;
        layout.max = ImVec2(node.position.x + width, node.position.y + headerHeight + subtitle + rows * rowHeight + padding * 0.6f);
        const float top = node.position.y + headerHeight + subtitle + rowHeight * 0.5f;
        for (size_t i = 0; i < node.inputs.size(); i++)
            layout.inputs.push_back(ImVec2(layout.min.x, top + i * rowHeight));
        for (size_t i = 0; i < node.outputs.size(); i++)
            layout.outputs.push_back(ImVec2(layout.max.x, top + i * rowHeight));
        layouts.push_back(layout);
    }
}

const editor::NodeCanvas::NodeLayout* editor::NodeCanvas::findLayout(int id) const {
    for (const NodeLayout& layout : layouts)
        if (layout.id == id)
            return &layout;
    return nullptr;
}

static bool isExecutionInput(const editor::CanvasModel& model, int node, const std::string& pin) {
    for (const editor::CanvasNode& item : model.nodes)
        if (item.id == node)
            for (const editor::CanvasPin& input : item.inputs)
                if (input.name == pin)
                    return input.execution;
    return false;
}

ImVec2 editor::NodeCanvas::pinPosition(const CanvasModel& model, int node, const std::string& pin, bool output) const {
    const NodeLayout* layout = findLayout(node);
    for (const CanvasNode& item : model.nodes) {
        if (item.id != node || !layout)
            continue;
        const std::vector<CanvasPin>& pins = output ? item.outputs : item.inputs;
        // a swizzle of an output leaves where that output does
        const std::string base = pin.substr(0, pin.find('.'));
        for (size_t i = 0; i < pins.size(); i++)
            if (pins[i].name == base)
                return output ? layout->outputs[i] : layout->inputs[i];
        if (output && !layout->outputs.empty())
            return layout->outputs.front();
    }
    return ImVec2(0.0f, 0.0f);
}

bool editor::NodeCanvas::pinAt(const CanvasModel& model, const ImVec2& point, int& node, std::string& pin, bool& output) const {
    const float reach = (pinRadius + 5.0f) / std::max(zoom, 0.5f);
    // topmost first, as nodeAt
    for (auto it = model.nodes.rbegin(); it != model.nodes.rend(); ++it) {
        const CanvasNode& item = *it;
        const NodeLayout* layout = findLayout(item.id);
        if (!layout)
            continue;
        for (int side = 0; side < 2; side++) {
            const std::vector<CanvasPin>& pins = side ? item.outputs : item.inputs;
            const std::vector<ImVec2>& positions = side ? layout->outputs : layout->inputs;
            for (size_t i = 0; i < pins.size(); i++) {
                const ImVec2 at = positions[i];
                // the label counts too, so a pin is easy to grab
                const bool row = std::abs(point.y - at.y) < rowHeight * 0.5f;
                const bool onPin = row && std::abs(point.x - at.x) < reach;
                const bool onLabel = row && (side ? (point.x > at.x - textWidth(pins[i].label) - padding && point.x <= at.x)
                                                  : (point.x >= at.x && point.x < at.x + textWidth(pins[i].label) + padding));
                if (onPin || onLabel) {
                    node = item.id;
                    pin = pins[i].name;
                    output = side == 1;
                    return true;
                }
            }
        }
    }
    return false;
}

int editor::NodeCanvas::nodeAt(const ImVec2& point) const {
    // the last drawn is on top
    for (auto it = layouts.rbegin(); it != layouts.rend(); ++it)
        if (inside(point, it->min, it->max))
            return it->id;
    return 0;
}

int editor::NodeCanvas::commentAt(const CanvasModel& model, const ImVec2& point) const {
    for (auto it = model.comments.rbegin(); it != model.comments.rend(); ++it) {
        const ImVec2 size = textSize(it->text);
        if (inside(point, it->position, ImVec2(it->position.x + size.x, it->position.y + size.y)))
            return it->id;
    }
    return 0;
}

int editor::NodeCanvas::groupTitleAt(const CanvasModel& model, const ImVec2& point) const {
    for (auto it = model.groups.rbegin(); it != model.groups.rend(); ++it)
        if (inside(point, it->position, ImVec2(it->position.x + it->size.x, it->position.y + groupTitle)))
            return it->id;
    return 0;
}

bool editor::NodeCanvas::linkAt(const CanvasModel& model, const ImVec2& point, const CanvasLink*& found) const {
    const float reach = 6.0f / zoom;
    for (const CanvasLink& link : model.links) {
        const ImVec2 from = pinPosition(model, link.fromNode, link.fromPin, true);
        const ImVec2 to = pinPosition(model, link.toNode, link.toPin, false);
        ImVec2 previous = from;
        for (int i = 1; i <= 16; i++) {
            const ImVec2 next = bezier(from, to, i / 16.0f);
            if (distanceToSegment(point, previous, next) < reach) {
                found = &link;
                return true;
            }
            previous = next;
        }
    }
    return false;
}

void editor::NodeCanvas::startMove(const CanvasModel& model, const ImVec2& point) {
    // a group carries what lies inside it
    moving = selection;
    for (const CanvasGroup& group : model.groups) {
        if (!selection.count(group.id))
            continue;
        const ImVec2 corner(group.position.x + group.size.x, group.position.y + group.size.y);
        for (const NodeLayout& layout : layouts)
            if (inside(layout.min, group.position, corner) && inside(layout.max, group.position, corner))
                moving.insert(layout.id);
        for (const CanvasComment& comment : model.comments)
            if (inside(comment.position, group.position, corner))
                moving.insert(comment.id);
    }
    gesture = Gesture::MOVE;
    gestureStart = point;
    moved = ImVec2(0.0f, 0.0f);
}

void editor::NodeCanvas::drawGrid(ImDrawList* drawList) const {
    const ImU32 minor = IM_COL32(255, 255, 255, 10);
    const ImU32 major = IM_COL32(255, 255, 255, 22);
    const float step = 20.0f * zoom * Theme::dpiScale();
    if (step < 6.0f)
        return;
    const ImVec2 end(origin.x + canvasSize.x, origin.y + canvasSize.y);
    const int firstX = (int)std::floor(-pan.x / step);
    const int firstY = (int)std::floor(-pan.y / step);
    for (int i = firstX; origin.x + pan.x + i * step < end.x; i++) {
        const float x = origin.x + pan.x + i * step;
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), i % 5 == 0 ? major : minor);
    }
    for (int i = firstY; origin.y + pan.y + i * step < end.y; i++) {
        const float y = origin.y + pan.y + i * step;
        drawList->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), i % 5 == 0 ? major : minor);
    }
}

void editor::NodeCanvas::drawLink(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) const {
    const ImVec2 a = toScreen(from);
    const ImVec2 b = toScreen(to);
    const float bend = std::max(std::abs(b.x - a.x) * 0.5f, 30.0f * zoom);
    drawList->AddBezierCubic(a, ImVec2(a.x + bend, a.y), ImVec2(b.x - bend, b.y), b, color, thickness * zoom * Theme::dpiScale());
}

void editor::NodeCanvas::drawNode(ImDrawList* drawList, const CanvasNode& node, const NodeLayout& layout) const {
    const float scale = zoom * Theme::dpiScale();
    const ImVec2 min = toScreen(layout.min);
    const ImVec2 max = toScreen(layout.max);
    const float rounding = 5.0f * scale;
    const bool selected = selection.count(node.id) > 0;

    drawList->AddRectFilled(ImVec2(min.x + 3 * scale, min.y + 4 * scale), ImVec2(max.x + 3 * scale, max.y + 4 * scale), IM_COL32(0, 0, 0, 70), rounding);
    drawList->AddRectFilled(min, max, IM_COL32(38, 39, 42, 240), rounding);
    drawList->AddRectFilled(min, ImVec2(max.x, min.y + headerHeight * scale), node.color, rounding, ImDrawFlags_RoundCornersTop);
    const ImU32 border = !node.error.empty() ? IM_COL32(230, 70, 60, 255) : (selected ? highlight : IM_COL32(15, 15, 15, 255));
    drawList->AddRect(min, max, border, rounding, 0, (selected || !node.error.empty() ? 2.0f : 1.0f) * Theme::dpiScale());
    // selection rings an error node's red outline
    if (selected && !node.error.empty()) {
        const float gap = 3.0f * Theme::dpiScale();
        drawList->AddRect(ImVec2(min.x - gap, min.y - gap), ImVec2(max.x + gap, max.y + gap), highlight, rounding + gap, 0, 2.0f * Theme::dpiScale());
    }

    // a data pin is a circle, an execution pin an arrow; filled when linked
    auto drawPin = [&](const CanvasPin& pin, const ImVec2& at, bool filled) {
        const float radius = pinRadius * scale;
        if (pin.execution) {
            const ImVec2 points[3] = {ImVec2(at.x - radius, at.y - radius), ImVec2(at.x + radius, at.y), ImVec2(at.x - radius, at.y + radius)};
            if (filled)
                drawList->AddTriangleFilled(points[0], points[1], points[2], pin.color);
            else
                drawList->AddTriangle(points[0], points[1], points[2], pin.color, 1.5f * Theme::dpiScale());
        } else if (filled) {
            drawList->AddCircleFilled(at, radius, pin.color);
        } else {
            drawList->AddCircle(at, radius, pin.color, 0, 1.5f * Theme::dpiScale());
        }
    };

    // far out, the boxes are enough
    if (zoom <= 0.35f) {
        for (size_t i = 0; i < node.inputs.size(); i++)
            drawPin(node.inputs[i], toScreen(layout.inputs[i]), true);
        for (size_t i = 0; i < node.outputs.size(); i++)
            drawPin(node.outputs[i], toScreen(layout.outputs[i]), true);
        return;
    }

    const float lineHeight = ImGui::GetTextLineHeight();
    drawList->AddText(ImVec2(min.x + padding * scale, min.y + (headerHeight * scale - lineHeight) * 0.5f), IM_COL32(240, 240, 240, 255), node.title.c_str());
    if (!node.subtitle.empty())
        drawList->AddText(ImVec2(min.x + padding * scale, min.y + headerHeight * scale + 2 * scale), IM_COL32(170, 170, 170, 255), node.subtitle.c_str());

    for (size_t i = 0; i < node.inputs.size(); i++) {
        const CanvasPin& pin = node.inputs[i];
        const ImVec2 at = toScreen(layout.inputs[i]);
        drawPin(pin, at, pin.connected);
        const ImVec2 labelAt(at.x + (pinRadius + 5.0f) * scale, at.y - lineHeight * 0.5f);
        drawList->AddText(labelAt, IM_COL32(215, 215, 215, 255), pin.label.c_str());
        if (!pin.connected && !pin.value.empty())
            drawList->AddText(ImVec2(labelAt.x + ImGui::CalcTextSize(pin.label.c_str()).x + 6.0f * scale, labelAt.y), IM_COL32(140, 160, 190, 255), pin.value.c_str());
    }
    for (size_t i = 0; i < node.outputs.size(); i++) {
        const CanvasPin& pin = node.outputs[i];
        const ImVec2 at = toScreen(layout.outputs[i]);
        drawPin(pin, at, pin.connected);
        const float width = ImGui::CalcTextSize(pin.label.c_str()).x;
        drawList->AddText(ImVec2(at.x - (pinRadius + 5.0f) * scale - width, at.y - lineHeight * 0.5f), IM_COL32(215, 215, 215, 255), pin.label.c_str());
    }
}

void editor::NodeCanvas::draw(const CanvasModel& model, std::vector<CanvasEdit>& edits, const char* id) {
    canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 1.0f);
    canvasSize.y = std::max(canvasSize.y, 1.0f);
    origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsWindowFocused();
    ImGuiIO& io = ImGui::GetIO();

    layoutNodes(model);

    // the graph, or the selection, centered
    if (framePending && !layouts.empty()) {
        ImVec2 low(1e9f, 1e9f), high(-1e9f, -1e9f);
        for (const NodeLayout& layout : layouts) {
            if (frameSelectionOnly && !selection.count(layout.id))
                continue;
            low = ImVec2(std::min(low.x, layout.min.x), std::min(low.y, layout.min.y));
            high = ImVec2(std::max(high.x, layout.max.x), std::max(high.y, layout.max.y));
        }
        if (low.x <= high.x) {
            const float scaleFit = std::min(canvasSize.x / (high.x - low.x + 80.0f), canvasSize.y / (high.y - low.y + 80.0f)) / Theme::dpiScale();
            zoom = std::clamp(scaleFit, minZoom, 1.0f);
            const float scale = zoom * Theme::dpiScale();
            pan = ImVec2(canvasSize.x * 0.5f - (low.x + high.x) * 0.5f * scale, canvasSize.y * 0.5f - (low.y + high.y) * 0.5f * scale);
        }
        framePending = canvasSize.x != framedSize.x || canvasSize.y != framedSize.y;
        framedSize = canvasSize;
        framed = !frameSelectionOnly;
    }

    const ImVec2 mouse = toGraph(io.MousePos);

    // zoom on the cursor
    if (hovered && io.MouseWheel != 0.0f) {
        const ImVec2 before = mouse;
        zoom = std::clamp(zoom * std::pow(1.12f, io.MouseWheel), minZoom, maxZoom);
        framed = false;
        const float scale = zoom * Theme::dpiScale();
        pan = ImVec2(io.MousePos.x - origin.x - before.x * scale, io.MousePos.y - origin.y - before.y * scale);
    }

    // pan with the middle button, or the right one dragged
    if (hovered && gesture == Gesture::NONE && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        gesture = Gesture::PAN;
        rightDragged = false;
    }
    if (gesture == Gesture::PAN) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f)) {
            pan = ImVec2(pan.x + io.MouseDelta.x, pan.y + io.MouseDelta.y);
            framed = framed && io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f;
            rightDragged = rightDragged || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !rightDragged) {
            // a right click asks for a node there
            CanvasEdit edit;
            edit.kind = CanvasEdit::Kind::ADD;
            edit.position = mouse;
            edits.push_back(edit);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) || ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            gesture = Gesture::NONE;
    }

    // left press: link, move, resize, pick a link or start a box
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gesture == Gesture::NONE) {
        int node = 0;
        std::string pin;
        bool output = true;
        const CanvasLink* link = nullptr;
        int picked = 0;
        if (pinAt(model, mouse, node, pin, output)) {
            gesture = Gesture::CONNECT;
            pinNode = node;
            pinName = pin;
            pinOutput = output;
            detachedLink.clear();
            // dragging from a fed input moves its link
            if (!output && !isExecutionInput(model, node, pin)) {
                for (const CanvasLink& existing : model.links) {
                    if (existing.toNode == node && existing.toPin == pin) {
                        detachedLink.set(existing);
                        pinNode = existing.fromNode;
                        pinName = existing.fromPin;
                        pinOutput = true;
                        break;
                    }
                }
            }
        } else if ((picked = nodeAt(mouse)) != 0 || (picked = commentAt(model, mouse)) != 0 || (picked = groupTitleAt(model, mouse)) != 0) {
            if (io.KeyCtrl || io.KeyShift) {
                if (!selection.erase(picked))
                    selection.insert(picked);
            } else if (!selection.count(picked)) {
                selection = {picked};
            }
            selectedLink.clear();
            startMove(model, mouse);
        } else if (linkAt(model, mouse, link)) {
            selection.clear();
            selectedLink.set(*link);
        } else {
            for (const CanvasGroup& item : model.groups) {
                const ImVec2 corner(item.position.x + item.size.x, item.position.y + item.size.y);
                if (std::abs(mouse.x - corner.x) < 10.0f / zoom && std::abs(mouse.y - corner.y) < 10.0f / zoom) {
                    gesture = Gesture::RESIZE;
                    resizeGroup = item.id;
                    resizeSize = item.size;
                    break;
                }
            }
            if (gesture == Gesture::NONE) {
                if (!io.KeyCtrl && !io.KeyShift)
                    selection.clear();
                selectedLink.clear();
                gesture = Gesture::BOX;
                gestureStart = mouse;
            }
        }
    }

    if (gesture == Gesture::MOVE && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const float scale = zoom * Theme::dpiScale();
        moved = ImVec2(moved.x + io.MouseDelta.x / scale, moved.y + io.MouseDelta.y / scale);
    }
    if (gesture == Gesture::RESIZE && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const float scale = zoom * Theme::dpiScale();
        resizeSize = ImVec2(std::max(resizeSize.x + io.MouseDelta.x / scale, 80.0f), std::max(resizeSize.y + io.MouseDelta.y / scale, 60.0f));
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && gesture != Gesture::PAN) {
        if (gesture == Gesture::MOVE && (std::abs(moved.x) > 0.5f || std::abs(moved.y) > 0.5f)) {
            CanvasEdit edit;
            edit.kind = CanvasEdit::Kind::MOVE;
            edit.ids.assign(moving.begin(), moving.end());
            edit.delta = moved;
            edits.push_back(edit);
        } else if (gesture == Gesture::RESIZE) {
            CanvasEdit edit;
            edit.kind = CanvasEdit::Kind::RESIZE_GROUP;
            edit.ids = {resizeGroup};
            edit.size = resizeSize;
            edits.push_back(edit);
        } else if (gesture == Gesture::CONNECT) {
            int node = 0;
            std::string pin;
            bool output = true;
            const bool onPin = pinAt(model, mouse, node, pin, output);
            const bool backHome = onPin && !output && node == detachedLink.toNode && pin == detachedLink.toPin;
            // a link taken off an input and dropped anywhere else leaves it
            if (!detachedLink.empty() && !backHome) {
                edits.push_back(detachedLink.disconnect());
            }
            if (onPin && !backHome && output != pinOutput && node != pinNode) {
                CanvasEdit edit;
                edit.kind = CanvasEdit::Kind::CONNECT;
                edit.fromNode = output ? node : pinNode;
                edit.fromPin = output ? pin : pinName;
                edit.toNode = output ? pinNode : node;
                edit.toPin = output ? pinName : pin;
                edits.push_back(edit);
            } else if (!onPin && detachedLink.empty() && nodeAt(mouse) == 0) {
                // dropped on nothing: a node to take the link
                CanvasEdit edit;
                edit.kind = CanvasEdit::Kind::ADD;
                edit.position = mouse;
                edit.fromNode = pinNode;
                edit.fromPin = pinName;
                edit.fromOutput = pinOutput;
                edits.push_back(edit);
            }
            detachedLink.clear();
        } else if (gesture == Gesture::BOX) {
            const ImVec2 low(std::min(gestureStart.x, mouse.x), std::min(gestureStart.y, mouse.y));
            const ImVec2 high(std::max(gestureStart.x, mouse.x), std::max(gestureStart.y, mouse.y));
            for (const NodeLayout& layout : layouts)
                if (layout.max.x >= low.x && layout.min.x <= high.x && layout.max.y >= low.y && layout.min.y <= high.y)
                    selection.insert(layout.id);
            for (const CanvasComment& comment : model.comments)
                if (inside(comment.position, low, high))
                    selection.insert(comment.id);
        }
        gesture = Gesture::NONE;
    }

    // double click: a reroute on a link, open a node
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const CanvasLink* link = nullptr;
        if (const int node = nodeAt(mouse)) {
            CanvasEdit edit;
            edit.kind = CanvasEdit::Kind::OPEN;
            edit.ids = {node};
            edits.push_back(edit);
        } else if (linkAt(model, mouse, link)) {
            CanvasEdit edit;
            edit.kind = CanvasEdit::Kind::REROUTE;
            edit.toNode = link->toNode;
            edit.toPin = link->toPin;
            edit.position = mouse;
            edits.push_back(edit);
        }
    }

    // keys act on the selection while the canvas has the focus
    if (focused && !io.WantTextInput && gesture == Gesture::NONE) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (!selection.empty()) {
                CanvasEdit edit;
                edit.kind = CanvasEdit::Kind::REMOVE;
                edit.ids.assign(selection.begin(), selection.end());
                edits.push_back(edit);
                selection.clear();
            } else if (!selectedLink.empty()) {
                edits.push_back(selectedLink.disconnect());
                selectedLink.clear();
            }
        }
        auto shortcut = [&](ImGuiKey key, CanvasEdit::Kind kind) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(key, false)) {
                CanvasEdit edit;
                edit.kind = kind;
                edit.ids.assign(selection.begin(), selection.end());
                edit.position = hovered ? mouse : getViewCenter();
                edits.push_back(edit);
            }
        };
        shortcut(ImGuiKey_C, CanvasEdit::Kind::COPY);
        shortcut(ImGuiKey_X, CanvasEdit::Kind::CUT);
        shortcut(ImGuiKey_V, CanvasEdit::Kind::PASTE);
        shortcut(ImGuiKey_D, CanvasEdit::Kind::CLONE);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            frame(!selection.empty());
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            selection.clear();
            for (const NodeLayout& layout : layouts)
                selection.insert(layout.id);
        }
    }

    // ---- drawing, back to front

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + canvasSize.x, origin.y + canvasSize.y);
    drawList->PushClipRect(origin, end, true);
    drawList->AddRectFilled(origin, end, IM_COL32(28, 29, 32, 255));
    drawGrid(drawList);

    // text follows the zoom
    const float scale = zoom * Theme::dpiScale();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom);
    const bool readable = zoom > 0.35f;

    for (const CanvasGroup& group : model.groups) {
        ImVec2 position = group.position;
        const ImVec2 size = (gesture == Gesture::RESIZE && resizeGroup == group.id) ? resizeSize : group.size;
        if (gesture == Gesture::MOVE && moving.count(group.id))
            position = ImVec2(position.x + moved.x, position.y + moved.y);
        const ImVec2 min = toScreen(position);
        const ImVec2 max = toScreen(ImVec2(position.x + size.x, position.y + size.y));
        drawList->AddRectFilled(min, max, group.color, 6.0f * scale);
        drawList->AddRectFilled(min, ImVec2(max.x, min.y + groupTitle * scale), (group.color & 0x00FFFFFF) | 0x50000000, 6.0f * scale, ImDrawFlags_RoundCornersTop);
        drawList->AddRect(min, max, selection.count(group.id) ? highlight : IM_COL32(255, 255, 255, 40), 6.0f * scale);
        if (readable)
            drawList->AddText(ImVec2(min.x + 8 * scale, min.y + 3 * scale), IM_COL32(230, 230, 230, 255), group.title.c_str());
        drawList->AddTriangleFilled(ImVec2(max.x - 10 * scale, max.y), max, ImVec2(max.x, max.y - 10 * scale), IM_COL32(255, 255, 255, 60));
    }
    for (const CanvasComment& comment : model.comments) {
        ImVec2 position = comment.position;
        if (gesture == Gesture::MOVE && moving.count(comment.id))
            position = ImVec2(position.x + moved.x, position.y + moved.y);
        if (readable)
            drawList->AddText(toScreen(position), selection.count(comment.id) ? highlight : IM_COL32(200, 200, 160, 255), comment.text.c_str());
    }

    // nodes being moved are drawn where they will land
    if (gesture == Gesture::MOVE) {
        for (NodeLayout& layout : layouts) {
            if (!moving.count(layout.id))
                continue;
            layout.min = ImVec2(layout.min.x + moved.x, layout.min.y + moved.y);
            layout.max = ImVec2(layout.max.x + moved.x, layout.max.y + moved.y);
            for (ImVec2& pin : layout.inputs)
                pin = ImVec2(pin.x + moved.x, pin.y + moved.y);
            for (ImVec2& pin : layout.outputs)
                pin = ImVec2(pin.x + moved.x, pin.y + moved.y);
        }
    }

    for (const CanvasLink& link : model.links) {
        if (detachedLink.is(link))
            continue;
        const bool picked = selectedLink.is(link);
        drawLink(drawList, pinPosition(model, link.fromNode, link.fromPin, true), pinPosition(model, link.toNode, link.toPin, false),
                 picked ? highlight : link.color, picked ? 3.0f : link.thickness);
    }
    if (gesture == Gesture::CONNECT) {
        const ImVec2 start = pinPosition(model, pinNode, pinName, pinOutput);
        drawLink(drawList, pinOutput ? start : mouse, pinOutput ? mouse : start, IM_COL32(255, 255, 255, 200), 2.0f);
    }

    for (const CanvasNode& node : model.nodes)
        if (const NodeLayout* layout = findLayout(node.id))
            drawNode(drawList, node, *layout);
    ImGui::PopFont();

    if (gesture == Gesture::BOX) {
        const ImVec2 a = toScreen(gestureStart);
        const ImVec2 b = io.MousePos;
        const ImVec2 low(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 high(std::max(a.x, b.x), std::max(a.y, b.y));
        drawList->AddRectFilled(low, high, IM_COL32(255, 190, 70, 30));
        drawList->AddRect(low, high, IM_COL32(255, 190, 70, 160));
    }
    drawList->PopClipRect();

    // what a node's error says, on hover
    if (hovered && gesture == Gesture::NONE) {
        const int node = nodeAt(mouse);
        for (const CanvasNode& item : model.nodes)
            if (item.id == node && !item.error.empty())
                ImGui::SetTooltip("%s", item.error.c_str());
    }
}
