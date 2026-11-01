// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "UIUtils.h"
#include "App.h"
#include "FileUtils.h"
#include "external/IconsFontAwesome6.h"
#include "window/widget/InputTextContextMenu.h"
#include "imgui_internal.h"

#include <algorithm>
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
        changed = ImGui::InputFloat(id, value, 0.0f, 0.0f, ImParseFormatTrimDecorations(format, inputFormat, IM_ARRAYSIZE(inputFormat)));
        if (ImGui::IsItemDeactivated()) {
            editingId = 0;
        }
    } else {
        const float before = *value;
        changed = ImGui::SliderFloat(id, value, minValue, maxValue, format, flags);

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

}
