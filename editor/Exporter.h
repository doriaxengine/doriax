// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "shader/ShaderBuilder.h"
#include "ShaderDataSerializer.h"
#include "util/CommandRunner.h"

#include <filesystem>
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

namespace doriax::editor {

    namespace fs = std::filesystem;

    enum class ShaderOutputFormat {
        Header,
        Binary,
        Json
    };

    enum class ExportMode {
        SourceCode,
        Desktop,
        Web
    };

    struct ExportConfig {
        ExportMode mode = ExportMode::SourceCode;
        fs::path targetDir;        // SourceCode: user dir; Desktop/Web: overwritten with the build cache dir
        fs::path destinationDir;   // Desktop/Web only: where the final artifacts are copied
        bool overwriteTarget = false;  // SourceCode: allow exporting into a non-empty targetDir
        fs::path assetsDir;
        fs::path luaDir;
        uint32_t startSceneId = 0;
        // Resolved from the scenes and the overrides below; CLI shader generation
        // fills it in directly
        std::set<ShaderKey> selectedShaderKeys;
        // An explicit --shader list on an export replaces what the scenes reported
        std::set<ShaderKey> shaderKeysOverride;
        std::set<ShaderKey> shaderAdditions;
        std::set<ShaderKey> shaderExclusions;
        // graphic backends the shaders are compiled for
        std::set<::doriax::ShaderBackend> selectedBackends;
        ShaderOutputFormat shaderOutputFormat = ShaderOutputFormat::Header;
        // Desktop: compiler kit from the editor settings ("" = CMake default toolchain)
        std::string cmakeCCompiler;
        std::string cmakeCxxCompiler;
        std::string cmakeGenerator;
        unsigned int buildJobs = 0;          // 0 = automatic
        std::string buildType = "Release";
        // Desktop/Web: value passed to the exported CMake GRAPHIC_BACKEND
        // setting (glcore, gles3, d3d11, metal, or vulkan).
        std::string graphicBackend;
        // Native builds: pack assets/lua into resources.pak instead of copying folders.
        // Experimental and disabled by default.
        bool packNativeResources = false;
        // Web: emsdk root override ("" = auto-detect via EMSDK env / PATH)
        std::string emsdkPath;
    };

    struct ExportProgress {
        std::string currentStep;
        std::string detailLine;              // last streamed build-output line
        float overallProgress = 0.0f;
        bool started = false;
        bool finished = false;
        bool failed = false;
        std::string errorMessage;
    };

    struct EmsdkInfo {
        bool found = false;
        std::string emcmake;                 // emcmake command or full path
        std::string description;             // where it was found, for the UI
    };

    class Exporter {
    private:
        Project* project = nullptr;
        ExportConfig config;
        ExportProgress progress;
        mutable std::mutex progressMutex;
        std::atomic<bool> cancelRequested{false};

        std::thread exportThread;

        ShaderBuilder shaderBuilder;
        CommandRunner commandRunner;

        // Lua sources copyLua() shipped, so copyAssets() can skip them
        std::set<fs::path> luaCopiedSources;
        // Paths copyLua() wrote, relative to the lua tree, so copyAssets() can keep
        // Android's merged assets root free of paths both trees would ship
        std::set<fs::path> luaExportedPaths;
        // C++ files copyGenerated() and copyCppScripts() wrote, relative to the project root,
        // so the Xcode workspace leaves out files an earlier export left there
        std::set<fs::path> cppExportedPaths;

        // Scales values passed to setProgress so the generation steps (written
        // for the [0,1] SourceCode range) map into [0, 0.5] for Desktop/Web,
        // leaving the upper half for configure/build/collect.
        float progressScale = 1.0f;

        void setProgress(const std::string& step, float value);
        void setProgressRaw(const std::string& step, float value);
        void setDetail(const std::string& line);
        void setError(const std::string& message);
        void runExport();

        bool isCancelled() const;

        void resolveShaderKeys();

        fs::path getExportProjectRoot() const;
        // Canonical directory copyAssets() reads from
        fs::path getAssetsSourceDir() const;
        // Canonical directory copyLua() reads from, empty without one
        fs::path getLuaSourceDir() const;
        bool isExportDir(const fs::directory_entry& entry) const;
        static bool shouldSkipExportSupportFile(const fs::path& relativePath);
        static bool isCppHeaderFile(const fs::path& path);
        static bool isCppSourceFile(const fs::path& path);
        static bool isLuaExportFile(const fs::path& path);
        static bool isLuaSourceFile(const fs::path& path);

        std::string getAppName() const;
        fs::path getBuildCacheDir() const;
        // Generator cmake will actually use: the project kit's, or with no
        // explicit -G, the CMAKE_GENERATOR environment variable cmake honors.
        std::string getEffectiveGenerator() const;

        bool checkTargetDir();
        bool prepareCacheTarget();
        bool configureBuild();
        bool runBuild();
        bool collectDesktopArtifacts();
        bool collectWebArtifacts();
        bool collectSourceResourcePack();
        bool collectPackedTreeEntries(const fs::path& dir, std::vector<fs::path>& out, const fs::path& keep = fs::path());
        bool removeResourcePack(const fs::path& packPath);
        bool writeNativeResourcePack(const fs::path& outputPath, bool& created);
        bool clearGenerated();
        bool loadAndSaveAllScenes();
        bool copyGenerated();
        bool copyAssets();
        bool copyLua();
        bool copyCppScripts();
        bool copyEngine();
        bool writeExportSettingsScript(std::string& cmakeContent);
        bool writeAndroidProjectSettings();
        bool writeAppleProjectSettings();
        bool addXcodeProjectSources(std::string& xcodeProject);
        bool writeXcodeBuildSettings(std::string& xcodeProject);
        bool writeWindowsResourceFile(bool includeIcon);
        bool writeAppIcon();
        bool buildAndSaveShaders();

        // Aggregates SceneMaxValues across all project scenes into the capacities that
        // size the engine's fixed-capacity HybridArrays (MAX_SUBMESHES, MAX_SPRITE_FRAMES, ...).
        std::vector<std::pair<std::string, unsigned int>> getSceneMaxValues() const;
        // add_definitions() block applying them to the export.
        std::string buildSceneMaxValuesDefinitions() const;
        // Project-relative form of a script root, empty when it falls outside the project.
        fs::path scriptDirRelativePath(const fs::path& scriptDir) const;
        // Script roots inside the exported project, as copyCppScripts() lays them out.
        std::vector<std::string> getExportedScriptRoots() const;
        // target_include_directories() call putting the script roots on the exported game target.
        std::string buildScriptDirIncludes() const;

        static void copyTree(const fs::path& src, const fs::path& dst, std::error_code& ec);
        static void copyTreeIfChanged(const fs::path& src, const fs::path& dst, std::error_code& ec, bool pruneStale = false,
                                      bool skipIdeOutput = false);

    public:
        Exporter();
        ~Exporter();

        void startExport(Project* project, const ExportConfig& config);
        bool exportProject(Project* project, const ExportConfig& config);
        bool generateShaders(const ExportConfig& config);
        void cancelExport();
        ExportProgress getProgress() const;
        bool isRunning() const;

        static std::string getShaderDisplayName(ShaderType type, uint32_t properties, uint16_t customId = 0);
        // a built-in lit mesh key (or one with only a vertex offset) ships and drops with its 'Dlo' (bit 28) twin
        static std::vector<ShaderKey> getPairedShaderKeys(ShaderKey key);
        static std::string getCMakeGraphicBackend(::doriax::ShaderBackend backend);
        static EmsdkInfo detectEmsdk(const std::string& overridePath);
    };

}
