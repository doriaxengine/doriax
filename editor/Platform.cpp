// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Platform.h"
#include "Out.h"
#include "Backend.h"
#include "EditorHost.h"

using namespace doriax;

int editor::Platform::width = 0;
int editor::Platform::height = 0;

editor::Platform::Platform(Project* project) : System() {
    this->project = project;
}

bool editor::Platform::setSizes(int width, int height){
    if (editor::Platform::width != width || editor::Platform::height != height){
        editor::Platform::width = width;
        editor::Platform::height = height;

        return true;
    }

    return false;
}

int editor::Platform::getScreenWidth(){
    return width;
}

int editor::Platform::getScreenHeight(){
    return height;
}

std::string editor::Platform::getAssetPath(){
    return project->getAssetsPath().string();
}

std::string editor::Platform::getLuaPath(){
    return project->getLuaPath().string();
}

std::string editor::Platform::getShaderPath(){
    // Compiled shaders are a build output, not a referenced asset
    return (project->getProjectPath() / "shaders").string();
}

std::string editor::Platform::getUserDataPath(){
    return project->getUserDataPath().string();
}

std::string editor::Platform::getImportPath(){
    return project->getImportPath().string();
}

sg_environment editor::Platform::getSokolEnvironment(){
    #if defined(SOKOL_VULKAN) || defined(SOKOL_METAL)
    return Backend::getSokolEnvironment();
    #else
    return System::getSokolEnvironment();
    #endif
}

sg_swapchain editor::Platform::getSokolSwapchain(){
    #if defined(SOKOL_VULKAN) || defined(SOKOL_METAL)
    return Backend::getSokolSwapchain();
    #else
    return System::getSokolSwapchain();
    #endif
}

bool editor::Platform::isTouchDevice(){
    return project->isSimulateTouch();
}

void editor::Platform::setMouseMode(MouseMode mode){
    Backend::setMouseMode(mode);
}

void editor::Platform::quit(){
    // stops play instead of closing the editor, deferred to not stop inside a script call
    editor::getEditorHost().enqueueMainThreadTask([this]() {
        project->stopActivePlay();
    });
}

void editor::Platform::platformLog(const int type, const char *fmt, va_list args){
    va_list sizeArgs;
    va_copy(sizeArgs, args);
    const int size = vsnprintf(nullptr, 0, fmt, sizeArgs);
    va_end(sizeArgs);
    if (size < 0) return;

    std::string message(static_cast<size_t>(size), '\0');
    vsnprintf(message.data(), message.size() + 1, fmt, args);

    LogType logType = LogType::Info;
    switch (type) {
        case D_LOG_VERBOSE: logType = LogType::Verbose; break;
        case D_LOG_DEBUG:   logType = LogType::Debug; break;
        case D_LOG_WARN:    logType = LogType::Warning; break;
        case D_LOG_ERROR:   logType = LogType::Error; break;
        default: break;
    }

    // During Play the engine log is the project's, not the editor's
    const LogSource source = project->isPlaySessionActive() ? LogSource::Project : LogSource::Editor;
    editor::Out::log(logType, source, message);
}
