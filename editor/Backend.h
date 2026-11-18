// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "App.h"
#include "PlatformMenu.h"
#include "System.h"

namespace doriax::editor{

    class Backend{
        private:
            static std::string title;

        public:
            static int init(int argc, char **argv);

            static App& getApp();

            static void disableMouseCursor();
            static void enableMouseCursor();
            static void setMouseMode(MouseMode mode);
            // Temporarily hands the OS cursor back to the editor (visible + free) while a
            // play session is paused or loading, without discarding the game's requested
            // mouse mode. The mode is reapplied when the suspension is lifted (resume).
            // Driven from the main loop. Window focus is intentionally not a factor here:
            // Platform backends release a captured cursor on focus loss.
            static void setMouseControlSuspended(bool suspended);
            static void setGameCursorInSceneRect(bool inSceneRect);
            static void closeWindow();

            // True when the windowing platform can't reposition OS windows at runtime
            // (Wayland). In that case toggling multi-viewport needs an app restart to
            // take effect, since the backend is chosen at startup from the saved setting.
            static bool isRunningOnWayland();

            static ImVec2 sceneRenderScale(ImVec2 framebufferScale, float dpiScale);

            // Installs or updates the application menu. A positive result reserves
            // that much client space, a negative result means the menu lives in the
            // non-client area, and zero asks App to render its ImGui fallback.
            static float setMainMenu(const PlatformMenuModel& menu, PlatformMenuCallback callback);

            static ImTextureID getImGuiTexture(TextureRender* texture);

            // Vulkan and Metal keep the device and drawable in the backend,
            // GL uses System's defaults
            #if defined(SOKOL_VULKAN) || defined(SOKOL_METAL)
            static sg_environment getSokolEnvironment();
            static sg_swapchain getSokolSwapchain();
            #endif

            static void updateWindowTitle(const std::string& projectName);

            static void* getNFDWindowHandle();
    };

}

