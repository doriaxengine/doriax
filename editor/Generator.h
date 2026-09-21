// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <filesystem>
#include <future>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <map>
#include "Scene.h"
#include "Factory.h"
#include "util/EntityBundle.h"
#include "util/CommandRunner.h"

namespace fs = std::filesystem;

namespace doriax::editor {

    struct ScriptPropertyInfo {
        std::string name;
        bool isPtr;
        std::string ptrTypeName;
    };

    struct SceneScriptSource {
        fs::path path;
        fs::path headerPath;
        std::string className;
        std::vector<ScriptPropertyInfo> properties;
    };


    struct SceneBuildInfo {
        uint32_t id;
        std::string name;
        std::vector<uint32_t> involvedScenes; // IDs of all scenes involved in this stack (main + layers)
        std::vector<uint32_t> activeScenes;   // IDs that should be added to Engine when the stack is loaded
        bool isMain;
    };

    struct BundleSceneInfo {
        std::filesystem::path bundlePath; // relative path of the .bundle file
        std::string functionName;         // generated C++ function name (e.g. create_bundle_X)
    };

    // Which cmake the editor would run, and where it came from.
    struct CMakeInfo {
        bool found = false;
        std::string path;         // e.g. "/usr/local/bin/cmake"
        std::string version;      // e.g. "3.31.6", empty when it could not be read
        std::string source;       // "configured path" or "on PATH"
        std::string error;        // why a configured path was rejected
    };

    struct CMakeKit {
        std::string displayName;  // e.g. "GCC 15.2.0 x86_64-linux-gnu"
        std::string cCompiler;    // e.g. "/usr/bin/gcc"
        std::string cxxCompiler;  // e.g. "/usr/bin/g++"
        std::string generator;    // e.g. "MinGW Makefiles" (Windows only, empty = CMake default)
        bool available = true;            // false = detected but not usable (shown disabled in UI)
        std::string unavailableReason;    // e.g. "requires Ninja on PATH"
    };

    // Values must stay in sync with the DORIAX_WINDOW_MODE macro consumed by
    // the engine platform backends (0 = windowed, 1 = maximized, 2 = fullscreen).
    enum class WindowMode {
        WINDOWED = 0,
        MAXIMIZED = 1,
        FULLSCREEN = 2
    };

    // Initial window state for generated/exported desktop builds.
    // Web and mobile targets ignore these settings.
    struct WindowSettings {
        WindowMode mode = WindowMode::WINDOWED;
        unsigned int width = 1280;
        unsigned int height = 720;
        bool resizable = true;
        std::string title = "Doriax";
    };

    class Generator {
    private:
        std::future<void> buildFuture;
        std::atomic<bool> lastBuildSucceeded;
        CommandRunner commandRunner;

        static fs::path getGeneratedPath(const fs::path& projectInternalPath);
        static std::string getEditorPluginAbiCheck();
        static const CMakeKit* chooseDefaultKit(const std::vector<CMakeKit>& kits);

        bool configureCMake(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType, const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator, unsigned int parallelJobs);
        bool buildProject(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType, const std::string& generator, unsigned int parallelJobs);
        // Resolve a "Default" (all-empty) selection in place to the best
        // ABI-compatible kit. No-op off Windows; false if none is compatible.
        bool resolveDefaultKit(std::string& cCompiler, std::string& cxxCompiler, std::string& generator);
        bool runCommand(const std::string& command, const fs::path& workingDir);
        bool clearStaleCMakeCache(const fs::path& projectPath, const fs::path& buildPath);
        // Remove CMake's cached configuration so the next configure starts clean.
        // Returns false (with a logged error) if the cache could not be removed,
        // usually because another program holds a lock on the build directory.
        bool cleanBuildDirectory(const fs::path& buildPath);
        std::string getPlatformCMakeConfig(bool vsyncEnabled, const WindowSettings& windowSettings, const fs::path& projectPath, const fs::path& assetsPath, const fs::path& luaPath);
        std::string buildInitSceneScriptsSource(const std::vector<SceneScriptSource>& scriptFiles);
        std::string buildCleanupSceneScriptsSource(const std::vector<SceneScriptSource>& scriptFiles);

        void writeSourceFiles(const fs::path& projectPath, const fs::path& projectInternalPath, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<SceneBuildInfo>& scenes, const std::vector<BundleSceneInfo>& bundles, bool vsyncEnabled, const WindowSettings& windowSettings, const fs::path& assetsPath, const fs::path& luaPath, const std::vector<fs::path>& scriptDirs, int cxxStandard, bool physics2DEnabled, bool physics3DEnabled);

    public:
        Generator();
        ~Generator();
        // Export rebuilds the engine, so it does not need the editor's ABI.
        // Optionally returns the resolved Default kit for the caller to reuse.
        static std::string checkBuildTools(bool requireEditorCompatibility = false, CMakeKit* resolvedDefaultKit = nullptr);
        static std::vector<CMakeKit> detectAvailableKits();
        // Explicit Visual Studio kits target the editor's architecture in both
        // Play and Export. Empty for Default and non-VS/non-Windows generators.
        static std::string getGeneratorPlatform(const std::string& generator);
        static CMakeInfo detectCMake();
        // cmake for a command line: the configured path (quoted) or plain
        // "cmake" for PATH to resolve.
        static std::string cmakeExecutable();
        // A file dialog pick turned into a cmake executable, "" when it holds none.
        static std::string resolveCMakePath(const std::string& userPath);
        // Version a cmake executable reports, "" when it is not one.
        static std::string probeCMakeVersion(const std::string& path);
        // MSVC's /MP processMax accepts at most 65536, making this the universal
        // (machine-independent) ceiling for a stored Parallel Jobs value.
        static constexpr unsigned int MAX_SUPPORTED_PARALLEL_BUILD_JOBS = 65536;
        static unsigned int getAutomaticParallelBuildJobs();
        static unsigned int getMaxParallelBuildJobs();
        std::vector<BundleInstanceInfo> writeBundleSources(const std::map<fs::path, EntityBundle>& entityBundles, uint32_t sceneId, const fs::path& projectPath, const fs::path& projectInternalPath);
        void writeSceneSource(Scene* scene, const std::string& sceneName, const std::vector<Entity>& entities, const Entity camera, const fs::path& projectPath, const fs::path& projectInternalPath, std::vector<BundleInstanceInfo>& bundleInstances);
        void clearSceneSource(const std::string& sceneName, const fs::path& projectInternalPath);
        void configure(const std::vector<SceneBuildInfo>& scenes, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<BundleSceneInfo>& bundles, const fs::path& projectPath, const fs::path& projectInternalPath, const fs::path& assetsPath, const fs::path& luaPath, const std::vector<fs::path>& scriptDirs, int cxxStandard, bool physics2DEnabled, bool physics3DEnabled, Scaling scalingMode = Scaling::FITWIDTH, TextureStrategy textureStrategy = TextureStrategy::RESIZE, unsigned int canvasWidth = 1280, unsigned int canvasHeight = 720, bool vsyncEnabled = true, const WindowSettings& windowSettings = WindowSettings());
        void build(const fs::path projectPath, const fs::path projectInternalPath, const fs::path buildPath, const std::string& cCompiler = "", const std::string& cxxCompiler = "", const std::string& generator = "", unsigned int parallelJobs = 0);
        bool isBuildInProgress() const;
        void waitForBuildToComplete();
        bool didLastBuildSucceed() const;
        // Request cancellation asynchronously. Returns a future you can wait on.
        std::future<void> cancelBuild();
    };
}
