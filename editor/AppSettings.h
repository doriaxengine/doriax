// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "ai/AiTypes.h"
#include "yaml-cpp/yaml.h"

namespace doriax::editor {

struct PanelVisibilitySettings {
    bool structure = true;
    bool properties = true;
    bool resources = true;
    bool output = true;
    bool animation = true;
    bool terrain = false;
    bool tilemap = false;
    bool aiChat = true;

    bool operator==(const PanelVisibilitySettings& other) const {
        return structure == other.structure
            && properties == other.properties
            && resources == other.resources
            && output == other.output
            && animation == other.animation
            && terrain == other.terrain
            && tilemap == other.tilemap
            && aiChat == other.aiChat;
    }

    bool operator!=(const PanelVisibilitySettings& other) const {
        return !(*this == other);
    }
};

// Where Lua and C++ scripts open
enum class ScriptEditor {
    BUILTIN,
    VSCODE,
    VISUAL_STUDIO,
    CLION,
    CUSTOM
};

// Debug information of the C++ scripts built for Play
enum class ScriptDebugInfo {
    NONE,
    SYMBOLS,
    NO_OPTIMIZATION
};

// Compiler paths and job count are machine-specific, so they live in the project's
// own .doriax/user/build.yaml rather than in project.yaml, and travel with the
// project when it moves.
struct LocalBuildSettings {
    std::string cCompiler;
    std::string cxxCompiler;
    std::string generator;
    unsigned int buildJobs = 0; // 0 = automatic
    ScriptDebugInfo debugInfo = ScriptDebugInfo::SYMBOLS;
    bool configured = false;    // set on read: false = no entry, so default toolchain
};

class AppSettings {
private:
    static std::filesystem::path configFilePath;
    static YAML::Node settingsData;
    
    // Recent projects list (max 10)
    static std::vector<std::filesystem::path> recentProjects;
    static std::filesystem::path lastProjectPath;

    // Last compiler kit chosen in Editor Settings, so new projects inherit it
    // instead of silently defaulting each time.
    static std::string lastCMakeCCompiler;
    static std::string lastCMakeCxxCompiler;
    static std::string lastCMakeGenerator;

    // Emscripten SDK root override for Web exports ("" = auto-detect).
    // Machine-specific, so stored editor-wide rather than per-project.
    static std::string emsdkPath;

    // cmake executable override ("" = look it up on PATH). Machine-specific,
    // stored editor-wide like emsdkPath.
    static std::string cmakePath;
    static std::filesystem::path defaultExportDirectory;
    
    // Window settings. The size is physical pixels, only meaningful again at the
    // scale it was captured at; 0 marks a file from before that was tracked.
    static int windowWidth;
    static int windowHeight;
    static bool isMaximized;
    static float windowUiScale;

    // The dock layout is pixels too, but keeps the scale it was built at even if
    // the window later moves to a monitor that changes windowUiScale.
    static float layoutUiScale;

    // Resources window settings
    static int resourcesIconSize;
    static int resourcesLayout;      // 0=AUTO, 1=GRID, 2=SPLIT_FILES_ONLY, 3=SPLIT
    static int resourcesItemViewStyle; // 0=CARD, 1=CLASSIC
    static float resourcesLeftPanelWidth;

    // Code editor settings
    static float codeEditorFontSize;
    static ScriptEditor scriptEditor;
    static std::string scriptEditorCommand;

    // Editor viewport settings
    static bool multiViewportEnabled;
    static bool editorVSyncEnabled;
    static bool outputClearOnPlay;
    static float uiScale;
    static PanelVisibilitySettings panelVisibility;

    // AI assistant settings (API keys are intentionally not stored here)
    static ai::Settings aiSettings;
    static ai::McpSettings mcpSettings;

    // Private methods
    static void ensureConfigDirectory();

public:
    static std::filesystem::path getExportTargetDir(const std::filesystem::path& projectFile, const std::string& mode);
    static bool setExportTargetDir(const std::filesystem::path& projectFile, const std::string& mode, const std::filesystem::path& targetDir);

    static LocalBuildSettings getBuildSettings(const std::filesystem::path& projectFile);
    // as build.yaml and the generated CMake read it
    static const char* getDebugInfoName(ScriptDebugInfo debugInfo);
    static bool setBuildSettings(const std::filesystem::path& projectFile, const LocalBuildSettings& value);

    // Initialization
    static bool initialize();

    // Directory where editor config files live (settings.yaml, layout ini, ...)
    static std::filesystem::path getConfigDirectory();

    // Project settings
    static std::filesystem::path getLastProjectPath();
    static void setLastProjectPath(const std::filesystem::path& path);
    
    static std::vector<std::filesystem::path> getRecentProjects();
    static void addToRecentProjects(const std::filesystem::path& path, bool needSave = true);
    static void clearRecentProjects();

    // Last compiler kit (empty strings = Default toolchain)
    static std::string getLastCMakeCCompiler();
    static std::string getLastCMakeCxxCompiler();
    static std::string getLastCMakeGenerator();
    static void setLastCMakeKit(const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator);

    // Emscripten SDK root override for Web exports ("" = auto-detect)
    static std::string getEmsdkPath();
    static void setEmsdkPath(const std::string& path);

    // cmake executable override ("" = look it up on PATH)
    static std::string getCMakePath();
    static void setCMakePath(const std::string& path);
    static std::filesystem::path getDefaultExportDirectory();
    static void setDefaultExportDirectory(const std::filesystem::path& path);
    
    // Window settings
    static int getWindowWidth();
    static int getWindowHeight();
    static bool getIsMaximized();
    
    static float getWindowUiScale();
    static float getLayoutUiScale();

    static void setWindowWidth(int width);
    static void setWindowHeight(int height);
    static void setIsMaximized(bool maximized);
    static void setWindowUiScale(float scale);
    static void setLayoutUiScale(float scale);
    
    // Resources window settings
    static int getResourcesIconSize();
    static int getResourcesLayout();
    static int getResourcesItemViewStyle();
    static float getResourcesLeftPanelWidth();
    
    static void setResourcesIconSize(int size);
    static void setResourcesLayout(int layout);
    static void setResourcesItemViewStyle(int style);
    static void setResourcesLeftPanelWidth(float width);

    // Code editor settings
    static constexpr float defaultCodeEditorFontSize = 14.0f; // VSCode default
    static constexpr float minCodeEditorFontSize = 8.0f;
    static constexpr float maxCodeEditorFontSize = 40.0f;

    static float getCodeEditorFontSize();
    static void setCodeEditorFontSize(float size);

    static ScriptEditor getScriptEditor();
    static void setScriptEditor(ScriptEditor editor);
    // {project}, {file}, {line} and {column} are replaced
    static std::string getScriptEditorCommand();
    static void setScriptEditorCommand(const std::string& command);

    // Multi-viewport: allow dockable windows to be dragged out into their own OS window
    static bool getMultiViewportEnabled();
    static void setMultiViewportEnabled(bool enabled);

    // VSync for the editor's own frames; Play mode and builds use the project setting
    static bool getEditorVSyncEnabled();
    static void setEditorVSyncEnabled(bool enabled);

    // Output window is emptied when Play starts
    static bool getOutputClearOnPlay();
    static void setOutputClearOnPlay(bool enabled);

    // Multiplies the display scale the system reports
    static float getUiScale();
    static void setUiScale(float scale);

    // Panels toggled from the View menu
    static PanelVisibilitySettings getPanelVisibility();
    static void setPanelVisibility(const PanelVisibilitySettings& visibility);

    // AI assistant settings
    static ai::Settings getAiSettings();
    static void setAiSettings(const ai::Settings& settings);
    static ai::McpSettings getMcpSettings();
    static void setMcpSettings(const ai::McpSettings& settings);

    // Load and save settings
    static bool loadSettings();
    static bool saveSettings();
};

} // namespace doriax::editor
