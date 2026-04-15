// Generator.cpp
#include "Generator.h"
#include "Factory.h"
#include "App.h"
#include "editor/Out.h"
#include "util/FileUtils.h"

#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <thread>
#include <cstring>
#include <cerrno>
#include <map>
#include <unordered_set>

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

namespace {
    constexpr std::chrono::milliseconds kReadSleepMs{10};
    constexpr std::chrono::milliseconds kKillGracePeriod{100};

#ifdef _WIN32
    std::string findVswherePath() {
        auto tryEnv = [](const char* envVar) -> std::string {
            const char* pf = std::getenv(envVar);
            if (!pf) return "";
            fs::path p = fs::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
            if (fs::exists(p)) return p.string();
            return "";
        };
        std::string result = tryEnv("ProgramFiles(x86)");
        if (result.empty()) result = tryEnv("ProgramFiles");
        return result;
    }

    bool hasVSWithCppTools() {
        std::string vswhere = findVswherePath();
        if (vswhere.empty()) return false;
        FILE* pipe = _popen(("\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>nul").c_str(), "r");
        if (!pipe) return false;
        std::string result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        _pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
            result.pop_back();
        }
        return !result.empty();
    }
#endif
}

fs::path editor::Generator::getExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
#else
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}

fs::path editor::Generator::getGeneratedPath(const fs::path& projectInternalPath) {
    return projectInternalPath / "generated";
}

editor::Generator::Generator() :
    lastBuildSucceeded(false),
    cancelRequested(false)
{
#ifdef _WIN32
    currentProcessHandle = NULL;
#else
    currentProcessPid = 0;
#endif
}

editor::Generator::~Generator() {
    // Request cancellation and wait for it to finish before destroying
    try {
        auto f = cancelBuild();
        if (f.valid()) f.wait();
    } catch (...) {}
    waitForBuildToComplete();
}

void editor::Generator::clearStaleCMakeCache(const fs::path& projectPath, const fs::path& buildPath) {
    fs::path cacheFile = buildPath / "CMakeCache.txt";
    if (!fs::exists(cacheFile)) {
        return;
    }

    std::ifstream cache(cacheFile);
    if (!cache.is_open()) {
        return;
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
        return;
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

        fs::remove_all(buildPath, ec);
        fs::create_directories(buildPath, ec);
    }
}

bool editor::Generator::configureCMake(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType, const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator) {
    clearStaleCMakeCache(projectPath, buildPath);

    // Detect kit change: if the compiler/generator selection changed since the
    // last configure, the build tree must be wiped (CMake does not support
    // switching generators or compilers in-place).
    {
        fs::path kitMarker = buildPath / ".doriax_kit";
        fs::path cacheFile = buildPath / "CMakeCache.txt";
        std::string currentKit = generator + "\n" + cCompiler + "\n" + cxxCompiler;
        if (fs::exists(kitMarker)) {
            std::ifstream f(kitMarker);
            std::string prevKit((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            if (prevKit != currentKit) {
                Out::warning("Compiler kit changed. Cleaning build directory...");
                std::error_code ec;
                fs::remove_all(buildPath, ec);
                fs::create_directories(buildPath, ec);
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
                Out::warning("Compiler kit changed. Cleaning build directory...");
                std::error_code ec;
                fs::remove_all(buildPath, ec);
                fs::create_directories(buildPath, ec);
            }
        }
    }

    const fs::path exePath = getExecutableDir();

    std::string cmakeCommand = "cmake ";
    if (!generator.empty()) {
        cmakeCommand += "-G \"" + generator + "\" ";
    }
    if (!cCompiler.empty()) {
        cmakeCommand += "-DCMAKE_C_COMPILER=\"" + cCompiler + "\" ";
    }
    if (!cxxCompiler.empty()) {
        cmakeCommand += "-DCMAKE_CXX_COMPILER=\"" + cxxCompiler + "\" ";
    }
    cmakeCommand += "-DCMAKE_BUILD_TYPE=" + configType + " ";
    // When configuring from inside the editor, ensure the generated project builds
    // in "plugin" mode (no Factory main.cpp/scene sources added).
    cmakeCommand += "-DDORIAX_EDITOR_PLUGIN=ON ";
    cmakeCommand += "\"" + projectPath.string() + "\" ";
    cmakeCommand += "-B \"" + buildPath.string() + "\" ";
    cmakeCommand += "-DDORIAX_LIB_DIR=\"" + exePath.string() + "\"";

    Out::info("Configuring CMake project with command: %s", cmakeCommand.c_str());
    bool result = runCommand(cmakeCommand, projectPath);

    // Record which kit was used so we can detect changes next time.
    if (result) {
        std::error_code ec;
        fs::create_directories(buildPath, ec);
        std::ofstream f(buildPath / ".doriax_kit");
        if (f.is_open()) {
            f << generator << "\n" << cCompiler << "\n" << cxxCompiler;
        }
    }

    return result;
}

bool editor::Generator::buildProject(const fs::path& projectPath, const fs::path& buildPath, const std::string& configType) {
    std::string buildCommand = "cmake --build \"" + buildPath.string() + "\" --config " + configType;
    Out::info("Building project...");
    return runCommand(buildCommand, buildPath);
}

bool editor::Generator::runCommand(const std::string& command, const fs::path& workingDir) {
    cancelRequested.store(false, std::memory_order_relaxed);

    #ifdef _WIN32
        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE hReadPipe = nullptr;
        HANDLE hWritePipe = nullptr;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            Out::error("Failed to create pipe (Win32). Error code: %lu", GetLastError());
            return false;
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(STARTUPINFOA);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError  = hWritePipe;
        si.hStdInput  = nullptr;

        PROCESS_INFORMATION pi{};
        std::string cmdLine = "cmd.exe /c " + command;

        if (!CreateProcessA(
                nullptr,
                cmdLine.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                workingDir.empty() ? nullptr : workingDir.string().c_str(),
                &si,
                &pi))
        {
            DWORD err = GetLastError();
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            Out::error("Failed to create process. Error code: %lu", err);
            return false;
        }

        CloseHandle(hWritePipe);

        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            currentProcessHandle = pi.hProcess;
        }

        constexpr size_t BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        std::string accumulator;

        DWORD exitCode = 1;
        bool finished = false;

        while (!finished) {
            if (cancelRequested.load(std::memory_order_relaxed)) {
                terminateCurrentProcess();
                CloseHandle(pi.hThread);
                {
                    std::lock_guard<std::mutex> lock(processHandleMutex);
                    currentProcessHandle = NULL;
                }
                CloseHandle(pi.hProcess);
                CloseHandle(hReadPipe);  // Close pipe at the end
                return false;
            }

            DWORD bytesAvailable = 0;
            while (PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr) && bytesAvailable > 0) {
                DWORD bytesRead = 0;
                if (!ReadFile(hReadPipe, buffer, static_cast<DWORD>(BUFFER_SIZE - 1), &bytesRead, nullptr) || bytesRead == 0) {
                    break;
                }

                buffer[bytesRead] = '\0';
                accumulator.append(buffer, bytesRead);

                size_t pos = 0;
                size_t nextPos = 0;
                while ((nextPos = accumulator.find('\n', pos)) != std::string::npos) {
                    std::string line = accumulator.substr(pos, nextPos - pos);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        Out::build("%s", line.c_str());
                    }
                    pos = nextPos + 1;
                }
                accumulator.erase(0, pos);
            }

            DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
            if (waitResult == WAIT_OBJECT_0) {
                finished = true;
                GetExitCodeProcess(pi.hProcess, &exitCode);
            } else if (waitResult == WAIT_FAILED) {
                finished = true;
            } else {
                std::this_thread::sleep_for(kReadSleepMs);
            }
        }

        if (!accumulator.empty()) {
            Out::build("%s", accumulator.c_str());
        }

        CloseHandle(hReadPipe);
        CloseHandle(pi.hThread);

        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            currentProcessHandle = NULL;
        }
        CloseHandle(pi.hProcess);

        return exitCode == 0;
    #else
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            Out::error("Failed to create pipe (POSIX): %s", strerror(errno));
            return false;
        }

        // Make read end non-blocking
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);

        pid_t pid = 0;
        std::string workDirStr = workingDir.string();
        std::string cdPrefix;
        if (!workDirStr.empty()) {
            cdPrefix = "cd \"" + workDirStr + "\" && ";
        }
        std::string shellCommand = cdPrefix + command;

        char *argv[] = { const_cast<char*>("/bin/sh"),
                        const_cast<char*>("-c"),
                        const_cast<char*>(shellCommand.c_str()),
                        nullptr };

        int spawnResult = posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[1]);

        if (spawnResult != 0) {
            close(pipefd[0]);
            Out::error("Failed to spawn process (POSIX): %s", strerror(spawnResult));
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            currentProcessPid = pid;
        }

        constexpr size_t BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        bool processExited = false;

        while (!processExited) {
            // Check for cancellation
            if (cancelRequested.load(std::memory_order_relaxed)) {
                terminateCurrentProcess();
                processExited = true;
                break;
            }

            // Try to read with a short timeout
            ssize_t bytesRead = read(pipefd[0], buffer, BUFFER_SIZE - 1);

            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string line(buffer);
                if (!line.empty() && line.back() == '\n') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    Out::build("%s", line.c_str());
                }
            } else if (bytesRead == 0) {
                // EOF - pipe closed
                processExited = true;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, check process status
                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid) {
                    processExited = true;
                } else if (result == -1) {
                    processExited = true;
                }
                // Sleep briefly to avoid busy-waiting
                std::this_thread::sleep_for(kReadSleepMs);
            } else {
                // Read error
                Out::error("Error reading from pipe: %s", strerror(errno));
                processExited = true;
            }
        }

        // Always close the pipe after the loop
        close(pipefd[0]);

        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
            // retry on EINTR
        }

        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            currentProcessPid = 0;
        }

        if (cancelRequested.load(std::memory_order_relaxed)) {
            return false;
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0;
        }
        if (WIFSIGNALED(status)) {
            Out::warning("Process terminated by signal %d", WTERMSIG(status));
        }
        return false;
    #endif
}

std::string editor::Generator::getPlatformCMakeConfig() {
    std::string content;
    content += "if (NOT DORIAX_EDITOR_PLUGIN)\n";
    content += "    add_definitions(\"-DDEFAULT_WINDOW_WIDTH=960\")\n";
    content += "    add_definitions(\"-DDEFAULT_WINDOW_HEIGHT=540\")\n";
    content += "\n";
    content += "    set(COMPILE_ZLIB OFF)\n";
    content += "    set(IS_ARM OFF)\n";
    content += "\n";
    content += "    add_definitions(\"-DSOKOL_GLCORE\")\n";
    content += "    add_definitions(\"-DWITH_MINIAUDIO\") # For SoLoud\n";
    content += "\n";
    content += "    list(APPEND PLATFORM_SOURCE\n";
    content += "        ${INTERNAL_DIR}/generated/PlatformEditor.cpp\n";
    content += "        ${INTERNAL_DIR}/generated/main.cpp\n";
    content += "    )\n";
    content += "\n";
    content += "    if(WIN32)\n";
    content += "        list(APPEND PLATFORM_LIBS opengl32 gdi32 user32 shell32 glfw)\n";
    content += "    elseif(APPLE)\n";
    content += "        list(APPEND PLATFORM_LIBS glfw \"-framework OpenGL\" \"-framework Cocoa\" \"-framework IOKit\" \"-framework CoreVideo\" \"-framework CoreFoundation\")\n";
    content += "    else()\n";
    content += "        list(APPEND PLATFORM_LIBS GL dl m glfw)\n";
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
        sourceContent += "            if (scriptEntry.type == ScriptType::SCRIPT_LUA) \n";
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
        sourceContent += "            if (scriptEntry.type == ScriptType::SCRIPT_LUA) \n";
        sourceContent += "                continue; \n";
        sourceContent += "\n";

        for (const auto& s : scriptFiles) {
            sourceContent += "            if (scriptEntry.className == \"" + s.className + "\") {\n";

            sourceContent += "                " + s.className + "* typedScript = static_cast<" + s.className + "*>(scriptEntry.instance);\n";
            sourceContent += "\n";
            sourceContent += "                for (auto& prop : scriptEntry.properties) {\n";

            for (const auto& prop : s.properties) {
                sourceContent += "\n";
                sourceContent += "                    if (prop.name == \"" + prop.name + "\") {\n";

                if (prop.isPtr && !prop.ptrTypeName.empty()) {
                    sourceContent += "                        const auto& entRef = std::get<doriax::EntityReference>(prop.value);\n";
                    sourceContent += "                        doriax::Entity targetEntity = entRef.entity;\n";
                    sourceContent += "                        void* instancePtr = nullptr;\n";
                    sourceContent += "\n";
                    sourceContent += "                        if (targetEntity != NULL_ENTITY) {\n";
                    sourceContent += "                            doriax::Scene* targetScene = scene;\n";
                    sourceContent += "                            if (entRef.sceneId != 0) {\n";
                    sourceContent += "                                targetScene = SceneManager::getScenePtr(entRef.sceneId);\n";
                    sourceContent += "                            }\n";
                    sourceContent += "                            if (targetScene) {\n";
                    sourceContent += "                                doriax::ScriptComponent* targetScriptComp = targetScene->findComponent<doriax::ScriptComponent>(targetEntity);\n";
                    sourceContent += "                                if (targetScriptComp) {\n";
                    sourceContent += "                                    for (auto& targetScript : targetScriptComp->scripts) {\n";
                    sourceContent += "                                        if (targetScript.type != ScriptType::SCRIPT_LUA) {\n";
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
        sourceContent += "            if (scriptEntry.type == ScriptType::SCRIPT_LUA) continue;\n";
        sourceContent += "\n";
        sourceContent += "            if (scriptEntry.instance) {\n";
        for (const auto& s : scriptFiles) {
            sourceContent += "                if (scriptEntry.className == \"" + s.className + "\") {\n";
            sourceContent += "                    std::string addr = \"_\" + std::to_string(reinterpret_cast<std::uintptr_t>(scriptEntry.instance)) + \"_\";\n";
            sourceContent += "                    Engine::removeSubscriptionsByTag(addr);\n";
            sourceContent += "                    delete static_cast<" + s.className + "*>(scriptEntry.instance);\n";
            sourceContent += "                }\n";
        }
        sourceContent += "                scriptEntry.instance = nullptr;\n";
        sourceContent += "            }\n";
        sourceContent += "        }\n";
        sourceContent += "    }\n";
    }

    sourceContent += "}\n";

    return sourceContent;
}

void editor::Generator::writeSourceFiles(const fs::path& projectPath, const fs::path& projectInternalPath, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<editor::SceneBuildInfo>& scenes, const std::vector<editor::BundleSceneInfo>& bundles) {
    const fs::path exePath = getExecutableDir();

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

    // Build SCRIPT_SOURCES list for CMake
    std::unordered_set<std::string> scriptIncludeDirs;
    std::string scriptSources = "set(SCRIPT_SOURCES\n";
    for (const auto& s : scriptFiles) {
        if (s.path.is_relative()) {
            scriptSources += "    ${CMAKE_CURRENT_SOURCE_DIR}/" + s.path.generic_string() + "\n";
        } else {
            scriptSources += "    " + s.path.generic_string() + "\n";
        }

        if (!s.headerPath.empty()) {
            if (s.headerPath.is_relative()) {
                scriptIncludeDirs.insert("    ${CMAKE_CURRENT_SOURCE_DIR}\n");
            } else {
                scriptIncludeDirs.insert("    " + s.headerPath.parent_path().generic_string() + "\n");
            }
        }
    }
    scriptSources += ")\n";

    std::string scriptDirs;
    for (const auto& includeDir : scriptIncludeDirs) {
        scriptDirs += includeDir;
    }

    std::string cmakeContent;
    cmakeContent += "# This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    cmakeContent += "cmake_minimum_required(VERSION 3.15)\n";
    cmakeContent += "project(ProjectLib)\n\n";
    cmakeContent += "set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})\n";
    cmakeContent += "set(INTERNAL_DIR ${PROJECT_ROOT}/.doriax)\n\n";

    cmakeContent += "# Specify C++ standard\n";
    cmakeContent += "set(CMAKE_CXX_STANDARD 17)\n";
    cmakeContent += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    cmakeContent += "# Build mode: when ON, build as Doriax Editor plugin (shared library)\n";
    cmakeContent += "option(DORIAX_EDITOR_PLUGIN \"Build as Doriax Editor plugin\" OFF)\n";
    cmakeContent += "if(DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "    add_compile_definitions(DORIAX_EDITOR_PLUGIN)\n";
    cmakeContent += "endif()\n\n";

    cmakeContent += getPlatformCMakeConfig() + "\n";

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
    cmakeContent += scriptDirs + "\n";
    cmakeContent += "    " + engineApiPathStr + "\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/sokol\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/box2d/include\n";
    cmakeContent += "    " + engineApiPathStr + "/libs/joltphysics\n";
    cmakeContent += "    " + engineApiPathStr + "/renders\n";
    cmakeContent += "    " + engineApiPathStr + "/core\n";
    cmakeContent += "    " + engineApiPathStr + "/core/action\n";
    cmakeContent += "    " + engineApiPathStr + "/core/buffer\n";
    cmakeContent += "    " + engineApiPathStr + "/core/component\n";
    cmakeContent += "    " + engineApiPathStr + "/core/ecs\n";
    cmakeContent += "    " + engineApiPathStr + "/core/io\n";
    cmakeContent += "    " + engineApiPathStr + "/core/manager\n";
    cmakeContent += "    " + engineApiPathStr + "/core/math\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object\n";
    cmakeContent += "    " + engineApiPathStr + "/core/object/audio\n";
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

    cmakeContent += "# Default DORIAX_LIB_DIR if not provided\n";
    cmakeContent += "if(NOT DEFINED DORIAX_LIB_DIR OR DORIAX_LIB_DIR STREQUAL \"\")\n";
    cmakeContent += "    set(DORIAX_LIB_DIR \"" + exePath.generic_string() + "\")\n";
    cmakeContent += "    # Must be the same as the editor's library to not ODR violation / ABI mismatch\n";
    cmakeContent += "    add_compile_definitions(DORIAX_EDITOR)\n";
    cmakeContent += "endif()\n\n";

    cmakeContent += "# Find doriax library in specified location\n";
    cmakeContent += "find_library(DORIAX_LIB doriax PATHS ${DORIAX_LIB_DIR} NO_DEFAULT_PATH)\n";
    cmakeContent += "if(NOT DORIAX_LIB)\n";
    cmakeContent += "    message(FATAL_ERROR \"Doriax library not found in ${DORIAX_LIB_DIR}\")\n";
    cmakeContent += "endif()\n\n";
    cmakeContent += "target_link_libraries(" + libName + " PRIVATE ${DORIAX_LIB} ${PLATFORM_LIBS})\n\n";
    cmakeContent += "# Set compile options based on compiler and platform\n";
    cmakeContent += "if(MSVC)\n";
    cmakeContent += "    target_compile_options(" + libName + " PRIVATE /W4 /EHsc)\n";
    cmakeContent += "else()\n";
    cmakeContent += "    target_compile_options(" + libName + " PRIVATE -Wall -Wextra -fPIC)\n";
    cmakeContent += "    target_link_options(" + libName + " PRIVATE -Wl,-z,defs,--no-undefined)\n"; // no undefined symbols
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
    cmakeContent += ")\n";

    // Build C++ source content
    std::string sourceContent;
    sourceContent += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
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

    FileUtils::writeIfChanged(cmakeFile, cmakeContent);
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
}

void editor::Generator::terminateCurrentProcess() {
    #ifdef _WIN32
        HANDLE handleSnapshot = nullptr;
        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            handleSnapshot = currentProcessHandle;
        }
        if (handleSnapshot) {
            TerminateProcess(handleSnapshot, 1);
        }
    #else
        pid_t pidSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            pidSnapshot = currentProcessPid;
        }
        if (pidSnapshot > 0) {
            if (kill(pidSnapshot, SIGTERM) != 0 && errno != ESRCH) {
                Out::warning("Failed to send SIGTERM to pid %d: %s", pidSnapshot, strerror(errno));
            }

            std::this_thread::sleep_for(kKillGracePeriod);
            if (kill(pidSnapshot, 0) == 0) {
                kill(pidSnapshot, SIGKILL);
            }
        }
    #endif
}

std::vector<editor::BundleInstanceInfo> editor::Generator::writeBundleSources(const std::map<fs::path, EntityBundle>& entityBundles, uint32_t sceneId, const fs::path& projectPath, const fs::path& projectInternalPath) {
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);

    std::vector<BundleInstanceInfo> bundleInstances;

    for (const auto& [bundlePath, bundle] : entityBundles) {
        auto sceneIt = bundle.instances.find(sceneId);
        if (sceneIt == bundle.instances.end()) continue;

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

void editor::Generator::configure(const std::vector<editor::SceneBuildInfo>& scenes, std::string libName, const std::vector<SceneScriptSource>& scriptFiles, const std::vector<editor::BundleSceneInfo>& bundles, const fs::path& projectPath, const fs::path& projectInternalPath, Scaling scalingMode, TextureStrategy textureStrategy, unsigned int canvasWidth, unsigned int canvasHeight){
    const fs::path generatedPath = getGeneratedPath(projectInternalPath);

    // Build main.cpp content
    std::string mainContent;
    mainContent += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    mainContent += "#include \"Doriax.h\"\n";
    mainContent += "#include \"PlatformEditor.h\"\n";

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
            mainContent += "    if (!" + sceneName + "){\n";
            mainContent += "        " + sceneName + " = new Scene();\n";
            mainContent += "        create" + sceneName + "(" + sceneName + ");\n";
            mainContent += "        SceneManager::setScenePtr(" + std::to_string(sceneId) + ", " + sceneName + ");\n";
            mainContent += "    }\n";
        }
        mainContent += "\n";
        for (const auto sceneId : sceneData.involvedScenes) {
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

    mainContent += "int main(int argc, char* argv[]) {\n";
    mainContent += "    return PlatformEditor::init(argc, argv);\n";
    mainContent += "}\n\n";

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
        mainContent += "    SceneManager::registerScene(" + std::to_string(sceneData.id) + ", \"" + sceneData.name + "\", load_" + stackId + ");\n";
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

    const fs::path platformHeaderFile = generatedPath / "PlatformEditor.h";
    FileUtils::writeIfChanged(platformHeaderFile, getPlatformEditorHeader());

    const fs::path platformSourceFile = generatedPath / "PlatformEditor.cpp";
    FileUtils::writeIfChanged(platformSourceFile, getPlatformEditorSource(projectPath));

    writeSourceFiles(projectPath, projectInternalPath, libName, scriptFiles, scenes, bundles);
}

std::string editor::Generator::getPlatformEditorHeader() {
    std::string content;
    content += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    content += "#pragma once\n\n";
    content += "#define GLFW_INCLUDE_NONE\n";
    content += "#include \"GLFW/glfw3.h\"\n\n";
    content += "#include \"System.h\"\n\n";
    content += "class PlatformEditor: public doriax::System{\n\n";
    content += "private:\n\n";
    content += "    static int windowPosX;\n";
    content += "    static int windowPosY;\n";
    content += "    static int windowWidth;\n";
    content += "    static int windowHeight;\n\n";
    content += "    static int screenWidth;\n";
    content += "    static int screenHeight;\n\n";
    content += "    static double mousePosX;\n";
    content += "    static double mousePosY;\n\n";
    content += "    static int sampleCount;\n\n";
    content += "    static GLFWwindow* window;\n";
    content += "    static GLFWmonitor* monitor;\n\n";
    content += "public:\n\n";
    content += "    PlatformEditor();\n\n";
    content += "    static int init(int argc, char **argv);\n\n";
    content += "    virtual int getScreenWidth();\n";
    content += "    virtual int getScreenHeight();\n\n";
    content += "    virtual int getSampleCount();\n\n";
    content += "    virtual bool isFullscreen();\n";
    content += "    virtual void requestFullscreen();\n";
    content += "    virtual void exitFullscreen();\n\n";
    content += "    virtual void setMouseCursor(doriax::CursorType type);\n";
    content += "    virtual void setShowCursor(bool showCursor);\n\n";
    content += "    virtual std::string getAssetPath();\n";
    content += "    virtual std::string getUserDataPath();\n";
    content += "    virtual std::string getLuaPath();\n";
    content += "    virtual std::string getShaderPath();\n\n";
    content += "};\n";
    return content;
}

std::string editor::Generator::getPlatformEditorSource(const fs::path& projectPath) {
    std::string content;
    content += "// This file is auto-generated by Doriax Editor. Do not edit manually.\n\n";
    content += "#include \"PlatformEditor.h\"\n\n";
    content += "#include \"Engine.h\"\n\n";
    content += "int PlatformEditor::windowPosX;\n";
    content += "int PlatformEditor::windowPosY;\n";
    content += "int PlatformEditor::windowWidth;\n";
    content += "int PlatformEditor::windowHeight;\n\n";
    content += "int PlatformEditor::screenWidth;\n";
    content += "int PlatformEditor::screenHeight;\n\n";
    content += "double PlatformEditor::mousePosX;\n";
    content += "double PlatformEditor::mousePosY;\n\n";
    content += "int PlatformEditor::sampleCount;\n\n";
    content += "GLFWwindow* PlatformEditor::window;\n";
    content += "GLFWmonitor* PlatformEditor::monitor;\n\n\n";
    content += "PlatformEditor::PlatformEditor(){\n\n";
    content += "}\n\n";
    content += "int PlatformEditor::init(int argc, char **argv){\n";
    content += "    windowWidth = DEFAULT_WINDOW_WIDTH;\n";
    content += "    windowHeight = DEFAULT_WINDOW_HEIGHT;\n\n";
    content += "    sampleCount = 1;\n\n";
    content += "    doriax::Engine::systemInit(argc, argv, new PlatformEditor());\n\n";
    content += "    /* create window and GL context via GLFW */\n";
    content += "    glfwInit();\n";
    content += "    glfwWindowHint(GLFW_SAMPLES, (sampleCount == 1) ? 0 : sampleCount);\n";
    content += "    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n";
    content += "    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);\n";
    content += "    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);\n";
    content += "    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);\n";
    content += "    window = glfwCreateWindow(windowWidth, windowHeight, \"Doriax\", 0, 0);\n\n";
    content += "    glfwMakeContextCurrent(window);\n";
    content += "    glfwSwapInterval(1);\n\n";
    content += "    monitor = glfwGetPrimaryMonitor();\n\n";
    content += "    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int btn, int action, int mods) {\n";
    content += "        if (action==GLFW_PRESS){\n";
    content += "            doriax::Engine::systemMouseDown(btn, float(mousePosX), float(mousePosY), mods);\n";
    content += "        }else if (action==GLFW_RELEASE){\n";
    content += "            doriax::Engine::systemMouseUp(btn, float(mousePosX), float(mousePosY), mods);\n";
    content += "        }\n";
    content += "    });\n";
    content += "    glfwSetCursorPosCallback(window, [](GLFWwindow*, double pos_x, double pos_y) {\n";
    content += "        float xscale, yscale;\n";
    content += "        glfwGetWindowContentScale(window, &xscale, &yscale);\n\n";
    content += "        mousePosX = pos_x * xscale;\n";
    content += "        mousePosY = pos_y * yscale;\n";
    content += "        doriax::Engine::systemMouseMove(float(mousePosX), float(mousePosY), 0);\n";
    content += "    });\n";
    content += "    glfwSetScrollCallback(window, [](GLFWwindow*, double xoffset, double yoffset){\n";
    content += "        doriax::Engine::systemMouseScroll((float)xoffset, (float)yoffset, 0);\n";
    content += "    });\n";
    content += "    glfwSetKeyCallback(window, [](GLFWwindow*, int key, int /*scancode*/, int action, int mods){\n";
    content += "        if (action==GLFW_PRESS){\n";
    content += "            if (key == GLFW_KEY_TAB)\n";
    content += "                doriax::Engine::systemCharInput('\\t');\n";
    content += "            if (key == GLFW_KEY_BACKSPACE)\n";
    content += "                doriax::Engine::systemCharInput('\\b');\n";
    content += "            if (key == GLFW_KEY_ENTER)\n";
    content += "                doriax::Engine::systemCharInput('\\r');\n";
    content += "            if (key == GLFW_KEY_ESCAPE)\n";
    content += "                doriax::Engine::systemCharInput('\\e');\n";
    content += "            doriax::Engine::systemKeyDown(key, false, mods);\n";
    content += "        }else if (action==GLFW_REPEAT){\n";
    content += "            doriax::Engine::systemKeyDown(key, true, mods);\n";
    content += "        }else if (action==GLFW_RELEASE){\n";
    content += "            doriax::Engine::systemKeyUp(key, false, mods);\n";
    content += "        }\n";
    content += "    });\n";
    content += "    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint){\n";
    content += "        doriax::Engine::systemCharInput(codepoint);\n";
    content += "    });\n\n";
    content += "    int cur_width, cur_height;\n";
    content += "    glfwGetFramebufferSize(window, &cur_width, &cur_height);\n\n";
    content += "    PlatformEditor::screenWidth = cur_width;\n";
    content += "    PlatformEditor::screenHeight = cur_height;\n\n";
    content += "    doriax::Engine::systemViewLoaded();\n";
    content += "    doriax::Engine::systemViewChanged();\n\n";
    content += "    /* draw loop */\n";
    content += "    while (!glfwWindowShouldClose(window)) {\n";
    content += "        int cur_width, cur_height;\n";
    content += "        glfwGetFramebufferSize(window, &cur_width, &cur_height);\n\n";
    content += "        if (cur_width != PlatformEditor::screenWidth || cur_height != PlatformEditor::screenHeight){\n";
    content += "            PlatformEditor::screenWidth = cur_width;\n";
    content += "            PlatformEditor::screenHeight = cur_height;\n";
    content += "            doriax::Engine::systemViewChanged();\n";
    content += "        }\n\n";
    content += "        doriax::Engine::systemDraw();\n\n";
    content += "        glfwSwapBuffers(window);\n";
    content += "        glfwPollEvents();\n";
    content += "    }\n\n";
    content += "    doriax::Engine::systemViewDestroyed();\n";
    content += "    doriax::Engine::systemShutdown();\n";
    content += "    glfwTerminate();\n";
    content += "    return 0;\n";
    content += "}\n\n";
    content += "int PlatformEditor::getScreenWidth(){\n";
    content += "    return PlatformEditor::screenWidth;\n";
    content += "}\n\n";
    content += "int PlatformEditor::getScreenHeight(){\n";
    content += "    return PlatformEditor::screenHeight;\n";
    content += "}\n\n";
    content += "int PlatformEditor::getSampleCount(){\n";
    content += "    return PlatformEditor::sampleCount;\n";
    content += "}\n\n";
    content += "bool PlatformEditor::isFullscreen(){\n";
    content += "    return glfwGetWindowMonitor(window) != nullptr;\n";
    content += "}\n\n";
    content += "void PlatformEditor::requestFullscreen(){\n";
    content += "    if (isFullscreen())\n";
    content += "        return;\n\n";
    content += "    // backup window position and window size\n";
    content += "    glfwGetWindowPos(window, &windowPosX, &windowPosY);\n";
    content += "    glfwGetWindowSize(window, &windowWidth, &windowHeight);\n\n";
    content += "    // get resolution of monitor\n";
    content += "    const GLFWvidmode * mode = glfwGetVideoMode(monitor);\n\n";
    content += "    // switch to full screen\n";
    content += "    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, 0 );\n";
    content += "}\n\n";
    content += "void PlatformEditor::exitFullscreen(){\n";
    content += "    if (!isFullscreen())\n";
    content += "        return;\n\n";
    content += "    // restore last window size and position\n";
    content += "    glfwSetWindowMonitor(window, nullptr,  windowPosX, windowPosY, windowWidth, windowHeight, 0);\n";
    content += "}\n\n";
    content += "void PlatformEditor::setMouseCursor(doriax::CursorType type){\n";
    content += "    GLFWcursor* cursor = NULL;\n\n";
    content += "    if (type == doriax::CursorType::ARROW){\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);\n";
    content += "    }else if (type == doriax::CursorType::IBEAM){\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);\n";
    content += "    }else if (type == doriax::CursorType::CROSSHAIR){\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);\n";
    content += "    }else if (type == doriax::CursorType::POINTING_HAND){\n";
    content += "        #ifdef GLFW_POINTING_HAND_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::RESIZE_EW){\n";
    content += "        #ifdef GLFW_RESIZE_EW_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::RESIZE_NS){\n";
    content += "        #ifdef GLFW_RESIZE_NS_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::RESIZE_NWSE){\n";
    content += "        #ifdef GLFW_RESIZE_NWSE_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::RESIZE_NESW){\n";
    content += "        #ifdef GLFW_RESIZE_NESW_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::RESIZE_ALL){\n";
    content += "        #ifdef GLFW_RESIZE_ALL_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);\n";
    content += "        #endif\n";
    content += "    }else if (type == doriax::CursorType::NOT_ALLOWED){\n";
    content += "        #ifdef GLFW_NOT_ALLOWED_CURSOR\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);\n";
    content += "        #else\n";
    content += "        cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);\n";
    content += "        #endif\n";
    content += "    }\n\n";
    content += "    if (cursor) {\n";
    content += "        glfwSetCursor(window, cursor);\n";
    content += "    } else {\n";
    content += "        // Handle error: cursor creation failed\n";
    content += "    }\n";
    content += "}\n\n";
    content += "void PlatformEditor::setShowCursor(bool showCursor){\n";
    content += "    if (showCursor){\n";
    content += "        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);\n";
    content += "    }else{\n";
    content += "        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);\n";
    content += "    }\n";
    content += "}\n\n";
    content += "std::string PlatformEditor::getAssetPath(){\n";
    content += "    return \"" + projectPath.generic_string() + "\";\n";
    content += "}\n\n";
    content += "std::string PlatformEditor::getUserDataPath(){\n";
    content += "    return \".\";\n";
    content += "}\n\n";
    content += "std::string PlatformEditor::getLuaPath(){\n";
    content += "    return \"" + projectPath.generic_string() + "\";\n";
    content += "}\n\n";
    content += "std::string PlatformEditor::getShaderPath(){\n";
    content += "    return \"" + App::getUserShaderCacheDir().string() + "\";\n";
    content += "}\n";
    return content;
}

std::vector<editor::CMakeKit> editor::Generator::detectAvailableKits() {
    std::vector<CMakeKit> kits;

    auto runCmd = [](const std::string& cmd) -> std::string {
#ifdef _WIN32
        FILE* pipe = _popen((cmd + " 2>nul").c_str(), "r");
#else
        FILE* pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
#endif
        if (!pipe) return "";
        std::string result;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
            result.pop_back();
        return result;
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
            // MSVC-targeting Clang works best with Ninja (or VS generator as fallback).
            if (machine.find("mingw") != std::string::npos) {
                kit.generator = "MinGW Makefiles";
            } else {
                if (!findCompiler("ninja").empty()) {
                    kit.generator = "Ninja";
                }
                // Otherwise leave empty; CMake will pick the VS generator.
            }
#endif
            kits.push_back(kit);
        }
    }

#ifdef _WIN32
    // --- MSVC ---
    {
        std::string clPath = findCompiler("cl");
        if (!clPath.empty()) {
            // cl.exe is on PATH (e.g. launched from Developer Command Prompt)
            CMakeKit kit;
            kit.cxxCompiler = clPath;
            kit.cCompiler = clPath;
            kit.displayName = "MSVC";
            kits.push_back(kit);
        } else {
            // cl.exe not on PATH; detect Visual Studio via vswhere
            std::string vswhere = findVswherePath();
            if (!vswhere.empty()) {
                std::string vsName = runCmd("\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName");
                if (!vsName.empty()) {
                    CMakeKit kit;
                    kit.displayName = vsName;
                    // Empty compiler/generator: CMake auto-detects the VS
                    // generator and locates cl.exe internally.
                    kits.push_back(kit);
                }
            }
        }
    }
#endif

    return kits;
}

std::string editor::Generator::checkBuildTools() {
    std::string missing;

#ifdef _WIN32
    auto commandExists = [](const char* cmd) -> bool {
        std::string check = std::string("where ") + cmd + " >nul 2>nul";
        return system(check.c_str()) == 0;
    };
#else
    auto commandExists = [](const char* cmd) -> bool {
        std::string check = std::string("command -v ") + cmd + " >/dev/null 2>&1";
        return system(check.c_str()) == 0;
    };
#endif

    if (!commandExists("cmake")) {
#ifdef _WIN32
        missing += "- CMake: not found. Download from https://cmake.org/download/ and ensure it is added to PATH during installation.\n";
#elif defined(__APPLE__)
        missing += "- CMake: not found. Install with: brew install cmake\n";
#else
        missing += "- CMake: not found. Install with: sudo apt install cmake (Debian/Ubuntu) or sudo dnf install cmake (Fedora).\n";
#endif
    }

    bool hasCompiler = false;
#ifdef _WIN32
    hasCompiler = commandExists("cl") || commandExists("g++") || commandExists("clang++") || hasVSWithCppTools();
#elif defined(__APPLE__)
    hasCompiler = commandExists("clang++") || commandExists("g++");
#else
    hasCompiler = commandExists("g++") || commandExists("clang++");
#endif

    if (!hasCompiler) {
#ifdef _WIN32
        missing += "- C++ compiler: not found. Install Visual Studio (https://visualstudio.microsoft.com/) and select the \"Desktop development with C++\" workload.\n";
#elif defined(__APPLE__)
        missing += "- C++ compiler: not found. Install Xcode Command Line Tools by running: xcode-select --install\n";
#else
        missing += "- C++ compiler (g++ or clang++): not found. Install with: sudo apt install build-essential (Debian/Ubuntu) or sudo dnf install gcc-c++ (Fedora).\n";
#endif
    }

    return missing;
}

void editor::Generator::build(const fs::path projectPath, const fs::path projectInternalPath, const fs::path buildPath, const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator) {
    cancelBuild();
    waitForBuildToComplete();

    lastBuildSucceeded.store(false, std::memory_order_relaxed);
    cancelRequested.store(false, std::memory_order_relaxed);

    buildFuture = std::async(std::launch::async, [this, projectPath, buildPath, cCompiler, cxxCompiler, generator]() {
        try {
            auto startTime = std::chrono::steady_clock::now();

            std::string configType = "Debug";

            if (!configureCMake(projectPath, buildPath, configType, cCompiler, cxxCompiler, generator)) {
                if (cancelRequested.load(std::memory_order_relaxed)) {
                    Out::warning("Build configuration cancelled.");
                } else {
                    Out::error("CMake configuration failed");
                }
                lastBuildSucceeded.store(false, std::memory_order_relaxed);
                return;
            }

            if (!buildProject(projectPath, buildPath, configType)) {
                if (cancelRequested.load(std::memory_order_relaxed)) {
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
            cancelRequested.store(false, std::memory_order_relaxed);
            return;
        }

        Out::warning("Cancelling build process...");
        cancelRequested.store(true, std::memory_order_relaxed);

        // Attempt to terminate the running process, then wait for the build to complete.
        terminateCurrentProcess();
        waitForBuildToComplete();
        cancelRequested.store(false, std::memory_order_relaxed);
        lastBuildSucceeded.store(false, std::memory_order_relaxed);
        Out::warning("Build process cancelled.");
    });
}