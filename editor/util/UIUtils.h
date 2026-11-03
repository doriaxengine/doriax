// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include "imgui.h"

namespace doriax::editor {
    class UIUtils {
    public:
        // Draw a search input field with a magnifying glass icon button
        // Returns true if the input text changed
        // If fixedWidth > 0, uses that width; otherwise uses available content region width
        static bool searchInput(const char* id, std::string hint, char* buffer, size_t bufferSize, bool autoFocus = false, bool* matchCase = nullptr, float fixedWidth = 0.0f);

        // Draw a float slider that turns into a text-input field when double-clicked, so an exact
        // value can be typed. Typed values are not clamped to [minValue, maxValue] (only dragging is),
        // so values beyond the slider range can be entered. Drag to adjust; a plain click does not
        // seek the value (this keeps double-click from nudging it). Returns true while it is changing.
        static bool sliderFloatInput(const char* id, float* value, float minValue, float maxValue, const char* format = "%.3f", ImGuiSliderFlags flags = 0);

        // ImGui number widgets that also take typed math, e.g. 288*3 gives 864
        static bool dragFloat(const char* label, float* value, float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
        static bool dragFloat2(const char* label, float value[2], float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
        static bool dragFloat3(const char* label, float value[3], float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
        static bool dragFloat4(const char* label, float value[4], float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
        static bool dragFloatRange2(const char* label, float* currentMin, float* currentMax, float speed = 1.0f, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", const char* formatMax = nullptr, ImGuiSliderFlags flags = 0);
        static bool dragInt(const char* label, int* value, float speed = 1.0f, int minValue = 0, int maxValue = 0, const char* format = "%d", ImGuiSliderFlags flags = 0);
        static bool dragScalar(const char* label, ImGuiDataType dataType, void* data, float speed = 1.0f, const void* minValue = nullptr, const void* maxValue = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0);
        static bool dragScalarN(const char* label, ImGuiDataType dataType, void* data, int components, float speed = 1.0f, const void* minValue = nullptr, const void* maxValue = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0);
        static bool sliderFloat(const char* label, float* value, float minValue, float maxValue, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
        static bool sliderInt(const char* label, int* value, int minValue, int maxValue, const char* format = "%d", ImGuiSliderFlags flags = 0);
        static bool sliderScalar(const char* label, ImGuiDataType dataType, void* data, const void* minValue, const void* maxValue, const char* format = nullptr, ImGuiSliderFlags flags = 0);
        static bool inputFloat(const char* label, float* value, float step = 0.0f, float stepFast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
        static bool inputInt(const char* label, int* value, int step = 1, int stepFast = 100, ImGuiInputTextFlags flags = 0);
        static bool inputInt3(const char* label, int value[3], ImGuiInputTextFlags flags = 0);
        static bool inputScalar(const char* label, ImGuiDataType dataType, void* data, const void* step = nullptr, const void* stepFast = nullptr, const char* format = nullptr, ImGuiInputTextFlags flags = 0);

        // Draws the folders below currentPath as a tree, writing the clicked one
        // into selectedPath. Shared by the creation dialogs.
        static void directoryTreeBrowser(const std::filesystem::path& currentPath,
                                         std::string& selectedPath);
    };
}
