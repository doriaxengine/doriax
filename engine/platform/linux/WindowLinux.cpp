// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "WindowLinux.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include <cstdio>
#include <cstring>

using namespace doriax;

namespace {

    // _NET_WM_STATE client message actions
    constexpr long NET_WM_STATE_REMOVE = 0;
    constexpr long NET_WM_STATE_ADD = 1;

    constexpr int CURSOR_TYPE_COUNT =
        static_cast<int>(CursorType::NOT_ALLOWED) + 1;

    Display* gDisplay = nullptr;
    bool gOwnsDisplay = false;
    int gScreen = 0;
    Window gRoot = 0;
    Window gWindow = 0;
    Colormap gColormap = 0;
    bool gOwnsWindow = false;

    Atom gWmProtocols = None;
    Atom gWmDeleteWindow = None;
    Atom gWmState = None;
    Atom gWmStateFullscreen = None;
    Atom gWmStateMaximizedVert = None;
    Atom gWmStateMaximizedHorz = None;
    Atom gWmStateHidden = None;
    Atom gNetWmName = None;
    Atom gUtf8String = None;

    std::function<void(int, int)> gResizeCallback;
    int gWidth = 0;
    int gHeight = 0;
    bool gResizable = true;
    bool gFullscreen = false;

    Cursor gInvisibleCursor = None;
    // XCreateFontCursor allocates a server-side resource, so the shapes are
    // created once and reused rather than per setMouseCursor call.
    Cursor gShapeCursors[CURSOR_TYPE_COUNT]{};
    Cursor gCurrentCursor = None;
    bool gCursorHidden = false;
    bool gRelativeMouse = false;
    bool gPointerGrabbed = false;
    int gLastWarpX = 0;
    int gLastWarpY = 0;
    bool gHasSavedPointer = false;
    int gSavedPointerX = 0;
    int gSavedPointerY = 0;

    void internAtoms() {
        gWmProtocols = XInternAtom(gDisplay, "WM_PROTOCOLS", False);
        gWmDeleteWindow = XInternAtom(gDisplay, "WM_DELETE_WINDOW", False);
        gWmState = XInternAtom(gDisplay, "_NET_WM_STATE", False);
        gWmStateFullscreen = XInternAtom(gDisplay, "_NET_WM_STATE_FULLSCREEN", False);
        gWmStateMaximizedVert = XInternAtom(gDisplay, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        gWmStateMaximizedHorz = XInternAtom(gDisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        gWmStateHidden = XInternAtom(gDisplay, "_NET_WM_STATE_HIDDEN", False);
        gNetWmName = XInternAtom(gDisplay, "_NET_WM_NAME", False);
        gUtf8String = XInternAtom(gDisplay, "UTF8_STRING", False);
    }

    // A 1x1 transparent pixmap is the portable way to hide the X cursor
    void createInvisibleCursor() {
        if (gInvisibleCursor != None || !gDisplay || !gWindow) return;
        static char noData[] = {0, 0, 0, 0, 0, 0, 0, 0};
        XColor black{};
        Pixmap pixmap = XCreateBitmapFromData(gDisplay, gWindow, noData, 8, 8);
        gInvisibleCursor = XCreatePixmapCursor(gDisplay, pixmap, pixmap,
                                               &black, &black, 0, 0);
        XFreePixmap(gDisplay, pixmap);
    }

    void sendWmState(long action, Atom first, Atom second) {
        if (!gDisplay || !gWindow) return;
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = gWindow;
        event.xclient.message_type = gWmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = action;
        event.xclient.data.l[1] = static_cast<long>(first);
        event.xclient.data.l[2] = static_cast<long>(second);
        event.xclient.data.l[3] = 1; // normal application
        XSendEvent(gDisplay, gRoot, False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
        XFlush(gDisplay);
    }

    bool hasWmState(Atom first, Atom second) {
        if (!gDisplay || !gWindow) return false;

        Atom actualType = None;
        int actualFormat = 0;
        unsigned long count = 0;
        unsigned long bytesAfter = 0;
        unsigned char* data = nullptr;
        bool hasFirst = false;
        bool hasSecond = second == None;

        if (XGetWindowProperty(gDisplay, gWindow, gWmState, 0, 32, False, XA_ATOM,
                               &actualType, &actualFormat, &count, &bytesAfter,
                               &data) == Success && data) {
            Atom* states = reinterpret_cast<Atom*>(data);
            for (unsigned long i = 0; i < count; ++i) {
                if (states[i] == first) hasFirst = true;
                if (states[i] == second) hasSecond = true;
            }
            XFree(data);
        }
        return hasFirst && hasSecond;
    }

    void applySizeHints(int width, int height) {
        XSizeHints* hints = XAllocSizeHints();
        if (!hints) return;
        if (gResizable) {
            hints->flags = 0;
        } else {
            // Pinning min and max to one size is how X11 refuses a resize
            hints->flags = PMinSize | PMaxSize;
            hints->min_width = hints->max_width = width;
            hints->min_height = hints->max_height = height;
        }
        XSetWMNormalHints(gDisplay, gWindow, hints);
        XFree(hints);
    }

    unsigned int cursorShape(CursorType type) {
        switch (type) {
            case CursorType::ARROW:         return XC_left_ptr;
            case CursorType::IBEAM:         return XC_xterm;
            case CursorType::CROSSHAIR:     return XC_crosshair;
            case CursorType::POINTING_HAND: return XC_hand2;
            case CursorType::RESIZE_EW:     return XC_sb_h_double_arrow;
            case CursorType::RESIZE_NS:     return XC_sb_v_double_arrow;
            case CursorType::RESIZE_NWSE:   return XC_bottom_right_corner;
            case CursorType::RESIZE_NESW:   return XC_bottom_left_corner;
            case CursorType::RESIZE_ALL:    return XC_fleur;
            case CursorType::NOT_ALLOWED:   return XC_X_cursor;
        }
        return XC_left_ptr;
    }

}

bool WindowLinux::openDisplay() {
    // The engine touches the display from the async loading thread (framebuffer
    // creation and window size queries) while the main thread polls for events,
    // so Xlib has to be told the connection is shared. Without this the client
    // aborts with "Unknown sequence number while processing queue" the first
    // time two threads interleave a request. The editor does the same before it
    // opens its own connection.
    XInitThreads();

    gDisplay = XOpenDisplay(nullptr);
    if (!gDisplay) {
        std::fprintf(stderr, "Error: Could not open the X display. "
                     "Set DISPLAY or install/enable XWayland.\n");
        return false;
    }
    gOwnsDisplay = true;
    gScreen = DefaultScreen(gDisplay);
    gRoot = RootWindow(gDisplay, gScreen);
    internAtoms();
    return true;
}

bool WindowLinux::create(const WindowLinuxConfig& config) {
    if (!gDisplay || !config.visual) return false;

    gWidth = config.width;
    gHeight = config.height;
    gResizable = config.resizable;

    gColormap = XCreateColormap(gDisplay, gRoot, config.visual, AllocNone);

    XSetWindowAttributes attributes{};
    attributes.colormap = gColormap;
    attributes.background_pixmap = None;
    // border_pixel is required whenever the visual differs from the root's
    attributes.border_pixel = 0;
    attributes.event_mask =
        StructureNotifyMask | PropertyChangeMask | ExposureMask | FocusChangeMask |
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | EnterWindowMask | LeaveWindowMask;

    gWindow = XCreateWindow(
        gDisplay, gRoot, 0, 0,
        static_cast<unsigned int>(config.width),
        static_cast<unsigned int>(config.height), 0,
        config.depth, InputOutput, config.visual,
        CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask, &attributes);
    if (!gWindow) {
        std::fprintf(stderr, "Error: Could not create the X window.\n");
        return false;
    }
    gOwnsWindow = true;

    XSetWMProtocols(gDisplay, gWindow, &gWmDeleteWindow, 1);
    setTitle(config.title);

    XClassHint* classHint = XAllocClassHint();
    if (classHint) {
        classHint->res_name = const_cast<char*>(config.appId.c_str());
        classHint->res_class = const_cast<char*>(config.appId.c_str());
        XSetClassHint(gDisplay, gWindow, classHint);
        XFree(classHint);
    }

    if (!gResizable) applySizeHints(config.width, config.height);

    createInvisibleCursor();
    return true;
}

void WindowLinux::adopt(Display* display, int screen, Window window) {
    gDisplay = display;
    gOwnsDisplay = false;
    gScreen = screen;
    gRoot = RootWindow(display, screen);
    gWindow = window;
    gOwnsWindow = false;
    internAtoms();
    createInvisibleCursor();
}

void WindowLinux::destroy() {
    if (!gDisplay) return;

    if (gPointerGrabbed) {
        XUngrabPointer(gDisplay, CurrentTime);
        gPointerGrabbed = false;
    }
    for (Cursor& cursor : gShapeCursors) {
        if (cursor != None) XFreeCursor(gDisplay, cursor);
        cursor = None;
    }
    if (gInvisibleCursor != None) {
        XFreeCursor(gDisplay, gInvisibleCursor);
        gInvisibleCursor = None;
    }
    if (gOwnsWindow && gWindow) XDestroyWindow(gDisplay, gWindow);
    if (gOwnsWindow && gColormap) XFreeColormap(gDisplay, gColormap);
    if (gOwnsDisplay) XCloseDisplay(gDisplay);

    gDisplay = nullptr;
    gWindow = 0;
    gColormap = 0;
    gOwnsWindow = gOwnsDisplay = false;
    gResizeCallback = nullptr;
    gWidth = gHeight = 0;
    gRelativeMouse = false;
    gHasSavedPointer = false;
    gCursorHidden = false;
    gCurrentCursor = None;
    gFullscreen = false;
}

Display* WindowLinux::display() { return gDisplay; }
int WindowLinux::screen() { return gScreen; }
Window WindowLinux::handle() { return gWindow; }

void WindowLinux::show() {
    if (!gDisplay || !gWindow) return;
    XMapWindow(gDisplay, gWindow);
    XFlush(gDisplay);
}

int WindowLinux::getWidth() { return gWidth; }
int WindowLinux::getHeight() { return gHeight; }

void WindowLinux::updateSize(int width, int height) {
    if (width == gWidth && height == gHeight) return;
    gWidth = width;
    gHeight = height;
    if (gResizeCallback) gResizeCallback(width, height);
}

void WindowLinux::setResizeCallback(std::function<void(int, int)> callback) {
    gResizeCallback = std::move(callback);
}

bool WindowLinux::isFullscreen() {
    return gFullscreen;
}

void WindowLinux::requestFullscreen() {
    if (gFullscreen) return;
    sendWmState(NET_WM_STATE_ADD, gWmStateFullscreen, None);
    gFullscreen = true;
}

void WindowLinux::exitFullscreen() {
    if (!gFullscreen) return;
    sendWmState(NET_WM_STATE_REMOVE, gWmStateFullscreen, None);
    gFullscreen = false;
}

bool WindowLinux::isMaximized() {
    return hasWmState(gWmStateMaximizedVert, gWmStateMaximizedHorz);
}

bool WindowLinux::isMinimized() {
    return hasWmState(gWmStateHidden, None);
}

bool WindowLinux::isStateEvent(const XEvent& event) {
    return event.type == PropertyNotify && event.xproperty.atom == gWmState;
}

void WindowLinux::maximize() {
    sendWmState(NET_WM_STATE_ADD, gWmStateMaximizedVert, gWmStateMaximizedHorz);
}

void WindowLinux::restore() {
    sendWmState(NET_WM_STATE_REMOVE, gWmStateMaximizedVert, gWmStateMaximizedHorz);
}

void WindowLinux::setSize(int width, int height) {
    if (width < 1 || height < 1 || !gDisplay || !gWindow) return;

    // In fullscreen the display mode stays; this resizes the window that
    // exitFullscreen restores.
    if (gFullscreen) {
        gWidth = width;
        gHeight = height;
        return;
    }

    if (!gResizable) applySizeHints(width, height);
    XResizeWindow(gDisplay, gWindow, static_cast<unsigned int>(width),
                  static_cast<unsigned int>(height));
    XFlush(gDisplay);
}

bool WindowLinux::isResizable() {
    return gResizable;
}

void WindowLinux::setResizable(bool resizable) {
    if (!gDisplay || !gWindow) return;
    gResizable = resizable;

    int width = gWidth;
    int height = gHeight;
    if (!resizable) {
        XWindowAttributes attributes{};
        XGetWindowAttributes(gDisplay, gWindow, &attributes);
        width = attributes.width;
        height = attributes.height;
    }
    applySizeHints(width, height);
    XFlush(gDisplay);
}

void WindowLinux::setTitle(const std::string& title) {
    if (!gDisplay || !gWindow) return;
    XStoreName(gDisplay, gWindow, title.c_str());
    // _NET_WM_NAME is the UTF-8 title modern desktops actually read
    XChangeProperty(gDisplay, gWindow, gNetWmName, gUtf8String, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.c_str()),
                    static_cast<int>(title.size()));
    XFlush(gDisplay);
}

void WindowLinux::quit() {
    if (!gDisplay || !gWindow) return;
    XEvent event{};
    event.type = ClientMessage;
    event.xclient.window = gWindow;
    event.xclient.message_type = gWmProtocols;
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(gWmDeleteWindow);
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(gDisplay, gWindow, False, NoEventMask, &event);
    XFlush(gDisplay);
}

bool WindowLinux::isCloseEvent(const XEvent& event) {
    return event.type == ClientMessage &&
           event.xclient.message_type == gWmProtocols &&
           static_cast<Atom>(event.xclient.data.l[0]) == gWmDeleteWindow;
}

Cursor WindowLinux::invisibleCursor() {
    return gInvisibleCursor;
}

void WindowLinux::setMouseCursor(CursorType type) {
    if (!gDisplay || !gWindow) return;

    const int index = static_cast<int>(type);
    if (index < 0 || index >= CURSOR_TYPE_COUNT) return;
    if (gShapeCursors[index] == None)
        gShapeCursors[index] = XCreateFontCursor(gDisplay, cursorShape(type));
    if (gShapeCursors[index] == None) return;

    gCurrentCursor = gShapeCursors[index];
    // A hidden pointer keeps the invisible cursor until the mode changes
    if (!gCursorHidden) {
        XDefineCursor(gDisplay, gWindow, gCurrentCursor);
        XFlush(gDisplay);
    }
}

void WindowLinux::setMouseMode(MouseMode mode) {
    if (gCurrentCursor == None) setMouseCursor(CursorType::ARROW);

    switch (mode) {
        case MouseMode::NORMAL:
            setPointer(false, false, gCurrentCursor);
            break;
        case MouseMode::HIDDEN:
            setPointer(false, false, gInvisibleCursor);
            break;
        case MouseMode::CAPTURED:
            setPointer(true, true, gInvisibleCursor);
            centerPointer();
            break;
        case MouseMode::CONFINED:
            setPointer(false, true, gCurrentCursor);
            break;
    }
}

void WindowLinux::setMousePosition(float x, float y) {
    if (!gDisplay || !gWindow) return;
    gLastWarpX = static_cast<int>(x);
    gLastWarpY = static_cast<int>(y);
    XWarpPointer(gDisplay, None, gWindow, 0, 0, 0, 0, gLastWarpX, gLastWarpY);
    XFlush(gDisplay);
}

void WindowLinux::setPointer(bool relative, bool confined, Cursor cursor) {
    if (!gDisplay || !gWindow) return;

    // Relative mode warps the pointer away, so remember where it was
    if (relative && !gHasSavedPointer) {
        Window root = None;
        Window child = None;
        int rootX = 0, rootY = 0, winX = 0, winY = 0;
        unsigned int mask = 0;
        if (XQueryPointer(gDisplay, gWindow, &root, &child, &rootX, &rootY, &winX, &winY, &mask)) {
            gSavedPointerX = winX;
            gSavedPointerY = winY;
            gHasSavedPointer = true;
        }
    }

    if (gPointerGrabbed) {
        XUngrabPointer(gDisplay, CurrentTime);
        gPointerGrabbed = false;
    }
    gRelativeMouse = false;

    if (relative || confined) {
        const int result = XGrabPointer(
            gDisplay, gWindow, True,
            PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
            GrabModeAsync, GrabModeAsync,
            confined ? gWindow : None, cursor, CurrentTime);
        gPointerGrabbed = result == GrabSuccess;
        // Relative motion is measured against our own warp, so it only means
        // anything while the grab holds
        gRelativeMouse = relative && gPointerGrabbed;
    }

    gCursorHidden = cursor != None && cursor == gInvisibleCursor;
    if (cursor != None) XDefineCursor(gDisplay, gWindow, cursor);
    else XUndefineCursor(gDisplay, gWindow);
    XFlush(gDisplay);

    if (!relative && gHasSavedPointer) {
        gHasSavedPointer = false;
        setMousePosition(static_cast<float>(gSavedPointerX), static_cast<float>(gSavedPointerY));
    }
}

bool WindowLinux::isRelativeMouse() {
    return gRelativeMouse;
}

bool WindowLinux::isPointerGrabbed() {
    return gPointerGrabbed;
}

void WindowLinux::releasePointerGrab() {
    if (!gDisplay || !gPointerGrabbed) return;
    XUngrabPointer(gDisplay, CurrentTime);
    gPointerGrabbed = false;
}

void WindowLinux::releasePointer() {
    releasePointerGrab();
    gRelativeMouse = false;
}

void WindowLinux::discardSavedPointer() {
    gHasSavedPointer = false;
}

void WindowLinux::centerPointer() {
    if (!gDisplay || !gWindow) return;
    gLastWarpX = gWidth / 2;
    gLastWarpY = gHeight / 2;
    XWarpPointer(gDisplay, None, gWindow, 0, 0, 0, 0, gLastWarpX, gLastWarpY);
    XFlush(gDisplay);
}

int WindowLinux::lastWarpX() { return gLastWarpX; }
int WindowLinux::lastWarpY() { return gLastWarpY; }
