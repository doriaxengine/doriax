#ifndef OUT_H
#define OUT_H

#include "window/OutputWindow.h"
#include <string>
#include <memory>

namespace doriax::editor {
    class Out {
    private:
        static OutputWindow* outputWindow;

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

        // Basic logging methods
        static void info(const std::string& message);
        static void success(const std::string& message);
        static void warning(const std::string& message);
        static void error(const std::string& message);
        static void build(const std::string& message);

        // Overloads for const char* without formatting
        static void info(const char* message) { info(std::string(message)); }
        static void success(const char* message) { success(std::string(message)); }
        static void warning(const char* message) { warning(std::string(message)); }
        static void error(const char* message) { error(std::string(message)); }
        static void build(const char* message) { build(std::string(message)); }

        // Formatted logging methods with variable arguments
        template<typename... Args>
        static void info(const char* fmt, Args... args) {
            if (outputWindow) {
                std::string message = formatMessage(fmt, args...);
                outputWindow->addLog(LogType::Info, message);
            }
        }

        template<typename... Args>
        static void success(const char* fmt, Args... args) {
            if (outputWindow) {
                std::string message = formatMessage(fmt, args...);
                outputWindow->addLog(LogType::Success, message);
            }
        }

        template<typename... Args>
        static void warning(const char* fmt, Args... args) {
            if (outputWindow) {
                std::string message = formatMessage(fmt, args...);
                outputWindow->addLog(LogType::Warning, message);
            }
        }

        template<typename... Args>
        static void error(const char* fmt, Args... args) {
            if (outputWindow) {
                std::string message = formatMessage(fmt, args...);
                outputWindow->addLog(LogType::Error, message);
            }
        }

        template<typename... Args>
        static void build(const char* fmt, Args... args) {
            if (outputWindow) {
                std::string message = formatMessage(fmt, args...);
                outputWindow->addLog(LogType::Build, message);
            }
        }

        // Debug assertion
        static void editor_assert(bool condition, const std::string& message);
    };
}

#endif /* OUT_H */