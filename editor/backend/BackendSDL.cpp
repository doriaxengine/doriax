#include "Backend.h"
#include "EditorHost.h"
#include "AppSettings.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_opengl.h>

#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <cstdlib>

#include "nfd.hpp"
#include "nfd_sdl3.h"

using namespace doriax;

static SDL_Window* window = nullptr;
static std::vector<std::string> droppedPaths;
static nfdwindowhandle_t nativeWindow;
static bool shouldClose = false;
static SDL_Cursor* invisibleCursor = nullptr;
static MouseMode gameMouseMode = MouseMode::NORMAL;
static bool gameCursorInSceneRect = false;
static bool gameCursorHidden = false;
// While true the editor has reclaimed the cursor (play paused or loading).
// gameMouseMode still holds the game's intent and is reapplied when this clears.
// Driven every frame from the main loop.
static bool mouseControlSuspended = false;

// for work with mingw32
int SDL_main(int argc, char* argv[]) {
    return editor::Backend::init(argc, argv);
}

editor::App editor::Backend::app;
std::string editor::Backend::title;

static void hideEditorCursor() {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_SetWindowMouseGrab(window, false);
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (invisibleCursor) {
        SDL_SetCursor(invisibleCursor);
    }
    SDL_HideCursor();
    gameCursorHidden = true;
}

static void showEditorCursor() {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;

    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_SetWindowMouseGrab(window, true);
    SDL_ShowCursor();
    SDL_SetCursor(SDL_GetDefaultCursor());
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    gameCursorHidden = false;
}

// CONFINED: keep the cursor visible but trapped inside the editor window.
static void confineEditorCursor() {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_ShowCursor();
    SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_SetWindowMouseGrab(window, true);
    gameCursorHidden = false;
}

// HIDDEN mode hides the cursor only while the pointer is over the game viewport,
// so the surrounding editor UI stays usable. The other modes don't depend on
// hover and are applied once by setMouseMode, so this is a no-op for them.
static void applyHoverVisibility(bool force = false) {
    if (mouseControlSuspended || gameMouseMode != MouseMode::HIDDEN) {
        return;
    }

    const bool shouldHide = gameCursorInSceneRect;
    if (!force && shouldHide == gameCursorHidden) {
        return;
    }

    if (shouldHide) {
        hideEditorCursor();
    } else {
        showEditorCursor();
    }
}

int editor::Backend::init(int argc, char* argv[]) {
    setEditorHost(&app);

    // Load settings early so the video-driver choice below can honor the persisted
    // multi-viewport preference (file I/O only, no window system needed).
    app.initializeSettings();

#ifdef __linux__
    // Match the GLFW backend: Dear ImGui multi-viewport needs to reposition OS
    // windows, which the SDL Wayland video driver doesn't support. Only when the
    // feature is enabled and we're on a Wayland session with XWayland reachable
    // (DISPLAY set), select the x11 driver. Respect an explicit user-provided
    // SDL_VIDEODRIVER. Must be set before SDL_Init().
    if (AppSettings::getMultiViewportEnabled() &&
        getenv("WAYLAND_DISPLAY") &&
        getenv("DISPLAY") &&
        !getenv("SDL_VIDEODRIVER")
    ) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    }
#endif

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Error: SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (NFD_Init() != NFD_OKAY) {
        printf("Error: NFD_Init failed: %s\n", NFD_GetError());
        return -1;
    }

    CameraRender render;

    // Set GL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    int sampleCount = 1;
    if (sampleCount > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, sampleCount);
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Get saved window dimensions from app
    int windowWidth = app.getInitialWindowWidth();
    int windowHeight = app.getInitialWindowHeight();
    int initWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    // Create window with OpenGL context
    window = SDL_CreateWindow(
        "Doriax Engine", windowWidth, windowHeight, initWindowFlags
    );

    // Get the scale of the window and not the display
    float window_scale = SDL_GetWindowDisplayScale(window);

    if (window_scale == 0.0f) {
        window_scale = 1.0f;
    }

    if (!window) {
        NFD_Quit();
        SDL_Quit();
        return -1;
    }

    // Apply saved window state
    if (app.getInitialWindowMaximized()) {
        SDL_MaximizeWindow(window);
    }

    SDL_Surface* cursorSurface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    if (cursorSurface) {
        SDL_memset(cursorSurface->pixels, 0, cursorSurface->pitch * cursorSurface->h);
        invisibleCursor = SDL_CreateColorCursor(cursorSurface, 0, 0);
        SDL_DestroySurface(cursorSurface);
    }

    NFD_GetNativeWindowFromSDLWindow(window, &nativeWindow);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        printf("Error: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        NFD_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // Initial default; project/Wayland policy is applied below.
    SDL_SetWindowPosition(
        window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED
    );
    SDL_ShowWindow(window);

    // Setup Dear ImGui context - MUST BE DONE BEFORE app.setup()
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Platform/Renderer bindings - MUST BE DONE AFTER ImGui::CreateContext()
    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    // Now we can safely call app.setup() which uses ImGui
    app.setup();
    app.engineInit(argc, argv);

    SDL_HideCursor();

    app.engineViewLoaded();

    // Main loop
    //
    // On Wayland a vsync'd SwapWindow waits on wl_surface.frame callbacks, and
    // the compositor stops sending those once the surface is hidden (minimized,
    // fully occluded, or the screen blanks). One blocked swap freezes this whole
    // loop — including the AI agent pump — until the window is visible again.
    // Wayland sessions are always composited (no tearing), so use swap interval
    // zero there and manually pace only while the project's VSync setting is on.
    const bool isWayland = isRunningOnWayland();
    if (isWayland) {
        SDL_GL_SetSwapInterval(0);
    }
    double framePeriod = 1.0 / 60.0;
    SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(0);
    if (displayMode && displayMode->refresh_rate > 0) {
        framePeriod = 1.0 / displayMode->refresh_rate;
    }
    const double perfFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    Project* activeProject = app.getProject();
    const bool initialFrameSyncEnabled = !activeProject->isPlaySessionActive() || activeProject->isVSyncEnabled();
    const int initialSwapInterval = (!isWayland && initialFrameSyncEnabled) ? 1 : 0;
    if (!isWayland && initialSwapInterval != 1) {
        SDL_GL_SetSwapInterval(initialSwapInterval);
    }

    bool done = false;
    int currentSwapInterval = initialSwapInterval;
    while (!done) {
        const Uint64 frameStart = SDL_GetPerformanceCounter();

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                // Handle quit event, but don't close immediately
                app.exit();
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
            {
                // Handle window close event, but don't close immediately
                app.exit();
            }
            if (event.type == SDL_EVENT_DROP_BEGIN) {
                droppedPaths.clear();
            }
            if (event.type == SDL_EVENT_DROP_FILE) {
                droppedPaths.push_back(event.drop.file);
                SDL_free(event.drop.file);
            }
            if (event.type == SDL_EVENT_DROP_COMPLETE) {
                app.handleExternalDrop(droppedPaths);
            }
        }

        // Check if we should close the window now (after potential confirmation dialogs)
        if (shouldClose) {
            done = true;
            continue;
        }

        // Skip presenting while minimized: SwapWindow of a hidden window can
        // block and stall clipboard + AI. Keep polling/updating so both keep
        // working.
        // On Wayland, "SDL_WINDOW_MINIMIZED" is not sent, when windows
        // are minimmized, unless we call "SDL_MinimizeWindow()" manually.
        const Uint32 windowFlags = SDL_GetWindowFlags(window);
        const bool minimized = (windowFlags & SDL_WINDOW_MINIMIZED) != 0;

        // On X11, with vsync on, SwapWindow can block waiting for a frame
        // callback the compositor won't send while the window is
        // unfocused/occluded, which stalls the event loop that serves clipboard
        // paste requests. Honor the project's VSync setting during Play mode and
        // keep regular editor use synchronized. Always drop the interval when
        // unfocused so SwapWindow never blocks; the delay below then paces the idle
        // loop. On Wayland, project VSync is implemented by manual pacing.
        const bool focused = (windowFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
        const bool frameSyncEnabled = !activeProject->isPlaySessionActive()
            || activeProject->isVSyncEnabled();

        // Hand the cursor back to the editor while a play session isn't actively
        // running (paused or loading) so a game-held cursor lock can't trap the
        // mouse; the game's mouse mode is restored on resume. Window focus is
        // deliberately NOT part of this gate: SDL releases relative-mode/grab on
        // focus loss natively (as an exported game does), and folding focus in here
        // broke capture under multi-viewport, where the main window reports
        // unfocused whenever input is on a viewport panel. No-op outside a session.
        setMouseControlSuspended(activeProject->isPlaySessionActive() && !activeProject->isMainScenePlaying());
        if (!isWayland) {
            const int desiredInterval = focused && frameSyncEnabled;
            if (desiredInterval != currentSwapInterval) {
                SDL_GL_SetSwapInterval(desiredInterval);
                currentSwapInterval = desiredInterval;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (!minimized) {
            app.engineRender();

            int display_w, display_h;
            SDL_GetWindowSizeInPixels(window, &display_w, &display_h);

            render.setClearColor(Vector4(0.45f, 0.55f, 0.60f, 1.00f));
            render.startRenderPass(display_w, display_h);
        } else {
            // engineRender() (skipped while minimized) is what normally drains
            // main-thread tasks. Keep draining them so AI-driven work, logging,
            // and async load callbacks still complete while minimized.
            app.processMainThreadTasks();
        }

        // Always run editor UI/update even while minimized (AI + clipboard).
        app.show();

        ImGui::Render();
        if (!minimized) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            render.endRenderPass();
        }

        // Multi-viewport: platform window maintenance must run every frame
        // (ImGui asserts otherwise), and torn-off panels can still be visible
        // while the main window is minimized. Render them in their own GL
        // contexts (they swap with vsync off and minimized ones are skipped),
        // then restore the main context so sokol's next-frame pass runs on the
        // right one.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window*   backup_window  = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_window, backup_context);
        }

        if (!minimized) {
            SDL_GL_SwapWindow(window);
        }

        // Pace the loop whenever project VSync must be emulated on Wayland, or
        // while unfocused/minimized to avoid wasting resources. A focused,
        // visible project with VSync off is intentionally left uncapped.
        if ((isWayland && frameSyncEnabled) || !focused || minimized) {
            const double frameSeconds = (SDL_GetPerformanceCounter() - frameStart) / perfFrequency;
            const int sleepMs = static_cast<int>((framePeriod - frameSeconds) * 1000.0);
            if (sleepMs > 0) {
                SDL_Delay(sleepMs);
            }
        }
    }

    // Stop the AI worker before tearing SDL down; nothing else ever joins it.
    app.shutdownBackgroundWork();

    // Save window size and state before closing
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    Uint32 flags = SDL_GetWindowFlags(window);
    bool isMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;

    // Save settings through app
    app.saveWindowSettings(width, height, isMaximized);

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    app.engineViewDestroyed();

    if (invisibleCursor) {
        SDL_DestroyCursor(invisibleCursor);
        invisibleCursor = nullptr;
    }

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    NFD_Quit();
    SDL_Quit();

    app.engineShutdown();

    return 0;
}

editor::App& editor::Backend::getApp() {
    return app;
}

void editor::Backend::disableMouseCursor() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;

    if (invisibleCursor) {
        SDL_SetCursor(invisibleCursor);
    }

    SDL_SetWindowMouseGrab(window, true);
    SDL_ShowCursor();
    SDL_SetWindowRelativeMouseMode(window, true);
}

void editor::Backend::enableMouseCursor() {
    // Editor viewport navigation (fly-camera) released its temporary cursor lock.
    // Restore the cursor the editor should currently show: while a play session has
    // the cursor suspended (paused/loading) the editor owns it, so hand it back
    // rather than re-asserting the game's mode (which setMouseMode would no-op
    // anyway, leaving the cursor stuck locked). Otherwise reapply the game's
    // requested mode (NORMAL while editing).
    if (mouseControlSuspended) {
        showEditorCursor();
    } else {
        setMouseMode(gameMouseMode);
    }
}

void editor::Backend::setMouseControlSuspended(bool suspended) {
    if (mouseControlSuspended == suspended) {
        return;
    }
    mouseControlSuspended = suspended;
    if (suspended) {
        // Hand the cursor back to the editor without forgetting gameMouseMode.
        showEditorCursor();
    } else {
        // Restore whatever the game asked for.
        setMouseMode(gameMouseMode);
    }
}

void editor::Backend::setMouseMode(MouseMode mode) {
    gameMouseMode = mode;
    if (mouseControlSuspended) {
        // Editor owns the cursor right now; just record the intent.
        return;
    }
    switch (mode) {
        case MouseMode::CAPTURED:
            // Relative mouse mode. Held for the whole session: the hover policy is
            // disabled for this mode so the virtual position leaving the scene
            // rect can't disable relative mode mid-play (see applyHoverVisibility).
            disableMouseCursor();
            break;
        case MouseMode::CONFINED:
            confineEditorCursor();
            break;
        case MouseMode::HIDDEN:
            applyHoverVisibility(true);
            break;
        case MouseMode::NORMAL:
            showEditorCursor();
            break;
    }
}

void editor::Backend::setGameCursorInSceneRect(bool inSceneRect) {
    gameCursorInSceneRect = inSceneRect;
    applyHoverVisibility();
}

void editor::Backend::closeWindow() {
    shouldClose = true;
}

bool editor::Backend::isRunningOnWayland() {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver && SDL_strcmp(driver, "wayland") == 0;
}

void editor::Backend::updateWindowTitle(const std::string& projectName) {
    if (projectName.empty()) {
        title = "Empty project - Doriax Engine";
    } else {
        title = projectName + " - Doriax Engine";
    }
    if (window){
        SDL_SetWindowTitle(window, title.c_str());
    }
}

void* editor::Backend::getNFDWindowHandle() {
    return &nativeWindow;
}
