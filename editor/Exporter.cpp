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

    // Collect C++ script paths to exclude
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

    for (auto& entry : fs::recursive_directory_iterator(luaSrc, fs::directory_options::skip_permission_denied, ec)) {
        fs::path relativePath = fs::relative(entry.path(), luaSrc, ec);

        // Skip hidden directories (starting with '.')
        std::string firstComponent = relativePath.begin()->string();
        if (!firstComponent.empty() && firstComponent[0] == '.') {
            continue;
        }
        // Skip CMakeLists.txt at root
        if (relativePath == "CMakeLists.txt") {
            continue;
        }
        // Skip C++ script files
        if (entry.is_regular_file() && scriptPaths.count(fs::weakly_canonical(entry.path(), ec))) {
            continue;
        }

        fs::path destPath = luaDst / relativePath;
        if (entry.is_directory()) {
            fs::create_directories(destPath, ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories(destPath.parent_path(), ec);
            fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
        }
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
        exeDir / "engine",
        exeDir.parent_path() / "engine",
        exeDir
    };

    std::error_code ec;
    for (const auto& candidate : sdkCandidates) {
        if (fs::exists(candidate / "CMakeLists.txt", ec)) {
            sdkRoot = candidate;
            break;
        }
    }

    if (sdkRoot.empty()) {
        setError("Doriax SDK root not found near executable");
        return false;
    }

    auto copyDir = [&](const std::string& name) -> bool {
        fs::path src = sdkRoot / name;
        fs::path dst = config.targetDir / name;

        if (!fs::exists(src, ec)) {
            setError(name + " directory not found at: " + src.string());
            return false;
        }
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy " + name + " directory: " + ec.message());
            return false;
        }
        return true;
    };

    if (!copyDir("core")) return false;
    if (!copyDir("libs")) return false;
    if (!copyDir("platform")) return false;
    if (!copyDir("renders")) return false;
    if (!copyDir("shaders")) return false;
    //if (!copyDir("tools")) return false;
    if (!copyDir("workspaces")) return false;

    fs::path cmakeSrc = sdkRoot / "CMakeLists.txt";
    fs::path cmakeDst = config.targetDir / "CMakeLists.txt";
    if (!fs::exists(cmakeSrc, ec)) {
        setError("CMakeLists.txt not found at: " + cmakeSrc.string());
        return false;
    }
    fs::copy_file(cmakeSrc, cmakeDst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        setError("Failed to copy CMakeLists.txt: " + ec.message());
        return false;
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
