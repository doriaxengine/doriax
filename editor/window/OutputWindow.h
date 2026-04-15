#pragma once

#include <vector>
#include <string>
#include "imgui.h"

namespace doriax::editor {
    enum class LogType {
        Info,
        Warning,
        Error,
        Success,
        Build
    };

    struct LogData {
        LogType type;
        std::string message;
        float timestamp;
    };

    class OutputWindow {
    private:
        ImGuiTextBuffer buf;
        ImGuiTextFilter filter;
        ImVector<int> lineOffsets;           // Start index of each line in buf (size = lines + 1)
        std::vector<LogData> logs;
        std::vector<LogType> lineTypes;      // One per displayed line
        std::vector<char> lineHardBreak;     // 1 if displayed line ends with a hard newline, 0 if soft-wrapped

        bool needsRebuild;
        float menuWidth;

        // Character-level selection (global indices into buf)
        int selectionStart;                  // inclusive
        int selectionEnd;                    // exclusive
        bool hasStoredSelection;
        bool isSelecting;                    // dragging selection?

        bool typeFilters[5];

        // Auto-scroll
        bool autoScroll;                // when true, keep pinned to bottom
        bool autoScrollLockedButton;    // the button state
        float lastScrollY;              // track last scroll position to detect manual scrolling

        // Search options
        bool searchMatchCase;           // enable case-sensitive search

        // Tab notification
        bool hasNotification;
        bool isWindowVisible;

        void rebuildBuffer(float wrapWidth); // accept wrap width

        // Helpers for selection
        int clampIndexToCodepointBoundary(int idx) const;
        bool isWordChar(unsigned int cp);
        void selectWordAt(int bufferIndex);
        void selectLineAt(int bufferIndex);

        // Text filter helper honoring match-case
        bool passTextFilter(const char* text) const;

    public:
        static constexpr const char* WINDOW_NAME = "Output";

        OutputWindow();
        void clear();
        void addLog(LogType type, const char* fmt, ...);
        void addLog(LogType type, const std::string& message);
        void show();
    };
}