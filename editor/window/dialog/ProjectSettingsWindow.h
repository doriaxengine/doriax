// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "imgui.h"

#include <string>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace doriax::editor {

    namespace fs = std::filesystem;

    class ProjectSettingsWindow {
    private:
        bool m_isOpen = false;
        Project* m_project = nullptr;

        // UI state
        char m_projectNameBuffer[256] = {0};
        std::string m_projectNameOriginal;
        int m_canvasWidth = 0;
        int m_canvasHeight = 0;
        int m_scalingModeIndex = 0;
        int m_textureStrategyIndex = 0;
        bool m_vsyncEnabled = true;
        int m_windowModeIndex = 0;
        int m_windowWidth = 0;
        int m_windowHeight = 0;
        bool m_windowResizable = true;
        char m_windowTitleBuffer[256] = {0};
        std::string m_windowTitleOriginal;
        fs::path m_windowIcon;
        char m_applicationNameBuffer[256] = {0};
        char m_applicationIdentifierBuffer[256] = {0};
        char m_applicationVersionBuffer[64] = {0};
        int m_applicationBuild = 1;

        // Per-frame preview textures over the pool-cached thumbnails, cleared
        // at the end of each draw (same lifecycle as Properties).
        std::unordered_map<std::string, Texture> m_thumbnailTextures;
        uint32_t m_startSceneId = NULL_PROJECT_SCENE;
        fs::path m_assetsDir;
        fs::path m_luaDir;
        std::vector<fs::path> m_scriptDirs;
        int m_cxxStandardIndex = 0;
        bool m_physics2DEnabled = true;
        bool m_physics3DEnabled = true;
        bool m_packNativeResources = false;
        bool m_versionControlMetadata = true;
        char m_webApplicationNameBuffer[256] = {0};
        fs::path m_webFavicon;
        fs::path m_webCustomHtmlShell;
        char m_webHeadIncludeBuffer[2048] = {0};
        bool m_webResizeCanvasToWindow = true;
        bool m_webHideEmscriptenUI = false;
        char m_linuxApplicationNameBuffer[256] = {0};
        char m_linuxCommentBuffer[512] = {0};
        char m_linuxCategoriesBuffer[256] = {0};
        char m_windowsProductNameBuffer[256] = {0};
        char m_windowsCompanyNameBuffer[256] = {0};
        char m_windowsFileVersionBuffer[64] = {0};
        char m_windowsProductVersionBuffer[64] = {0};
        char m_macOSApplicationNameBuffer[256] = {0};
        char m_macOSBundleIdentifierBuffer[256] = {0};
        char m_macOSVersionNameBuffer[64] = {0};
        char m_macOSBuildNumberBuffer[64] = {0};
        fs::path m_macOSIcon;
        bool m_macOSHighDpi = true;
        char m_iosApplicationNameBuffer[256] = {0};
        char m_iosBundleIdentifierBuffer[256] = {0};
        char m_iosVersionNameBuffer[64] = {0};
        char m_iosBuildNumberBuffer[64] = {0};
        fs::path m_iosIcon;
        bool m_iosHideStatusBar = true;
        bool m_iosHideHomeIndicator = true;
        bool m_iosSupportsHighRefreshRate = true;
        char m_androidApplicationNameBuffer[256] = {0};
        char m_androidPackageNameBuffer[256] = {0};
        char m_androidVersionNameBuffer[64] = {0};
        fs::path m_androidLauncherIcon;
        fs::path m_androidAdaptiveIconForeground;
        fs::path m_androidAdaptiveIconBackground;
        char m_androidVersionCodeBuffer[16] = {0};
        int m_androidMinSdk = 21;
        int m_androidTargetSdk = 33;
        int m_androidOrientationIndex = 0;
        bool m_androidAbiArmeabiV7a = true;
        bool m_androidAbiArm64V8a = true;
        bool m_androidAbiX86 = true;
        bool m_androidAbiX86_64 = true;
        std::set<std::string> m_androidPermissions;
        bool m_androidAllowBackup = true;
        bool m_androidFullscreen = true;
        bool m_androidKeepScreenOn = false;

        void drawSettings();
        void drawGeneralSettings();
        void drawCanvasSettings();
        void drawWindowSettings();
        Texture* findThumbnail(const std::string& path);
        void drawDirectoriesSettings();
        void drawBuildSettings();

        // Each platform section draws rows into the table its header opens.
        void drawPlatformsSettings();
        void drawWebSettings();
        void drawLinuxSettings();
        void drawWindowsSettings();
        void drawMacOSSettings();
        void drawIOSSettings();
        void drawAndroidSettings();

        // What an empty override resolves to, read from the buffers so hints follow typing.
        std::string inheritedApplicationName() const;
        std::string inheritedApplicationIdentifier() const;
        std::string inheritedApplicationVersion() const;
        std::string inheritedApplicationBuild() const;

        bool applySettings();

    public:
        ProjectSettingsWindow() = default;
        ~ProjectSettingsWindow() = default;

        void open(Project* project);
        void show();
        bool isOpen() const { return m_isOpen; }
    };

}
