// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "imgui.h"

#include <string>
#include <filesystem>
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

        // Per-frame preview textures over the pool-cached thumbnails, cleared
        // at the end of each draw (same lifecycle as Properties).
        std::unordered_map<std::string, Texture> m_thumbnailTextures;
        uint32_t m_startSceneId = NULL_PROJECT_SCENE;
        fs::path m_assetsDir;
        fs::path m_luaDir;
        std::vector<fs::path> m_scriptDirs;
        std::vector<CMakeKit> m_availableKits;
        int m_cmakeKitIndex = 0;
        // Editor-wide, so it is applied when picked instead of on Apply, the
        // same way the Export dialog treats the Emscripten SDK path.
        std::string m_cmakeOverride;
        CMakeInfo m_cmakeInfo;
        std::string m_cmakePickError;   // why the last pick was rejected
        int m_cmakeBuildJobs = 0;
        std::string m_cmakeBuildJobsTooltip;
        bool m_packNativeResources = false;

        void drawSettings();
        void drawGeneralSettings();
        void drawCanvasSettings();
        void drawWindowSettings();
        Texture* findThumbnail(const std::string& path);
        void drawDirectoriesSettings();
        void drawBuildSettings();
        void drawCMakeSetting();
        void refreshCMakeStatus();
        void applySettings();

    public:
        ProjectSettingsWindow() = default;
        ~ProjectSettingsWindow() = default;

        void open(Project* project);
        void show();
        bool isOpen() const { return m_isOpen; }
    };

}
