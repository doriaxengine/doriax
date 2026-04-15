#include "Backend.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "nfd.hpp"
#include "nfd_sdl2.h"

static SDL_Window* window = nullptr;
static std::vector<std::string> droppedPaths;
static nfdwindowhandle_t nativeWindow;
static bool shouldClose = false;
static SDL_Cursor* invisibleCursor = nullptr;

using namespace doriax;

// for work with mingw32
int SDL_main(int argc, char* argv[]) {
    return editor::Backend::init(argc, argv);
}

editor::App editor::Backend::app;
std::string editor::Backend::title;

int editor::Backend::init(int argc, char* argv[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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

    // Initialize settings first (this doesn't need ImGui yet)
    app.initializeSettings();

    // Get saved window dimensions from app
    int windowWidth = app.getInitialWindowWidth();
    int windowHeight = app.getInitialWindowHeight();

    // Create window with OpenGL context
    window = SDL_CreateWindow(
        "Doriax Engine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        NFD_Quit();
        SDL_Quit();
        return -1;
    }

    // Apply saved window state
    if (app.getInitialWindowMaximized()) {
        SDL_MaximizeWindow(window);
    }

    SDL_Surface* cursorSurface = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_RGBA32);
    if (cursorSurface) {
        SDL_memset(cursorSurface->pixels, 0, cursorSurface->pitch * cursorSurface->h);
        invisibleCursor = SDL_CreateColorCursor(cursorSurface, 0, 0);
        SDL_FreeSurface(cursorSurface);
    }

    NFD_GetNativeWindowFromSDLWindow(window, &nativeWindow);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        SDL_DestroyWindow(window);
        NFD_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context - MUST BE DONE BEFORE app.setup()
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Platform/Renderer bindings - MUST BE DONE AFTER ImGui::CreateContext()
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    // Now we can safely call app.setup() which uses ImGui
    app.setup();
    app.engineInit(argc, argv);

    SDL_ShowCursor(SDL_DISABLE);

    app.engineViewLoaded();

    // Main loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                // Handle quit event, but don't close immediately
                app.exit();
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                // Handle window close event, but don't close immediately
                app.exit();
            }
            if (event.type == SDL_DROPBEGIN) {
                droppedPaths.clear();
            }
            if (event.type == SDL_DROPFILE) {
                droppedPaths.push_back(event.drop.file);
                SDL_free(event.drop.file);
            }
            if (event.type == SDL_DROPCOMPLETE) {
                app.handleExternalDrop(droppedPaths);
            }
        }

        // Check if we should close the window now (after potential confirmation dialogs)
        if (shouldClose) {
            done = true;
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.engineRender();

        // Get window size
        int display_w, display_h;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);

        render.setClearColor(Vector4(0.45f, 0.55f, 0.60f, 1.00f));
        render.startRenderPass(display_w, display_h);

        app.show();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        render.endRenderPass();

        SDL_GL_SwapWindow(window);
    }

    // Save window size and state before closing
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    Uint32 flags = SDL_GetWindowFlags(window);
    bool isMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;

    // Save settings through app
    app.saveWindowSettings(width, height, isMaximized);

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    app.engineViewDestroyed();

    if (invisibleCursor) {
        SDL_FreeCursor(invisibleCursor);
        invisibleCursor = nullptr;
    }

    SDL_GL_DeleteContext(glContext);
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

    // TODO: cursor is not leaving the window
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

void editor::Backend::enableMouseCursor() {
    // TODO: cursor is not leaving the window
    SDL_ShowCursor(SDL_ENABLE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_SetCursor(SDL_GetDefaultCursor());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
}

void editor::Backend::closeWindow() {
    shouldClose = true;
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