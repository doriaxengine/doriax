// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef DoriaxWeb_h
#define DoriaxWeb_h

#include <emscripten/html5.h>

#include "System.h"

#define DORIAX_WEB_MAX_GAMEPADS 16

class DoriaxWeb: public doriax::System{

private:

    typedef struct GamepadState{
        bool connected = false;
        bool buttons[64] = {false};
        float axes[64] = {0.0f};
        float triggers[2] = {0.0f};
    } GamepadState;

    static std::string canvas;

    static int syncWaitTime;
    static bool enabledIDB;

    // framebuffer size
    static int screenWidth;
    static int screenHeight;

    // framebuffer pixels per CSS pixel, the unit of input events
    static double pixelRatio;

    static double mousePosX;
    static double mousePosY;

    static int sampleCount;

    static bool quitRequested;

    static unsigned int pauseReasons;

    static GamepadState gamepads[DORIAX_WEB_MAX_GAMEPADS];

    static EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData);
    static EM_BOOL mouse_callback(int eventType, const EmscriptenMouseEvent *e, void *userData);
    static EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent *e, void *userData);
    static EM_BOOL touch_callback(int emsc_type, const EmscriptenTouchEvent* emsc_event, void* user_data);
    static EM_BOOL resize_callback(int event_type, const EmscriptenUiEvent* ui_event, void* user_data);

    static EM_BOOL canvas_resize(int eventType, const void *reserved, void *userData);
    static EM_BOOL webgl_context_callback(int emsc_type, const void* reserved, void* user_data);
    static EM_BOOL visibility_callback(int eventType, const EmscriptenVisibilityChangeEvent *e, void *userData);

    static EM_BOOL renderLoop(double time, void* userdata);

    static bool getCanvasCssSize(double& width, double& height);
    static void setFramebufferSize(int width, int height);
    static void updateFramebufferSize();

    static void unregisterCallbacks();
    static void syncFileSystemNow();

    static wchar_t toCodepoint(const std::string &u);
    static std::string toUTF8(wchar_t cp);
    static int doriax_mouse_button(int button);
    static int doriax_legacy_input(int code);
    static int doriax_input(const char code[32]);
    static int doriax_gamepad_button(int button);

    static void pollGamepads();

public:

    enum class PauseReason{
        AD,
        PORTAL,
        HIDDEN,
        CONTEXT_LOST
    };

    DoriaxWeb();

    static void setEnabledIDB(bool enabledIDB);

    static int init(int argc, char **argv);
    // CSS size of the canvas, which the framebuffer follows
    static void changeCanvasSize(int width, int height);

    // The game resumes when no reason is left
    static void setPaused(PauseReason reason, bool paused);

    virtual int getScreenWidth() override;
    virtual int getScreenHeight() override;

    virtual int getSampleCount() override;

    virtual bool isTouchDevice() override;

    virtual bool isFullscreen() override;
    virtual void requestFullscreen() override;
    virtual void exitFullscreen() override;

    // Sets the browser tab title; the other window APIs keep the base
    // no-ops since the canvas has no OS window
    virtual void setWindowTitle(const std::string& title) override;
    virtual void quit() override;

    virtual void setMouseCursor(doriax::CursorType type) override;
    virtual void setMouseMode(doriax::MouseMode mode) override;

    virtual std::string getUserDataPath() override;

    virtual bool syncFileSystem() override;

    virtual std::string getStringForKey(const char *key, const std::string& defaultValue) override;
    virtual void setStringForKey(const char* key, const std::string& value) override;
    virtual void removeKey(const char *key) override;

    // WebPortals.cpp
    virtual doriax::WebPortalBackend* getWebPortalBackend() override;

};


#endif /* DoriaxWeb_h */
