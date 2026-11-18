// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Backend.h"
#include "EditorHost.h"
#include "EditorFrame.h"
#include "GamepadWin.h"
#include "WindowWin.h"
#include "renderer/Renderer.h"

#include "Engine.h"

#include "imgui_impl_win32.h"

#include "nfd.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <xinput.h>

#if defined(SOKOL_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

using namespace doriax;
using doriax::editor::PlatformMenuCallback;
using doriax::editor::PlatformMenuCommand;
using doriax::editor::PlatformMenuItem;
using doriax::editor::PlatformMenuItemType;
using doriax::editor::PlatformMenuModel;

namespace {

constexpr wchar_t WINDOW_CLASS_NAME[] = L"DoriaxEditorNativeWindow";
constexpr UINT WM_DORIAX_WAKE = WM_APP + 1;
constexpr UINT FIRST_MENU_COMMAND = 0x1000;
constexpr UINT_PTR LIVE_RESIZE_TIMER_ID = 0xD04A;
constexpr UINT LIVE_RESIZE_INTERVAL_MS = 16;
#if defined(SOKOL_VULKAN)
constexpr UINT WINDOW_CLASS_STYLE = CS_HREDRAW | CS_VREDRAW;
#else
// OpenGL keeps a device context per window, which needs a private DC
constexpr UINT WINDOW_CLASS_STYLE = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
#endif

struct NativeMenu {
    HMENU handle = nullptr;
    PlatformMenuModel model;
    std::unordered_map<UINT, PlatformMenuCommand> commands;
    std::vector<PlatformMenuCommand> pendingCommands;
};

struct WinBackendData {
    // Cached from WindowWin, which owns the window and the cursor state
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    bool shouldClose = false;
    bool redrawRequested = false;
    bool inSizeMove = false;
    bool gameCursorInSceneRect = false;
    bool mouseControlSuspended = false;
    MouseMode gameMouseMode = MouseMode::NORMAL;
    double virtualMouseX = 0.0;
    double virtualMouseY = 0.0;
    LONG rawMouseX = 0;
    LONG rawMouseY = 0;
    double framePeriod = 1.0 / 60.0;
    // Win32 blocks the outer frame loop while a sizing border is being
    // dragged. This callback renders from that modal message loop instead.
    std::function<void()> liveResizeFrame;
    void (*createImGuiWindow)(ImGuiViewport*) = nullptr;
    NativeMenu menu;
    bool nativeLoadingPaint = true;

    GamepadWin gamepads;
    std::unique_ptr<editor::Renderer> renderer;
    editor::EditorFrame editorFrame;
};

WinBackendData* backend = nullptr;
nfdwindowhandle_t nativeWindowHandle{};

void applyDarkWindowTheme(HWND window) {
    if (!window) return;

    const BOOL enabled = TRUE;
    // Windows 10 versions before 20H1 used attribute 19. Newer Windows 10
    // releases and Windows 11 use attribute 20.
    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD immersiveDarkModeLegacy = 19;
    if (FAILED(DwmSetWindowAttribute(
            window, immersiveDarkMode, &enabled, sizeof(enabled)))) {
        DwmSetWindowAttribute(
            window, immersiveDarkModeLegacy, &enabled, sizeof(enabled));
    }

    // Windows 11 supports explicit non-client colors. Calls fail harmlessly
    // on older releases, where the immersive-dark attributes above apply.
    constexpr DWORD borderColorAttribute = 34;
    constexpr DWORD captionColorAttribute = 35;
    constexpr DWORD textColorAttribute = 36;
    const COLORREF borderColor = RGB(45, 45, 48);
    const COLORREF captionColor = RGB(31, 31, 31);
    const COLORREF textColor = RGB(240, 240, 240);
    DwmSetWindowAttribute(window, borderColorAttribute,
                          &borderColor, sizeof(borderColor));
    DwmSetWindowAttribute(window, captionColorAttribute,
                          &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(window, textColorAttribute,
                          &textColor, sizeof(textColor));
}

void createThemedImGuiWindow(ImGuiViewport* viewport) {
    if (backend && backend->createImGuiWindow)
        backend->createImGuiWindow(viewport);
    applyDarkWindowTheme(static_cast<HWND>(viewport->PlatformHandleRaw));
}

std::string wideToUtf8(const wchar_t* text, int length) {
    if (!text || length <= 0) return {};
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), count,
                        nullptr, nullptr);
    return result;
}

#if defined(DORIAX_NATIVE_MENU)

std::wstring menuLabel(const PlatformMenuItem& item) {
    std::string label;
    label.reserve(item.label.size() + item.shortcut.size() + 2);
    for (char ch : item.label) {
        label.push_back(ch);
        if (ch == '&') label.push_back('&');
    }
    if (!item.shortcut.empty()) {
        label.push_back('\t');
        label += item.shortcut;
    }
    return winUtf8ToWide(label);
}

bool appendMenuItems(
    HMENU menu, const std::vector<PlatformMenuItem>& items, UINT& nextCommand,
    std::unordered_map<UINT, PlatformMenuCommand>& commands) {
    for (const PlatformMenuItem& item : items) {
        if (item.type == PlatformMenuItemType::Separator) {
            if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)) return false;
            continue;
        }

        UINT flags = MF_STRING;
        if (!item.enabled) flags |= MF_GRAYED;
        if (item.checked) flags |= MF_CHECKED;
        const std::wstring label = menuLabel(item);
        if (item.type == PlatformMenuItemType::Submenu) {
            HMENU submenu = CreatePopupMenu();
            if (!submenu) return false;
            if (!appendMenuItems(submenu, item.children, nextCommand, commands) ||
                !AppendMenuW(menu, flags | MF_POPUP,
                             reinterpret_cast<UINT_PTR>(submenu), label.c_str())) {
                DestroyMenu(submenu);
                return false;
            }
            continue;
        }

        if (nextCommand >= 0xF000) return false;
        const UINT commandId = nextCommand++;
        if (!AppendMenuW(menu, flags, commandId, label.c_str())) return false;
        commands.emplace(commandId, item.command);
    }
    return true;
}

bool rebuildNativeMenu(const PlatformMenuModel& model) {
    HMENU newMenu = CreateMenu();
    if (!newMenu) return false;
    std::unordered_map<UINT, PlatformMenuCommand> commands;
    UINT nextCommand = FIRST_MENU_COMMAND;
    if (!appendMenuItems(newMenu, model.menus, nextCommand, commands)) {
        DestroyMenu(newMenu);
        return false;
    }

    RECT oldClient{};
    RECT oldWindow{};
    GetClientRect(backend->window, &oldClient);
    GetWindowRect(backend->window, &oldWindow);
    HMENU oldMenu = backend->menu.handle;
    if (!SetMenu(backend->window, newMenu)) {
        DestroyMenu(newMenu);
        return false;
    }
    backend->menu.handle = newMenu;
    backend->menu.model = model;
    backend->menu.commands = std::move(commands);
    DrawMenuBar(backend->window);

    // A menu can change the non-client height when it is attached or wraps.
    // Keep the editor's saved client dimensions stable.
    if (!IsZoomed(backend->window)) {
        RECT newClient{};
        GetClientRect(backend->window, &newClient);
        const LONG oldHeight = oldClient.bottom - oldClient.top;
        const LONG newHeight = newClient.bottom - newClient.top;
        if (oldHeight != newHeight) {
            SetWindowPos(backend->window, nullptr, 0, 0,
                         oldWindow.right - oldWindow.left,
                         oldWindow.bottom - oldWindow.top + oldHeight - newHeight,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (oldMenu) DestroyMenu(oldMenu);
    backend->redrawRequested = true;
    return true;
}

#endif

void releaseRelativeMouse() {
    WindowWin::setRelativeMouse(false, true);
    backend->rawMouseX = backend->rawMouseY = 0;
}

void showEditorCursor() {
    if (!backend) return;
    releaseRelativeMouse();
    WindowWin::setCursorConfined(false);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    WindowWin::setCursorHidden(false);
}

void hideEditorCursor() {
    if (!backend) return;
    releaseRelativeMouse();
    WindowWin::setCursorConfined(false);
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    WindowWin::setCursorHidden(true);
}

void confineEditorCursor() {
    if (!backend) return;
    releaseRelativeMouse();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    WindowWin::setCursorHidden(false);
    WindowWin::setCursorConfined(true);
}

void applyHoverVisibility(bool force = false) {
    if (!backend || backend->mouseControlSuspended ||
        backend->gameMouseMode != MouseMode::HIDDEN)
        return;
    const bool shouldHide = backend->gameCursorInSceneRect;
    if (!force && shouldHide == WindowWin::isCursorHidden()) return;
    if (shouldHide) hideEditorCursor();
    else showEditorCursor();
}

void applyRelativeMouseData() {
    if (!WindowWin::isRelativeMouse() || !WindowWin::hasFocus()) {
        backend->rawMouseX = backend->rawMouseY = 0;
        return;
    }
    backend->virtualMouseX += backend->rawMouseX;
    backend->virtualMouseY += backend->rawMouseY;
    backend->rawMouseX = backend->rawMouseY = 0;

    // WM_MOUSEMOVE still queues the physical cursor position while raw input is
    // active. If a key event follows it, ImGui's input trickling can defer the
    // virtual position below until the next frame and expose the physical one
    // as a large look delta. Override MousePos now and make UpdateInputEvents
    // discard every queued absolute position for this frame. NavUpdate clears
    // WantSetMousePos before the next platform frame, so the Win32 backend does
    // not warp the OS cursor in response to this flag.
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(
        static_cast<float>(backend->virtualMouseX),
        static_cast<float>(backend->virtualMouseY));
    io.WantSetMousePos = true;
}

void pollGamepads() {
    backend->gamepads.poll();
}

void handleRawInput(HRAWINPUT handle) {
    if (!WindowWin::isRelativeMouse()) return;
    LONG deltaX = 0;
    LONG deltaY = 0;
    if (!WindowWin::readRawMouseDelta(handle, deltaX, deltaY)) return;
    backend->rawMouseX += deltaX;
    backend->rawMouseY += deltaY;
    backend->redrawRequested = true;
}

void handleDrop(HDROP drop) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::string> paths;
    paths.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, index, path.data(), length + 1);
        path.resize(length);
        paths.push_back(wideToUtf8(path.c_str(), static_cast<int>(path.size())));
    }
    DragFinish(drop);
    if (!paths.empty()) editor::Backend::getApp().handleExternalDrop(paths);
}

void paintNativeLoading(HWND window, HDC deviceContext) {
    RECT client{};
    GetClientRect(window, &client);
    const HBRUSH brush = CreateSolidBrush(RGB(41, 38, 36));
    FillRect(deviceContext, &client, brush);
    DeleteObject(brush);

    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, RGB(191, 186, 186));
    const int fontHeight = -MulDiv(20, GetDeviceCaps(deviceContext, LOGPIXELSY), 72);
    HFONT font = CreateFontW(
        fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ previousFont = font ? SelectObject(deviceContext, font) : nullptr;
    DrawTextW(deviceContext, L"Loading Doriax Engine...", -1, &client,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (font) {
        SelectObject(deviceContext, previousFont);
        DeleteObject(font);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (backend) {
        switch (message) {
            case WM_INPUT:
                handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
                return DefWindowProcW(window, message, wParam, lParam);
            case WM_COMMAND:
                if (HIWORD(wParam) == 0 && lParam == 0) {
                    const auto command = backend->menu.commands.find(LOWORD(wParam));
                    if (command != backend->menu.commands.end()) {
                        backend->menu.pendingCommands.push_back(command->second);
                        backend->redrawRequested = true;
                        return 0;
                    }
                }
                break;
            case WM_DROPFILES:
                handleDrop(reinterpret_cast<HDROP>(wParam));
                backend->redrawRequested = true;
                return 0;
            case WM_CLOSE:
                editor::Backend::getApp().exit();
                backend->redrawRequested = true;
                return 0;
            case WM_DORIAX_WAKE:
                backend->redrawRequested = true;
                return 0;
            case WM_SETFOCUS:
                WindowWin::updateCursorClip();
                backend->redrawRequested = true;
                break;
            case WM_KILLFOCUS: {
                HWND focused = reinterpret_cast<HWND>(wParam);
                DWORD processId = 0;
                if (focused) GetWindowThreadProcessId(focused, &processId);
                if (processId == GetCurrentProcessId()) {
                    WindowWin::releaseCursorClip();
                    WindowWin::resetRawMouseDelta();
                } else {
                    WindowWin::handleFocusLost();
                }
                backend->rawMouseX = backend->rawMouseY = 0;
                backend->redrawRequested = true;
                break;
            }
            case WM_MOVE:
            case WM_DISPLAYCHANGE:
                WindowWin::updateCursorClip();
                backend->redrawRequested = true;
                break;
            case WM_SIZE:
                WindowWin::refreshClientSize();
                WindowWin::updateCursorClip();
                backend->redrawRequested = true;
                break;
            case WM_ENTERSIZEMOVE:
                backend->inSizeMove = true;
                SetTimer(window, LIVE_RESIZE_TIMER_ID,
                         LIVE_RESIZE_INTERVAL_MS, nullptr);
                backend->redrawRequested = true;
                return 0;
            case WM_EXITSIZEMOVE:
                KillTimer(window, LIVE_RESIZE_TIMER_ID);
                backend->inSizeMove = false;
                backend->redrawRequested = true;
                return 0;
            case WM_TIMER:
                if (wParam == LIVE_RESIZE_TIMER_ID && backend->inSizeMove) {
                    if (backend->liveResizeFrame) backend->liveResizeFrame();
                    return 0;
                }
                break;
            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED:
                applyDarkWindowTheme(window);
                RedrawWindow(window, nullptr, nullptr,
                             RDW_FRAME | RDW_INVALIDATE);
                backend->redrawRequested = true;
                break;
            case WM_MOUSEMOVE:
            case WM_NCMOUSEMOVE:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                backend->redrawRequested = true;
                break;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT && WindowWin::isCursorHidden()) {
                    SetCursor(nullptr);
                    return TRUE;
                }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                HDC deviceContext = BeginPaint(window, &paint);
                if (backend->nativeLoadingPaint)
                    paintNativeLoading(window, deviceContext);
                EndPaint(window, &paint);
                backend->redrawRequested = true;
                return 0;
            }
            default:
                break;
        }
    }

    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return TRUE;

    if (message == WM_DPICHANGED) {
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void updateFramePeriod() {
    HMONITOR monitor = MonitorFromWindow(backend->window, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1)
        backend->framePeriod = 1.0 / static_cast<double>(mode.dmDisplayFrequency);
}

bool initializeWindow(int width, int height, bool maximized) {
    WindowWinConfig config;
    config.title = "Doriax Engine";
    config.width = width;
    config.height = height;
    config.maximized = maximized;
    config.windowProc = windowProc;
    config.className = WINDOW_CLASS_NAME;
    config.classStyle = WINDOW_CLASS_STYLE;
    config.icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), L"GLFW_ICON", IMAGE_ICON, 0, 0,
        LR_DEFAULTSIZE | LR_SHARED));
    // ImGui's detached viewports are child windows of this one
    config.clipChildren = true;
    // The restored size is converted from the scale it was saved at, so it can
    // come out larger than this monitor.
    config.clampToWorkArea = true;
    if (!WindowWin::create(config)) return false;

    backend->instance = WindowWin::instance();
    backend->window = WindowWin::handle();

    applyDarkWindowTheme(backend->window);
    DragAcceptFiles(backend->window, TRUE);

    nativeWindowHandle.type = NFD_WINDOW_HANDLE_TYPE_WINDOWS;
    nativeWindowHandle.handle = backend->window;
    backend->gamepads.init();
    updateFramePeriod();
    WindowWin::show(maximized);
    return true;
}

void shutdownWindow() {
    if (!backend) return;
    backend->gamepads.shutdown();
    if (backend->window) {
        KillTimer(backend->window, LIVE_RESIZE_TIMER_ID);
        DragAcceptFiles(backend->window, FALSE);
        if (backend->menu.handle) {
            SetMenu(backend->window, nullptr);
            DestroyMenu(backend->menu.handle);
            backend->menu.handle = nullptr;
        }
        backend->window = nullptr;
    }
    WindowWin::destroy();
    delete backend;
    backend = nullptr;
    nativeWindowHandle = {};
}

// Window-system half of the renderer, see renderer/Renderer.h
#if defined(SOKOL_VULKAN)

int createSurface(ImGuiViewport* viewport, ImU64 instance, const void* allocator,
                  ImU64* surface) {
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = backend->instance;
    info.hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
    return static_cast<int>(vkCreateWin32SurfaceKHR(
        reinterpret_cast<VkInstance>(instance), &info,
        static_cast<const VkAllocationCallbacks*>(allocator),
        reinterpret_cast<VkSurfaceKHR*>(surface)));
}

bool initImGuiPlatform() {
    return ImGui_ImplWin32_Init(backend->window);
}

void installViewportHooks(ImGuiPlatformIO&) {}

editor::RendererPlatform rendererPlatform() {
    editor::RendererPlatform platform;
    platform.surfaceExtension = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
    platform.createSurface = createSurface;
    platform.requestRedraw = []() { backend->redrawRequested = true; };
    return platform;
}

#else // SOKOL_GLCORE

using CreateContextAttribsProc = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using SwapIntervalProc = BOOL (WINAPI*)(int);

constexpr int WGL_CONTEXT_MAJOR_VERSION = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT = 0x00000001;
constexpr int WGL_CONTEXT_FLAGS = 0x2094;
// WGL_ARB_create_context_robustness
constexpr int WGL_CONTEXT_ROBUST_ACCESS_BIT = 0x00000004;
constexpr int WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY = 0x8256;
constexpr int WGL_LOSE_CONTEXT_ON_RESET = 0x8252;

HGLRC glContext = nullptr;
bool glContextResetNotification = false;
int pixelFormat = 0;
SwapIntervalProc swapIntervalEXT = nullptr;
void (*imguiDestroyWindow)(ImGuiViewport*) = nullptr;
std::unordered_map<HWND, HDC> windowContexts;

template <typename T>
T wglProc(const char* name) {
    PROC address = wglGetProcAddress(name);
    if (!address || address == reinterpret_cast<PROC>(1) ||
        address == reinterpret_cast<PROC>(2) ||
        address == reinterpret_cast<PROC>(3) ||
        address == reinterpret_cast<PROC>(-1))
        return nullptr;
    return reinterpret_cast<T>(address);
}

PIXELFORMATDESCRIPTOR pixelFormatDescriptor() {
    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    return descriptor;
}

// Every window takes the main window's pixel format and keeps its own DC
HDC deviceContextOf(ImGuiViewport* viewport) {
    HWND window = viewport ? static_cast<HWND>(viewport->PlatformHandleRaw)
                           : backend->window;
    if (!window) return nullptr;
    auto existing = windowContexts.find(window);
    if (existing != windowContexts.end()) return existing->second;

    HDC deviceContext = GetDC(window);
    if (!deviceContext) return nullptr;
    const int existingFormat = GetPixelFormat(deviceContext);
    if (existingFormat == 0) {
        PIXELFORMATDESCRIPTOR descriptor = pixelFormatDescriptor();
        if (!SetPixelFormat(deviceContext, pixelFormat, &descriptor)) {
            ReleaseDC(window, deviceContext);
            return nullptr;
        }
    } else if (existingFormat != pixelFormat) {
        ReleaseDC(window, deviceContext);
        return nullptr;
    }
    windowContexts.emplace(window, deviceContext);
    return deviceContext;
}

// wglMakeCurrent is costly even when the context is already current
bool makeContextCurrent(HDC deviceContext) {
    if (wglGetCurrentContext() == glContext && wglGetCurrentDC() == deviceContext)
        return true;
    return wglMakeCurrent(deviceContext, glContext) != FALSE;
}

void destroyViewportWindow(ImGuiViewport* viewport) {
    HWND window = static_cast<HWND>(viewport->PlatformHandleRaw);
    auto existing = windowContexts.find(window);
    if (existing != windowContexts.end()) {
        ReleaseDC(window, existing->second);
        windowContexts.erase(existing);
    }
    if (imguiDestroyWindow) imguiDestroyWindow(viewport);
}

// InitForOpenGL gives viewport windows the private DC their context needs
bool initImGuiPlatform() {
    return ImGui_ImplWin32_InitForOpenGL(backend->window);
}

void installViewportHooks(ImGuiPlatformIO& platformIo) {
    imguiDestroyWindow = platformIo.Platform_DestroyWindow;
    platformIo.Platform_DestroyWindow = destroyViewportWindow;
}

editor::RendererPlatform rendererPlatform() {
    editor::RendererPlatform platform;
    platform.createContext = []() {
        HDC deviceContext = GetDC(backend->window);
        if (!deviceContext) {
            std::fprintf(stderr, "Error: Could not acquire the OpenGL device context.\n");
            return false;
        }
        PIXELFORMATDESCRIPTOR descriptor = pixelFormatDescriptor();
        pixelFormat = ChoosePixelFormat(deviceContext, &descriptor);
        if (!pixelFormat || !SetPixelFormat(deviceContext, pixelFormat, &descriptor)) {
            std::fprintf(stderr, "Error: No OpenGL pixel format available.\n");
            ReleaseDC(backend->window, deviceContext);
            return false;
        }
        windowContexts.emplace(backend->window, deviceContext);

        // wglCreateContextAttribsARB is only reachable through a current context
        HGLRC legacy = wglCreateContext(deviceContext);
        if (!legacy) {
            std::fprintf(stderr, "Error: Could not create an OpenGL context.\n");
            return false;
        }
        if (!wglMakeCurrent(deviceContext, legacy)) {
            wglDeleteContext(legacy);
            std::fprintf(stderr, "Error: Could not activate the OpenGL context.\n");
            return false;
        }
        auto createContext = wglProc<CreateContextAttribsProc>(
            "wglCreateContextAttribsARB");
        if (createContext) {
            // A GPU reset goes unnoticed without a reset notification strategy:
            // the context keeps taking calls that never reach the screen.
            const int robustAttributes[] = {
                WGL_CONTEXT_MAJOR_VERSION, 4,
                WGL_CONTEXT_MINOR_VERSION, 1,
                WGL_CONTEXT_PROFILE_MASK, WGL_CONTEXT_CORE_PROFILE_BIT,
                WGL_CONTEXT_FLAGS, WGL_CONTEXT_ROBUST_ACCESS_BIT,
                WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY, WGL_LOSE_CONTEXT_ON_RESET,
                0
            };
            glContext = createContext(deviceContext, nullptr, robustAttributes);
            glContextResetNotification = glContext != nullptr;
            if (!glContext) {
                const int attributes[] = {
                    WGL_CONTEXT_MAJOR_VERSION, 4,
                    WGL_CONTEXT_MINOR_VERSION, 1,
                    WGL_CONTEXT_PROFILE_MASK, WGL_CONTEXT_CORE_PROFILE_BIT,
                    0
                };
                glContext = createContext(deviceContext, nullptr, attributes);
            }
        }
        wglMakeCurrent(nullptr, nullptr);
        if (!glContext) {
            wglDeleteContext(legacy);
            std::fprintf(stderr, "Error: Could not create an OpenGL 4.1 core context.\n");
            return false;
        }
        wglDeleteContext(legacy);
        if (!wglMakeCurrent(deviceContext, glContext)) {
            std::fprintf(stderr, "Error: Could not activate the OpenGL 4.1 context.\n");
            return false;
        }
        swapIntervalEXT = wglProc<SwapIntervalProc>("wglSwapIntervalEXT");
        return true;
    };
    platform.destroyContext = []() {
        if (glContext) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glContext);
            glContext = nullptr;
        }
        glContextResetNotification = false;
        for (const auto& entry : windowContexts) ReleaseDC(entry.first, entry.second);
        windowContexts.clear();
        swapIntervalEXT = nullptr;
        pixelFormat = 0;
    };
    platform.makeCurrent = [](ImGuiViewport* viewport) {
        HDC deviceContext = deviceContextOf(viewport);
        if (!deviceContext || !makeContextCurrent(deviceContext))
            std::fprintf(stderr, "Error: Could not activate an OpenGL viewport.\n");
    };
    platform.swapBuffers = [](ImGuiViewport* viewport) {
        HDC deviceContext = deviceContextOf(viewport);
        if (!deviceContext || !makeContextCurrent(deviceContext)) {
            std::fprintf(stderr, "Error: Could not activate an OpenGL viewport.\n");
            return;
        }
        if (viewport && swapIntervalEXT) swapIntervalEXT(0);
        SwapBuffers(deviceContext);
    };
    platform.setSwapInterval = [](int interval) {
        if (swapIntervalEXT) swapIntervalEXT(interval);
    };
    platform.getProcAddress = [](const char* name) {
        return wglProc<editor::GLProc>(name);
    };
    platform.hasResetNotification = []() { return glContextResetNotification; };
    return platform;
}

#endif

// Play expects these keys' chars, but Alt combos post WM_SYSCHAR (not queued by
// imgui_impl_win32) and Ctrl+Backspace posts 0x7f
unsigned int controlCharForKey(const MSG& message) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
        return 0;
    switch (message.wParam) {
        case VK_BACK: return '\b';
        case VK_TAB: return '\t';
        case VK_RETURN: return '\r';
        case VK_ESCAPE: return 0x1b;
        default: return 0;
    }
}

void translateAndDispatch(MSG& message) {
    unsigned int control = controlCharForKey(message);
    if (!control) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        return;
    }

    // Earlier presses' chars go first, leaving only this press's after translating
    MSG pending{};
    while (PeekMessageW(&pending, message.hwnd, WM_CHAR, WM_CHAR, PM_REMOVE))
        DispatchMessageW(&pending);
    TranslateMessage(&message);
    std::vector<MSG> chars;
    while (PeekMessageW(&pending, message.hwnd, WM_CHAR, WM_CHAR, PM_REMOVE)) {
        if (pending.wParam == control || (control == '\r' && pending.wParam == '\n'))
            control = 0;
        chars.push_back(pending);
    }

    DispatchMessageW(&message);
    for (const MSG& c : chars) DispatchMessageW(&c);
    if (control && ImGui::GetCurrentContext())
        ImGui::GetIO().AddInputCharacter(control);
}

void processMessages(bool waitForMessage) {
    if (waitForMessage)
        MsgWaitForMultipleObjectsEx(
            0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            backend->shouldClose = true;
            continue;
        }
        translateAndDispatch(message);
    }
}

} // namespace

std::string editor::Backend::title;

int editor::Backend::init(int argc, char* argv[]) {
    App& app = getApp();
    setEditorHost(&app);
    app.initializeSettings();

    backend = new WinBackendData();

    // Before the window exists and before any monitor DPI is read, or the query
    // below returns the 96 DPI Windows reports to unaware processes. ImGui's
    // own helper only covers the calling thread, which is not enough.
    WindowWin::enableDpiAwareness();

    const float uiScale = WindowWin::monitorScale(nullptr);
    const int initialWidth = app.getInitialWindowWidth(uiScale);
    const int initialHeight = app.getInitialWindowHeight(uiScale);
    if (!initializeWindow(initialWidth, initialHeight,
                          app.getInitialWindowMaximized())) {
        shutdownWindow();
        return -1;
    }
    processMessages(false);

    if (NFD_Init() != NFD_OKAY) {
        std::fprintf(stderr, "Error: NFD_Init failed: %s\n", NFD_GetError());
        shutdownWindow();
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!initImGuiPlatform()) {
        ImGui::DestroyContext();
        NFD_Quit();
        shutdownWindow();
        return -1;
    }
    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    backend->createImGuiWindow = platformIO.Platform_CreateWindow;
    platformIO.Platform_CreateWindow = createThemedImGuiWindow;
    installViewportHooks(platformIO);

    app.setup();
    processMessages(false);
    int clientWidth = 0;
    int clientHeight = 0;
    WindowWin::getClientSize(clientWidth, clientHeight);
    backend->renderer = std::make_unique<editor::Renderer>();
    if (!backend->renderer->init(
            rendererPlatform(), clientWidth, clientHeight, true)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        NFD_Quit();
        backend->renderer.reset();
        shutdownWindow();
        return -1;
    }
    processMessages(false);

    backend->editorFrame.init(*backend->renderer, app, []() {
        ImGui_ImplWin32_NewFrame();
        applyRelativeMouseData();
    });

    bool frameInProgress = false;
    bool engineStarted = false;

    auto renderFrame = [&](bool forceRedraw) {
        if (frameInProgress || backend->shouldClose) return;
        frameInProgress = true;
        pollGamepads();
        WindowWin::updateCursorClip();

        editor::EditorFrameState state{backend->redrawRequested};
        state.framePeriod = backend->framePeriod;
        state.forceRedraw = forceRedraw;
        state.minimized = IsIconic(backend->window) != FALSE;
        state.focused = WindowWin::hasFocus();
        WindowWin::getClientSize(state.width, state.height);
        if (!backend->editorFrame.run(state))
            backend->shouldClose = true;
        if (!state.minimized)
            backend->nativeLoadingPaint = false;
        frameInProgress = false;
    };

    app.setLoadingPump([&](bool render) {
        processMessages(false);
        if (render && !backend->shouldClose)
            renderFrame(true);
    });

    if (!backend->shouldClose) {
        app.engineInit(argc, argv);
        engineStarted = true;
        // Keep the initial hide soft so ImGui can restore the normal editor cursor.
        SetCursor(nullptr);
    }
    bool engineViewReady = false;
    if (engineStarted && !backend->shouldClose) {
        app.engineViewLoaded();
        engineViewReady = true;
    }
    if (engineStarted && !backend->shouldClose)
        app.loadStartupProject();

    app.setWakeCallback([]() {
        if (backend && backend->window)
            PostMessageW(backend->window, WM_DORIAX_WAKE, 0, 0);
    });

    backend->liveResizeFrame = [&]() { renderFrame(true); };
    while (!backend->shouldClose) {
        processMessages(backend->editorFrame.isIdle());
        if (backend->shouldClose) break;
        app.processProjectChange();
        if (backend->shouldClose) break;
        renderFrame(false);
        if (app.consumeBenchmarkExit())
            backend->shouldClose = true;
    }
    backend->liveResizeFrame = {};
    KillTimer(backend->window, LIVE_RESIZE_TIMER_ID);
    backend->inSizeMove = false;

    app.shutdownBackgroundWork();
    int width = 0;
    int height = 0;
    WindowWin::getClientSize(width, height);
    app.saveWindowSettings(width, height, IsZoomed(backend->window) != FALSE,
                           WindowWin::monitorScale(backend->window));

    backend->renderer->shutdownImGui();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (engineViewReady)
        app.engineViewDestroyed();
    NFD_Quit();
    backend->renderer.reset();
    shutdownWindow();
    if (engineStarted)
        app.engineShutdown();
    return app.getExitCode();
}

editor::App& editor::Backend::getApp() {
    static App app;
    return app;
}

void editor::Backend::disableMouseCursor() {
    if (!backend) return;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;
    if (!WindowWin::isRelativeMouse()) {
        POINT position{};
        const bool hasPosition = GetCursorPos(&position) != FALSE;
        if (io.MousePos.x > -FLT_MAX && io.MousePos.y > -FLT_MAX) {
            backend->virtualMouseX = io.MousePos.x;
            backend->virtualMouseY = io.MousePos.y;
        } else if (hasPosition) {
            if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
                ScreenToClient(backend->window, &position);
            backend->virtualMouseX = position.x;
            backend->virtualMouseY = position.y;
        }
    }
    // Saves the cursor position and clips it to the client area
    WindowWin::setRelativeMouse(true, true);
    backend->rawMouseX = backend->rawMouseY = 0;
    WindowWin::setCursorHidden(true);
}

void editor::Backend::enableMouseCursor() {
    if (backend->mouseControlSuspended) showEditorCursor();
    else setMouseMode(backend->gameMouseMode);
}

void editor::Backend::setMouseControlSuspended(bool suspended) {
    if (!backend || backend->mouseControlSuspended == suspended) return;
    backend->mouseControlSuspended = suspended;
    if (suspended) showEditorCursor();
    else setMouseMode(backend->gameMouseMode);
}

void editor::Backend::setMouseMode(MouseMode mode) {
    if (!backend) return;
    backend->gameMouseMode = mode;
    if (backend->mouseControlSuspended) return;
    switch (mode) {
        case MouseMode::CAPTURED: disableMouseCursor(); break;
        case MouseMode::CONFINED: confineEditorCursor(); break;
        case MouseMode::HIDDEN: applyHoverVisibility(true); break;
        case MouseMode::NORMAL: showEditorCursor(); break;
    }
}

void editor::Backend::setGameCursorInSceneRect(bool inSceneRect) {
    if (!backend) return;
    backend->gameCursorInSceneRect = inSceneRect;
    applyHoverVisibility();
}

void editor::Backend::closeWindow() {
    if (!backend) return;
    backend->shouldClose = true;
    if (backend->window) PostMessageW(backend->window, WM_DORIAX_WAKE, 0, 0);
}

bool editor::Backend::isRunningOnWayland() {
    return false;
}

ImVec2 editor::Backend::sceneRenderScale(ImVec2 framebufferScale, float dpiScale) {
    (void)dpiScale;
    if (framebufferScale.x <= 0.0f) framebufferScale.x = 1.0f;
    if (framebufferScale.y <= 0.0f) framebufferScale.y = 1.0f;
    return framebufferScale;
}

float editor::Backend::setMainMenu(const PlatformMenuModel& model,
                                   PlatformMenuCallback callback) {
#if !defined(DORIAX_NATIVE_MENU)
    // App draws the menu bar with ImGui
    (void)model;
    (void)callback;
    return 0.0f;
#else
    if (!backend || !backend->window) return 0.0f;
    if (!backend->menu.handle || backend->menu.model.menus != model.menus) {
        if (!rebuildNativeMenu(model))
            return backend->menu.handle ? -1.0f : 0.0f;
    }
    std::vector<PlatformMenuCommand> pendingCommands;
    pendingCommands.swap(backend->menu.pendingCommands);
    for (const PlatformMenuCommand& command : pendingCommands)
        if (callback) callback(command);
    // HMENU is non-client UI, so ImGui must suppress its fallback without
    // reserving an in-client side bar.
    return -1.0f;
#endif
}

ImTextureID editor::Backend::getImGuiTexture(TextureRender* texture) {
    if (!texture || !texture->isCreated()) return ImTextureID{};
    return backend->renderer->getTexture(texture);
}

#if defined(SOKOL_VULKAN)

sg_environment editor::Backend::getSokolEnvironment() {
    return backend->renderer->getSokolEnvironment();
}

sg_swapchain editor::Backend::getSokolSwapchain() {
    return backend->renderer->getSokolSwapchain();
}

#endif

void editor::Backend::updateWindowTitle(const std::string& projectName) {
    title = editor::EditorFrame::formatWindowTitle(projectName);
    WindowWin::setTitle(title);
}

void* editor::Backend::getNFDWindowHandle() {
    return &nativeWindowHandle;
}
