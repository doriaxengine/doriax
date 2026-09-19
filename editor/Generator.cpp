// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

// Generator.cpp
#include "Generator.h"
#include "Factory.h"
#include "App.h"
#include "EditorVersion.h"
#include "editor/Out.h"
#include "AppSettings.h"
#include "util/CxxStandards.h"
#include "util/FileUtils.h"
#include "util/ShellEnv.h"
#include "util/Util.h"

#include <cstdlib>
#include <cctype>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <thread>
#include <cstring>
#include <cerrno>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <regex>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <signal.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>

    extern char **environ;
#endif

using namespace doriax;

std::string editor::Generator::getGeneratorPlatform(const std::string& generator) {
#ifdef _WIN32
    if (generator.compare(0, 14, "Visual Studio ") != 0) return "";
#if defined(_M_ARM64) || defined(__aarch64__)
    return "ARM64";
#elif defined(_M_ARM) || defined(__arm__)
    return "ARM";
#else
    return sizeof(void*) == 8 ? "x64" : "Win32";
#endif
#else
    (void)generator;
    return "";
#endif
}

fs::path editor::Generator::getGeneratedPath(const fs::path& projectInternalPath) {
    return projectInternalPath / "generated";
}

editor::Generator::Generator() :
    lastBuildSucceeded(false)
{
}

editor::Generator::~Generator() {
    // Request cancellation and wait for it to finish before destroying
    try {
        auto f = cancelBuild();
        if (f.valid()) f.wait();
    } catch (...) {}
    waitForBuildToComplete();
}

bool editor::Generator::cleanBuildDirectory(const fs::path& buildPath) {
    std::error_code ec;

    // Best-effort full wipe first.
    fs::remove_all(buildPath, ec);

    // CMakeCache.txt and CMakeFiles/ are what actually pin the previous
    // generator/compiler. If the full wipe above was blocked partway by a
    // locked file elsewhere in the tree (a loaded plugin DLL, an IDE's
    // CMake Tools extension, an antivirus scan), make sure at least these are
    // gone, retrying briefly in case the lock is transient.
    const fs::path cacheFile = buildPath / "CMakeCache.txt";
    const fs::path cacheDir  = buildPath / "CMakeFiles";
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (!fs::exists(cacheFile) && !fs::exists(cacheDir)) {
            break;
        }
        fs::remove(cacheFile, ec);
        fs::remove_all(cacheDir, ec);
        if (!fs::exists(cacheFile) && !fs::exists(cacheDir)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fs::create_directories(buildPath, ec);

    if (fs::exists(cacheFile)) {
        Out::error("Could not remove the previous CMake cache at: %s", cacheFile.string().c_str());
        Out::error("The build directory is locked by another program. Close any editor or IDE using it (e.g. the VS Code \"CMake Tools\" extension), stop any running build, then delete the '.doriax/build' folder and try again.");
        return false;
    }
    return true;
}

bool editor::Generator::clearStaleCMakeCache(const fs::path& projectPath, const fs::path& buildPath) {
    fs::path cacheFile = buildPath / "CMakeCache.txt";
    if (!fs::exists(cacheFile)) {
        return true;
    }

    std::ifstream cache(cacheFile);
    if (!cache.is_open()) {
        return true;
    }

    std::string line;
    std::string cachedHomeDir;
    while (std::getline(cache, line)) {
        // Look for CMAKE_HOME_DIRECTORY:INTERNAL=<path>
        const std::string prefix = "CMAKE_HOME_DIRECTORY:INTERNAL=";
        if (line.compare(0, prefix.size(), prefix) == 0) {
            cachedHomeDir = line.substr(prefix.size());
            break;
        }
    }
    cache.close();

    if (cachedHomeDir.empty()) {
        return true;
    }

    // Compare canonical paths to handle trailing slashes, symlinks, etc.
    std::error_code ec;
    fs::path cachedPath = fs::canonical(cachedHomeDir, ec);
    if (ec) {
        // Old path no longer exists — definitely stale
        cachedPath = fs::path(cachedHomeDir);
    }
    fs::path currentPath = fs::canonical(projectPath, ec);
    if (ec) {
        currentPath = projectPath;
    }

    if (cachedPath != currentPath) {
        Out::warning("Project path changed. Cleaning build directory...");
        Out::warning("  Previous: %s", cachedHomeDir.c_str());
        Out::warning("  Current: %s", projectPath.string().c_str());

        return cleanBuildDirectory(buildPath);
    }
    return true;
}

const editor::CMakeKit* editor::Generator::chooseDefaultKit(const std::vector<CMakeKit>& kits) {
    const CMakeKit* chosen = nullptr;
    for (const auto& k : kits) {
        if (!k.available) continue;
        // Prefer MSVC on an MSVC editor, including the explicit Ninja/cl
        // fallback. A MinGW editor has already filtered these kits out.
        if (k.cxxCompiler.empty() || fs::path(k.cxxCompiler).stem() == "cl") {
            return &k;
        }
        if (!chosen) chosen = &k;
    }
    return chosen;
}

bool editor::Generator::resolveDefaultKit(std::string& cCompiler, std::string& cxxCompiler, std::string& generator) {
    // Only a "Default" selection (everything empty) is resolved; an explicit kit
    // is left untouched.
    if (!cCompiler.empty() || !cxxCompiler.empty() || !generator.empty()) {
        return true;
    }

    // Windows-only: a bare cmake lets CMake auto-detect a toolchain that may not
    // match the editor's C++ ABI (e.g. a MinGW editor would get MSVC and fail to
    // link). Resolve Default to the best ABI-compatible detected kit so it obeys
    // the same ABI check as the Editor Settings dropdown. Other platforms have a
    // single C++ ABI, so CMake's auto-detection is always safe and left alone.
    bool resolveDefault = false;
#ifdef _WIN32
    resolveDefault = true;
#endif
    if (!resolveDefault) {
        return true;
    }

    std::vector<CMakeKit> kits = detectAvailableKits();
    const CMakeKit* chosen = chooseDefaultKit(kits);
    if (chosen) {
        cCompiler = chosen->cCompiler;
        cxxCompiler = chosen->cxxCompiler;
        generator = chosen->generator;
        if (!chosen->displayName.empty()) {
            Out::info("Default compiler resolved to: %s", chosen->displayName.c_str());
        }
        return true;
    }
    Out::error("No compatible C++ build toolchain was found for this editor. Select an available compiler in Editor Settings > Desktop.");
    return false;
}

bool editor::Generator::configureCMake(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType, const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator, unsigned int parallelJobs) {
    if (!clearStaleCMakeCache(projectPath, buildPath)) {
        return false;
    }

    const fs::path exePath = FileUtils::getExecutableDir();
    const std::string platform = getGeneratorPlatform(generator);
    std::string currentKit = generator + "\n" + cCompiler + "\n" + cxxCompiler + "\n" + exePath.string();
    if (!platform.empty()) currentKit += "\n" + platform;

    // Detect kit change: if the compiler/generator selection changed since the
    // last configure, the build tree must be wiped (CMake does not support
    // switching generators or compilers in-place). The editor engine library
    // directory participates too: find_library() caches absolute paths, so a
    // moved/updated editor install must force a fresh library lookup instead
    // of retaining an old libdoriax path.
    {
        fs::path kitMarker = buildPath / ".doriax_kit";
        fs::path cacheFile = buildPath / "CMakeCache.txt";
        if (fs::exists(kitMarker)) {
            std::ifstream f(kitMarker);
            std::string prevKit((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            if (prevKit != currentKit) {
                Out::warning("Compiler kit or engine library directory changed. Cleaning build directory...");
                if (!cleanBuildDirectory(buildPath)) {
                    return false;
                }
            }
        } else if (fs::exists(cacheFile)) {
            std::ifstream cache(cacheFile);
            std::string line;
            std::string cachedGenerator;
            std::string cachedCCompiler;
            std::string cachedCxxCompiler;

            while (std::getline(cache, line)) {
                const std::string genPrefix = "CMAKE_GENERATOR:INTERNAL=";
                const std::string cPrefix = "CMAKE_C_COMPILER:FILEPATH=";
                const std::string cxxPrefix = "CMAKE_CXX_COMPILER:FILEPATH=";

                if (line.compare(0, genPrefix.size(), genPrefix) == 0) {
                    cachedGenerator = line.substr(genPrefix.size());
                } else if (line.compare(0, cPrefix.size(), cPrefix) == 0) {
                    cachedCCompiler = line.substr(cPrefix.size());
                } else if (line.compare(0, cxxPrefix.size(), cxxPrefix) == 0) {
                    cachedCxxCompiler = line.substr(cxxPrefix.size());
                }
            }

            bool kitChanged = false;
            if (!generator.empty()) {
                kitChanged = cachedGenerator != generator;
            } else if (!cachedGenerator.empty() && !cCompiler.empty()) {
                kitChanged = true;
            }
            if (!kitChanged && !cCompiler.empty()) {
                kitChanged = cachedCCompiler != cCompiler;
            }
            if (!kitChanged && !cxxCompiler.empty()) {
                kitChanged = cachedCxxCompiler != cxxCompiler;
            }
            if (!kitChanged && cCompiler.empty() && cxxCompiler.empty()) {
                kitChanged = !cachedCCompiler.empty() || !cachedCxxCompiler.empty();
            }

            if (kitChanged) {
                Out::warning("Compiler kit or engine library directory changed. Cleaning build directory...");
                if (!cleanBuildDirectory(buildPath)) {
                    return false;
                }
            }
        }
    }

    // Safety net: even when the kit bookkeeping above is out of sync (e.g. a
    // previous configure left a cache the marker does not describe), a
    // generator recorded in the cache that differs from the one we are about to
    // request is fatal to CMake ("Does not match the generator used
    // previously"). Force a clean so the configure can actually proceed.
    if (!generator.empty()) {
        fs::path cacheFile = buildPath / "CMakeCache.txt";
        std::ifstream cache(cacheFile);
        if (cache.is_open()) {
            const std::string genPrefix = "CMAKE_GENERATOR:INTERNAL=";
            const std::string platformPrefix = "CMAKE_GENERATOR_PLATFORM:INTERNAL=";
            std::string line;
            std::string cachedGenerator;
            std::string cachedPlatform;
            while (std::getline(cache, line)) {
                if (line.compare(0, genPrefix.size(), genPrefix) == 0) {
                    cachedGenerator = line.substr(genPrefix.size());
                } else if (line.compare(0, platformPrefix.size(), platformPrefix) == 0) {
                    cachedPlatform = line.substr(platformPrefix.size());
                }
            }
            cache.close();
            if ((!cachedGenerator.empty() && cachedGenerator != generator) ||
                (!platform.empty() && cachedPlatform != platform)) {
                Out::warning("CMake generator or architecture changed. Cleaning build directory...");
                if (!cleanBuildDirectory(buildPath)) {
                    return false;
                }
            }
        }
    }

#ifdef _WIN32
    // A compiler override without an explicit generator makes CMake fall back
    // to the Visual Studio generator, which ignores CMAKE_C/CXX_COMPILER
    // (MSBuild always drives cl.exe) and fails at link time. Fail early with
    // an actionable message instead. Honor CMAKE_GENERATOR if the user set it.
    if (generator.empty() && (!cCompiler.empty() || !cxxCompiler.empty())) {
        const char* envGenerator = std::getenv("CMAKE_GENERATOR");
        if (!envGenerator || !*envGenerator) {
            Out::error("The selected compiler '%s' cannot be used with the default Visual Studio generator. Install Ninja (https://ninja-build.org), add it to PATH and re-select the compiler in Editor Settings, or switch to the MSVC compiler.",
                (!cxxCompiler.empty() ? cxxCompiler : cCompiler).c_str());
            return false;
        }
    }
#endif

    // CMake stores path values (e.g. CMAKE_C_COMPILER) into generated .cmake
    // files and re-parses them, treating backslashes as escape sequences. On
    // Windows a path like "C:\clang...\bin\clang.exe" then fails with
    // "Invalid character escape '\c'". CMake accepts forward slashes on every
    // platform, so normalize backslashes before building the command.
    auto toCMakePath = [](const std::string& p) {
        std::string out = p;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    };

    std::string cmakeCommand = cmakeExecutable() + " ";
    if (!generator.empty()) {
        cmakeCommand += "-G \"" + generator + "\" ";
    }
    if (!platform.empty()) {
        cmakeCommand += "-A " + platform + " ";
    }
    if (!cCompiler.empty()) {
        cmakeCommand += "-DCMAKE_C_COMPILER=\"" + toCMakePath(cCompiler) + "\" ";
    }
    if (!cxxCompiler.empty()) {
        cmakeCommand += "-DCMAKE_CXX_COMPILER=\"" + toCMakePath(cxxCompiler) + "\" ";
    }
    cmakeCommand += "-DCMAKE_BUILD_TYPE=" + configType + " ";
    // When configuring from inside the editor, ensure the generated project builds
    // in "plugin" mode (no Factory main.cpp/scene sources added).
    cmakeCommand += "-DDORIAX_EDITOR_PLUGIN=ON ";
    // The generated CMakeLists.txt stays machine-independent; the effective
    // local limit is supplied through the build tree's cache on every configure.
    cmakeCommand += "-DDORIAX_PARALLEL_BUILD_JOBS=" + std::to_string(parallelJobs) + " ";
    cmakeCommand += "\"" + toCMakePath(projectPath.string()) + "\" ";
    cmakeCommand += "-B \"" + toCMakePath(buildPath.string()) + "\" ";
    cmakeCommand += "-DDORIAX_LIB_DIR=\"" + toCMakePath(exePath.string()) + "\"";

    Out::info("Configuring CMake project with command: %s", cmakeCommand.c_str());
    bool result = runCommand(CommandRunner::msvcEnvPrefix(generator) + cmakeCommand, projectPath);

    // Record which kit was used so we can detect changes next time.
    if (result) {
        std::error_code ec;
        fs::create_directories(buildPath, ec);
        std::ofstream f(buildPath / ".doriax_kit");
        if (f.is_open()) {
            f << currentKit;
        }
    }

    return result;
}

unsigned int editor::Generator::getAutomaticParallelBuildJobs() {
    return std::max(std::thread::hardware_concurrency(), 1u);
}

unsigned int editor::Generator::getMaxParallelBuildJobs() {
    constexpr unsigned int maxJobsPerLogicalCpu = 4;
    // Keeping the shared limit within MAX_SUPPORTED_PARALLEL_BUILD_JOBS lets
    // the same effective value drive every generator.
    const unsigned long long scaledJobs =
        static_cast<unsigned long long>(getAutomaticParallelBuildJobs()) * maxJobsPerLogicalCpu;
    return static_cast<unsigned int>(std::min(
        scaledJobs,
        static_cast<unsigned long long>(MAX_SUPPORTED_PARALLEL_BUILD_JOBS)
    ));
}

bool editor::Generator::buildProject(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType, const std::string& generator, unsigned int parallelJobs) {
    std::string buildCommand = cmakeExecutable() + " --build \"" + buildPath.string() + "\" --config " + configType;
    buildCommand += " --parallel " + std::to_string(parallelJobs);
    Out::info("Building project with %u parallel jobs...", parallelJobs);
    // The build step invokes the compiler/linker again, so it needs the same
    // MSVC environment as configure (see CommandRunner::msvcEnvPrefix).
    return runCommand(CommandRunner::msvcEnvPrefix(generator) + buildCommand, buildPath);
}

bool editor::Generator::runCommand(const std::string& command, const fs::path& workingDir) {
    return commandRunner.run(command, workingDir);
}

// Relative to ${PROJECT_ROOT} so the generated CMakeLists.txt reads the same on every
// checkout. A path configured outside the project has no such spelling.
static std::string toProjectRootCMakePath(const fs::path& projectPath, const fs::path& target) {
    std::error_code ec;
    const fs::path relative = fs::relative(target, projectPath, ec);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return target.generic_string();
    }
    if (relative == ".") {
        return "${PROJECT_ROOT}";
    }
    return "${PROJECT_ROOT}/" + relative.generic_string();
}

std::string editor::Generator::getPlatformCMakeConfig(bool vsyncEnabled, const WindowSettings& windowSettings, const fs::path& projectPath, const fs::path& assetsPath, const fs::path& luaPath) {
    // The title crosses two quoting layers: the generated CMake string and the
    // C string literal passed to the platform backend. Escape both, matching the
    // exported-project configuration produced by Exporter.
    std::string title;
    for (char c : windowSettings.title) {
        if (c == '\\' || c == '"') title += '\\';
        title += c;
    }
    std::string cmakeTitle;
    for (char c : title) {
        if (c == '\\' || c == '"' || c == '$') cmakeTitle += '\\';
        cmakeTitle += c;
    }

    std::string content;
    content += "if (NOT DORIAX_EDITOR_PLUGIN)\n";
    content += "    add_definitions(\"-DDORIAX_VSYNC_ENABLED=" + std::string(vsyncEnabled ? "1" : "0") + "\")\n";
    content += "    add_definitions(\"-DDEFAULT_WINDOW_WIDTH=" + std::to_string(windowSettings.width) + "\")\n";
    content += "    add_definitions(\"-DDEFAULT_WINDOW_HEIGHT=" + std::to_string(windowSettings.height) + "\")\n";
    content += "    add_definitions(\"-DDORIAX_WINDOW_MODE=" + std::to_string(static_cast<int>(windowSettings.mode)) + "\")\n";
    content += "    add_definitions(\"-DDORIAX_WINDOW_RESIZABLE=" + std::string(windowSettings.resizable ? "1" : "0") + "\")\n";
    content += "    add_definitions(\"-DDORIAX_WINDOW_TITLE=\\\"" + cmakeTitle + "\\\"\")\n";
    content += "\n";
    content += "    set(COMPILE_ZLIB OFF)\n";
    content += "    set(IS_ARM OFF)\n";
    content += "\n";
    content += "    add_definitions(\"-DWITH_MINIAUDIO\") # For SoLoud\n";
    content += "\n";
    // Resolved from the project directory rather than written out absolute, so a
    // teammate who clones elsewhere still finds the assets
    content += "    add_definitions(\"-DDORIAX_ASSET_PATH=\\\"" + toProjectRootCMakePath(projectPath, assetsPath) + "\\\"\")\n";
    content += "    add_definitions(\"-DDORIAX_LUA_PATH=\\\"" + toProjectRootCMakePath(projectPath, luaPath) + "\\\"\")\n";
    content += "\n";
    content += "    # The compiled shader cache sits in this user's cache directory and is named\n";
    content += "    # after the engine version, so its path belongs to neither the project nor any\n";
    content += "    # other machine. The editor writes it to ${INTERNAL_DIR}/LocalPaths.cmake, which\n";
    content += "    # version control ignores; -DDORIAX_SHADER_PATH=<dir> overrides it.\n";
    content += "    include(${INTERNAL_DIR}/LocalPaths.cmake OPTIONAL)\n";
    content += "    if(NOT DORIAX_SHADER_PATH)\n";
    content += "        message(FATAL_ERROR \"DORIAX_SHADER_PATH is not set and ${INTERNAL_DIR}/LocalPaths.cmake is missing. Open this project in Doriax Editor once to regenerate it, or configure with -DDORIAX_SHADER_PATH=<shader cache directory>.\")\n";
    content += "    endif()\n";
    content += "    add_definitions(\"-DDORIAX_SHADER_PATH=\\\"${DORIAX_SHADER_PATH}\\\"\")\n";
    content += "\n";
    content += "    list(APPEND PLATFORM_SOURCE\n";
    content += "        ${INTERNAL_DIR}/generated/main.cpp\n";
    content += "    )\n";
    content += "\n";
    content += "    # Each desktop OS uses the same native application backend as the editor,\n";
    content += "    # compiled from the engine API snapshot in ${INTERNAL_DIR}/engine-api.\n";
    // This build links the editor's own engine library, whose sokol_gfx was
    // compiled for a single graphics backend, so the default has to be that one.
#if defined(SOKOL_VULKAN)
    const std::string editorGraphicBackend = "vulkan";
#elif defined(SOKOL_METAL)
    const std::string editorGraphicBackend = "metal";
#else
    const std::string editorGraphicBackend = "glcore";
#endif
    content += "    if(NOT DORIAX_GRAPHIC_BACKEND)\n";
    content += "        set(DORIAX_GRAPHIC_BACKEND \"" + editorGraphicBackend + "\")\n";
    content += "    endif()\n";
    content += "\n";
    content += "\n";
    content += "    # FindVulkan reads the VULKAN_SDK environment variable the SDK installer sets\n";
    content += "    macro(doriax_find_vulkan)\n";
    content += "        # sokol_gfx binds every resource through VK_EXT_descriptor_buffer, which\n";
    content += "        # MoltenVK does not implement, so the build only fails at device selection\n";
    content += "        if(APPLE)\n";
    content += "            message(WARNING \"DORIAX_GRAPHIC_BACKEND=vulkan runs on MoltenVK here, which does not implement VK_EXT_descriptor_buffer: this build will fail at startup unless the driver provides it. DORIAX_GRAPHIC_BACKEND=metal is the working macOS backend.\")\n";
    content += "        endif()\n";
    content += "\n";
    content += "        # An app started from the Dock inherits no shell environment, so on macOS\n";
    content += "        # the SDK is also looked for where its installer unpacks it\n";
    content += "        if(APPLE AND NOT DEFINED ENV{VULKAN_SDK})\n";
    content += "            file(GLOB doriaxVulkanSdkDirs \"$ENV{HOME}/VulkanSDK/*/macOS\")\n";
    content += "            if(doriaxVulkanSdkDirs)\n";
    content += "                list(SORT doriaxVulkanSdkDirs)\n";
    content += "                list(POP_BACK doriaxVulkanSdkDirs doriaxVulkanSdkNewest)\n";
    content += "                set(ENV{VULKAN_SDK} \"${doriaxVulkanSdkNewest}\")\n";
    content += "            endif()\n";
    content += "        endif()\n";
    content += "        find_package(Vulkan QUIET)\n";
    content += "        if(NOT Vulkan_FOUND)\n";
    content += "            message(FATAL_ERROR \"DORIAX_GRAPHIC_BACKEND=vulkan needs the Vulkan SDK (https://vulkan.lunarg.com/sdk/home), and VULKAN_SDK is not set here. If it is installed outside the default location, restart the shell or editor this build was started from with VULKAN_SDK in its environment: Windows only gives the variable to processes started after the installer ran. Otherwise use DORIAX_GRAPHIC_BACKEND=glcore, or metal on macOS.\")\n";
    content += "        endif()\n";
    content += "    endmacro()\n";
    content += "\n";
    content += "    set(DORIAX_PLATFORM_DIR ${DORIAX_API_DIR}/platform)\n";
    content += "    include_directories(${DORIAX_PLATFORM_DIR}/win ${DORIAX_PLATFORM_DIR}/linux ${DORIAX_PLATFORM_DIR}/mac ${DORIAX_PLATFORM_DIR}/apple ${DORIAX_PLATFORM_DIR}/common)\n";
    content += "\n";
    content += "    if(WIN32)\n";
    content += "        list(APPEND PLATFORM_SOURCE\n";
    content += "            ${DORIAX_PLATFORM_DIR}/win/DoriaxWin.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/win/WindowWin.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/win/SystemWin.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/win/WinInputRouter.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/win/GamepadWin.cpp\n";
    content += "        )\n";
    content += "        list(APPEND PLATFORM_LIBS gdi32 user32 shell32)\n";
    content += "        if(DORIAX_GRAPHIC_BACKEND STREQUAL \"vulkan\")\n";
    content += "            add_definitions(\"-DSOKOL_VULKAN\")\n";
    content += "            add_definitions(\"-DVK_USE_PLATFORM_WIN32_KHR\")\n";
    content += "            list(APPEND PLATFORM_SOURCE ${DORIAX_PLATFORM_DIR}/common/VulkanContext.cpp)\n";
    content += "            doriax_find_vulkan()\n";
    content += "            list(APPEND PLATFORM_LIBS Vulkan::Vulkan)\n";
    content += "        else()\n";
    content += "            add_definitions(\"-DSOKOL_GLCORE\")\n";
    content += "            list(APPEND PLATFORM_LIBS opengl32)\n";
    content += "        endif()\n";
    content += "    elseif(APPLE)\n";
    content += "        set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -fobjc-arc\")\n";
    content += "        set(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} -fobjc-arc\")\n";
    content += "        list(APPEND PLATFORM_SOURCE\n";
    content += "            ${DORIAX_PLATFORM_DIR}/apple/DoriaxGameController.mm\n";
    content += "            ${DORIAX_PLATFORM_DIR}/mac/DoriaxMac.mm\n";
    content += "            ${DORIAX_PLATFORM_DIR}/mac/WindowMac.mm\n";
    content += "            ${DORIAX_PLATFORM_DIR}/mac/SystemMac.mm\n";
    content += "            ${DORIAX_PLATFORM_DIR}/mac/MacInputRouter.mm\n";
    content += "            ${DORIAX_PLATFORM_DIR}/mac/main.mm\n";
    content += "        )\n";
    content += "        list(APPEND PLATFORM_LIBS \"-framework Cocoa\" \"-framework QuartzCore\" \"-framework GameController\")\n";
    content += "        # MTKView has no GL or Vulkan drawable, so those get views of their own\n";
    content += "        if(DORIAX_GRAPHIC_BACKEND STREQUAL \"glcore\")\n";
    content += "            add_definitions(\"-DSOKOL_GLCORE\")\n";
    content += "            list(APPEND PLATFORM_SOURCE ${DORIAX_PLATFORM_DIR}/mac/MacViewGL.mm)\n";
    content += "            list(APPEND PLATFORM_LIBS \"-framework OpenGL\")\n";
    content += "        elseif(DORIAX_GRAPHIC_BACKEND STREQUAL \"vulkan\")\n";
    content += "            add_definitions(\"-DSOKOL_VULKAN\")\n";
    content += "            add_definitions(\"-DVK_USE_PLATFORM_METAL_EXT\")\n";
    content += "            list(APPEND PLATFORM_SOURCE ${DORIAX_PLATFORM_DIR}/mac/MacViewVulkan.mm ${DORIAX_PLATFORM_DIR}/common/VulkanContext.cpp)\n";
    content += "            doriax_find_vulkan()\n";
    content += "            list(APPEND PLATFORM_LIBS Vulkan::Vulkan)\n";
    content += "        else()\n";
    content += "            add_definitions(\"-DSOKOL_METAL\")\n";
    content += "            list(APPEND PLATFORM_SOURCE ${DORIAX_PLATFORM_DIR}/mac/MacViewMetal.mm)\n";
    content += "            list(APPEND PLATFORM_LIBS \"-framework Metal\" \"-framework MetalKit\")\n";
    content += "        endif()\n";
    content += "    else()\n";
    content += "        find_package(X11 REQUIRED)\n";
    content += "        list(APPEND PLATFORM_SOURCE\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/DoriaxLinux.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/WindowLinux.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/SystemLinux.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/LinuxInputRouter.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/GamepadLinux.cpp\n";
    content += "            ${DORIAX_PLATFORM_DIR}/linux/GamepadDB.cpp\n";
    content += "        )\n";
    content += "        list(APPEND PLATFORM_LIBS X11::X11 dl m)\n";
    content += "        if(DORIAX_GRAPHIC_BACKEND STREQUAL \"vulkan\")\n";
    content += "            add_definitions(\"-DSOKOL_VULKAN\")\n";
    content += "            add_definitions(\"-DVK_USE_PLATFORM_XLIB_KHR\")\n";
    content += "            list(APPEND PLATFORM_SOURCE ${DORIAX_PLATFORM_DIR}/common/VulkanContext.cpp)\n";
    content += "            doriax_find_vulkan()\n";
    content += "            list(APPEND PLATFORM_LIBS Vulkan::Vulkan)\n";
    content += "        else()\n";
    content += "            add_definitions(\"-DSOKOL_GLCORE\")\n";
    content += "            list(APPEND PLATFORM_LIBS GL)\n";
    content += "        endif()\n";
    content += "    endif()\n";
    content += "endif() \n";
    return content;
}

std::string editor::Generator::buildInitSceneScriptsSource(const std::vector<SceneScriptSource>& scriptFiles) {
    std::string sourceContent;

    sourceContent += "\n";
    sourceContent += "using namespace doriax;\n";

    sourceContent += "\n";
    sourceContent += "#if defined(_MSC_VER)\n";
    sourceContent += "    #define PROJECT_API __declspec(dllexport)\n";
    sourceContent += "#else\n";
    sourceContent += "    #define PROJECT_API\n";
    sourceContent += "#endif\n\n";

    sourceContent += "extern \"C\" void PROJECT_API initScripts(doriax::Scene* scene) {\n";
    sourceContent += "    LuaBinding::initializeLuaScripts(scene);\n";

    if (!scriptFiles.empty()) {

        sourceContent += "\n";

        sourceContent += "    const auto& scriptsArray = scene->getComponentArray<ScriptComponent>();\n";
        sourceContent += "\n";

        sourceContent += "    for (size_t i = 0; i < scriptsArray->size(); i++) {\n";
        sourceContent += "        doriax::ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);\n";
        sourceContent += "        doriax::Entity entity = scriptsArray->getEntity(i);\n";
        sourceContent += "        for (auto& scriptEntry : scriptComp.scripts) {\n";
        sourceContent += "            if (scriptEntry.type == ScriptType::LUA) \n";
        sourceContent += "                continue; \n";
        sourceContent += "\n";

        for (const auto& s : scriptFiles) {
            sourceContent += "            if (scriptEntry.className == \"" + s.className + "\") {\n";
            sourceContent += "                " + s.className + "* script = new " + s.className + "(scene, entity);\n";
            sourceContent += "                scriptEntry.instance = static_cast<void*>(script);\n";
            sourceContent += "            }\n";
        }
        sourceContent += "        }\n";
        sourceContent += "    }\n";

        sourceContent += "    for (size_t i = 0; i < scriptsArray->size(); i++) {\n";
        sourceContent += "        doriax::ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);\n";
        sourceContent += "        for (auto& scriptEntry : scriptComp.scripts) {\n";
        sourceContent += "            if (scriptEntry.type == ScriptType::LUA) \n";
        sourceContent += "                continue; \n";
        sourceContent += "\n";

        for (const auto& s : scriptFiles) {
            if (s.properties.empty()) {
                continue;
            }
            sourceContent += "            if (scriptEntry.className == \"" + s.className + "\") {\n";

            sourceContent += "                " + s.className + "* typedScript = static_cast<" + s.className + "*>(scriptEntry.instance);\n";
            sourceContent += "\n";
            sourceContent += "                for (auto& prop : scriptEntry.properties) {\n";

            for (const auto& prop : s.properties) {
                sourceContent += "\n";
                sourceContent += "                    if (prop.name == \"" + prop.name + "\") {\n";

                if (prop.isPtr && !prop.ptrTypeName.empty()) {
                    sourceContent += "                        doriax::EntityReference entRef;\n";
                    sourceContent += "                        if (std::holds_alternative<doriax::EntityReference>(prop.value)) {\n";
                    sourceContent += "                            entRef = std::get<doriax::EntityReference>(prop.value);\n";
                    sourceContent += "                        }\n";
                    sourceContent += "                        doriax::Entity targetEntity = entRef.entity;\n";
                    sourceContent += "                        void* instancePtr = nullptr;\n";
                    sourceContent += "\n";
                    sourceContent += "                        if (targetEntity != NULL_ENTITY) {\n";
                    sourceContent += "                            doriax::Scene* targetScene = scene;\n";
                    sourceContent += "                            if (entRef.sceneId != 0) {\n";
                    sourceContent += "                                targetScene = SceneManager::getScenePtr(entRef.sceneId);\n";
                    sourceContent += "                            }\n";
                    sourceContent += "                            if (!targetScene || !targetScene->isEntityCreated(targetEntity)) {\n";
                    sourceContent += "                                Log::error(\"Script property " + s.className + "::" + prop.name + ": entity %u not found\", targetEntity);\n";
                    sourceContent += "                            } else {\n";
                    sourceContent += "                                doriax::ScriptComponent* targetScriptComp = targetScene->findComponent<doriax::ScriptComponent>(targetEntity);\n";
                    sourceContent += "                                if (targetScriptComp) {\n";
                    sourceContent += "                                    for (auto& targetScript : targetScriptComp->scripts) {\n";
                    sourceContent += "                                        if (targetScript.type != ScriptType::LUA) {\n";
                    sourceContent += "                                            if (targetScript.className == \"" + prop.ptrTypeName + "\" && targetScript.instance) {\n";
                    sourceContent += "                                                instancePtr = targetScript.instance;\n";
                    sourceContent += "                                                #ifdef DORIAX_EDITOR_PLUGIN\n";
                    sourceContent += "                                                printf(\"[DEBUG]   Found matching C++ script instance: '%s'\\n\", targetScript.className.c_str());\n";
                    sourceContent += "                                                #endif\n";
                    sourceContent += "                                                break;\n";
                    sourceContent += "                                            }\n";
                    sourceContent += "                                        }\n";
                    sourceContent += "                                    }\n";
                    sourceContent += "                                }\n";
                    sourceContent += "\n";
                    if (!prop.ptrTypeName.empty()) {
                        sourceContent += "                                if (!instancePtr) {\n";
                        sourceContent += "                                    #ifdef DORIAX_EDITOR_PLUGIN\n";
                        sourceContent += "                                    printf(\"[DEBUG]   No C++ script instance found, creating '" + prop.ptrTypeName + "' type\\n\");\n";
                        sourceContent += "                                    #endif\n";
                        sourceContent += "                                    instancePtr = new " + prop.ptrTypeName + "(targetScene, targetEntity);\n";
                        sourceContent += "                                    prop.ownedInstance = instancePtr;\n";
                        sourceContent += "                                }\n";
                    }
                    sourceContent += "                            }\n";
                    sourceContent += "                        }\n";
                    sourceContent += "\n";
                    sourceContent += "                        typedScript->" + prop.name + " = nullptr;\n";
                    sourceContent += "                        if (instancePtr) {\n";
                    sourceContent += "                            typedScript->" + prop.name + " = static_cast<" + prop.ptrTypeName + "*>(instancePtr);\n";
                    sourceContent += "                        }\n";
                    sourceContent += "\n";
                }

                sourceContent += "                        prop.memberPtr = &typedScript->" + prop.name + ";\n";
                sourceContent += "                    }\n";
            }

            sourceContent += "\n";
            sourceContent += "                    prop.syncToMember();\n";
            sourceContent += "                }\n";

            sourceContent += "            }\n";
        }

        sourceContent += "\n";
        sourceContent += "        }\n";
        sourceContent += "    }\n";

    } else{
        sourceContent += "    (void)scene; // Suppress unused parameter warning\n";
    }

    sourceContent += "}\n\n";

    return sourceContent;
}

std::string editor::Generator::buildCleanupSceneScriptsSource(const std::vector<SceneScriptSource>& scriptFiles) {
    std::string sourceContent;

    sourceContent += "extern \"C\" void PROJECT_API cleanupScripts(doriax::Scene* scene) {\n";
    sourceContent += "    LuaBinding::cleanupLuaScripts(scene);\n";

    if (!scriptFiles.empty()) {
        sourceContent += "    const auto& scriptsArray = scene->getComponentArray<ScriptComponent>();\n";
        sourceContent += "    for (size_t i = 0; i < scriptsArray->size(); i++) {\n";
        sourceContent += "        doriax::ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);\n";
        sourceContent += "        for (auto& scriptEntry : scriptComp.scripts) {\n";
        sourceContent += "            if (scriptEntry.type == ScriptType::LUA) continue;\n";
        sourceContent += "\n";
        sourceContent += "            if (scriptEntry.instance) {\n";
        for (const auto& s : scriptFiles) {
            sourceContent += "                if (scriptEntry.className == \"" + s.className + "\") {\n";
            sourceContent += "                    std::string addr = \"_\" + std::to_string(reinterpret_cast<std::uintptr_t>(scriptEntry.instance)) + \"_\";\n";
            sourceContent += "                    Engine::removeSubscriptionsByTag(addr);\n";
            sourceContent += "                    scene->removeSubscriptionsByTag(addr);\n";
            sourceContent += "                    delete static_cast<" + s.className + "*>(scriptEntry.instance);\n";
            sourceContent += "                }\n";
        }
        sourceContent += "                scriptEntry.instance = nullptr;\n";
        sourceContent += "            }\n";

        // delete the wrappers initScripts created for entity reference members
        for (const auto& s : scriptFiles) {
            std::string deleteContent;
            for (const auto& prop : s.properties) {
                if (!prop.isPtr || prop.ptrTypeName.empty()) {
                    continue;
                }
                deleteContent += "                    if (prop.name == \"" + prop.name + "\") {\n";
                deleteContent += "                        delete static_cast<" + prop.ptrTypeName + "*>(prop.ownedInstance);\n";
                deleteContent += "                    }\n";
            }
            if (deleteContent.empty()) {
                continue;
            }

            sourceContent += "\n";
            sourceContent += "            if (scriptEntry.className == \"" + s.className + "\") {\n";
            sourceContent += "                for (auto& prop : scriptEntry.properties) {\n";
            sourceContent += "                    if (!prop.ownedInstance) continue;\n";
            sourceContent += "\n";
            sourceContent += "                    std::string addr = \"_\" + std::to_string(reinterpret_cast<std::uintptr_t>(prop.ownedInstance)) + \"_\";\n";
            sourceContent += "                    Engine::removeSubscriptionsByTag(addr);\n";
            sourceContent += "                    scene->removeSubscriptionsByTag(addr);\n";
            sourceContent += "\n";
            sourceContent += deleteContent;
            sourceContent += "\n";
            sourceContent += "                    prop.ownedInstance = nullptr;\n";
            sourceContent += "                }\n";
            sourceContent += "            }\n";
        }

        sourceContent += "        }\n";
        sourceContent += "    }\n";
    }

    sourceContent += "}\n";

    return sourceContent;
}

std::string editor::Generator::getEditorPluginAbiCheck() {
    std::string cmakeContent;
#ifdef _WIN32
    // Check CMake's detected ABI: compiler names alone miss Clang targeting
    // MSVC, and saved/custom kits bypass discovery entirely.
    cmakeContent += "if(DORIAX_EDITOR_PLUGIN)\n";
#if defined(__MINGW32__)
    cmakeContent += "    if(NOT WIN32 OR NOT MINGW OR MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL \"MSVC\")\n";
    cmakeContent += "        message(FATAL_ERROR \"This Doriax editor requires MinGW/GNU C++ plugins. Use the same MinGW toolchain that built the editor and engine.\")\n";
#else
    cmakeContent += "    if(NOT WIN32 OR NOT (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL \"MSVC\"))\n";
    cmakeContent += "        message(FATAL_ERROR \"This Doriax editor requires MSVC-compatible C++ plugins. MSYS2/MinGW GCC cannot link its engine library. Select an MSVC-compatible compiler in Editor Settings > Desktop, or rebuild the editor and engine with your MinGW toolchain.\")\n";
#endif
    cmakeContent += "    endif()\n";
    cmakeContent += "    if(NOT CMAKE_SIZEOF_VOID_P EQUAL " + std::to_string(sizeof(void*)) + ")\n";
    cmakeContent += "        message(FATAL_ERROR \"C++ plugins must match this Doriax editor's " + std::to_string(sizeof(void*) * 8) + "-bit architecture. Select a matching compiler in Editor Settings > Desktop.\")\n";
    cmakeContent += "    endif()\n";
    cmakeContent += "endif()\n\n";
#endif
    return cmakeContent;
}

void editor::Generator::writeSourceFiles(const fs::path& projectPath, const fs::path& projectInternalPath, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<editor::SceneBuildInfo>& scenes, const std::vector<editor::BundleSceneInfo>& bundles, bool vsyncEnabled, const WindowSettings& windowSettings, const fs::path& assetsPath, const fs::path& luaPath, const std::vector<fs::path>& scriptDirs, int cxxStandard) {
    fs::path relativeInternalPath = fs::relative(projectInternalPath, projectPath);
    fs::path engineApiRelativePath = relativeInternalPath / "engine-api";

    std::string internalPathStr = "${CMAKE_CURRENT_SOURCE_DIR}/" + relativeInternalPath.generic_string();
    std::string engineApiPathStr = "${CMAKE_CURRENT_SOURCE_DIR}/" + engineApiRelativePath.generic_string();

    // Build FACTORY_SOURCES list for CMake (generated by Factory in configure())
    std::string factorySources = "set(FACTORY_SOURCES\n";
    std::unordered_set<std::string> addedFactorySources;
    for (const auto& sceneData : scenes) {
        std::string sceneName = Factory::toIdentifier(sceneData.name);
        std::string filename = sceneName + ".cpp";
        if (addedFactorySources.insert(filename).second) {
            factorySources += "    " + internalPathStr + "/generated/" + filename + "\n";
        }
    }
    for (const auto& bundle : bundles) {
        std::string filename = Factory::bundleToFileName(bundle.bundlePath) + ".cpp";
        if (addedFactorySources.insert(filename).second) {
            factorySources += "    " + internalPathStr + "/generated/" + filename + "\n";
        }
    }
    factorySources += ")\n";

    // Build SCRIPT_SOURCES list for CMake. Insertion order is kept so the file
    // only changes when the project does, and roots are searched as listed.
    std::vector<std::string> includeDirs;
    std::unordered_set<std::string> addedIncludeDirs;
    const auto addIncludeDir = [&](const std::string& dir) {
        if (addedIncludeDirs.insert(dir).second) {
            includeDirs.push_back(dir);
        }
    };

    std::string scriptSources = "set(SCRIPT_SOURCES\n";
    std::unordered_set<std::string> addedScriptSources;
    const auto addScriptSource = [&](const fs::path& path) {
        const fs::path absolute = path.is_absolute() ? path : projectPath / path;
        if (!addedScriptSources.insert(absolute.lexically_normal().generic_string()).second) {
            return;
        }
        if (path.is_relative()) {
            scriptSources += "    ${CMAKE_CURRENT_SOURCE_DIR}/" + path.generic_string() + "\n";
        } else {
            scriptSources += "    " + path.generic_string() + "\n";
        }
    };

    for (const auto& s : scriptFiles) {
        addScriptSource(s.path);

        if (!s.headerPath.empty()) {
            if (s.headerPath.is_relative()) {
                addIncludeDir("${CMAKE_CURRENT_SOURCE_DIR}");
            } else {
                addIncludeDir(s.headerPath.parent_path().generic_string());
            }
        }
    }

    // Each script root is an include directory, and every source under it is
    // compiled: a file builds by living there, not by being referenced.
    for (const fs::path& scriptDir : scriptDirs) {
        const fs::path rootPath = (scriptDir.is_absolute() ? scriptDir : projectPath / scriptDir).lexically_normal();

        std::error_code ec;
        if (!fs::is_directory(rootPath, ec)) {
            Out::warning("Script directory not found: %s", scriptDir.generic_string().c_str());
            continue;
        }

        if (scriptDir.is_absolute()) {
            addIncludeDir(scriptDir.generic_string());
        } else {
            addIncludeDir("${CMAKE_CURRENT_SOURCE_DIR}/" + scriptDir.generic_string());
        }

        // Iteration order is unspecified, so sort to keep the file stable.
        std::vector<fs::path> rootSources;
        try {
            for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                // Hidden and build directories hold the generated sources the
                // target already compiles through PROJECT_SOURCE and FACTORY_SOURCES.
                const std::string name = it->path().filename().string();
                if (it->is_directory() && (name.empty() || name[0] == '.' || name == "build")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file() || !Util::isSourceFile(it->path().string())) {
                    continue;
                }

                const fs::path relativePath = fs::relative(it->path(), projectPath, ec);
                const bool insideProject = !ec && !relativePath.empty() && *relativePath.begin() != "..";
                rootSources.push_back(insideProject ? relativePath : it->path());
            }
        } catch (const fs::filesystem_error& e) {
            Out::warning("Failed to scan script directory \"%s\": %s", scriptDir.generic_string().c_str(), e.what());
        }

        std::sort(rootSources.begin(), rootSources.end(), [](const fs::path& a, const fs::path& b) {
            return a.generic_string() < b.generic_string();
        });
        for (const fs::path& source : rootSources) {
            addScriptSource(source);
        }
    }
    scriptSources += ")\n";

    std::string includeDirsBlock;
    for (const std::string& includeDir : includeDirs) {
        includeDirsBlock += "    " + includeDir + "\n";
    }

    std::string cmakeContent;
    cmakeContent += "# This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    cmakeContent += "cmake_minimum_required(VERSION 3.20)\n";
    cmakeContent += "project(" + libName + ")\n\n";
    cmakeContent += "# Editor-resolved parallel build job limit, supplied via -D on every editor\n";
    cmakeContent += "# configure. Declared here so it is documented and so CMake does not warn\n";
    cmakeContent += "# about an unused variable on generators that do not consume it (only the\n";
    cmakeContent += "# MSVC branch below reads it). Empty or 0 means no per-file compile limit.\n";
    cmakeContent += "set(DORIAX_PARALLEL_BUILD_JOBS \"\" CACHE STRING \"Editor-resolved parallel build job limit\")\n\n";
    cmakeContent += "set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})\n";
    cmakeContent += "set(INTERNAL_DIR ${PROJECT_ROOT}/.doriax)\n\n";
    cmakeContent += "# Doriax runtime API headers for this project come from ${INTERNAL_DIR}/engine-api.\n";
    cmakeContent += "# Full engine + editor source (including YAML serialization for *.scene/*.bundle/project.yaml): https://github.com/doriaxengine/doriax\n\n";

    cmakeContent += "set(DORIAX_API_DIR " + engineApiPathStr + ")\n\n";

    cmakeContent += "# C++ standard from Project Settings\n";
    cmakeContent += "set(CMAKE_CXX_STANDARD " + std::to_string(cxxStandard) + ")\n";
    cmakeContent += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmakeContent += "set(CMAKE_OBJCXX_STANDARD ${CMAKE_CXX_STANDARD})\n";
    cmakeContent += "set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)\n\n";

    cmakeContent += "# Build mode: when ON, build as Doriax Editor plugin (shared library)\n";
    cmakeContent += "option(DORIAX_EDITOR_PLUGIN \"Build as Doriax Editor plugin\" OFF)\n";
    cmakeContent += getEditorPluginAbiCheck();
    cmakeContent += "if(DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    add_compile_definitions(DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    # The editor links its own engine build, which is compiled as a shared\n";
    cmakeContent += "    # library with editor-specific data layouts. The plugin must match both:\n";
    cmakeContent += "    #  - DORIAX_SHARED makes DORIAX_API resolve to __declspec(dllimport) so\n";
    cmakeContent += "    #    static members such as Engine::onUpdate link against the import lib on MSVC.\n";
    cmakeContent += "    #  - DORIAX_EDITOR keeps HybridArray (and other editor-only layouts)\n";
    cmakeContent += "    #    ABI-compatible across the DLL boundary.\n";
    cmakeContent += "    add_compile_definitions(DORIAX_SHARED DORIAX_EDITOR)\n";
    cmakeContent += "endif()\n\n";

    // Match the engine DLL shipped beside this editor, which DORIAX_LIB_DIR defaults
    // to; _DEBUG is defined iff this editor was built against the debug CRT.
    std::string runtimeLibrary = "MultiThreadedDLL";
#if defined(_DEBUG)
    runtimeLibrary = "MultiThreadedDebugDLL";
#endif

    cmakeContent += "# Every translation unit linking the engine DLL needs the CRT that DLL was built\n";
    cmakeContent += "# with: debug and release STL disagree on std::string/std::vector layout and use\n";
    cmakeContent += "# separate heaps, and an import library carries no /FAILIFMISMATCH records, so\n";
    cmakeContent += "# the linker cannot catch the mismatch. The build type cannot pick it either,\n";
    cmakeContent += "# since IDEs default to Debug, so it follows the editor that generated this file.\n";
    cmakeContent += "if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)\n";
    cmakeContent += "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"" + runtimeLibrary + "\")\n";
    cmakeContent += "endif()\n\n";

    cmakeContent += getPlatformCMakeConfig(vsyncEnabled, windowSettings, projectPath, assetsPath, luaPath) + "\n";

    cmakeContent += scriptSources + "\n";
    cmakeContent += factorySources + "\n";
    cmakeContent += "set(PROJECT_SOURCE " + internalPathStr + "/scene_scripts.cpp)\n\n";
    cmakeContent += "# Project target\n";
    cmakeContent += "if(NOT CMAKE_SYSTEM_NAME STREQUAL \"Android\" AND NOT DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    add_executable(" + libName + "\n";
    cmakeContent += "        ${PROJECT_SOURCE}\n";
    cmakeContent += "        ${SCRIPT_SOURCES}\n";
    cmakeContent += "        ${PLATFORM_SOURCE}\n";
    cmakeContent += "    )\n";
    cmakeContent += "else()\n";
    cmakeContent += "    add_library(" + libName + " SHARED\n";
    cmakeContent += "        ${PROJECT_SOURCE}\n";
    cmakeContent += "        ${SCRIPT_SOURCES}\n";
    cmakeContent += "        ${PLATFORM_SOURCE}\n";
    cmakeContent += "    )\n";
    cmakeContent += "endif()\n\n";

    cmakeContent += "# When building outside the editor, also compile Factory-generated sources\n";
    cmakeContent += "if(NOT DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    target_sources(" + libName + " PRIVATE ${FACTORY_SOURCES})\n";
    cmakeContent += "endif()\n\n";
    cmakeContent += "# To suppress warnings if not Debug\n";
    cmakeContent += "if(NOT CMAKE_BUILD_TYPE STREQUAL \"Debug\")\n";
    cmakeContent += "    set(DORIAX_LIB_SYSTEM SYSTEM)\n";
    cmakeContent += "endif()\n\n";
    cmakeContent += "target_include_directories(" + libName + " ${DORIAX_LIB_SYSTEM} PRIVATE\n";
    cmakeContent += includeDirsBlock + "\n";
    cmakeContent += "    " + engineApiPathStr + "\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/sokol\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/box2d/include\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/joltphysics\n";
    cmakeContent += "    " + engineApiPathStr + "/renders\n";
    cmakeContent += "    " + engineApiPathStr + "/core\n";
    cmakeContent += "    " + engineApiPathStr + "/core/action\n";
    cmakeContent += "    " + engineApiPathStr + "/core/action/keyframe\n";
    cmakeContent += "    " + engineApiPathStr + "/core/buffer\n";
    cmakeContent += "    " + engineApiPathStr + "/core/component\n";
    cmakeContent += "    " + engineApiPathStr + "/core/ecs\n";
    cmakeContent += "    " + engineApiPathStr + "/core/io\n";
    cmakeContent += "    " + engineApiPathStr + "/core/manager\n";
    cmakeContent += "    " + engineApiPathStr + "/core/math\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object/sound\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object/ui\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object/environment\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object/physics\n";
    cmakeContent += "    " + engineApiPathStr + "/core/pool\n";
    cmakeContent += "    " + engineApiPathStr + "/core/registry\n";
    cmakeContent += "    " + engineApiPathStr + "/core/render\n";
    cmakeContent += "    " + engineApiPathStr + "/core/script\n";
    cmakeContent += "    " + engineApiPathStr + "/core/shader\n";
    cmakeContent += "    " + engineApiPathStr + "/core/subsystem\n";
    cmakeContent += "    " + engineApiPathStr + "/core/texture\n";
    cmakeContent += "    " + engineApiPathStr + "/core/util\n";
    cmakeContent += ")\n\n";

    cmakeContent += "# libdoriax is searched in DORIAX_LIB_DIR; by default it points to the Doriax editor\n";
    cmakeContent += "# executable directory, which differs per machine and so comes from LocalPaths.cmake.\n";
    cmakeContent += "include(${INTERNAL_DIR}/LocalPaths.cmake OPTIONAL)\n";
    cmakeContent += "if(NOT DEFINED DORIAX_LIB_DIR OR DORIAX_LIB_DIR STREQUAL \"\")\n";
    cmakeContent += "    set(DORIAX_LIB_DIR \"${DORIAX_DEFAULT_LIB_DIR}\")\n";
    cmakeContent += "    # Default target is the editor's own engine build (shared, editor layouts).\n";
    cmakeContent += "    # Match its macros so symbols link (DORIAX_SHARED -> dllimport on MSVC) and\n";
    cmakeContent += "    # data layouts stay ABI-compatible (DORIAX_EDITOR), avoiding ODR/ABI mismatch.\n";
    cmakeContent += "    add_compile_definitions(DORIAX_SHARED DORIAX_EDITOR)\n";
    cmakeContent += "endif()\n\n";

    cmakeContent += "# Find doriax library in specified location. find_library() caches its\n";
    cmakeContent += "# result, so a stale DORIAX_LIB from a previous configure (e.g. before the\n";
    cmakeContent += "# editor/engine install moved) would otherwise stick around unchanged;\n";
    cmakeContent += "# force a fresh search on every configure instead.\n";
    cmakeContent += "unset(DORIAX_LIB CACHE)\n";
    cmakeContent += "find_library(DORIAX_LIB NAMES doriax PATHS \"${DORIAX_LIB_DIR}\" NO_DEFAULT_PATH)\n";
    cmakeContent += "if(NOT DORIAX_LIB)\n";
    cmakeContent += "    message(FATAL_ERROR \"Doriax library not found in ${DORIAX_LIB_DIR}\")\n";
    cmakeContent += "endif()\n\n";
    cmakeContent += "target_link_libraries(" + libName + " PRIVATE ${DORIAX_LIB} ${PLATFORM_LIBS})\n\n";
    cmakeContent += "# Set compile options based on compiler and platform\n";
    cmakeContent += "if(MSVC)\n";
    cmakeContent += "    # C4251/C4275: exported engine classes (DORIAX_API -> dllimport) expose STL\n";
    cmakeContent += "    # members (std::vector, std::string, FunctionSubscribe, ...). This is safe\n";
    cmakeContent += "    # here because the engine DLL and this plugin use the same dynamic CRT/STL,\n";
    cmakeContent += "    # so suppress the dll-interface warnings that would otherwise flood the build.\n";
    cmakeContent += "    # MSBuild's /m only parallelizes projects. /MP applies the editor-resolved\n";
    cmakeContent += "    # limit to translation units within this single-target plugin project.\n";
    cmakeContent += "    # /utf-8: generated sources embed UTF-8 literals and carry no BOM, so\n";
    cmakeContent += "    # MSVC would otherwise decode them in the active code page.\n";
    cmakeContent += "    if(DEFINED DORIAX_PARALLEL_BUILD_JOBS AND DORIAX_PARALLEL_BUILD_JOBS GREATER 0)\n";
    cmakeContent += "        target_compile_options(" + libName + " PRIVATE /W4 /EHsc /utf-8 /MP${DORIAX_PARALLEL_BUILD_JOBS} /wd4251 /wd4275)\n";
    cmakeContent += "    else()\n";
    cmakeContent += "        target_compile_options(" + libName + " PRIVATE /W4 /EHsc /utf-8 /MP /wd4251 /wd4275)\n";
    cmakeContent += "    endif()\n";
    cmakeContent += "else()\n";
    cmakeContent += "    target_compile_options(" + libName + " PRIVATE -Wall -Wextra -fPIC)\n";
    cmakeContent += "    # Error on unresolved symbols at link time. Apple ld does this by default;\n";
    cmakeContent += "    # GNU ld needs '-z defs --no-undefined' and MinGW only understands '--no-undefined'.\n";
    cmakeContent += "    if(WIN32)\n";
    cmakeContent += "        target_link_options(" + libName + " PRIVATE -Wl,--no-undefined)\n";
    cmakeContent += "    elseif(NOT APPLE)\n";
    cmakeContent += "        target_link_options(" + libName + " PRIVATE -Wl,-z,defs,--no-undefined)\n";
    cmakeContent += "    endif()\n";
    cmakeContent += "endif()\n\n";
    cmakeContent += "# Set properties for the shared library\n";
    cmakeContent += "set_target_properties(" + libName + " PROPERTIES\n";
    cmakeContent += "    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}\n";
    cmakeContent += "    OUTPUT_NAME \"" + libName + "\"\n";
    cmakeContent += "    PREFIX \"\"\n";
    cmakeContent += ")\n\n";

    cmakeContent += "# The engine DLL lives next to the editor, and Windows searches only the\n";
    cmakeContent += "# executable directory and PATH, so a standalone build needs its own copy or\n";
    cmakeContent += "# it dies with STATUS_DLL_NOT_FOUND before main(). The other desktops get\n";
    cmakeContent += "# DORIAX_LIB_DIR in the build rpath from CMake.\n";
    cmakeContent += "if(WIN32 AND NOT DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    # Same reason as DORIAX_LIB above: never reuse a cached path\n";
    cmakeContent += "    unset(DORIAX_RUNTIME_LIB CACHE)\n";
    cmakeContent += "    find_file(DORIAX_RUNTIME_LIB NAMES doriax.dll PATHS \"${DORIAX_LIB_DIR}\" NO_DEFAULT_PATH)\n";
    cmakeContent += "    if(DORIAX_RUNTIME_LIB)\n";
    cmakeContent += "        add_custom_command(TARGET " + libName + " POST_BUILD\n";
    cmakeContent += "            COMMAND ${CMAKE_COMMAND} -E copy_if_different\n";
    cmakeContent += "                    \"${DORIAX_RUNTIME_LIB}\" \"$<TARGET_FILE_DIR:" + libName + ">\"\n";
    cmakeContent += "            COMMENT \"Copying the Doriax engine runtime next to the executable\")\n";
    cmakeContent += "    else()\n";
    cmakeContent += "        message(WARNING \"doriax.dll not found in ${DORIAX_LIB_DIR}: the executable will not start outside the editor.\")\n";
    cmakeContent += "    endif()\n";
    cmakeContent += "endif()\n";

    cmakeContent += "\n# Optional user build file, never written by the editor: extra include roots,\n";
    cmakeContent += "# libraries and defines go there so they survive regeneration. Attach to\n";
    cmakeContent += "# DORIAX_TARGET, not the literal target name, and use DORIAX_SCRIPTS_DIR as the\n";
    cmakeContent += "# root of script paths; the exported build sets both, so one file serves either.\n";
    cmakeContent += "set(DORIAX_TARGET " + libName + ")\n";
    cmakeContent += "set(DORIAX_SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})\n";
    cmakeContent += "include(${CMAKE_CURRENT_SOURCE_DIR}/ProjectBuild.cmake OPTIONAL)\n";

    // Build C++ source content
    std::string sourceContent;
    sourceContent += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    sourceContent += "// This file binds scene script metadata to compiled C++ and Lua scripts for the current build configuration.\n\n";
    sourceContent += "#include <vector>\n";
    sourceContent += "#include <string>\n";
    sourceContent += "#include <stdio.h>\n";
    sourceContent += "#include \"Doriax.h\"\n\n";

    for (const auto& s : scriptFiles) {
        fs::path headerPath = s.headerPath;
        if (headerPath.is_relative()) {
            headerPath = projectPath / headerPath;
        }
        if (!headerPath.empty() && fs::exists(headerPath) && fs::is_regular_file(headerPath)) {
            fs::path relativePath = fs::relative(headerPath, projectPath);
            std::string inc = relativePath.generic_string();
            sourceContent += "#include \"" + inc + "\"\n";
        }
    }

    sourceContent += buildInitSceneScriptsSource(scriptFiles);
    sourceContent += buildCleanupSceneScriptsSource(scriptFiles);

    const fs::path cmakeFile = projectPath / "CMakeLists.txt";
    const fs::path sourceFile = projectInternalPath / "scene_scripts.cpp";

    // Everything that would otherwise name this machine in CMakeLists.txt, so that
    // file is byte-identical for the whole team
    std::string localPathsContent;
    localPathsContent += "# This file is auto-generated by Doriax Editor. Do not edit manually.\n";
    localPathsContent += "# Paths that belong to this machine only, so that CMakeLists.txt does not have\n";
    localPathsContent += "# to. Version control ignores .doriax/; the editor rewrites this on every build.\n\n";
    localPathsContent += "if(NOT DORIAX_SHADER_PATH)\n";
    localPathsContent += "    set(DORIAX_SHADER_PATH \"" + App::getUserShaderCacheDir().generic_string() + "\")\n";
    localPathsContent += "endif()\n\n";
    localPathsContent += "# Where libdoriax sits on this machine: the directory of the editor that wrote\n";
    localPathsContent += "# this file. -DDORIAX_LIB_DIR=<dir> overrides it.\n";
    localPathsContent += "set(DORIAX_DEFAULT_LIB_DIR \"" + FileUtils::getExecutableDir().generic_string() + "\")\n";
    if (!FileUtils::isEngineDirEphemeral()) {
        localPathsContent += "\n# Engine API source this editor build was compiled from, recorded for tooling.\n";
        localPathsContent += "set(DORIAX_LOCAL_ENGINE_DIR \"" + FileUtils::getEngineDir().generic_string() + "\")\n";
    }

    FileUtils::writeIfChanged(cmakeFile, cmakeContent);
    FileUtils::writeIfChanged(projectInternalPath / "LocalPaths.cmake", localPathsContent);
    FileUtils::writeIfChanged(sourceFile, sourceContent);

    // Generate .vscode/settings.json for VS Code if it doesn't exist
    fs::path vscodeDir = projectPath / ".vscode";
    if (!fs::exists(vscodeDir)) {
        fs::create_directories(vscodeDir);
    }
    fs::path settingsFile = vscodeDir / "settings.json";
    if (!fs::exists(settingsFile)) {
        std::string settingsContent;
        settingsContent += "{\n";
        settingsContent += "    \"cmake.buildDirectory\": \"${workspaceFolder}/" + relativeInternalPath.generic_string() + "/externalbuild\"\n";
        settingsContent += "}\n";
        FileUtils::writeIfChanged(settingsFile, settingsContent);
    }

    fs::path agentsFile = projectPath / "AGENTS.md";
    std::string agentsContent;
    const std::string agentsBegin = "<!-- BEGIN DORIAX GENERATED CONTEXT -->";
    const std::string agentsEnd = "<!-- END DORIAX GENERATED CONTEXT -->";
    agentsContent += agentsBegin + "\n";
    agentsContent += "<!-- This section is regenerated by Doriax Editor. Keep project-specific instructions outside these markers. -->\n\n";
    agentsContent += "# Doriax Project Context\n\n";
    agentsContent += "This project was generated by Doriax Editor version `" DORIAX_EDITOR_VERSION "`.\n\n";
    agentsContent += "## Source references\n\n";
    agentsContent += "- Runtime API headers used by this project (snapshot): `" + engineApiRelativePath.generic_string() + "`\n";
    agentsContent += "- Full engine **and editor** source (upstream): https://github.com/doriaxengine/doriax\n\n";
    agentsContent += "Use the local API snapshot first for runtime API details. For upstream source, use the tag or commit matching the editor version above; do not assume the latest upstream code matches this project. If the version is unknown or cannot be matched, report that limitation before relying on upstream behavior.\n\n";
    agentsContent += "The local paths above contain only the runtime engine API (what user code links against).\n";
    agentsContent += "They do **not** include the editor itself. To understand the YAML schema of `*.scene`, `*.bundle`, and `project.yaml`, or how the generator/factory produces C++, consult the upstream repository — specifically the `editor/` directory.\n\n";
    agentsContent += "## Generated files\n\n";
    agentsContent += "These files are produced by the editor when a scene is played/run and are intended for **local development and testing**. Project export/distribution uses a separate pipeline and does not reuse these files. Do not edit them manually — they will be overwritten on the next generation:\n\n";
    agentsContent += "- `CMakeLists.txt` (project root)\n";
    agentsContent += "- `" + relativeInternalPath.generic_string() + "/LocalPaths.cmake` (paths belonging to this machine, included by `CMakeLists.txt`)\n";
    agentsContent += "- `" + relativeInternalPath.generic_string() + "/scene_scripts.cpp`\n";
    agentsContent += "- `" + relativeInternalPath.generic_string() + "/generated/` (scene factories, bundle factories, `main.cpp`)\n";
    agentsContent += "- `" + engineApiRelativePath.generic_string() + "/` (engine API snapshot copied from the editor)\n\n";

    agentsContent += "## Version control\n\n";
    agentsContent += "`project.yaml`, `*.scene`, `*.bundle`, `ProjectBuild.cmake`, `AGENTS.md` and the assets describe the game and are meant to be committed. "
                     "Everything under `" + relativeInternalPath.generic_string() + "/` is not: it holds the engine API snapshot and generated C++, the build settings for this machine "
                     "(`user/build.yaml`), this developer's editor layout and viewport cameras (`user/workspace.yaml`), and whatever the game writes through `data://` during Play (`user/rundata/`). "
                     "The editor regenerates all of it when the project is opened or built.\n\n";
    agentsContent += "`CMakeLists.txt` is regenerated too and is ignored for the same reason. It contains no machine-specific path: asset and Lua roots resolve from `${PROJECT_ROOT}`, and the shader cache comes from `LocalPaths.cmake`.\n\n";
    agentsContent += "Do not move per-user or per-machine values into `project.yaml`; a value that differs between two developers working on the same project belongs in `" + relativeInternalPath.generic_string() + "/user/`.\n\n";
    agentsContent += "## Custom build settings\n\n";
    agentsContent += "`CMakeLists.txt` is regenerated on every build, so edits to it are lost. Put your own build settings in `ProjectBuild.cmake` at the project root instead: the editor never writes that file, and both the editor and exported builds include it when it exists.\n\n";
    agentsContent += "Use `${DORIAX_TARGET}` instead of the literal target name, which differs between the two builds. `${DORIAX_SCRIPTS_DIR}` is the root your `.h`/`.cpp` paths are relative to.\n\n";
    agentsContent += "```cmake\n";
    agentsContent += "target_include_directories(${DORIAX_TARGET} PRIVATE ${DORIAX_SCRIPTS_DIR}/Source/Game/Public)\n";
    agentsContent += "target_link_libraries(${DORIAX_TARGET} PRIVATE mylib)\n";
    agentsContent += "```\n\n";
    agentsContent += "## Regenerating C++ code\n\n";
    agentsContent += "The generated C++ sources (scene factories, script bindings) are derived from `*.scene`, `*.bundle`, and `project.yaml` files.\n";
    agentsContent += "After changing any `*.scene`, `*.bundle`, or `project.yaml` file, regenerate through Doriax Editor by playing/running the scene before validating the resulting build. This refreshes the generated sources, including `" + relativeInternalPath.generic_string() + "/generated/` and script bindings. If editor access is unavailable, report that regeneration and runtime validation remain pending.\n\n";
    agentsContent += "## C++ Scripts\n\n";
    agentsContent += "User C++ script files (`.h`/`.cpp`) can be placed anywhere in the project outside of `" + relativeInternalPath.generic_string() + "/`.\n";
    agentsContent += "Each script class must inherit from `doriax::Script`; ordinary helper classes and source files do not need to. "
                     "The editor discovers and registers scripts automatically; "
                     "they are added to `SCRIPT_SOURCES` in the generated `CMakeLists.txt` and compiled into the project.\n";
    agentsContent += "A directory listed as a script root (Doriax Editor: Project Settings > Directories) is compiled whole: "
                     "source files are built even when no component references them. Discovery skips hidden directories and directories named `build`. The root is an include directory, "
                     "so a header at `<root>/PLAYER/Thing.h` is included as `PLAYER/Thing.h`.\n";
    agentsContent += "The C++ language standard is Project Settings > Build > C++ Standard (`cxxStandard` in `project.yaml`): "
                     + editor::cxxStandardList() + ". Play and export both build with it; do not edit `CMAKE_CXX_STANDARD` in the generated CMakeLists.txt.\n";
    agentsContent += "Lua scripts (`.lua`) are separate — they are loaded at runtime and are not compiled into the binary.\n\n";
    agentsContent += "## BundleManager API\n\n";
    agentsContent += "`registerBundle` factories return `bool`. Void-returning factories still register and are treated as success. "
                     "Every createBundle call creates its own instance root, so a bundle can be spawned repeatedly; the third argument is the entity the new root is parented to. "
                     "Entity IDs are scene-local, so parent by name with `createBundle(name, scene, parentName)` to resolve it in that scene.\n";
    agentsContent += "Only bundles a scene instantiates and the ones listed under `standaloneBundles` in `project.yaml` are built and registered. "
                     "A bundle spawned by script alone must be in that list (Doriax Editor: Project > Bundles), otherwise `createBundle` reports it as not found.\n\n";
    agentsContent += "## Build modes\n\n";
    agentsContent += "- **Editor mode** (`DORIAX_EDITOR_PLUGIN=ON`): builds as a shared library; `main.cpp` and Factory-generated scene sources are excluded. Used by the editor to hot-reload the project.\n";
    agentsContent += "- **Standalone mode** (`DORIAX_EDITOR_PLUGIN=OFF`, default): builds as an executable; includes `main.cpp` and all Factory sources. This standalone build is for local testing only — production distribution uses the editor's separate export pipeline.\n";
    agentsContent += "\n" + agentsEnd;

    // Only replace our marked section. Unmarked files may contain user edits,
    // including additions to older generated files, so retain them in full.
    if (fs::exists(agentsFile)) {
        std::ifstream input(agentsFile, std::ios::binary);
        std::string existing((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (!input || input.bad()) {
            Out::warning("Could not read AGENTS.md; leaving it unchanged.");
            return;
        }
        const size_t begin = existing.find(agentsBegin);
        const size_t end = existing.find(agentsEnd);
        if (begin == std::string::npos && end == std::string::npos) {
            agentsContent = existing + (existing.empty() ? "" : "\n\n") + agentsContent + "\n";
        } else if (begin == std::string::npos || end == std::string::npos || end < begin ||
                   existing.find(agentsBegin, begin + agentsBegin.size()) != std::string::npos ||
                   existing.find(agentsEnd, end + agentsEnd.size()) != std::string::npos) {
            Out::warning("AGENTS.md has incomplete or duplicate Doriax section markers; leaving it unchanged.");
            return;
        } else {
            existing.replace(begin, end + agentsEnd.size() - begin, agentsContent);
            agentsContent = std::move(existing);
        }
    } else {
        agentsContent += "\n";
    }

    FileUtils::writeIfChanged(agentsFile, agentsContent);
}

std::vector<editor::BundleInstanceInfo> editor::Generator::writeBundleSources(const std::map<fs::path, EntityBundle>& entityBundles, uint32_t sceneId, const fs::path& projectPath, const fs::path& projectInternalPath) {
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);

    std::vector<BundleInstanceInfo> bundleInstances;

    for (const auto& [bundlePath, bundle] : entityBundles) {
        auto sceneIt = bundle.instances.find(sceneId);
        if (sceneIt != bundle.instances.end()) {
            for (const auto& instance : sceneIt->second) {
                BundleInstanceInfo info;
                info.bundlePath = bundlePath;
                info.rootEntity = instance.rootEntity;
                for (const auto& member : instance.members) {
                    info.memberEntities.insert(member.localEntity);
                }

                // Build override info from instance overrides
                if (!instance.overrides.empty()) {
                    for (const auto& [sceneEntity, bitmask] : instance.overrides) {
                        BundleOverrideInfo ovr;
                        ovr.sceneEntity = sceneEntity;

                        // Decode bitmask to ComponentType list
                        for (int bit = 0; bit < 64; bit++) {
                            if (bitmask & (1ULL << bit)) {
                                ovr.overriddenComponents.push_back(static_cast<ComponentType>(bit));
                            }
                        }

                        if (!ovr.overriddenComponents.empty()) {
                            info.overrides.push_back(std::move(ovr));
                        }
                    }
                }

                bundleInstances.push_back(std::move(info));
            }
        }

        if (!bundle.registry) {
            continue;
        }

        // Write bundle .h and .cpp files
        std::string headerContent = Factory::createBundleHeader(bundlePath);
        std::string sourceContent = Factory::createBundle(bundlePath, bundle.registry.get(), bundle.registryEntities, projectPath, generatedPath);

        std::string bundleFileName = Factory::bundleToFileName(bundlePath);
        FileUtils::writeIfChanged(generatedPath / (bundleFileName + ".h"), headerContent);
        FileUtils::writeIfChanged(generatedPath / (bundleFileName + ".cpp"), sourceContent);
    }

    return bundleInstances;
}

void editor::Generator::writeSceneSource(Scene* scene, const std::string& sceneName, const std::vector<Entity>& entities, const Entity camera, const fs::path& projectPath, const fs::path& projectInternalPath, std::vector<BundleInstanceInfo>& bundleInstances){
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);

    std::string sceneIdStr = Factory::toIdentifier(sceneName);

    std::string sceneContent = Factory::createScene(0, scene, sceneName, entities, camera, projectPath, generatedPath, bundleInstances);

    std::string filename = sceneIdStr + ".cpp";
    const fs::path sourceFile = generatedPath / filename;

    FileUtils::writeIfChanged(sourceFile, sceneContent);
}

void editor::Generator::clearSceneSource(const std::string& sceneName, const fs::path& projectInternalPath) {
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);
    std::string filename = Factory::toIdentifier(sceneName) + ".cpp";
    const fs::path sourceFile = generatedPath / filename;

    if (fs::exists(sourceFile)) {
        std::error_code ec;
        fs::remove(sourceFile, ec);
        if (ec) {
            Out::error("Failed to remove scene source file '%s': %s", sourceFile.string().c_str(), ec.message().c_str());
        }
    }
}

void editor::Generator::configure(const std::vector<editor::SceneBuildInfo>& scenes, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<editor::BundleSceneInfo>& bundles, const fs::path& projectPath, const fs::path& projectInternalPath, const fs::path& assetsPath, const fs::path& luaPath, const std::vector<fs::path>& scriptDirs, int cxxStandard, Scaling scalingMode, TextureStrategy textureStrategy, unsigned int canvasWidth, unsigned int canvasHeight, bool vsyncEnabled, const WindowSettings& windowSettings){
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);

    // The editor used to emit a GLFW application host into every project. It is
    // gone, but projects created before that still carry the files, and nothing
    // else removes them; the exported build globs every *.cpp under the project
    // root, so a leftover would be compiled against an engine that no longer
    // has GLFW in it.
    std::error_code removeError;
    fs::remove(generatedPath / "PlatformEditor.h", removeError);
    fs::remove(generatedPath / "PlatformEditor.cpp", removeError);

    // Build main.cpp content
    std::string mainContent;
    mainContent += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    mainContent += "#include \"Doriax.h\"\n";

    // Include bundle headers (contain declarations for bundle creation functions)
    for (const auto& bundleData : bundles) {
        std::string headerName = Factory::bundleToFileName(bundleData.bundlePath) + ".h";
        mainContent += "#include \"" + headerName + "\"\n";
    }
    if (!bundles.empty()) {
        mainContent += "\n";
    }

    mainContent += "using namespace doriax;\n\n";

    // Forward declarations for per-scene initialization functions (defined in generated scene .cpp files)
    for (const auto& sceneData : scenes) {
        std::string sceneName = Factory::toIdentifier(sceneData.name);
        mainContent += "void create_" + sceneName + "(Scene* scene);\n";
    }
    mainContent += "extern \"C\" void initScripts(doriax::Scene* scene);\n";
    mainContent += "extern \"C\" void cleanupScripts(doriax::Scene* scene);\n";
    mainContent += "\n";

    std::map<uint32_t, std::string> sceneIdToName;
    for (const auto& sceneData : scenes) {
        std::string sceneName = "_" + Factory::toIdentifier(sceneData.name);
        mainContent += "static Scene* " + sceneName + " = nullptr;\n";
        sceneIdToName[sceneData.id] = sceneName;
    }
    mainContent += "\n";

    // Per-stack: static scene pointers + load function
    for (const auto& sceneData : scenes) {
        std::string stackId = Factory::toIdentifier(sceneData.name);
        mainContent += "// --- Scene stack: " + sceneData.name + " ---\n";
        mainContent += "void load_" + stackId + "() {\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    bool " + sceneName + "_needsInit = false;\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (!" + sceneName + "){\n";
            mainContent += "        " + sceneName + " = new Scene();\n";
            mainContent += "        SceneManager::setScenePtr(" + std::to_string(sceneId) + ", " + sceneName + ");\n";
            mainContent += "        " + sceneName + "_needsInit = true;\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (" + sceneName + "_needsInit) {\n";
            mainContent += "        create" + sceneName + "(" + sceneName + ");\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (" + sceneName + "_needsInit) {\n";
            mainContent += "        initScripts(" + sceneName + ");\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.activeScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            if (sceneData.id == sceneId) {
                mainContent += "    Engine::setScene(" + sceneName + ");\n";
            } else {
                mainContent += "    Engine::addSceneLayer(" + sceneName + ");\n";
            }
        }
        mainContent += "\n";
        for (const auto& sceneDataAux: scenes) {
            if (sceneDataAux.id == sceneData.id) continue;
            if (std::find(sceneData.involvedScenes.begin(), sceneData.involvedScenes.end(), sceneDataAux.id) != sceneData.involvedScenes.end()) continue;
            std::string sceneName = "_" + Factory::toIdentifier(sceneDataAux.name);
            mainContent += "    if (" + sceneName + ") {\n";
            mainContent += "        cleanupScripts(" + sceneName + ");\n";
            mainContent += "        SceneManager::removeScenePtr(" + std::to_string(sceneDataAux.id) + ");\n";
            mainContent += "        delete " + sceneName + ";\n";
            mainContent += "        " + sceneName + " = nullptr;\n";
            mainContent += "    }\n";
        }
        mainContent += "}\n\n";
    }

    for (const auto& sceneData : scenes) {
        std::string stackId = Factory::toIdentifier(sceneData.name);
        mainContent += "// --- Scene stack: " + sceneData.name + " ---\n";
        mainContent += "void add_" + stackId + "() {\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    bool " + sceneName + "_needsInit = false;\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (!" + sceneName + "){\n";
            mainContent += "        " + sceneName + " = new Scene();\n";
            mainContent += "        SceneManager::setScenePtr(" + std::to_string(sceneId) + ", " + sceneName + ");\n";
            mainContent += "        " + sceneName + "_needsInit = true;\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (" + sceneName + "_needsInit) {\n";
            mainContent += "        create" + sceneName + "(" + sceneName + ");\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            mainContent += "    if (" + sceneName + "_needsInit) {\n";
            mainContent += "        initScripts(" + sceneName + ");\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.activeScenes) {
            std::string sceneName = "_" + Factory::toIdentifier(sceneIdToName[sceneId]);
            if (sceneData.id != sceneId) {
                mainContent += "    Engine::addSceneLayer(" + sceneName + ");\n";
            }
        }

        mainContent += "}\n\n";
    }

    // Entry point of the native application backend for this OS. On Apple the
    // process starts in platform/apple/macos/main.m instead, so defining main()
    // here would collide with it.
    // The markers let Exporter strip this block wholesale: an exported project
    // compiles the engine's own platform main.cpp, which already defines main().
    mainContent += "// DORIAX_ENTRY_POINT_BEGIN\n";
    mainContent += "#if defined(_WIN32)\n";
    mainContent += "#include \"DoriaxWin.h\"\n";
    mainContent += "int main(int argc, char* argv[]) {\n";
    mainContent += "    return DoriaxWin::init(argc, argv);\n";
    mainContent += "}\n";
    mainContent += "#elif !defined(__APPLE__)\n";
    mainContent += "#include \"DoriaxLinux.h\"\n";
    mainContent += "int main(int argc, char* argv[]) {\n";
    mainContent += "    return DoriaxLinux::init(argc, argv);\n";
    mainContent += "}\n";
    mainContent += "#endif\n";
    mainContent += "// DORIAX_ENTRY_POINT_END\n\n";

    mainContent += "DORIAX_INIT void init() {\n";
    mainContent += "    Engine::setCanvasSize(" + std::to_string(canvasWidth) + ", " + std::to_string(canvasHeight) + ");\n";

    // Scaling mode
    switch (scalingMode) {
        case Scaling::FITWIDTH:  mainContent += "    Engine::setScalingMode(Scaling::FITWIDTH);\n"; break;
        case Scaling::FITHEIGHT: mainContent += "    Engine::setScalingMode(Scaling::FITHEIGHT);\n"; break;
        case Scaling::LETTERBOX: mainContent += "    Engine::setScalingMode(Scaling::LETTERBOX);\n"; break;
        case Scaling::CROP:      mainContent += "    Engine::setScalingMode(Scaling::CROP);\n"; break;
        case Scaling::STRETCH:   mainContent += "    Engine::setScalingMode(Scaling::STRETCH);\n"; break;
        case Scaling::NATIVE:    mainContent += "    Engine::setScalingMode(Scaling::NATIVE);\n"; break;
    }

    // Texture strategy
    switch (textureStrategy) {
        case TextureStrategy::FIT:    mainContent += "    Engine::setTextureStrategy(TextureStrategy::FIT);\n"; break;
        case TextureStrategy::RESIZE: mainContent += "    Engine::setTextureStrategy(TextureStrategy::RESIZE);\n"; break;
        case TextureStrategy::NONE:   mainContent += "    Engine::setTextureStrategy(TextureStrategy::NONE);\n"; break;
    }

    mainContent += "\n";

    // Register all stacks with SceneManager
    for (const auto& sceneData : scenes) {
        std::string stackId = Factory::toIdentifier(sceneData.name);
        std::string sceneIds;
        for (size_t i = 0; i < sceneData.activeScenes.size(); i++) {
            if (i > 0) sceneIds += ", ";
            sceneIds += std::to_string(sceneData.activeScenes[i]);
        }
        mainContent += "    SceneManager::registerScene(" + std::to_string(sceneData.id) + ", \"" + sceneData.name + "\", load_" + stackId + ", add_" + stackId + ", {" + sceneIds + "});\n";
    }
    mainContent += "\n";

    // Register all bundles with BundleManager
    for (size_t i = 0; i < bundles.size(); i++) {
        fs::path noExt = bundles[i].bundlePath;
        noExt.replace_extension();
        std::string bundleName = noExt.generic_string();
        mainContent += "    BundleManager::registerBundle(" + std::to_string(i + 1) + ", \"" + bundleName + "\", " + bundles[i].functionName + ");\n";
    }
    if (!bundles.empty()) {
        mainContent += "\n";
    }

    for (const auto& scene : scenes) {
        if (scene.isMain) {
            mainContent += "    SceneManager::loadScene(\"" + scene.name + "\");\n";
            break; // Load the first main scene found, or the only scene if none are marked main
        }
    }
    mainContent += "}\n";

    const fs::path mainFile = generatedPath / "main.cpp";
    FileUtils::writeIfChanged(mainFile, mainContent);

    writeSourceFiles(projectPath, projectInternalPath, libName, scriptFiles, scenes, bundles, vsyncEnabled, windowSettings, assetsPath, luaPath, scriptDirs, cxxStandard);
}

std::string editor::Generator::resolveCMakePath(const std::string& userPath) {
    if (userPath.empty()) return "";

    std::error_code ec;
    fs::path candidate = fs::path(userPath);

#ifdef _WIN32
    const std::string exeName = "cmake.exe";
#else
    const std::string exeName = "cmake";
#endif

    if (fs::is_regular_file(candidate, ec)) {
        // CMake.app/Contents/MacOS/CMake is the GUI, which answers --version
        // like the real thing; the command line tool is next door in bin.
        if (candidate.parent_path().filename() == "MacOS") {
            const fs::path cli = candidate.parent_path().parent_path() / "bin" / exeName;
            if (fs::is_regular_file(cli, ec)) {
                return cli.string();
            }
        }

        // The dialog lists every file, and an installer or a disk image must not
        // become the cmake every later build runs.
        std::string name = candidate.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return (name == "cmake" || name == "cmake.exe") ? candidate.string() : std::string();
    }

    if (fs::is_directory(candidate, ec)) {
        // A pick can land on the install root, its bin directory or the macOS
        // bundle, all of which the user thinks of as "CMake".
        const fs::path insideDir[] = {
            candidate / exeName,
            candidate / "bin" / exeName,
            candidate / "Contents" / "bin" / exeName,
            candidate / "CMake.app" / "Contents" / "bin" / exeName
        };
        for (const auto& path : insideDir) {
            if (fs::is_regular_file(path, ec)) {
                return path.string();
            }
        }
    }

    return "";
}

std::string editor::Generator::cmakeExecutable() {
    const std::string configured = AppSettings::getCMakePath();
    // Quoted: an install path may contain spaces ("Program Files", a bundle).
    return configured.empty() ? "cmake" : ("\"" + configured + "\"");
}

std::string editor::Generator::probeCMakeVersion(const std::string& path) {
    const std::string command = "\"" + path + "\" --version";
#ifdef _WIN32
    const std::string output = CommandRunner::runCaptureNoWindow(command + " 2>nul");
#else
    const std::string output = ShellEnv::runCapture(command + " 2>/dev/null", 5000);
#endif

    // "cmake version 3.31.6" on the first line.
    const std::string prefix = "cmake version ";
    const size_t at = output.find(prefix);
    if (at == std::string::npos) return "";

    size_t end = output.find_first_of("\r\n", at + prefix.size());
    if (end == std::string::npos) end = output.size();

    std::string version = output.substr(at + prefix.size(), end - (at + prefix.size()));
    while (!version.empty() && (version.back() == '\r' || version.back() == ' ')) {
        version.pop_back();
    }
    return version;
}

editor::CMakeInfo editor::Generator::detectCMake() {
    CMakeInfo info;

    const std::string configured = AppSettings::getCMakePath();
    if (!configured.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(configured, ec)) {
            info.error = "The configured CMake path no longer exists: " + configured;
            return info;
        }
        // A hand-picked path is trusted only once it answers --version, so it
        // cannot show as healthy here and then fail on Play.
        info.version = probeCMakeVersion(configured);
        if (info.version.empty()) {
            info.error = "The configured CMake path does not run as CMake: " + configured;
            return info;
        }
        info.found = true;
        info.path = configured;
        info.source = "configured path";
        return info;
    }

    // What PATH yields is an executable named cmake that the shell would run
    // too, so it stays usable even if the version cannot be read.
    const std::string onPath = ShellEnv::findExecutable("cmake");
    if (onPath.empty()) return info;

    info.found = true;
    info.path = onPath;
    info.source = "on PATH";
    info.version = probeCMakeVersion(onPath);

    return info;
}

std::vector<editor::CMakeKit> editor::Generator::detectAvailableKits() {
    std::vector<CMakeKit> kits;

    auto runCmd = [](const std::string& cmd) -> std::string {
#ifdef _WIN32
        return CommandRunner::runCaptureNoWindow(cmd + " 2>nul");
#else
        FILE* pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
        if (!pipe) return "";
        std::string result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
            result.pop_back();
        return result;
#endif
    };

    auto findCompiler = [&runCmd](const std::string& compiler) -> std::string {
#ifdef _WIN32
        std::string result = runCmd("where " + compiler);
#else
        std::string result = runCmd("which " + compiler);
#endif
        size_t nl = result.find('\n');
        if (nl != std::string::npos) result = result.substr(0, nl);
        while (!result.empty() && (result.back() == '\r' || result.back() == ' '))
            result.pop_back();
        return result;
    };

#ifdef _WIN32
    // The editor and the engine library it links (doriax.lib/.dll) are built with a
    // single C++ toolchain. A plugin compiled for a different C++ ABI cannot link
    // against that library, and would crash even if it did: MSVC and MinGW/GNU
    // disagree on name mangling, class layout, RTTI, exception handling and STL
    // types. Flag kits whose ABI does not match this editor's build so they cannot
    // be selected (otherwise the mismatch surfaces only as a cryptic wall of
    // "undefined reference" at link time). This is a Windows-only concern; other
    // platforms have a single system C++ ABI.
    enum class CxxAbi { MSVC, GNU };
#if defined(__MINGW32__)
    const CxxAbi editorAbi = CxxAbi::GNU;
    const char* editorAbiName = "MinGW/GNU";
#else
    const CxxAbi editorAbi = CxxAbi::MSVC;
    const char* editorAbiName = "MSVC";
#endif
    auto enforceAbi = [&](CMakeKit& kit, CxxAbi kitAbi, const char* kitAbiName) {
        if (kitAbi != editorAbi) {
            kit.available = false;
            kit.unavailableReason = std::string("incompatible C++ ABI (kit is ") + kitAbiName
                + ", this editor is " + editorAbiName + "): build plugins with the editor's "
                "toolchain, or rebuild the editor with this compiler";
        }
    };
#endif

    // --- GCC ---
    {
        std::string cxxPath = findCompiler("g++");
        if (!cxxPath.empty()) {
            std::string ccPath = findCompiler("gcc");
            std::string version = runCmd("\"" + cxxPath + "\" -dumpfullversion -dumpversion");
            std::string machine = runCmd("\"" + cxxPath + "\" -dumpmachine");

            CMakeKit kit;
            kit.cxxCompiler = cxxPath;
            kit.cCompiler = ccPath;
            kit.displayName = "GCC " + version;
            if (!machine.empty()) kit.displayName += " " + machine;
#ifdef _WIN32
            kit.generator = "MinGW Makefiles";
            enforceAbi(kit, CxxAbi::GNU, "MinGW/GNU");
#endif
            kits.push_back(kit);
        }
    }

    // --- Clang ---
    {
        std::string cxxPath = findCompiler("clang++");
        if (!cxxPath.empty()) {
            std::string ccPath = findCompiler("clang");
            std::string versionOutput = runCmd("\"" + cxxPath + "\" --version");
            std::string version;
            size_t nl = versionOutput.find('\n');
            std::string firstLine = (nl != std::string::npos) ? versionOutput.substr(0, nl) : versionOutput;
            size_t vpos = firstLine.find("version ");
            if (vpos != std::string::npos) {
                vpos += 8;
                size_t vend = firstLine.find_first_of(" (", vpos);
                version = (vend != std::string::npos) ? firstLine.substr(vpos, vend - vpos) : firstLine.substr(vpos);
            }
            std::string machine = runCmd("\"" + cxxPath + "\" -dumpmachine");

            CMakeKit kit;
            kit.cxxCompiler = cxxPath;
            kit.cCompiler = ccPath;
            kit.displayName = "Clang " + version;
            if (!machine.empty()) kit.displayName += " " + machine;
#ifdef _WIN32
            // Determine generator from the target triple.
            // MinGW-targeting Clang uses MinGW Makefiles;
            // MSVC-targeting Clang requires Ninja: the Visual Studio
            // generator ignores CMAKE_C/CXX_COMPILER (MSBuild always drives
            // cl.exe), so without Ninja this kit has no usable generator.
            if (machine.find("mingw") != std::string::npos) {
                kit.generator = "MinGW Makefiles";
            } else {
                std::string ninjaPath = findCompiler("ninja");
                // Verify ninja actually executes: a broken binary (e.g. a pip
                // wrapper copied out of its Scripts folder, or a blocked
                // download) passes the PATH check but fails when CMake runs it.
                if (!ninjaPath.empty() && !runCmd("\"" + ninjaPath + "\" --version").empty()) {
                    kit.generator = "Ninja";
                } else if (ninjaPath.empty()) {
                    kit.available = false;
                    kit.unavailableReason = "requires Ninja on PATH (https://ninja-build.org)";
                } else {
                    kit.available = false;
                    kit.unavailableReason = "Ninja at '" + ninjaPath + "' failed to run; reinstall from https://ninja-build.org";
                }
            }
            // ABI follows the target triple: MinGW triples are GNU ABI; everything
            // else (e.g. x86_64-pc-windows-msvc) is MSVC ABI and can link an
            // MSVC-built engine. This is why Clang, unlike GCC, can target either.
            {
                CxxAbi clangAbi = (machine.find("mingw") != std::string::npos) ? CxxAbi::GNU : CxxAbi::MSVC;
                enforceAbi(kit, clangAbi, (clangAbi == CxxAbi::GNU) ? "MinGW/GNU" : "MSVC");
            }
#endif
            kits.push_back(kit);
        }
    }

#ifdef _WIN32
    // --- MSVC ---
    {
        std::string vswhere = CommandRunner::findVswherePath();
        std::string vsName;
        if (!vswhere.empty()) {
            vsName = runCmd("\"" + vswhere + "\" -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName");
        }
        // cl.exe on PATH (e.g. a Developer Command Prompt) also signals MSVC is
        // available even if vswhere cannot be located.
        bool clOnPath = !findCompiler("cl").empty();
        if (!vsName.empty() || clOnPath) {
            CMakeKit kit;
            kit.displayName = vsName.empty() ? "MSVC" : vsName;
            // Pin a generator both the selected CMake and an installed VS
            // support, newest first: MSYS2 CMake would default to Ninja/GCC.
            const std::string cmakeHelp = runCmd(cmakeExecutable() + " --help");
            const std::regex vsGenerator("Visual Studio ([0-9]+) [0-9]{4}");
            std::map<int, std::string, std::greater<int>> generators;
            for (std::sregex_iterator it(cmakeHelp.begin(), cmakeHelp.end(), vsGenerator), end; it != end; ++it) {
                generators.emplace(std::stoi((*it)[1].str()), it->str());
            }
            for (const auto& entry : generators) {
                if (!vswhere.empty()) {
                    const std::string versionRange = "[" + std::to_string(entry.first) + "," + std::to_string(entry.first + 1) + ")";
                    const std::string name = runCmd("\"" + vswhere + "\" -products * -latest -version \"" + versionRange + "\" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName");
                    if (name.empty()) continue;
                    kit.displayName = name;
                } else {
                    // A Developer Command Prompt can work without vswhere.
                    const char* version = std::getenv("VisualStudioVersion");
                    if (!version || std::string(version).substr(0, std::string(version).find('.')) != std::to_string(entry.first)) continue;
                }
                kit.generator = entry.second;
                break;
            }
            if (kit.generator.empty()) {
                // Also supports CMake distributions without VS generators.
                // vcvars supplies cl/SDK paths when cl is not already on PATH.
                const std::string ninjaPath = findCompiler("ninja");
                if (!ninjaPath.empty() && !runCmd("\"" + ninjaPath + "\" --version").empty() &&
                    (clOnPath || !CommandRunner::findVcvarsall().empty())) {
                    kit.generator = "Ninja";
                    kit.cCompiler = "cl";
                    kit.cxxCompiler = "cl";
                } else {
                    kit.available = false;
                    kit.unavailableReason = "requires a CMake version supporting the installed Visual Studio, or Ninja and an MSVC toolchain environment";
                }
            }
            enforceAbi(kit, CxxAbi::MSVC, "MSVC");
            kits.push_back(kit);
        }
    }
#endif

    return kits;
}

std::string editor::Generator::checkBuildTools(bool requireEditorCompatibility, CMakeKit* resolvedDefaultKit) {
    std::string missing;

#ifdef _WIN32
    auto commandExists = [](const char* cmd) -> bool {
        // `where` prints the path when found, nothing when not. Use the no-window
        // runner so this probe doesn't flash a cmd.exe window.
        return !CommandRunner::runCaptureNoWindow(std::string("where ") + cmd + " 2>nul").empty();
    };
#else
    auto commandExists = [](const char* cmd) -> bool {
        std::string check = std::string("command -v ") + cmd + " >/dev/null 2>&1";
        return system(check.c_str()) == 0;
    };
#endif

    const CMakeInfo cmakeInfo = detectCMake();
    if (!cmakeInfo.error.empty()) {
        missing += "- CMake: " + cmakeInfo.error + ". Choose it again in Editor Settings > Desktop.\n";
    } else if (!cmakeInfo.found) {
#ifdef _WIN32
        missing += "- CMake: not found. Download from https://cmake.org/download/ and ensure it is added to PATH during installation.\n";
#elif defined(__APPLE__)
        missing += "- CMake: not found. Install with: brew install cmake, or choose an existing install in Editor Settings > Desktop.\n";
#else
        missing += "- CMake: not found. Install with: sudo apt install cmake (Debian/Ubuntu) or sudo dnf install cmake (Fedora), or choose an existing install in Editor Settings > Desktop.\n";
#endif
    }

    bool hasCompiler = false;
#ifdef _WIN32
    if (requireEditorCompatibility) {
        const auto kits = detectAvailableKits();
        const CMakeKit* chosen = chooseDefaultKit(kits);
        hasCompiler = chosen != nullptr;
        if (chosen && resolvedDefaultKit && missing.empty()) {
            *resolvedDefaultKit = *chosen;
            Out::info("Default compiler resolved to: %s", chosen->displayName.c_str());
        }
        if (!hasCompiler) {
            missing += "- C++ toolchain compatible with this editor: not found. Select an available compiler in Editor Settings > Desktop.\n";
            for (const auto& kit : kits) {
                missing += "  " + kit.displayName + ": " + kit.unavailableReason + "\n";
            }
        }
    } else {
        hasCompiler = commandExists("cl") || commandExists("g++") || commandExists("clang++") || CommandRunner::hasVSWithCppTools();
    }
#elif defined(__APPLE__)
    hasCompiler = commandExists("clang++") || commandExists("g++");
#else
    hasCompiler = commandExists("g++") || commandExists("clang++");
#endif

    if (!hasCompiler) {
#ifdef _WIN32
        if (!requireEditorCompatibility) {
            missing += "- C++ compiler: not found. Install Visual Studio (https://visualstudio.microsoft.com/) and select the \"Desktop development with C++\" workload.\n";
        }
#elif defined(__APPLE__)
        missing += "- C++ compiler: not found. Install Xcode Command Line Tools by running: xcode-select --install\n";
#else
        missing += "- C++ compiler (g++ or clang++): not found. Install with: sudo apt install build-essential (Debian/Ubuntu) or sudo dnf install gcc-c++ (Fedora).\n";
#endif
    }

    (void)requireEditorCompatibility;
    (void)resolvedDefaultKit;
    return missing;
}

void editor::Generator::build(const fs::path projectPath, const fs::path projectInternalPath, const fs::path buildPath, const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator, unsigned int parallelJobs) {
    cancelBuild();
    waitForBuildToComplete();

    lastBuildSucceeded.store(false, std::memory_order_relaxed);
    commandRunner.resetCancel();

    buildFuture = std::async(std::launch::async, [this, projectPath, buildPath, cCompiler, cxxCompiler, generator, parallelJobs]() {
        try {
            auto startTime = std::chrono::steady_clock::now();

            // The compiled plugin is loaded into the running editor and shares its
            // engine library, CRT and STL across the shared-library boundary, so it
            // must use the same build configuration. On Windows especially, a Debug
            // plugin (/MDd, _ITERATOR_DEBUG_LEVEL=2) loaded by a Release editor (/MD)
            // has incompatible std::vector/std::string layouts and crashes the moment
            // the editor calls into it. Match the editor's own configuration; _DEBUG
            // is defined iff this editor was built against the debug CRT.
            std::string configType = "Debug";
#if defined(_WIN32) && !defined(_DEBUG)
            configType = "Release";
#endif

            // Resolve a "Default" selection to a concrete ABI-compatible kit once,
            // so configure and the build step (which both need the generator, e.g.
            // for the vcvars prefix) agree on the same toolchain.
            std::string cc = cCompiler, cxx = cxxCompiler, gen = generator;
            if (!resolveDefaultKit(cc, cxx, gen)) {
                return;
            }

            // Resolve automatic mode and the per-machine safety cap once. The
            // exact same value must drive both MSVC /MP and cmake --parallel.
            const unsigned int requestedOrAutomaticJobs = parallelJobs == 0
                ? getAutomaticParallelBuildJobs()
                : parallelJobs;
            const unsigned int effectiveParallelJobs =
                std::min(requestedOrAutomaticJobs, getMaxParallelBuildJobs());

            if (!configureCMake(projectPath, buildPath, configType, cc, cxx, gen, effectiveParallelJobs)) {
                if (commandRunner.isCancelRequested()) {
                    Out::warning("Build configuration cancelled.");
                } else {
                    Out::error("CMake configuration failed");
                }
                lastBuildSucceeded.store(false, std::memory_order_relaxed);
                return;
            }

            if (!buildProject(projectPath, buildPath, configType, gen, effectiveParallelJobs)) {
                if (commandRunner.isCancelRequested()) {
                    Out::warning("Build cancelled.");
                } else {
                    Out::error("Build failed");
                }
                lastBuildSucceeded.store(false, std::memory_order_relaxed);
                return;
            }

            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            double seconds = duration.count() / 1000.0;
            Out::build("Build completed successfully in %.2f seconds.", seconds);
            lastBuildSucceeded.store(true, std::memory_order_relaxed);
        } catch (const std::exception& ex) {
            Out::error("Build exception: %s", ex.what());
            lastBuildSucceeded.store(false, std::memory_order_relaxed);
        }
    });
}

bool editor::Generator::isBuildInProgress() const {
    return buildFuture.valid() &&
           buildFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void editor::Generator::waitForBuildToComplete() {
    if (buildFuture.valid()) {
        buildFuture.wait();
    }
}

bool editor::Generator::didLastBuildSucceed() const {
    return lastBuildSucceeded.load(std::memory_order_relaxed);
}

std::future<void> editor::Generator::cancelBuild() {
    // Launch cancellation on a separate thread so the UI thread is not blocked.
    return std::async(std::launch::async, [this]() {
        // Check if build is in progress inside the async task
        if (!isBuildInProgress()) {
            commandRunner.resetCancel();
            return;
        }

        Out::warning("Cancelling build process...");
        // Attempt to terminate the running process, then wait for the build to complete.
        commandRunner.cancel();
        waitForBuildToComplete();
        commandRunner.resetCancel();
        lastBuildSucceeded.store(false, std::memory_order_relaxed);
        Out::warning("Build process cancelled.");
    });
}
