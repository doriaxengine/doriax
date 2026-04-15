#include "Exporter.h"
#include "App.h"
#include "Backend.h"
#include "Out.h"
#include "Stream.h"
#include "util/FileUtils.h"
#include "pool/ShaderPool.h"

#include <fstream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

using namespace doriax;

editor::Exporter::Exporter() {
}

editor::Exporter::~Exporter() {
    cancelRequested.store(true);
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

void editor::Exporter::setProgress(const std::string& step, float value) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentStep = step;
    progress.overallProgress = value;
}

void editor::Exporter::setError(const std::string& message) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.failed = true;
    progress.errorMessage = message;
    Out::error("Export failed: %s", message.c_str());
}

editor::ExportProgress editor::Exporter::getProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress;
}

bool editor::Exporter::isRunning() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.started && !progress.finished && !progress.failed;
}

void editor::Exporter::cancelExport() {
    cancelRequested.store(true);
}

bool editor::Exporter::isCancelled() const {
    return cancelRequested.load();
}

void editor::Exporter::startExport(Project* proj, const ExportConfig& cfg) {
    if (exportThread.joinable()) {
        exportThread.join();
    }

    this->project = proj;
    this->config = cfg;
    cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        this->progress = ExportProgress();
        this->progress.started = true;
    }

    // Launch export process in a separate thread so UI does not block
    exportThread = std::thread(&Exporter::runExport, this);
}

void editor::Exporter::runExport() {
    if (!checkTargetDir()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!clearGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!loadAndSaveAllScenes()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyAssets()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyLua()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyCppScripts()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyEngine()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!buildAndSaveShaders()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!generateCMakeLists()) return;

    setProgress("Export complete", 1.0f);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress.finished = true;
    }
    Out::info("Project exported successfully to: %s", config.targetDir.string().c_str());
}

fs::path editor::Exporter::getExportProjectRoot() const {
    return config.targetDir / "project";
}

bool editor::Exporter::checkTargetDir() {
    setProgress("Checking target directory...", 0.0f);

    if (config.targetDir.empty()) {
        setError("Target directory not specified");
        return false;
    }

    std::error_code ec;
    if (!fs::exists(config.targetDir, ec)) {
        fs::create_directories(config.targetDir, ec);
        if (ec) {
            setError("Failed to create target directory: " + ec.message());
            return false;
        }
    }

    if (!fs::is_empty(config.targetDir, ec)) {
        setError("Target directory is not empty");
        return false;
    }

    return true;
}

bool editor::Exporter::clearGenerated() {
    setProgress("Clearing generated directory...", 0.05f);

    fs::path generatedDir = project->getProjectInternalPath() / "generated";

    std::error_code ec;
    if (fs::exists(generatedDir, ec)) {
        fs::remove_all(generatedDir, ec);
        if (ec) {
            setError("Failed to clear generated directory: " + ec.message());
            return false;
        }
    }
    fs::create_directories(generatedDir, ec);
    if (ec) {
        setError("Failed to recreate generated directory: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::loadAndSaveAllScenes() {
    setProgress("Saving scene sources...", 0.1f);

    std::promise<bool> savePromise;
    auto saveFuture = savePromise.get_future();

    Backend::getApp().enqueueMainThreadTask([this, &savePromise]() {
        try {
            std::vector<uint32_t> temporarilyLoaded;
            auto& scenes = project->getScenes();

            // Load all unloaded scenes (opened=false to avoid UI side-effects)
            for (size_t i = 0; i < scenes.size(); i++) {
                auto& sceneProject = scenes[i];
                if (sceneProject.filepath.empty() || sceneProject.scene) {
                    continue;
                }
                project->loadScene(sceneProject.filepath, false, false, true);

                temporarilyLoaded.push_back(sceneProject.id);
            }

            // Save all scenes to regenerate their .cpp sources
            for (size_t i = 0; i < scenes.size(); i++) {
                auto& sceneProject = scenes[i];
                if (sceneProject.filepath.empty() || !sceneProject.scene) {
                    continue;
                }
                project->saveSceneToPath(sceneProject.id, sceneProject.filepath);
            }

            // Unload all scenes that were not loaded before export
            for (uint32_t sceneId : temporarilyLoaded) {
                SceneProject* sp = project->getScene(sceneId);
                if (sp) {
                    project->deleteSceneProject(sp);
                }
            }

            savePromise.set_value(true);
        } catch (...) {
            savePromise.set_exception(std::current_exception());
        }
    });

    try {
        saveFuture.get();
    } catch (const std::exception& e) {
        setError(std::string("Scene save failed: ") + e.what());
        return false;
    }

    return true;
}

bool editor::Exporter::copyGenerated() {
    setProgress("Copying generated files...", 0.2f);

    fs::path generatedSrc = project->getProjectInternalPath() / "generated";
    fs::path generatedDst = getExportProjectRoot();

    std::error_code ec;
    fs::create_directories(generatedDst, ec);

    // Exclude editor-specific platform files; main.cpp is handled separately below
    static const std::set<std::string> excludedFiles = {
        "main.cpp", "PlatformEditor.h", "PlatformEditor.cpp"
    };

    if (fs::exists(generatedSrc, ec)) {
        for (auto& entry : fs::recursive_directory_iterator(generatedSrc, fs::directory_options::skip_permission_denied, ec)) {
            fs::path relativePath = fs::relative(entry.path(), generatedSrc, ec);

            if (entry.is_regular_file() && excludedFiles.count(relativePath.filename().string())) {
                continue;
            }

            fs::path destPath = generatedDst / relativePath;
            if (entry.is_directory()) {
                fs::create_directories(destPath, ec);
            } else if (entry.is_regular_file()) {
                fs::create_directories(destPath.parent_path(), ec);
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // Process main.cpp: copy from Generator output but strip editor-specific parts
    fs::path mainSrc = generatedSrc / "main.cpp";
    if (fs::exists(mainSrc, ec)) {
        std::ifstream ifs(mainSrc, std::ios::in | std::ios::binary);
        if (!ifs) {
            setError("Failed to read generated main.cpp");
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        // Remove #include "PlatformEditor.h"
        std::string platformInclude = "#include \"PlatformEditor.h\"\n";
        size_t pos = content.find(platformInclude);
        if (pos != std::string::npos) {
            content.erase(pos, platformInclude.size());
        }

        // Remove int main(...) { ... } function (platform provides its own entry point)
        pos = content.find("int main(");
        if (pos != std::string::npos) {
            size_t endPos = content.find("\n}\n", pos);
            if (endPos != std::string::npos) {
                endPos += 3; // include "}\n"
                while (endPos < content.size() && content[endPos] == '\n') endPos++;
                content.erase(pos, endPos - pos);
            }
        }

        // Update start scene if user selected one
        if (config.startSceneId != 0) {
            std::string startSceneName;
            for (const auto& sceneProject : project->getScenes()) {
                if (sceneProject.id == config.startSceneId) {
                    startSceneName = sceneProject.name;
                    break;
                }
            }
            if (!startSceneName.empty()) {
                std::string loadPrefix = "SceneManager::loadScene(\"";
                pos = content.find(loadPrefix);
                if (pos != std::string::npos) {
                    size_t nameStart = pos + loadPrefix.size();
                    size_t nameEnd = content.find("\")", nameStart);
                    if (nameEnd != std::string::npos) {
                        content.replace(nameStart, nameEnd - nameStart, startSceneName);
                    }
                }
            }
        }

        FileUtils::writeIfChanged(generatedDst / "main.cpp", content);
    }

    // Also copy scene_scripts.cpp
    fs::path sceneScriptsSrc = project->getProjectInternalPath() / "scene_scripts.cpp";
    if (fs::exists(sceneScriptsSrc, ec)) {
        fs::copy_file(sceneScriptsSrc, generatedDst / "scene_scripts.cpp",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy scene_scripts.cpp: " + ec.message());
            return false;
        }
    }

    return true;
}

bool editor::Exporter::copyAssets() {
    setProgress("Copying assets...", 0.3f);

    fs::path assetsSrc = config.assetsDir;
    fs::path assetsDst = getExportProjectRoot() / "assets";

    if (assetsSrc.empty()) {
        assetsSrc = project->getProjectPath();
    }

    std::error_code ec;
    if (!fs::exists(assetsSrc, ec)) {
        setError("Assets directory does not exist: " + assetsSrc.string());
        return false;
    }

    fs::create_directories(assetsDst, ec);

    // Collect C++ script paths to exclude from asset copy
    std::set<fs::path> scriptPaths;
    for (const auto& sceneProject : project->getScenes()) {
        for (const auto& script : sceneProject.cppScripts) {
            if (!script.path.empty()) {
                fs::path p = script.path;
                if (p.is_relative()) p = project->getProjectPath() / p;
                scriptPaths.insert(fs::weakly_canonical(p, ec));
            }
            if (!script.headerPath.empty()) {
                fs::path p = script.headerPath;
                if (p.is_relative()) p = project->getProjectPath() / p;
                scriptPaths.insert(fs::weakly_canonical(p, ec));
            }
        }
    }

    // Copy assets, excluding .doriax directory and C++ scripts
    for (auto& entry : fs::recursive_directory_iterator(assetsSrc, fs::directory_options::skip_permission_denied, ec)) {
        fs::path relativePath = fs::relative(entry.path(), assetsSrc, ec);

        // Skip hidden directories (starting with '.')
        std::string firstComponent = relativePath.begin()->string();
        if (!firstComponent.empty() && firstComponent[0] == '.') {
            continue;
        }
        // Skip CMakeLists.txt at root
        if (relativePath == "CMakeLists.txt") {
            continue;
        }
        // Skip C++ script files (already handled by copyCppScripts)
        if (entry.is_regular_file() && scriptPaths.count(fs::weakly_canonical(entry.path(), ec))) {
            continue;
        }

        fs::path destPath = assetsDst / relativePath;
        if (entry.is_directory()) {
            fs::create_directories(destPath, ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories(destPath.parent_path(), ec);
            fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
        }
    }

    return true;
}

bool editor::Exporter::copyLua() {
    setProgress("Copying Lua scripts...", 0.35f);

    fs::path luaSrc = config.luaDir;
    if (luaSrc.empty()) {
        return true; // No Lua directory configured, skip
    }

    std::error_code ec;
    if (!fs::exists(luaSrc, ec)) {
        return true; // Lua directory doesn't exist, not an error
    }

    fs::path luaDst = getExportProjectRoot() / "lua";
    fs::create_directories(luaDst, ec);

    fs::copy(luaSrc, luaDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        setError("Failed to copy Lua directory: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::copyCppScripts() {
    setProgress("Copying C++ scripts...", 0.4f);

    fs::path scriptsDst = getExportProjectRoot() / "scripts";

    std::error_code ec;
    std::set<std::string> copiedPaths;

    for (const auto& sceneProject : project->getScenes()) {
        for (const auto& script : sceneProject.cppScripts) {
            // Copy source file
            if (!script.path.empty()) {
                fs::path srcPath = script.path;
                if (srcPath.is_relative()) {
                    srcPath = project->getProjectPath() / srcPath;
                }
                std::string pathKey = srcPath.string();
                if (copiedPaths.count(pathKey)) continue;
                copiedPaths.insert(pathKey);

                if (fs::exists(srcPath, ec)) {
                    fs::path relPath = fs::relative(srcPath, project->getProjectPath(), ec);
                    fs::path dstPath = scriptsDst / relPath;
                    fs::create_directories(dstPath.parent_path(), ec);
                    fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
                }
            }

            // Copy header file
            if (!script.headerPath.empty()) {
                fs::path hdrPath = script.headerPath;
                if (hdrPath.is_relative()) {
                    hdrPath = project->getProjectPath() / hdrPath;
                }
                std::string pathKey = hdrPath.string();
                if (copiedPaths.count(pathKey)) continue;
                copiedPaths.insert(pathKey);

                if (fs::exists(hdrPath, ec)) {
                    fs::path relPath = fs::relative(hdrPath, project->getProjectPath(), ec);
                    fs::path dstPath = scriptsDst / relPath;
                    fs::create_directories(dstPath.parent_path(), ec);
                    fs::copy_file(hdrPath, dstPath, fs::copy_options::overwrite_existing, ec);
                }
            }
        }
    }

    return true;
}

bool editor::Exporter::copyEngine() {
    setProgress("Copying engine...", 0.5f);

#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
#elif defined(__APPLE__)
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    _NSGetExecutablePath(exePath, &size);
    fs::path exeDir = fs::canonical(exePath).parent_path();
#else
    fs::path exeDir = fs::canonical("/proc/self/exe").parent_path();
#endif

    fs::path sdkRoot;
    const std::vector<fs::path> sdkCandidates = {
        exeDir / "doriax",
        exeDir.parent_path() / "doriax",
        exeDir
    };

    std::error_code ec;
    for (const auto& candidate : sdkCandidates) {
        if (fs::exists(candidate / "engine" / "CMakeLists.txt", ec)) {
            sdkRoot = candidate;
            break;
        }
    }

    if (sdkRoot.empty()) {
        setError("Doriax SDK root not found near executable");
        return false;
    }

    fs::path engineSrc = sdkRoot / "engine";
    fs::path platformSrc = sdkRoot / "platform";
    fs::path workspacesSrc = sdkRoot / "workspaces";

    fs::path engineDst = config.targetDir / "engine";
    fs::path platformDst = config.targetDir / "platform";
    fs::path workspacesDst = config.targetDir / "workspaces";

    if (fs::exists(engineSrc, ec)) {
        fs::copy(engineSrc, engineDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy engine directory: " + ec.message());
            return false;
        }
    } else {
        setError("Engine directory not found at: " + engineSrc.string());
        return false;
    }

    if (fs::exists(platformSrc, ec)) {
        ec.clear();
        fs::copy(platformSrc, platformDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy platform directory: " + ec.message());
            return false;
        }
    }

    if (fs::exists(workspacesSrc, ec)) {
        ec.clear();
        fs::copy(workspacesSrc, workspacesDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy workspaces directory: " + ec.message());
            return false;
        }
    }

    return true;
}

bool editor::Exporter::buildAndSaveShaders() {
    setProgress("Building shaders...", 0.6f);

    fs::path shadersDst = getExportProjectRoot() / "assets" / "shaders";

    std::error_code ec;
    fs::create_directories(shadersDst, ec);
    if (ec) {
        setError("Failed to create shaders directory: " + ec.message());
        return false;
    }

    struct ShaderFormat {
        shadercompiler::lang_type_t lang;
        int version;
        bool es;
        shadercompiler::platform_t platform;
        std::string suffix;
    };

    std::vector<ShaderFormat> requiredFormats;
    // Collect formats based on selected platforms
    if (config.selectedPlatforms.count(Platform::Linux) || config.selectedPlatforms.count(Platform::Windows)) {
        requiredFormats.push_back({shadercompiler::LANG_GLSL, 410, false, shadercompiler::SHADER_DEFAULT, "glsl410"});
    }
    if (config.selectedPlatforms.count(Platform::Android) || config.selectedPlatforms.count(Platform::Web)) {
        requiredFormats.push_back({shadercompiler::LANG_GLSL, 300, true, shadercompiler::SHADER_DEFAULT, "glsl300es"});
    }
    if (config.selectedPlatforms.count(Platform::MacOS)) {
        requiredFormats.push_back({shadercompiler::LANG_MSL, 21, false, shadercompiler::SHADER_MACOS, "msl21macos"});
    }
    if (config.selectedPlatforms.count(Platform::iOS)) {
        requiredFormats.push_back({shadercompiler::LANG_MSL, 21, false, shadercompiler::SHADER_IOS, "msl21ios"});
    }

    // Default to glsl410 if no platforms require anything
    if (requiredFormats.empty()) {
        requiredFormats.push_back({shadercompiler::LANG_GLSL, 410, false, shadercompiler::SHADER_DEFAULT, "glsl410"});
    }

    int total = (int)config.selectedShaderKeys.size() * requiredFormats.size();
    int current = 0;

    for (const ShaderKey& shaderKey : config.selectedShaderKeys) {
        ShaderType type = ShaderPool::getShaderTypeFromKey(shaderKey);
        uint32_t props = ShaderPool::getPropertiesFromKey(shaderKey);
        std::string shaderStr = ShaderPool::getShaderStr(type, props);

        for (const auto& fmt : requiredFormats) {
            float shaderProgress = 0.6f + (0.3f * (float)current / (float)std::max(total, 1));
            std::string fmtStr = fmt.suffix;
            setProgress("Building shader: " + shaderStr + " (" + fmtStr + ")", shaderProgress);

            try {
                // Synchronous build without cache
                ShaderData resultData = shaderBuilder.buildShaderForExport(shaderKey, fmt.lang, fmt.version, fmt.es, fmt.platform);

                std::string filename = shaderStr + "_" + fmtStr + ".sdat";
                fs::path outputPath = shadersDst / filename;

                std::string err;
                if (!ShaderDataSerializer::writeToFile(outputPath.string(), shaderKey, resultData, &err)) {
                    Out::warning("Failed to save shader %s: %s", filename.c_str(), err.c_str());
                }
            } catch (const std::exception& e) {
                Out::warning("Failed to build shader %s (%s): %s", shaderStr.c_str(), fmtStr.c_str(), e.what());
            }

            current++;
        }
    }

    return true;
}

bool editor::Exporter::generateCMakeLists() {
    setProgress("Generating CMakeLists.txt...", 0.9f);

    std::string cmakeContent = R"CMAKE(# This file is auto-generated by Doriax Editor Export. Do not edit manually.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED APP_NAME)
   set(APP_NAME doriax-project)
endif()

project(${APP_NAME})

set(DORIAX_SHARED OFF)

if(NOT DORIAX_ROOT)
    set(DORIAX_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if (NOT EXISTS "${DORIAX_ROOT}")
    message(FATAL_ERROR "Can't find Doriax root directory: ${DORIAX_ROOT}")
endif()
file(TO_CMAKE_PATH ${DORIAX_ROOT} DORIAX_ROOT)

if(NOT PROJECT_ROOT)
    set(PROJECT_ROOT ${DORIAX_ROOT}/project)
endif()
if (NOT EXISTS "${PROJECT_ROOT}")
    message(FATAL_ERROR "Can't find project root directory: ${PROJECT_ROOT}")
endif()
file(TO_CMAKE_PATH ${PROJECT_ROOT} PROJECT_ROOT)

add_definitions("-DDEFAULT_WINDOW_WIDTH=960")
add_definitions("-DDEFAULT_WINDOW_HEIGHT=540")

if(NOT GRAPHIC_BACKEND)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(GRAPHIC_BACKEND "gles3")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(GRAPHIC_BACKEND "gles3")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(GRAPHIC_BACKEND "d3d11")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(GRAPHIC_BACKEND "glcore")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(GRAPHIC_BACKEND "glcore")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(GRAPHIC_BACKEND "metal")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(GRAPHIC_BACKEND "metal")
    else()
        message(FATAL_ERROR "GRAPHIC_BACKEND is not set")
    endif()
endif()
message(STATUS "Graphic backend is set to ${GRAPHIC_BACKEND}")

if(NOT APP_BACKEND)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(APP_BACKEND "emscripten")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(APP_BACKEND "android")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(APP_BACKEND "sokol")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(APP_BACKEND "glfw")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(APP_BACKEND "glfw")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if (CMAKE_GENERATOR STREQUAL "Xcode")
            set(APP_BACKEND "apple")
        else()
            set(APP_BACKEND "sokol")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(APP_BACKEND "apple")
    else()
        message(FATAL_ERROR "APP_BACKEND is not set")
    endif()
endif()
message(STATUS "Application backend is set to ${APP_BACKEND}")

set(COMPILE_ZLIB OFF)
set(IS_ARM OFF)

set(PLATFORM_EXEC_FLAGS)
set(PLATFORM_ROOT)
set(PLATFORM_SOURCE)
set(PLATFORM_LIBS)
set(PLATFORM_RESOURCES)
set(PLATFORM_PROPERTIES)
set(PLATFORM_OPTIONS)

find_package(Threads REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_definitions("-DDORIAX_WEB")

    if(GRAPHIC_BACKEND STREQUAL "gles3")
        add_definitions("-DSOKOL_GLES3")
        set(USE_WEBGL2 "-s USE_WEBGL2=1")
    endif()

    add_definitions("-DWITH_MINIAUDIO")

    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/emscripten)

    set(EM_PRELOAD_FILES)
    if (EXISTS "${PROJECT_ROOT}/assets")
        set(EM_PRELOAD_FILES "${EM_PRELOAD_FILES} --preload-file ${PROJECT_ROOT}/assets@/")
    endif()
    if (EXISTS "${PROJECT_ROOT}/lua")
        set(EM_PRELOAD_FILES "${EM_PRELOAD_FILES} --preload-file ${PROJECT_ROOT}/lua@/")
    endif()

    list(APPEND PLATFORM_SOURCE
        ${PLATFORM_ROOT}/DoriaxWeb.cpp
        ${PLATFORM_ROOT}/main.cpp
    )

    list(APPEND PLATFORM_LIBS
        idbfs.js
    )

    option(EMSCRIPTEN_THREAD_SUPPORT "Enable pthreads for Emscripten builds" OFF)

    if(EMSCRIPTEN_THREAD_SUPPORT)
        set(USE_PTHREADS "-s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4")
        list(APPEND PLATFORM_OPTIONS -pthread)
    endif()

    set(ALLOW_MEMORY_GROWTH "-s ALLOW_MEMORY_GROWTH=1")

    list(APPEND PLATFORM_PROPERTIES
        LINK_FLAGS
            "-g \
            -s INITIAL_MEMORY=256MB \
            -s STACK_SIZE=4MB \
            -s EXPORTED_FUNCTIONS=\"['_getScreenWidth','_getScreenHeight','_changeCanvasSize','_main']\" \
            -s EXPORTED_RUNTIME_METHODS=\"['ccall', 'cwrap']\" \
            ${EM_PRELOAD_FILES} \
            ${EM_ADDITIONAL_LINK_FLAGS} \
            ${USE_PTHREADS} \
            ${ALLOW_MEMORY_GROWTH} \
            ${USE_WEBGL2}"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_definitions("-DDORIAX_ANDROID")

    if(GRAPHIC_BACKEND STREQUAL "gles3")
        add_definitions("-DSOKOL_GLES3")
        set(OPENGL_LIB GLESv3)
    endif()

    add_definitions("-D\"lua_getlocaledecpoint()='.'\"")
    add_definitions("-DLUA_ANSI")
    add_definitions("-DLUA_USE_C89")
    add_definitions("-DWITH_MINIAUDIO")

    set(APP_NAME doriax-android)

    if(ANDROID_ABI MATCHES "^arm(eabi)?(-v7a)?(64-v8a)?$")
        if(ANDROID_ABI MATCHES "^arm(eabi)?(-v7a)?$")
            add_compile_options("-mfpu=fp-armv8")
        endif()
        set(IS_ARM ON)
    endif()

    set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/android)

    list(APPEND PLATFORM_SOURCE
        ${PLATFORM_ROOT}/DoriaxAndroid.cpp
        ${PLATFORM_ROOT}/AndroidMain.cpp
        ${PLATFORM_ROOT}/NativeEngine.cpp
    )

    set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

    find_package(game-activity REQUIRED CONFIG)
    find_package(games-frame-pacing REQUIRED CONFIG)

    list(APPEND PLATFORM_LIBS
        ${OPENGL_LIB}
        log
        android
        EGL
        OpenSLES
        game-activity::game-activity_static
        games-frame-pacing::swappy_static
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions("-DDORIAX_SOKOL")

    if(GRAPHIC_BACKEND STREQUAL "glcore")
        add_definitions("-DSOKOL_GLCORE")
    elseif(GRAPHIC_BACKEND STREQUAL "d3d11")
        add_definitions("-DSOKOL_D3D11")
    endif()

    add_definitions("-DWITH_MINIAUDIO")

    if (EXISTS "${PROJECT_ROOT}/assets")
        set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/assets)
    endif()
    if (EXISTS "${PROJECT_ROOT}/lua")
        set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/lua)
    endif()

    set(PLATFORM_EXEC_FLAGS WIN32)
    set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

    list(APPEND PLATFORM_SOURCE
        ${PLATFORM_ROOT}/DoriaxSokol.cpp
        ${PLATFORM_ROOT}/main.cpp
    )

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND PLATFORM_LIBS
            -static -static-libgcc -static-libstdc++
        )
    endif()
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

if((CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"))
    if(GRAPHIC_BACKEND STREQUAL "glcore")
        add_definitions("-DSOKOL_GLCORE")
    endif()

    add_definitions("-DWITH_MINIAUDIO")

    if (EXISTS "${PROJECT_ROOT}/assets")
        set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
    endif()
    if (EXISTS "${PROJECT_ROOT}/lua")
        set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
    endif()

    if (APP_BACKEND STREQUAL "glfw")
        add_definitions("-DDORIAX_GLFW")

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/glfw)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxGLFW.cpp
            ${PLATFORM_ROOT}/main.cpp
        )

        list(APPEND PLATFORM_LIBS
            GL dl m glfw
        )
    else()
        add_definitions("-DDORIAX_SOKOL")

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxSokol.cpp
            ${PLATFORM_ROOT}/main.cpp
        )
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(GRAPHIC_BACKEND STREQUAL "glcore")
        add_definitions("-DSOKOL_GLCORE")
    elseif(GRAPHIC_BACKEND STREQUAL "metal")
        add_definitions("-DSOKOL_METAL")
    endif()

    add_definitions("-DWITH_MINIAUDIO")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

    if (APP_BACKEND STREQUAL "apple")
        add_definitions("-DDORIAX_APPLE")
        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/apple)
        set(APP_BUNDLE_IDENTIFIER "org.doriaxengine.doriax")
        set(PLATFORM_EXEC_FLAGS MACOSX_BUNDLE)

        include_directories(/System/Library/Frameworks)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/macos/main.m
            ${PLATFORM_ROOT}/macos/AppDelegate.h
            ${PLATFORM_ROOT}/macos/AppDelegate.m
            ${PLATFORM_ROOT}/macos/EngineView.h
            ${PLATFORM_ROOT}/macos/EngineView.mm
            ${PLATFORM_ROOT}/macos/ViewController.h
            ${PLATFORM_ROOT}/macos/ViewController.m
            ${PLATFORM_ROOT}/Renderer.h
            ${PLATFORM_ROOT}/Renderer.mm
            ${PLATFORM_ROOT}/DoriaxApple.h
            ${PLATFORM_ROOT}/DoriaxApple.mm
        )

        list(APPEND PLATFORM_RESOURCES
            ${PLATFORM_ROOT}/macos/Main.storyboard
        )

        list(APPEND PLATFORM_PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${DORIAX_ROOT}/workspaces/xcode/macos/Info.plist"
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        )
    else()
        if (EXISTS "${PROJECT_ROOT}/assets")
            set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
        endif()
        if (EXISTS "${PROJECT_ROOT}/lua")
            set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
        endif()

        if (APP_BACKEND STREQUAL "glfw")
            add_definitions("-DDORIAX_GLFW")
            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/glfw)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/DoriaxGLFW.cpp
                ${PLATFORM_ROOT}/main.cpp
            )

            list(APPEND PLATFORM_LIBS
                glfw
            )
        else()
            add_definitions("-DDORIAX_SOKOL")
            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/DoriaxSokol.cpp
                ${PLATFORM_ROOT}/main.cpp
            )
        endif()
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_definitions("-DDORIAX_APPLE")

    if(GRAPHIC_BACKEND STREQUAL "metal")
        add_definitions("-DSOKOL_METAL")
    endif()

    add_definitions("-DWITH_MINIAUDIO")

    set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/apple)
    set(APP_BUNDLE_IDENTIFIER "org.doriaxengine.doriax")

    include_directories(/System/Library/Frameworks)

    list(APPEND PLATFORM_SOURCE
        ${PLATFORM_ROOT}/ios/main.m
        ${PLATFORM_ROOT}/ios/AppDelegate.h
        ${PLATFORM_ROOT}/ios/AppDelegate.m
        ${PLATFORM_ROOT}/ios/EngineView.h
        ${PLATFORM_ROOT}/ios/EngineView.mm
        ${PLATFORM_ROOT}/ios/ViewController.h
        ${PLATFORM_ROOT}/ios/ViewController.m
        ${PLATFORM_ROOT}/ios/AdMobAdapter.h
        ${PLATFORM_ROOT}/ios/AdMobAdapter.m
        ${PLATFORM_ROOT}/Renderer.h
        ${PLATFORM_ROOT}/Renderer.mm
        ${PLATFORM_ROOT}/DoriaxApple.h
        ${PLATFORM_ROOT}/DoriaxApple.mm
    )

    list(APPEND PLATFORM_RESOURCES
        ${PLATFORM_ROOT}/ios/LaunchScreen.storyboard
        ${PLATFORM_ROOT}/ios/Main.storyboard
    )

    list(APPEND PLATFORM_LIBS
        -ObjC
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/FBLPromises.xcframework/ios-arm64_x86_64-simulator/FBLPromises.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurement.xcframework/ios-arm64_x86_64-simulator/GoogleAppMeasurement.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurementIdentitySupport.xcframework/ios-arm64_x86_64-simulator/GoogleAppMeasurementIdentitySupport.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleMobileAds.xcframework/ios-arm64_x86_64-simulator/GoogleMobileAds.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleUtilities.xcframework/ios-arm64_x86_64-simulator/GoogleUtilities.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/UserMessagingPlatform.xcframework/ios-arm64_x86_64-simulator/UserMessagingPlatform.framework
        ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/nanopb.xcframework/ios-arm64_x86_64-simulator/nanopb.framework
    )

    add_compile_options(-fmodules)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")

    list(APPEND PLATFORM_PROPERTIES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${APP_BUNDLE_IDENTIFIER}
        MACOSX_BUNDLE_INFO_PLIST "${DORIAX_ROOT}/workspaces/xcode/ios/Info.plist"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    )
endif()

include_directories(${PLATFORM_ROOT})

include_directories(${DORIAX_ROOT}/engine/libs/sokol)
include_directories(${DORIAX_ROOT}/engine/libs/lua)
include_directories(${DORIAX_ROOT}/engine/libs/box2d/include)
include_directories(${DORIAX_ROOT}/engine/libs/joltphysics)

include_directories(${DORIAX_ROOT}/engine/core)
include_directories(${DORIAX_ROOT}/engine/core/action)
include_directories(${DORIAX_ROOT}/engine/core/buffer)
include_directories(${DORIAX_ROOT}/engine/core/component)
include_directories(${DORIAX_ROOT}/engine/core/ecs)
include_directories(${DORIAX_ROOT}/engine/core/io)
include_directories(${DORIAX_ROOT}/engine/core/manager)
include_directories(${DORIAX_ROOT}/engine/core/math)
include_directories(${DORIAX_ROOT}/engine/core/object)
include_directories(${DORIAX_ROOT}/engine/core/object/audio)
include_directories(${DORIAX_ROOT}/engine/core/object/ui)
include_directories(${DORIAX_ROOT}/engine/core/object/environment)
include_directories(${DORIAX_ROOT}/engine/core/object/physics)
include_directories(${DORIAX_ROOT}/engine/core/pool)
include_directories(${DORIAX_ROOT}/engine/core/registry)
include_directories(${DORIAX_ROOT}/engine/core/render)
include_directories(${DORIAX_ROOT}/engine/core/script)
include_directories(${DORIAX_ROOT}/engine/core/shader)
include_directories(${DORIAX_ROOT}/engine/core/subsystem)
include_directories(${DORIAX_ROOT}/engine/core/texture)
include_directories(${DORIAX_ROOT}/engine/core/util)
include_directories(${DORIAX_ROOT}/engine/renders)

add_subdirectory(${DORIAX_ROOT}/engine)

include_directories(${PROJECT_ROOT})
include_directories(${PROJECT_ROOT}/scripts)
file(GLOB_RECURSE PROJECT_SOURCE ${PROJECT_ROOT}/*.cpp)

set(all_code_files
    ${PROJECT_SOURCE}
    ${PLATFORM_SOURCE}
    ${PLATFORM_RESOURCES}
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_executable(
        ${APP_NAME}
        ${PLATFORM_EXEC_FLAGS}
        ${all_code_files}
    )
else()
    add_library(
        ${APP_NAME}
        SHARED
        ${all_code_files}
    )
endif()

if(DEFINED ASSETS_DEST_DIR)
    add_custom_command(
        TARGET ${APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory ${ASSETS_DEST_DIR}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_ROOT}/assets ${ASSETS_DEST_DIR}
    )
endif()

if(DEFINED LUA_DEST_DIR)
    add_custom_command(
        TARGET ${APP_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory ${LUA_DEST_DIR}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_ROOT}/lua ${LUA_DEST_DIR}
    )
endif()

set_target_properties(
    ${APP_NAME}
    PROPERTIES
    ${PLATFORM_PROPERTIES}
    RESOURCE "${PLATFORM_RESOURCES}"
    CXX_STANDARD 17
)

target_compile_options(
    ${APP_NAME}
    PUBLIC
    ${PLATFORM_OPTIONS}
)

target_link_libraries(
    ${APP_NAME}
    doriax
    Threads::Threads
    ${PLATFORM_LIBS}
)
)CMAKE";

    fs::path cmakeFile = config.targetDir / "CMakeLists.txt";
    FileUtils::writeIfChanged(cmakeFile, cmakeContent);

    return true;
}

// Static helpers for UI display

std::string editor::Exporter::getShaderDisplayName(ShaderType type, uint32_t properties) {
    std::string name = ShaderPool::getShaderTypeName(type);
    int propCount = ShaderPool::getShaderPropertyCount(type);

    std::string props;
    for (int i = 0; i < propCount; i++) {
        if (properties & (1 << i)) {
            if (!props.empty()) props += ", ";
            props += ShaderPool::getShaderPropertyName(type, i, true);
        }
    }

    if (!props.empty()) {
        name += " (" + props + ")";
    }

    return name;
}

std::string editor::Exporter::getPlatformName(Platform platform) {
    switch (platform) {
        case Platform::MacOS:   return "macOS";
        case Platform::iOS:     return "iOS";
        case Platform::Web:     return "Web";
        case Platform::Android: return "Android";
        case Platform::Linux:   return "Linux";
        case Platform::Windows: return "Windows";
        default:                return "Unknown";
    }
}
