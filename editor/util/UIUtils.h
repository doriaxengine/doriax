#pragma once

#include <string>
#include "imgui.h"

namespace doriax::editor {
    class UIUtils {
    public:
        // Draw a search input field with a magnifying glass icon button
        // Returns true if the input text changed
        // If fixedWidth > 0, uses that width; otherwise uses available content region width
        static bool searchInput(const char* id, std::string hint, char* buffer, size_t bufferSize, bool autoFocus = false, bool* matchCase = nullptr, float fixedWidth = 0.0f);
    };
}