#include "Platform.h"
#include "Out.h"

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
    return project->getProjectPath().string();
}

void editor::Platform::platformLog(const int type, const char *fmt, va_list args){
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);

    switch (type) {
        case S_LOG_WARN:
            editor::Out::warning(buf);
            break;
        case S_LOG_ERROR:
            editor::Out::error(buf);
            break;
        default:
            editor::Out::info(buf);
            break;
    }
}