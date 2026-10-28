// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Out.h"
#include "EditorHost.h"
#include "EditorVersion.h"
#include "util/TextEncoding.h"
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>

using namespace doriax::editor;

OutputWindow* Out::outputWindow = nullptr;

namespace {

// Several sessions fit, so a crash log survives the restarts before it is sent.
constexpr std::uintmax_t kMaxLogFileSize = 2 * 1024 * 1024;

std::mutex logFileMutex;
std::ofstream logFileStream;
std::filesystem::path logFilePath;

const char* logTypeLabel(LogType type) {
    switch (type) {
        case LogType::Warning: return "WARN ";
        case LogType::Error:   return "ERROR";
        case LogType::Success: return "OK   ";
        case LogType::Build:   return "BUILD";
        default:               return "INFO ";
    }
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &parts);
    return buffer;
}

void writeToLogFile(LogType type, const std::string& message) {
    std::lock_guard<std::mutex> lock(logFileMutex);
    if (!logFileStream.is_open()) {
        return;
    }
    // Flushed per line, the lines just before a crash are the ones worth having.
    logFileStream << timestamp() << " [" << logTypeLabel(type) << "] " << message << std::endl;
}

}

void Out::setLogFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(logFileMutex);

    // Startup settles the config directory twice, the banner is worth writing once.
    if (logFileStream.is_open()) {
        if (logFilePath == path) {
            return;
        }
        logFileStream.close();
    }

    std::error_code ec;
    const std::uintmax_t existingSize = std::filesystem::file_size(path, ec);
    if (!ec && existingSize > kMaxLogFileSize) {
        std::filesystem::path rotated = path;
        rotated += ".1";
        std::filesystem::remove(rotated, ec);
        std::filesystem::rename(path, rotated, ec);
    }

    logFileStream.open(path, std::ios::out | std::ios::app);
    if (!logFileStream.is_open()) {
        logFilePath.clear();
        std::cerr << "[WARNING] Could not open log file: " << path.string() << std::endl;
        return;
    }

    logFilePath = path;
    logFileStream << "\n=== Doriax editor " << DORIAX_EDITOR_VERSION << " started at "
                  << timestamp() << " ===" << std::endl;
}

void Out::logMessage(LogType type, const std::string& text, const char* fallbackPrefix, std::ostream& fallbackStream) {
    const std::string message = TextEncoding::legacyToUtf8(text);

    // Before the hop to the main thread, so a worker's last message still reaches the file.
    writeToLogFile(type, message);

    if (OutputWindow* window = Out::getOutputWindow()) {
        EditorHost& host = getEditorHost();
        if (host.isMainThread()) {
            window->addLog(type, message);
            return;
        }

        host.enqueueMainThreadTask([type, message]() {
            if (OutputWindow* window = Out::getOutputWindow()) {
                window->addLog(type, message);
            }
        });
    } else {
        fallbackStream << fallbackPrefix << message << std::endl;
    }
}

void Out::setOutputWindow(OutputWindow* outputWindow) {
    Out::outputWindow = outputWindow;
}

OutputWindow* Out::getOutputWindow() {
    return outputWindow;
}

std::string Out::getRecentLog(size_t maxEntries, bool onlyProblems) {
    if (OutputWindow* window = getOutputWindow()) {
        return window->getRecentLogText(maxEntries, onlyProblems);
    }
    return {};
}

void Out::info(const std::string& message) {
    logMessage(LogType::Info, message, "[INFO] ", std::cout);
}

void Out::success(const std::string& message) {
    logMessage(LogType::Success, message, "[SUCCESS] ", std::cout);
}

void Out::warning(const std::string& message) {
    logMessage(LogType::Warning, message, "[WARNING] ", std::cout);
}

void Out::error(const std::string& message) {
    logMessage(LogType::Error, message, "[ERROR] ", std::cerr);
}

void Out::build(const std::string& message) {
    logMessage(LogType::Build, message, "[BUILD] ", std::cerr);
}

void Out::editor_assert(bool condition, const std::string& message) {
    if (!condition) {
        error("Assertion failed: " + message);
        #ifdef _DEBUG
            // Break into debugger if in debug mode
            #if defined(_MSC_VER)
                __debugbreak();
            #elif defined(__GNUC__)
                __builtin_trap();
            #endif
        #endif
    }
}
