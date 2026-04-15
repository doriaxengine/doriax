//
// (c) 2026 Eduardo Doria.
//

#ifndef DoriaxGLFW_h
#define DoriaxGLFW_h

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include "System.h"

class DoriaxGLFW: public doriax::System{

private:

    static int windowPosX;
    static int windowPosY;
    static int windowWidth;
    static int windowHeight;  

    static int screenWidth;
    static int screenHeight;

    static double mousePosX;
    static double mousePosY;

    static int sampleCount;

    static GLFWwindow* window;
    static GLFWmonitor* monitor;

public:

    DoriaxGLFW();

    static int init(int argc, char **argv);

    virtual int getScreenWidth();
    virtual int getScreenHeight();

    virtual int getSampleCount();

    virtual bool isFullscreen();
    virtual void requestFullscreen();
    virtual void exitFullscreen();

    virtual void setMouseCursor(doriax::CursorType type);
    virtual void setShowCursor(bool showCursor);

    virtual std::string getAssetPath();
    virtual std::string getUserDataPath();
    virtual std::string getLuaPath();
    
};


#endif /* DoriaxGLFW_h */