#include "Out.h"
#include <cstdarg>
#include <iostream>

using namespace doriax::editor;

OutputWindow* Out::outputWindow = nullptr;

void Out::setOutputWindow(OutputWindow* outputWindow) {
    Out::outputWindow = outputWindow;
}

OutputWindow* Out::getOutputWindow() {
    return outputWindow;
}

void Out::info(const std::string& message) {
    if (outputWindow) {
        outputWindow->addLog(LogType::Info, message);
    } else {
        std::cout << "[INFO] " << message << std::endl;
    }
}

void Out::success(const std::string& message) {
    if (outputWindow) {
        outputWindow->addLog(LogType::Success, message);
    } else {
        std::cout << "[SUCCESS] " << message << std::endl;
    }
}

void Out::warning(const std::string& message) {
    if (outputWindow) {
        outputWindow->addLog(LogType::Warning, message);
    } else {
        std::cout << "[WARNING] " << message << std::endl;
    }
}

void Out::error(const std::string& message) {
    if (outputWindow) {
        outputWindow->addLog(LogType::Error, message);
    } else {
        std::cerr << "[ERROR] " << message << std::endl;
    }
}

void Out::build(const std::string& message) {
    if (outputWindow) {
        outputWindow->addLog(LogType::Build, message);
    } else {
        std::cerr << "[BUILD] " << message << std::endl;
    }
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