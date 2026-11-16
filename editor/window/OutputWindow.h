// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include "imgui.h"

namespace doriax::editor {
    enum class LogType {
        Info,
        Warning,
        Error,
        Success,
        Build,
        Debug,
        Verbose,
        Count
    };

    // Who wrote the line: the editor or the project being played
    enum class LogSource {
        Editor,
        Project,
        Count
    };

    struct LogData {
        LogType type;
        LogSource source;
        std::string message;
        float timestamp;
    };

    // A file location in a log line, as compilers and Lua print it
    struct OutputFileLink {
        int start = -1;     // buffer range it covers
        int end = -1;
        std::string path;
        int line = 0;
        int column = 0;
    };

    class OutputWindow {
    private:
        ImGuiTextBuffer buf;
        ImGuiTextFilter filter;
        ImVector<int> lineOffsets;           // Start index of each line in buf (size = lines + 1)
        std::vector<LogData> logs;
        size_t storedLogLineCount;           // Raw log lines kept before wrapping/filtering
        std::vector<LogType> lineTypes;      // One per displayed line
        std::vector<int> linePrefixLen;      // Prefix length of the entry each displayed line belongs to
        std::vector<char> lineHardBreak;     // 1 if displayed line ends with a hard newline, 0 if soft-wrapped

        static constexpr size_t MAX_STORED_LOG_LINES = 10000;

        bool needsRebuild;
        float menuWidth;

        // Character-level selection (global indices into buf)
        int selectionStart;                  // inclusive
        int selectionEnd;                    // exclusive
        bool hasStoredSelection;
        bool isSelecting;                    // dragging selection?

        bool typeFilters[static_cast<size_t>(LogType::Count)];
        bool sourceFilters[static_cast<size_t>(LogSource::Count)];

        // Auto-scroll
        bool autoScroll;                // when true, keep pinned to bottom
        bool autoScrollLockedButton;    // the button state
        float lastScrollY;              // track last scroll position to detect manual scrolling
        bool userScrollInputPending;    // scroll input may be applied by ImGui on the next Begin()
        bool pendingScrollToBottom;       // one-shot scroll from context menu

        // Search options
        bool searchMatchCase;           // enable case-sensitive search

        // Tab notification
        bool hasNotification;
        bool windowOpen;
        bool focusRequested;
        bool isWindowVisible;

        void rebuildBuffer(float wrapWidth); // accept wrap width
        static size_t countRawLines(const std::string& message);
        static size_t trimLeadingRawLines(std::string& message, size_t linesToTrim);
        bool enforceStoredLineLimit();
        void resetDisplayBuffer();

        // Helpers for selection
        int clampIndexToCodepointBoundary(int idx) const;
        bool isWordChar(unsigned int cp);
        void selectWordAt(int bufferIndex);
        void selectLineAt(int bufferIndex);

        // Text filter helper honoring match-case
        bool passTextFilter(const char* text) const;

        // Ctrl+Click or the context menu opens it
        OutputFileLink contextLink;
        bool findFileLink(int line, int bufferIndex, OutputFileLink& link) const;
        void openFileLink(const OutputFileLink& link);

        std::string buildCopyText(int startInclusive, int endExclusive) const;
        bool copySelectionToClipboard();
        void copyAllToClipboard();
        void selectAll();

    public:
        static constexpr const char* WINDOW_NAME = "Output";

        OutputWindow();
        void clear();
        void addLog(LogType type, const char* fmt, ...);
        void addLog(LogType type, const std::string& message);
        void addLog(LogType type, LogSource source, const std::string& message);

        // Text form, "[Project] [Warning] " for the played project
        static std::string getPrefix(LogType type, LogSource source);
        void show();
        void setOpen(bool open);
        bool isOpen() const;

        // Return the most recent log entries as prefixed text (e.g. "[Error] ...",
        // "[Project] [Error] ..." when the played project wrote it), oldest-first.
        // When onlyProblems is true, keep only Error/Warning/Build entries (build
        // compiler output is logged as Build). Used by the AI to read build errors
        // and runtime script crashes after starting a scene.
        std::string getRecentLogText(size_t maxEntries, bool onlyProblems) const;
    };
}
