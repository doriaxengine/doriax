// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "UIUtils.h"
#include "App.h"
#include "FileUtils.h"
#include "external/IconsFontAwesome6.h"
#include "window/widget/InputTextContextMenu.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace doriax::editor {

// Symlinks are skipped so browsing cannot leave the tree it started in.
static bool isBrowsableDir(const std::filesystem::directory_entry& entry) {
    if (!FileUtils::isNarrowPath(entry.path())) return false;
    std::string name = entry.path().filename().string();
    return !name.empty() && name[0] != '.' && entry.is_directory() && !entry.is_symlink();
}

void UIUtils::directoryTreeBrowser(const std::filesystem::path& currentPath,
                                   std::string& selectedPath) {
    try {
        std::vector<std::filesystem::path> subDirs;
        for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
            if (isBrowsableDir(entry))
                subDirs.push_back(entry.path());
        }
        std::sort(subDirs.begin(), subDirs.end());

        for (const auto& dirPath : subDirs) {
            std::string name = FileUtils::pathToUtf8(dirPath.filename());

            ImGui::PushID(dirPath.string().c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_SpanFullWidth;
            if (std::filesystem::path(selectedPath).lexically_normal() == dirPath.lexically_normal())
                flags |= ImGuiTreeNodeFlags_Selected;

            bool hasSubdirectory = false;
            try {
                for (const auto& child : std::filesystem::directory_iterator(dirPath)) {
                    if (isBrowsableDir(child)) {
                        hasSubdirectory = true;
                        break;
                    }
                }
            } catch (...) {}
            if (!hasSubdirectory)
                flags |= ImGuiTreeNodeFlags_Leaf;

            bool open = ImGui::TreeNodeEx("##dir", flags);
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "%s",
                               open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER);
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemClicked() ||
                    (ImGui::IsMouseClicked(0) &&
                     ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
                selectedPath = dirPath.lexically_normal().string();
            }

            if (open) {
                if (hasSubdirectory)
                    directoryTreeBrowser(dirPath, selectedPath);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error reading directory");
    }
}

bool UIUtils::searchInput(const char* id, std::string hint, char* buffer, size_t bufferSize, bool autoFocus, bool* matchCase, float fixedWidth) {
    ImGui::BeginGroup();

    ImGuiStyle& style = ImGui::GetStyle();

    // Calculate icon button width
    ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_MAGNIFYING_GLASS);
    float buttonWidth = iconSize.x + style.FramePadding.x * 2.0f;

    // Calculate input field width
    float totalWidth = (fixedWidth > 0.0f) ? fixedWidth : ImGui::GetContentRegionAvail().x;
    float inputWidth = totalWidth - buttonWidth;
    if (inputWidth < 50.0f) inputWidth = 50.0f; // Minimum width

    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
    ImGui::PushItemWidth(inputWidth);

    if (autoFocus) {
        ImGui::SetKeyboardFocusHere();
    }

    bool changed = false;
    if (hint.empty()) {
        changed = ImGui::InputText(id, buffer, bufferSize);
    } else {
        changed = ImGui::InputTextWithHint(id, hint.c_str(), buffer, bufferSize);
    }
    ImGuiID inputId = ImGui::GetItemID();
    ImRect inputRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();

    changed = InputTextContextMenu::drawForLastItem(buffer, bufferSize) || changed;

    // Button on the same line with no spacing
    ImGui::SameLine(0.0f, 0.0f);

    // Style the button to match the input field
    ImVec4 bgColor = style.Colors[ImGuiCol_FrameBg];
    ImVec4 bgHoveredColor = style.Colors[ImGuiCol_FrameBgHovered];
    ImVec4 bgActiveColor = style.Colors[ImGuiCol_FrameBgActive];

    // Determine icon text color based on match case
    bool isMatchCase = (matchCase != nullptr) && (*matchCase);
    ImVec4 iconTextColor = isMatchCase ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled];

    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHoveredColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgActiveColor);
    ImGui::PushStyleColor(ImGuiCol_Text, iconTextColor);

    // Create unique popup ID based on the input ID
    std::string popupId = std::string("SearchOptions") + id;
    std::string buttonId = std::string(ICON_FA_MAGNIFYING_GLASS) + "##SearchButton" + id;

    if (ImGui::Button(buttonId.c_str(), ImVec2(buttonWidth, 0.0f))) {
        ImGui::OpenPopup(popupId.c_str());
    }
    ImGuiID iconButtonId = ImGui::GetItemID();
    ImRect iconButtonRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImGui::PopStyleColor(4);
    ImGui::PopStyleColor();

    ImRect searchRect = inputRect;
    searchRect.Add(iconButtonRect);
    ImGui::RenderNavCursor(searchRect, inputId);
    ImGui::RenderNavCursor(searchRect, iconButtonId);

    // Context menu for search options
    if (ImGui::BeginPopup(popupId.c_str())) {
        if (matchCase) {
            ImGui::Checkbox(ICON_FA_SPELL_CHECK "  Match Case", matchCase);
        } else {
            // Show disabled checkbox if matchCase is not provided
            bool dummyMatchCase = false;
            ImGui::BeginDisabled();
            ImGui::Text("No options available");
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
    }

    ImGui::EndGroup();

    return changed;
}

bool UIUtils::sliderFloatInput(const char* id, float* value, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    // Only one widget can be text-edited at a time (it holds keyboard focus), so shared statics are
    // enough to remember which slider was double-clicked into an input field and its pre-click value.
    static ImGuiID editingId = 0;
    static bool focusPending = false;
    static float clickValue = 0.0f;
    static int editFrame = -1; // ImGui frame count when the edit target was last drawn

    const ImGuiID widgetId = ImGui::GetID(id);
    const int frame = ImGui::GetFrameCount();

    // If the edit target was not drawn on the previous frame, its input field was torn down without a
    // deactivation event (row collapsed, panel switched, or hidden while focus was still pending).
    // Cancel the edit and fall back to the slider; clearing focusPending too avoids stealing focus
    // when the row reappears.
    if (editingId == widgetId && frame - editFrame > 1) {
        editingId = 0;
        focusPending = false;
    }

    bool changed = false;

    if (editingId == widgetId) {
        editFrame = frame;
        // Double-clicked: type the exact value. InputFloat auto-selects its text on activation and
        // does not clamp, so values outside [minValue, maxValue] can be entered.
        if (focusPending) {
            ImGui::SetKeyboardFocusHere();
            focusPending = false;
        }
        // Strip any decorations (icons/labels) from the slider's display format so the text field
        // shows just the number, matching how ImGui's own Ctrl+Click temp-input behaves.
        char inputFormat[32];
        changed = inputFloat(id, value, 0.0f, 0.0f, ImParseFormatTrimDecorations(format, inputFormat, IM_ARRAYSIZE(inputFormat)));
        if (ImGui::IsItemDeactivated()) {
            editingId = 0;
        }
    } else {
        const float before = *value;
        changed = sliderFloat(id, value, minValue, maxValue, format, flags);

        // Capture the value from before this click sequence so a double-click can restore it.
        // Sliders seek to the cursor on click, so the slider behaves normally (click-to-seek and
        // drag both move the value). On the second click of a double-click the slider re-activates
        // after the first click already moved the value, so skip that re-capture and keep the
        // value from before the first click.
        if (ImGui::IsItemActivated() && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            clickValue = before;
        }

        // Double-click opens the text field to type an exact value. Restore the value from before
        // the double-click so opening the editor discards the two clicks' seek, and report the
        // restore as a change so an undoing caller merges the transient seek away into the value
        // that is ultimately typed instead of leaving a stray undo step at the clicked position.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            editingId = widgetId;
            focusPending = true;
            editFrame = frame; // mark handled this frame so the teardown guard waits for a real gap
            if (*value != clickValue) {
                *value = clickValue;
                changed = true;
            } else {
                changed = false;
            }
        }
    }

    return changed;
}

namespace {

// Parses + - * / and parentheses over numbers
struct MathParser {
    const char* p;
    int operations = 0;
    int signs = 0;

    void skipSpaces() {
        while (*p == ' ')
            p++;
    }

    bool factor(double& out) {
        skipSpaces();
        if (*p == '+' || *p == '-') {
            const bool negative = (*p++ == '-');
            signs++;
            if (!factor(out))
                return false;
            if (negative)
                out = -out;
            return true;
        }
        if (*p == '(') {
            p++;
            operations++;
            if (!expression(out))
                return false;
            skipSpaces();
            if (*p != ')')
                return false;
            p++;
            return true;
        }
        if (!std::isdigit(static_cast<unsigned char>(*p)) && *p != '.')
            return false;
        char* end = nullptr;
        out = std::strtod(p, &end);
        if (end == p)
            return false;
        p = end;
        return true;
    }

    bool term(double& out) {
        if (!factor(out))
            return false;
        while (true) {
            skipSpaces();
            const char op = *p;
            if (op != '*' && op != '/')
                return true;
            p++;
            operations++;
            double rhs;
            if (!factor(rhs))
                return false;
            out = (op == '*') ? out * rhs : out / rhs;
        }
    }

    bool expression(double& out) {
        if (!term(out))
            return false;
        while (true) {
            skipSpaces();
            const char op = *p;
            if (op != '+' && op != '-')
                return true;
            p++;
            operations++;
            double rhs;
            if (!term(rhs))
                return false;
            out = (op == '+') ? out + rhs : out - rhs;
        }
    }
};

enum class TypedValue { Number, Expression, Invalid };

// One number field of a widget, with its value from before the widget ran
struct NumberField {
    ImGuiID id = 0;
    bool tempInput = false;
    ImGuiDataType type = ImGuiDataType_Float;
    void* data = nullptr;
    const char* format = nullptr;
    const void* minValue = nullptr;
    const void* maxValue = nullptr;
    ImGuiDataTypeStorage before;
};

struct TypedText {
    ImGuiID id;
    std::string text;
    int frame;
};

// Text of the fields being typed into, until they apply it
std::vector<TypedText> typedTexts;

}

static void trimSpaces(std::string& text) {
    text.erase(0, text.find_first_not_of(' '));
    text.erase(text.find_last_not_of(' ') + 1);
}

// Text the format prints around the number, like the " s" of "%.2f s"
static std::string decoration(const char* begin, const char* end) {
    std::string text;
    for (const char* c = begin; c < end; c++) {
        text += *c;
        if (c[0] == '%' && c + 1 < end && c[1] == '%')
            c++;
    }
    trimSpaces(text);
    return text;
}

// Input boxes keep the format decorations, so "20.00 s" must read as 20
static std::string stripDecorations(std::string text, const char* format) {
    const char* start = ImParseFormatFindStart(format);
    if (*start != '%')
        return text; // literal format, nothing to strip
    const char* end = ImParseFormatFindEnd(start);
    const std::string prefix = decoration(format, start);
    const std::string suffix = decoration(end, end + strlen(end));

    trimSpaces(text);
    if (!prefix.empty() && text.compare(0, prefix.size(), prefix) == 0)
        text.erase(0, prefix.size());
    if (!suffix.empty() && text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0)
        text.erase(text.size() - suffix.size());
    return text;
}

static TypedValue evaluateTyped(const char* text, double& result) {
    MathParser parser{text};
    if (!parser.expression(result))
        return TypedValue::Invalid;
    parser.skipSpaces();
    if (*parser.p != '\0' || !std::isfinite(result))
        return TypedValue::Invalid;
    // A plain number is left to ImGui
    if (parser.operations == 0 && parser.signs <= 1)
        return TypedValue::Number;
    return TypedValue::Expression;
}

template <typename T>
static void storeInteger(void* data, double value) {
    const double limited = std::clamp(std::round(value), static_cast<double>(std::numeric_limits<T>::lowest()), static_cast<double>(std::numeric_limits<T>::max()));
    *static_cast<T*>(data) = static_cast<T>(limited);
}

static bool storeNumber(ImGuiDataType type, void* data, double value) {
    switch (type) {
        case ImGuiDataType_S8:     storeInteger<ImS8>(data, value); return true;
        case ImGuiDataType_U8:     storeInteger<ImU8>(data, value); return true;
        case ImGuiDataType_S16:    storeInteger<ImS16>(data, value); return true;
        case ImGuiDataType_U16:    storeInteger<ImU16>(data, value); return true;
        case ImGuiDataType_S32:    storeInteger<ImS32>(data, value); return true;
        case ImGuiDataType_U32:    storeInteger<ImU32>(data, value); return true;
        case ImGuiDataType_Float:  *static_cast<float*>(data) = static_cast<float>(std::clamp(value, -static_cast<double>(FLT_MAX), static_cast<double>(FLT_MAX))); return true;
        case ImGuiDataType_Double: *static_cast<double*>(data) = value; return true;
        default: return false; // 64-bit integers do not fit a double
    }
}

static NumberField numberField(ImGuiID id, bool tempInput, ImGuiDataType type, void* data, const char* format, const void* minValue = nullptr, const void* maxValue = nullptr) {
    NumberField field;
    field.id = id;
    field.tempInput = tempInput;
    field.type = type;
    field.data = data;
    field.format = format ? format : ImGui::DataTypeGetInfo(type)->PrintFmt;
    field.minValue = minValue;
    field.maxValue = maxValue;
    if (minValue && maxValue && ImGui::DataTypeCompare(type, minValue, maxValue) > 0)
        std::swap(field.minValue, field.maxValue);
    memcpy(&field.before, data, ImGui::DataTypeGetInfo(type)->Size);
    return field;
}

// Gives the typed text once, on the frame the field applies it (Enter, Tab or click away)
static bool finishedTyping(const NumberField& field, std::string& text) {
    if (field.id == 0)
        return false;

    ImGuiContext& g = *GImGui;
    auto it = std::find_if(typedTexts.begin(), typedTexts.end(), [&](const TypedText& t) { return t.id == field.id; });

    // Drags and sliders share their id with the text box, TempInputId tells typing from dragging
    const bool typing = g.ActiveId == field.id && g.InputTextState.ID == field.id && (!field.tempInput || g.TempInputId == field.id);
    if (typing) {
        if (it != typedTexts.end()) {
            it->text = g.InputTextState.GetText();
            it->frame = g.FrameCount;
        } else {
            typedTexts.erase(std::remove_if(typedTexts.begin(), typedTexts.end(), [&](const TypedText& t) { return g.FrameCount - t.frame > 1; }), typedTexts.end());
            typedTexts.push_back({field.id, g.InputTextState.GetText(), g.FrameCount});
        }
        return false;
    }

    if (it == typedTexts.end())
        return false;
    const TypedText typed = *it;
    typedTexts.erase(it);
    if (g.FrameCount - typed.frame > 1)
        return false;

    // The text box keeps the final text unless another one took it
    text = (g.InputTextState.ID == field.id) ? g.InputTextState.GetText() : typed.text;
    return true;
}

// Returns true when the typed text was math and replaced what ImGui parsed
static bool applyTypedMath(const NumberField& field) {
    std::string text;
    if (!finishedTyping(field, text))
        return false;

    double result = 0.0;
    // Drag and slider text boxes already show the number without decorations
    const std::string number = field.tempInput ? text : stripDecorations(text, field.format);
    const TypedValue typed = evaluateTyped(number.c_str(), result);
    if (typed == TypedValue::Number)
        return false;

    const size_t size = ImGui::DataTypeGetInfo(field.type)->Size;
    if (typed == TypedValue::Expression && storeNumber(field.type, field.data, result)) {
        if (field.minValue || field.maxValue)
            ImGui::DataTypeClamp(field.type, field.data, field.minValue, field.maxValue);
    } else {
        // Invalid text keeps the value
        memcpy(field.data, &field.before, size);
    }

    // ImGui only saw its own parse, so IsItemDeactivatedAfterEdit() needs the mark
    if (memcmp(field.data, &field.before, size) != 0 && GImGui->DeactivatedItemData.ID == field.id)
        ImGui::MarkItemEdited(field.id);
    return true;
}

static bool finishFields(const NumberField* fields, int count, bool changed) {
    bool replaced = false;
    for (int i = 0; i < count; i++)
        replaced |= applyTypedMath(fields[i]);
    if (!replaced)
        return changed;

    for (int i = 0; i < count; i++) {
        if (memcmp(fields[i].data, &fields[i].before, ImGui::DataTypeGetInfo(fields[i].type)->Size) != 0)
            return true;
    }
    return false;
}

// Same rule as ImGui TempInputIsClampEnabled()
static bool dragClampsInput(ImGuiSliderFlags flags, ImGuiDataType type, const void* minValue, const void* maxValue) {
    if (!(flags & ImGuiSliderFlags_ClampOnInput) || (!minValue && !maxValue))
        return false;
    if (!minValue || !maxValue)
        return true;
    const int order = ImGui::DataTypeCompare(type, minValue, maxValue);
    if (order == 0)
        return !ImGui::DataTypeIsZero(type, minValue) || (flags & ImGuiSliderFlags_ClampZeroRange) != 0;
    return order < 0;
}

static ImGuiID dragFieldId(const char* label, ImGuiSliderFlags flags) {
    return (flags & ImGuiSliderFlags_ReadOnly) ? 0 : ImGui::GetID(label);
}

static ImGuiID inputFieldId(const char* label, bool stepButtons, ImGuiInputTextFlags flags) {
    if (flags & (ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal))
        return 0;
    if (!stepButtons)
        return ImGui::GetID(label);
    // With step buttons the text box is pushed under the label
    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("");
    ImGui::PopID();
    return id;
}

bool UIUtils::dragScalar(const char* label, ImGuiDataType dataType, void* data, float speed, const void* minValue, const void* maxValue, const char* format, ImGuiSliderFlags flags) {
    const bool clamp = dragClampsInput(flags, dataType, minValue, maxValue);
    const NumberField field = numberField(dragFieldId(label, flags), true, dataType, data, format, clamp ? minValue : nullptr, clamp ? maxValue : nullptr);
    const bool changed = ImGui::DragScalar(label, dataType, data, speed, minValue, maxValue, format, flags);
    return finishFields(&field, 1, changed);
}

bool UIUtils::dragScalarN(const char* label, ImGuiDataType dataType, void* data, int components, float speed, const void* minValue, const void* maxValue, const char* format, ImGuiSliderFlags flags) {
    if (components > 4)
        return ImGui::DragScalarN(label, dataType, data, components, speed, minValue, maxValue, format, flags);

    NumberField fields[4];
    const bool clamp = dragClampsInput(flags, dataType, minValue, maxValue);
    const size_t size = ImGui::DataTypeGetInfo(dataType)->Size;
    ImGui::PushID(label);
    for (int i = 0; i < components; i++) {
        ImGui::PushID(i);
        fields[i] = numberField(dragFieldId("", flags), true, dataType, static_cast<char*>(data) + size * i, format, clamp ? minValue : nullptr, clamp ? maxValue : nullptr);
        ImGui::PopID();
    }
    ImGui::PopID();

    const bool changed = ImGui::DragScalarN(label, dataType, data, components, speed, minValue, maxValue, format, flags);
    return finishFields(fields, components, changed);
}

bool UIUtils::dragFloat(const char* label, float* value, float speed, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    return dragScalar(label, ImGuiDataType_Float, value, speed, &minValue, &maxValue, format, flags);
}

bool UIUtils::dragFloat2(const char* label, float value[2], float speed, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    return dragScalarN(label, ImGuiDataType_Float, value, 2, speed, &minValue, &maxValue, format, flags);
}

bool UIUtils::dragFloat3(const char* label, float value[3], float speed, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    return dragScalarN(label, ImGuiDataType_Float, value, 3, speed, &minValue, &maxValue, format, flags);
}

bool UIUtils::dragFloat4(const char* label, float value[4], float speed, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    return dragScalarN(label, ImGuiDataType_Float, value, 4, speed, &minValue, &maxValue, format, flags);
}

bool UIUtils::dragInt(const char* label, int* value, float speed, int minValue, int maxValue, const char* format, ImGuiSliderFlags flags) {
    return dragScalar(label, ImGuiDataType_S32, value, speed, &minValue, &maxValue, format, flags);
}

bool UIUtils::dragFloatRange2(const char* label, float* currentMin, float* currentMax, float speed, float minValue, float maxValue, const char* format, const char* formatMax, ImGuiSliderFlags flags) {
    // Same limits ImGui gives each end
    const bool unbounded = minValue >= maxValue;
    float minMin = unbounded ? -FLT_MAX : minValue;
    float minMax = unbounded ? *currentMax : std::min(maxValue, *currentMax);
    float maxMin = unbounded ? *currentMin : std::max(minValue, *currentMin);
    float maxMax = unbounded ? FLT_MAX : maxValue;
    const ImGuiSliderFlags minFlags = flags | ((minMin == minMax) ? ImGuiSliderFlags_ReadOnly : 0);
    const ImGuiSliderFlags maxFlags = flags | ((maxMin == maxMax) ? ImGuiSliderFlags_ReadOnly : 0);
    const bool clampMin = dragClampsInput(minFlags, ImGuiDataType_Float, &minMin, &minMax);
    const bool clampMax = dragClampsInput(maxFlags, ImGuiDataType_Float, &maxMin, &maxMax);

    NumberField fields[2];
    ImGui::PushID(label);
    fields[0] = numberField(dragFieldId("##min", minFlags), true, ImGuiDataType_Float, currentMin, format, clampMin ? &minMin : nullptr, clampMin ? &minMax : nullptr);
    fields[1] = numberField(dragFieldId("##max", maxFlags), true, ImGuiDataType_Float, currentMax, formatMax ? formatMax : format, clampMax ? &maxMin : nullptr, clampMax ? &maxMax : nullptr);
    ImGui::PopID();

    const bool changed = ImGui::DragFloatRange2(label, currentMin, currentMax, speed, minValue, maxValue, format, formatMax, flags);
    return finishFields(fields, 2, changed);
}

bool UIUtils::sliderScalar(const char* label, ImGuiDataType dataType, void* data, const void* minValue, const void* maxValue, const char* format, ImGuiSliderFlags flags) {
    // Sliders clamp typed values only with ClampOnInput
    const bool clamp = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
    const NumberField field = numberField(dragFieldId(label, flags), true, dataType, data, format, clamp ? minValue : nullptr, clamp ? maxValue : nullptr);
    const bool changed = ImGui::SliderScalar(label, dataType, data, minValue, maxValue, format, flags);
    return finishFields(&field, 1, changed);
}

bool UIUtils::sliderFloat(const char* label, float* value, float minValue, float maxValue, const char* format, ImGuiSliderFlags flags) {
    return sliderScalar(label, ImGuiDataType_Float, value, &minValue, &maxValue, format, flags);
}

bool UIUtils::sliderInt(const char* label, int* value, int minValue, int maxValue, const char* format, ImGuiSliderFlags flags) {
    return sliderScalar(label, ImGuiDataType_S32, value, &minValue, &maxValue, format, flags);
}

bool UIUtils::inputScalar(const char* label, ImGuiDataType dataType, void* data, const void* step, const void* stepFast, const char* format, ImGuiInputTextFlags flags) {
    const NumberField field = numberField(inputFieldId(label, step != nullptr, flags), false, dataType, data, format);
    const bool changed = ImGui::InputScalar(label, dataType, data, step, stepFast, format, flags);
    if (!applyTypedMath(field))
        return changed;

    // A step button click ends the typing, but ImGui stepped from its own parse
    if (step) {
        ImGui::PushID(label);
        const ImGuiID activeId = ImGui::GetActiveID();
        const char op = (activeId == ImGui::GetID("+")) ? '+' : (activeId == ImGui::GetID("-")) ? '-' : 0;
        ImGui::PopID();
        if (op)
            ImGui::DataTypeApplyOp(dataType, op, data, data, (ImGui::GetIO().KeyCtrl && stepFast) ? stepFast : step);
    }
    return memcmp(data, &field.before, ImGui::DataTypeGetInfo(dataType)->Size) != 0;
}

bool UIUtils::inputFloat(const char* label, float* value, float step, float stepFast, const char* format, ImGuiInputTextFlags flags) {
    return inputScalar(label, ImGuiDataType_Float, value, step > 0.0f ? &step : nullptr, stepFast > 0.0f ? &stepFast : nullptr, format, flags);
}

bool UIUtils::inputInt(const char* label, int* value, int step, int stepFast, ImGuiInputTextFlags flags) {
    const char* format = (flags & ImGuiInputTextFlags_CharsHexadecimal) ? "%08X" : "%d";
    return inputScalar(label, ImGuiDataType_S32, value, step > 0 ? &step : nullptr, stepFast > 0 ? &stepFast : nullptr, format, flags);
}

bool UIUtils::inputInt3(const char* label, int value[3], ImGuiInputTextFlags flags) {
    NumberField fields[3];
    ImGui::PushID(label);
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        fields[i] = numberField(inputFieldId("", false, flags), false, ImGuiDataType_S32, &value[i], "%d");
        ImGui::PopID();
    }
    ImGui::PopID();

    const bool changed = ImGui::InputInt3(label, value, flags);
    return finishFields(fields, 3, changed);
}

}
