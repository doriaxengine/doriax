//
// (c) 2026 Eduardo Doria.
//

#include "DoriaxGLFW.h"

#include "Engine.h"

int DoriaxGLFW::windowPosX;
int DoriaxGLFW::windowPosY;
int DoriaxGLFW::windowWidth;
int DoriaxGLFW::windowHeight;  

int DoriaxGLFW::screenWidth;
int DoriaxGLFW::screenHeight;

double DoriaxGLFW::mousePosX;
double DoriaxGLFW::mousePosY;

int DoriaxGLFW::sampleCount;

GLFWwindow* DoriaxGLFW::window;
GLFWmonitor* DoriaxGLFW::monitor;


DoriaxGLFW::DoriaxGLFW(){

}

int DoriaxGLFW::init(int argc, char **argv){
    windowWidth = DEFAULT_WINDOW_WIDTH;
    windowHeight = DEFAULT_WINDOW_HEIGHT;

    sampleCount = 1;

    doriax::Engine::systemInit(argc, argv, new DoriaxGLFW());

    /* create window and GL context via GLFW */
    glfwInit();
    glfwWindowHint(GLFW_SAMPLES, (sampleCount == 1) ? 0 : sampleCount);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(windowWidth, windowHeight, "Doriax", 0, 0);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    monitor = glfwGetPrimaryMonitor();

    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int btn, int action, int mods) {
        if (action==GLFW_PRESS){
            doriax::Engine::systemMouseDown(btn, float(mousePosX), float(mousePosY), mods);
        }else if (action==GLFW_RELEASE){
            doriax::Engine::systemMouseUp(btn, float(mousePosX), float(mousePosY), mods);
        }
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double pos_x, double pos_y) {
        float xscale, yscale;
        glfwGetWindowContentScale(window, &xscale, &yscale);

        mousePosX = pos_x * xscale;
        mousePosY = pos_y * yscale;
        doriax::Engine::systemMouseMove(float(mousePosX), float(mousePosY), 0);
    });
    glfwSetScrollCallback(window, [](GLFWwindow*, double xoffset, double yoffset){
        doriax::Engine::systemMouseScroll((float)xoffset, (float)yoffset, 0);
    });
    glfwSetKeyCallback(window, [](GLFWwindow*, int key, int /*scancode*/, int action, int mods){
        if (action==GLFW_PRESS){
            if (key == GLFW_KEY_TAB)
                doriax::Engine::systemCharInput('\t');
            if (key == GLFW_KEY_BACKSPACE)
                doriax::Engine::systemCharInput('\b');
            if (key == GLFW_KEY_ENTER)
                doriax::Engine::systemCharInput('\r');
            if (key == GLFW_KEY_ESCAPE)
                doriax::Engine::systemCharInput('\e');
            doriax::Engine::systemKeyDown(key, false, mods);
        }else if (action==GLFW_REPEAT){
            doriax::Engine::systemKeyDown(key, true, mods);
        }else if (action==GLFW_RELEASE){
            doriax::Engine::systemKeyUp(key, false, mods);
        }
    });
    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint){
        doriax::Engine::systemCharInput(codepoint);
    });

    int cur_width, cur_height;
    glfwGetFramebufferSize(window, &cur_width, &cur_height);

    DoriaxGLFW::screenWidth = cur_width;
    DoriaxGLFW::screenHeight = cur_height;

    doriax::Engine::systemViewLoaded();
    doriax::Engine::systemViewChanged();

    /* draw loop */
    while (!glfwWindowShouldClose(window)) {
        int cur_width, cur_height;
        glfwGetFramebufferSize(window, &cur_width, &cur_height);

        if (cur_width != DoriaxGLFW::screenWidth || cur_height != DoriaxGLFW::screenHeight){
            DoriaxGLFW::screenWidth = cur_width;
            DoriaxGLFW::screenHeight = cur_height;
            doriax::Engine::systemViewChanged();
        }

        doriax::Engine::systemDraw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    doriax::Engine::systemViewDestroyed();
    doriax::Engine::systemShutdown();
    glfwTerminate();
    return 0;
}

int DoriaxGLFW::getScreenWidth(){
    return DoriaxGLFW::screenWidth;
}

int DoriaxGLFW::getScreenHeight(){
    return DoriaxGLFW::screenHeight;
}

int DoriaxGLFW::getSampleCount(){
    return DoriaxGLFW::sampleCount;
}

bool DoriaxGLFW::isFullscreen(){
    return glfwGetWindowMonitor(window) != nullptr;
}

void DoriaxGLFW::requestFullscreen(){
    if (isFullscreen())
        return;

    // backup window position and window size
    glfwGetWindowPos(window, &windowPosX, &windowPosY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
        
    // get resolution of monitor
    const GLFWvidmode * mode = glfwGetVideoMode(monitor);

    // switch to full screen
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, 0 );
}

void DoriaxGLFW::exitFullscreen(){
    if (!isFullscreen())
        return;

    // restore last window size and position
    glfwSetWindowMonitor(window, nullptr,  windowPosX, windowPosY, windowWidth, windowHeight, 0);
}

void DoriaxGLFW::setMouseCursor(doriax::CursorType type){
    GLFWcursor* cursor = NULL;

    if (type == doriax::CursorType::ARROW){
        cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    }else if (type == doriax::CursorType::IBEAM){
        cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    }else if (type == doriax::CursorType::CROSSHAIR){
        cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    }else if (type == doriax::CursorType::POINTING_HAND){
        #ifdef GLFW_POINTING_HAND_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        #endif
    }else if (type == doriax::CursorType::RESIZE_EW){
        #ifdef GLFW_RESIZE_EW_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        #endif
    }else if (type == doriax::CursorType::RESIZE_NS){
        #ifdef GLFW_RESIZE_NS_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        #endif
    }else if (type == doriax::CursorType::RESIZE_NWSE){
        #ifdef GLFW_RESIZE_NWSE_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        #endif
    }else if (type == doriax::CursorType::RESIZE_NESW){
        #ifdef GLFW_RESIZE_NESW_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        #endif
    }else if (type == doriax::CursorType::RESIZE_ALL){
        #ifdef GLFW_RESIZE_ALL_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        #endif
    }else if (type == doriax::CursorType::NOT_ALLOWED){
        #ifdef GLFW_NOT_ALLOWED_CURSOR
        cursor = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
        #else
        cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        #endif
    }

    if (cursor) {
        glfwSetCursor(window, cursor);
    } else {
        // Handle error: cursor creation failed
    }
}

void DoriaxGLFW::setShowCursor(bool showCursor){
    if (showCursor){
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }else{
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
}

std::string DoriaxGLFW::getAssetPath(){
    return "assets";
}

std::string DoriaxGLFW::getUserDataPath(){
    return ".";
}

std::string DoriaxGLFW::getLuaPath(){
    return "lua";
}