// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "window/OutputWindow.h"
#include <filesystem>
#include <iosfwd>
#include <string>
#include <memory>

namespace doriax::editor {
    class Out {
    private:
        static OutputWindow* outputWindow;
        static void logMessage(LogType type, LogSource source, const std::string& message);

        // Helper method for formatting with no arguments
        static std::string formatMessage(const char* fmt) {
            return std::string(fmt);
        }

        // Helper method for formatting with arguments
        template<typename... Args>
        static std::string formatMessage(const char* fmt, Args... args) {
            if (!fmt) return std::string();

            // Calculate buffer size needed
            int size = snprintf(nullptr, 0, fmt, args...);
            if (size <= 0) return std::string();

            // Create buffer with required size (+1 for null terminator)
            std::string result;
            result.resize(size + 1);

            // Format the string
            snprintf(&result[0], size + 1, fmt, args...);

            // Remove null terminator from string
            result.resize(size);
            return result;
        }

    public:
        static void setOutputWindow(OutputWindow* outputWindow);
        static OutputWindow* getOutputWindow();

        // Mirrors every message to a file, from any thread. The Output window dies with
        // the process, this is what is left after a crash.
        static void setLogFile(const std::filesystem::path& path);

        // Recent output-window log as prefixed text (oldest-first). onlyProblems keeps
        // Error/Warning/Build entries. Empty if no output window is attached. Call on
        // the main thread. Used by the AI to read build/runtime errors from the log.
        static std::string getRecentLog(size_t maxEntries, bool onlyProblems);

        // Basic logging methods
        static void info(const std::string& message);
        static void success(const std::string& message);
        static void warning(const std::string& message);
        static void error(const std::string& message);
        static void build(const std::string& message);

        // Any type and source, used for the played project's lines
        static void log(LogType type, LogSource source, const std::string& message);

        // Overloads for const char* without formatting
        static void info(const char* message) { info(std::string(message)); }
        static void success(const char* message) { success(std::string(message)); }
        static void warning(const char* message) { warning(std::string(message)); }
        static void error(const char* message) { error(std::string(message)); }
        static void build(const char* message) { build(std::string(message)); }

        // Formatted logging methods with variable arguments
        template<typename... Args>
        static void info(const char* fmt, Args... args) {
            info(formatMessage(fmt, args...));
        }

        template<typename... Args>
        static void success(const char* fmt, Args... args) {
            success(formatMessage(fmt, args...));
        }

        template<typename... Args>
        static void warning(const char* fmt, Args... args) {
            warning(formatMessage(fmt, args...));
        }

        template<typename... Args>
        static void error(const char* fmt, Args... args) {
            error(formatMessage(fmt, args...));
        }

        template<typename... Args>
        static void build(const char* fmt, Args... args) {
            build(formatMessage(fmt, args...));
        }

        // Debug assertion
        static void editor_assert(bool condition, const std::string& message);
    };
}

