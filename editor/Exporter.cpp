// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "Exporter.h"
#include "EditorHost.h"
#include "Factory.h"
#include "Generator.h"
#include "Out.h"
#include "Stream.h"
#include "util/Base64.h"
#include "util/FileUtils.h"
#include "util/MsBuildProgress.h"
#include "util/ShaderHeaderBuilder.h"
#include "pool/ShaderPool.h"

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <vector>

using namespace doriax;

namespace {
    constexpr float BUILD_PROGRESS_START = 0.6f;
    constexpr float BUILD_PROGRESS_END = 0.95f;
    constexpr float MSBUILD_COMPILE_END = 0.9f;
    constexpr char RESOURCE_PACK_MAGIC[] = {'D', 'X', 'P', 'K', '1'};

    struct ResourcePackBuildEntry {
        std::string path;
        std::vector<unsigned char> data;
        uint64_t offset = 0;
        uint8_t key = 0;
        uint32_t shift = 0;
    };

    // Parses make-style "[ 47%]" and ninja-style "[123/456]" build-line prefixes
    // into a 0..1 fraction so the compile step can drive the progress bar.
    bool parseBuildProgress(const std::string& line, float& fraction) {
        if (line.empty() || line[0] != '[') return false;
        size_t close = line.find(']');
        if (close == std::string::npos || close > 16) return false;
        std::string inner = line.substr(1, close - 1);
        try {
            size_t slash = inner.find('/');
            if (slash != std::string::npos) {
                int current = std::stoi(inner.substr(0, slash));
                int total = std::stoi(inner.substr(slash + 1));
                if (total <= 0 || current < 0 || current > total) return false;
                fraction = (float)current / (float)total;
                return true;
            }
            size_t percent = inner.find('%');
            if (percent == std::string::npos) return false;
            int value = std::stoi(inner.substr(0, percent));
            if (value < 0 || value > 100) return false;
            fraction = value / 100.0f;
            return true;
        } catch (...) {
            return false;
        }
    }

    void writeU8(std::ofstream& out, uint8_t value) {
        out.put(static_cast<char>(value));
    }

    void writeU16(std::ofstream& out, uint16_t value) {
        out.put(static_cast<char>(value & 0xff));
        out.put(static_cast<char>((value >> 8) & 0xff));
    }

    void writeU32(std::ofstream& out, uint32_t value) {
        for (int i = 0; i < 4; i++) {
            out.put(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    void writeU64(std::ofstream& out, uint64_t value) {
        for (int i = 0; i < 8; i++) {
            out.put(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    void shiftRight(std::vector<unsigned char>& data, uint32_t shift) {
        if (data.empty()) return;
        shift %= static_cast<uint32_t>(data.size());
        if (shift == 0) return;
        std::rotate(data.begin(), data.end() - shift, data.end());
    }

    void applyXor(std::vector<unsigned char>& data, uint8_t key) {
        for (unsigned char& byte : data) {
            byte ^= key;
        }
    }

    bool readWholeFile(const fs::path& path, std::vector<unsigned char>& data) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        std::streamoff size = in.tellg();
        if (size < 0) return false;
        in.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(size));
        return data.empty() || static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), size));
    }
}

editor::Exporter::Exporter() {
}

editor::Exporter::~Exporter() {
    cancelRequested.store(true);
    commandRunner.cancel();
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

void editor::Exporter::setProgress(const std::string& step, float value) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentStep = step;
    progress.overallProgress = value * progressScale;
    progress.detailLine.clear();
}

void editor::Exporter::setProgressRaw(const std::string& step, float value) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentStep = step;
    progress.overallProgress = value;
    progress.detailLine.clear();
}

void editor::Exporter::setDetail(const std::string& line) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.detailLine = line;
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
    commandRunner.cancel();
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

bool editor::Exporter::exportProject(Project* proj, const ExportConfig& cfg) {
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

    runExport();

    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.finished && !progress.failed;
}

bool editor::Exporter::generateShaders(const ExportConfig& cfg) {
    if (exportThread.joinable()) {
        exportThread.join();
    }

    this->project = nullptr;
    this->config = cfg;
    cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        this->progress = ExportProgress();
        this->progress.started = true;
    }

    runExport();

    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.finished && !progress.failed;
}

void editor::Exporter::runExport() {
    const bool shaderGenerationOnly = (project == nullptr);
    const bool useSceneShaderKeys = !shaderGenerationOnly && config.selectedShaderKeys.empty();
    const bool buildMode = !shaderGenerationOnly && config.mode != ExportMode::SourceCode;

    // Generation steps report progress in the [0,1] SourceCode range; for build
    // modes they occupy the first half, leaving the rest for configure/build/collect.
    progressScale = buildMode ? 0.5f : 1.0f;

    if (buildMode) {
        if (!prepareCacheTarget()) return;
    } else {
        if (!checkTargetDir()) return;
    }
    if (isCancelled()) { setError("Export cancelled"); return; }

    if (shaderGenerationOnly) {
        collectSelectedShaderKeys();
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!buildAndSaveShaders()) return;

        setProgress("Shader generation complete", 1.0f);
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.finished = true;
        }
        Out::info("Shaders generated successfully at: %s", config.targetDir.string().c_str());
        return;
    }

    if (useSceneShaderKeys) collectSelectedShaderKeys();
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!clearGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!loadAndSaveAllScenes()) return;
    if (useSceneShaderKeys) collectSelectedShaderKeys(true);
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    // Lua before assets, which skips the sources the lua tree took
    if (!copyLua()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyAssets()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyCppScripts()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyEngine()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!buildAndSaveShaders()) return;
    if (config.mode == ExportMode::SourceCode && config.packNativeResources) {
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!collectSourceResourcePack()) return;
    }

    if (buildMode) {
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!configureBuild()) return;
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!runBuild()) return;
        if (isCancelled()) { setError("Export cancelled"); return; }
        bool collected = (config.mode == ExportMode::Desktop)
            ? collectDesktopArtifacts()
            : collectWebArtifacts();
        if (!collected) return;

        setProgressRaw("Export complete", 1.0f);
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.finished = true;
        }
        Out::info("Project exported successfully to: %s", config.destinationDir.string().c_str());
        return;
    }

    setProgress("Export complete", 1.0f);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress.finished = true;
    }
    Out::info("Project exported successfully to: %s", config.targetDir.string().c_str());
}

void editor::Exporter::collectSelectedShaderKeys(bool mergeWithExisting) {
    if (!mergeWithExisting && !config.selectedShaderKeys.empty()) {
        return;
    }

    if (!project) {
        return;
    }

    for (const auto& sceneProject : project->getScenes()) {
        config.selectedShaderKeys.insert(sceneProject.shaderKeys.begin(), sceneProject.shaderKeys.end());
    }
}

fs::path editor::Exporter::getExportProjectRoot() const {
    return config.targetDir / "project";
}

std::string editor::Exporter::getAppName() const {
    if (project->getName().empty()) {
        return "doriax-project";
    }
    return Factory::toIdentifier(project->getName());
}

fs::path editor::Exporter::getBuildCacheDir() const {
    return project->getProjectInternalPath() / "export" / (config.mode == ExportMode::Desktop ? "desktop" : "web");
}

std::string editor::Exporter::getEffectiveGenerator() const {
    if (!config.cmakeGenerator.empty()) {
        return config.cmakeGenerator;
    }
    const char* envGenerator = std::getenv("CMAKE_GENERATOR");
    return envGenerator ? envGenerator : "";
}

bool editor::Exporter::shouldSkipExportSupportFile(const fs::path& relativePath) {
    return relativePath == "CMakeLists.txt" || relativePath == "ProjectBuild.cmake"
        || relativePath.filename() == "AGENTS.md";
}

bool editor::Exporter::isCppHeaderFile(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> headerExtensions = {
        ".h", ".hpp", ".hh", ".hxx", ".h++", ".inl", ".ipp", ".tpp"
    };
    return headerExtensions.count(ext) > 0;
}

bool editor::Exporter::isCppSourceFile(const fs::path& path) {
    // C++ scripts are exported to project/scripts by copyCppScripts(); they must
    // never be duplicated into the assets or lua trees. The exported build globs
    // every .cpp under the project root recursively, so a stray source file in
    // both assets and lua would compile twice and break linking with multiple
    // definition errors. Filter by extension so unregistered/orphan scripts are
    // skipped too (not only those attached to a scene).
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> sourceExtensions = {
        ".cpp", ".cc", ".cxx", ".c++", ".c"
    };
    return sourceExtensions.count(ext) > 0 || isCppHeaderFile(path);
}

bool editor::Exporter::isLuaExportFile(const fs::path& path) {
    // The lua directory carries the scripts and the data files they can read
    // at runtime; assets and C++ scripts ship through their own copy steps.
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> luaExtensions = {
        ".lua", ".luac", ".json", ".txt", ".csv", ".tsv", ".xml", ".ini", ".cfg", ".conf", ".toml", ".dat"
    };
    return luaExtensions.count(ext) > 0;
}

bool editor::Exporter::isLuaSourceFile(const fs::path& path) {
    // Only the lua tree needs these; every platform puts it on the "lua://" lookup path
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".lua" || ext == ".luac";
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

    if (project && !config.overwriteTarget && !fs::is_empty(config.targetDir, ec)) {
        setError("Target directory is not empty");
        return false;
    }

    return true;
}

bool editor::Exporter::prepareCacheTarget() {
    setProgress("Preparing build cache...", 0.0f);

    if (config.destinationDir.empty()) {
        setError("Destination directory not specified");
        return false;
    }

    config.targetDir = getBuildCacheDir();

    std::error_code ec;
    fs::create_directories(config.targetDir, ec);
    if (ec) {
        setError("Failed to create build cache directory: " + ec.message());
        return false;
    }

    // Stale generated sources from renamed/deleted scenes would be picked up by
    // the exported CMake's recursive glob and break the link, so the (small)
    // project tree is wiped and fully re-copied every export. The engine tree is
    // kept and synced with copyTreeIfChanged() so the build stays incremental.
    fs::remove_all(getExportProjectRoot(), ec);
    if (ec) {
        setError("Failed to clear cached project sources: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::configureBuild() {
    setProgressRaw("Configuring build...", 0.5f);

    if (isCancelled()) { setError("Export cancelled"); return false; }

    const fs::path buildDir = config.targetDir / "build";

    std::string emcmake;
    std::string kitId;
    if (config.mode == ExportMode::Web) {
        EmsdkInfo emsdk = detectEmsdk(config.emsdkPath);
        if (!emsdk.found) {
            setError("Emscripten SDK not found. Set the EMSDK environment variable, add emcmake to PATH, or choose the emsdk folder in the export settings.");
            return false;
        }
        emcmake = emsdk.emcmake;
        Out::info("Using Emscripten %s", emsdk.description.c_str());
        kitId = "web\n" + emcmake + "\n" + config.buildType + "\n" + config.graphicBackend;
    } else {
        std::string effectiveGenerator = getEffectiveGenerator();
        // The Xcode generator builds a macOS .app bundle (MACOSX_BUNDLE in the
        // engine CMake), whose layout artifact collection does not handle and
        // whose assets would need embedding into the bundle. Every other
        // generator on macOS uses the sokol backend with a plain executable.
        if (effectiveGenerator == "Xcode") {
            if (config.cmakeGenerator.empty()) {
                setError("Desktop export does not support the Xcode generator (it produces an app bundle). Unset the CMAKE_GENERATOR environment variable to use the default toolchain.");
            } else {
                setError("Desktop export does not support the Xcode generator (it produces an app bundle). Clear the generator in Project Settings to use the default toolchain.");
            }
            return false;
        }
        kitId = "desktop\n" + effectiveGenerator + "\n" + config.cmakeCCompiler + "\n" + config.cmakeCxxCompiler + "\n" + config.buildType + "\n" + config.graphicBackend;
    }

    // CMake cannot switch generators or toolchains in place: wipe the build
    // tree whenever the kit differs from the one that configured it.
    std::error_code ec;
    const fs::path kitMarker = buildDir / ".doriax_export_kit";
    if (fs::exists(buildDir, ec)) {
        std::string prevKit;
        {
            std::ifstream f(kitMarker);
            if (f.is_open()) {
                prevKit.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            }
        }
        if (prevKit != kitId) {
            Out::warning("Build settings changed. Cleaning export build directory...");
            fs::remove_all(buildDir, ec);
            if (ec) {
                setError("Failed to clean export build directory: " + ec.message());
                return false;
            }
        }
    }

    // CMake re-parses stored paths treating backslashes as escapes; it accepts
    // forward slashes on every platform (see Generator::configureCMake).
    auto toCMakePath = [](const std::string& p) {
        std::string out = p;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    };

    std::string cmd;
    if (config.mode == ExportMode::Web) {
        // The emcmake wrapper injects the Emscripten toolchain file; the
        // subsequent cmake --build needs no wrapper. emcmake resolves the cmake
        // it wraps itself, so the editor override does not apply here.
        cmd = "\"" + toCMakePath(emcmake) + "\" cmake ";
    } else {
        cmd = Generator::cmakeExecutable() + " ";
        if (!config.cmakeGenerator.empty()) {
            cmd += "-G \"" + config.cmakeGenerator + "\" ";
        }
        if (!config.cmakeCCompiler.empty()) {
            cmd += "-DCMAKE_C_COMPILER=\"" + toCMakePath(config.cmakeCCompiler) + "\" ";
        }
        if (!config.cmakeCxxCompiler.empty()) {
            cmd += "-DCMAKE_CXX_COMPILER=\"" + toCMakePath(config.cmakeCxxCompiler) + "\" ";
        }
    }
    cmd += "-DCMAKE_BUILD_TYPE=" + config.buildType + " ";
    if (!config.graphicBackend.empty()) {
        cmd += "-DGRAPHIC_BACKEND=" + config.graphicBackend + " ";
    }
    cmd += "\"" + toCMakePath(config.targetDir.string()) + "\" ";
    cmd += "-B \"" + toCMakePath(buildDir.string()) + "\"";

    if (config.mode == ExportMode::Desktop) {
        cmd = CommandRunner::msvcEnvPrefix(getEffectiveGenerator()) + cmd;
    }

    Out::info("Configuring export build: %s", cmd.c_str());
    bool ok = commandRunner.run(cmd, config.targetDir, [this](const std::string& line) {
        Out::build("%s", line.c_str());
        setDetail(line);
    });

    if (!ok) {
        if (isCancelled()) {
            setError("Export cancelled");
        } else {
            setError("Build configuration failed. See the Output window for details.");
        }
        return false;
    }

    fs::create_directories(buildDir, ec);
    std::ofstream f(kitMarker);
    if (f.is_open()) {
        f << kitId;
    }

    return true;
}

bool editor::Exporter::runBuild() {
    setProgressRaw("Compiling project...", BUILD_PROGRESS_START);

    if (isCancelled()) { setError("Export cancelled"); return false; }

    const fs::path buildDir = config.targetDir / "build";

    unsigned int jobs = config.buildJobs == 0 ? Generator::getAutomaticParallelBuildJobs() : config.buildJobs;
    jobs = std::min(jobs, Generator::getMaxParallelBuildJobs());

    // A Web tree was configured by the cmake emcmake picked, and driving its
    // build with a different one either fails or reconfigures the tree.
    const std::string cmakeCmd = config.mode == ExportMode::Web
        ? std::string("cmake")
        : Generator::cmakeExecutable();

    std::string cmd = cmakeCmd + " --build \"" + buildDir.string() + "\" --config " + config.buildType + " --parallel " + std::to_string(jobs);
    if (config.mode == ExportMode::Desktop) {
        // The build step invokes the compiler/linker, so it needs the same
        // MSVC environment as configure.
        cmd = CommandRunner::msvcEnvPrefix(getEffectiveGenerator()) + cmd;
    }

    // Initialization only predicts the work denominator. Do not advance the
    // bar until MSBuild confirms work by emitting a source filename.
    MsBuildProgress msBuildProgress(buildDir, config.buildType);

    Out::info("Building export with %u parallel jobs...", jobs);
    bool ok = commandRunner.run(cmd, buildDir, [this, &msBuildProgress](const std::string& line) {
        Out::build("%s", line.c_str());
        float fraction = 0.0f;
        bool hasProgress = parseBuildProgress(line, fraction);
        float buildEnd = BUILD_PROGRESS_END;
        if (!hasProgress && msBuildProgress.consumeLine(line, fraction)) {
            hasProgress = true;
            buildEnd = MSBUILD_COMPILE_END;
        }
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.detailLine = line;
            if (hasProgress) {
                progress.overallProgress = BUILD_PROGRESS_START
                    + (buildEnd - BUILD_PROGRESS_START) * fraction;
            }
        }
    });

    if (!ok) {
        if (isCancelled()) {
            setError("Export cancelled");
        } else {
            setError("Build failed. See the Output window for details.");
        }
        return false;
    }

    return true;
}

bool editor::Exporter::collectDesktopArtifacts() {
    setProgressRaw("Copying artifacts...", 0.95f);

    const fs::path buildDir = config.targetDir / "build";
    std::string exeName = getAppName();
#ifdef _WIN32
    exeName += ".exe";
#endif

    std::error_code ec;
    const fs::path projectRoot = getExportProjectRoot();
    fs::path exePath = buildDir / exeName;
    if (!fs::exists(exePath, ec)) {
        // Multi-config generators (Visual Studio) place binaries in a per-config subdir.
        fs::path configExePath = buildDir / config.buildType / exeName;
        if (fs::exists(configExePath, ec)) {
            exePath = configExePath;
        } else if (fs::exists(buildDir / (getAppName() + ".app"), ec)
                   || fs::exists(buildDir / config.buildType / (getAppName() + ".app"), ec)) {
            // A stale Xcode-configured cache can slip past the generator guard.
            setError("The build produced an app bundle, which Desktop export does not support. Delete the project's .doriax/export/desktop folder and export again with the default toolchain.");
            return false;
        } else {
            setError("Built executable not found at: " + exePath.string() + " or " + configExePath.string());
            return false;
        }
    }

    fs::create_directories(config.destinationDir, ec);
    if (ec) {
        setError("Failed to create destination directory: " + ec.message());
        return false;
    }

    fs::copy_file(exePath, config.destinationDir / exeName, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        setError("Failed to copy executable: " + ec.message());
        return false;
    }

    bool packCreated = false;
    if (config.packNativeResources && !writeNativeResourcePack(config.destinationDir / "game.pak", packCreated)) {
        return false;
    }
    if (!config.packNativeResources) {
        ec.clear();
        fs::remove(config.destinationDir / "game.pak", ec);
        if (ec) {
            setError("Failed to remove stale resource pack: " + ec.message());
            return false;
        }
    }

    if (config.packNativeResources && packCreated) {
        for (const char* dir : {"assets", "lua"}) {
            ec.clear();
            fs::remove_all(config.destinationDir / dir, ec);
            if (ec) {
                setError(std::string("Failed to remove unpacked ") + dir + ": " + ec.message());
                return false;
            }
        }
    } else {
        // Default and fallback behavior: ship resource folders next to the executable.
        for (const char* dir : {"assets", "lua"}) {
            fs::path src = projectRoot / dir;
            if (!fs::exists(src, ec)) continue;
            copyTree(src, config.destinationDir / dir, ec);
            if (ec) {
                setError(std::string("Failed to copy ") + dir + ": " + ec.message());
                return false;
            }
        }
    }

#if defined(__linux__)
    // Linux executables cannot embed icons, and Wayland windows only get one
    // through an installed .desktop entry matched by app_id. When the project
    // has an icon, ship it as icon.png plus a ready-made launcher entry.
    fs::path launcherPng = projectRoot / "app_icon.png";
    const std::string appName = getAppName();
    if (!fs::exists(launcherPng, ec)) {
        // Icon removed from the project (or generation failed): drop launcher
        // files from a previous export so a stale .desktop can't keep pointing
        // at an icon the project no longer has.
        fs::remove(config.destinationDir / "icon.png", ec);
        fs::remove(config.destinationDir / (appName + ".desktop"), ec);
    } else {
        const fs::path destAbs = fs::absolute(config.destinationDir, ec);

        fs::copy_file(launcherPng, config.destinationDir / "icon.png", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy icon.png: " + ec.message());
            return false;
        }

        // Exec strings are quoted per the Desktop Entry spec: escape the
        // characters that are special inside double quotes.
        auto quoteExec = [](const std::string& value) {
            std::string out = "\"";
            for (char c : value) {
                if (c == '"' || c == '\\' || c == '`' || c == '$') out += '\\';
                out += c;
            }
            out += "\"";
            return out;
        };

        // getWindowSettings() applies the window-title fallback chain AND
        // strips control characters (a newline in a hand-edited title would
        // split the desktop entry and break parsing).
        std::string displayName = project->getWindowSettings().title;

        std::string entry;
        entry += "# Generated by the Doriax editor export.\n";
        entry += "# Install with: cp \"" + appName + ".desktop\" ~/.local/share/applications/\n";
        entry += "# to add the game to your launcher and give its window an icon on Wayland.\n";
        entry += "[Desktop Entry]\n";
        entry += "Type=Application\n";
        entry += "Name=" + displayName + "\n";
        entry += "Exec=" + quoteExec((destAbs / exeName).string()) + "\n";
        entry += "Path=" + destAbs.string() + "\n";
        entry += "Icon=" + (destAbs / "icon.png").string() + "\n";
        entry += "Terminal=false\n";
        entry += "Categories=Game;\n";
        entry += "StartupWMClass=" + appName + "\n";

        std::ofstream f(config.destinationDir / (appName + ".desktop"), std::ios::binary);
        if (!f) {
            setError("Failed to write " + appName + ".desktop");
            return false;
        }
        f << entry;
    }
#endif

    return true;
}

bool editor::Exporter::writeNativeResourcePack(const fs::path& outputPath, bool& created) {
    setProgressRaw("Packing resources...", 0.95f);

    created = false;
    std::vector<ResourcePackBuildEntry> entries;
    const fs::path projectRoot = getExportProjectRoot();

    std::error_code ec;
    for (const char* rootName : {"assets", "lua"}) {
        const fs::path root = projectRoot / rootName;
        if (!fs::exists(root, ec)) continue;

        for (const auto& file : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            if (!file.is_regular_file()) continue;

            fs::path relPath = fs::relative(file.path(), projectRoot, ec);
            if (ec || relPath.empty()) continue;

            ResourcePackBuildEntry entry;
            entry.path = relPath.generic_string();
            if (entry.path == "assets/game.pak") continue;

            if (!readWholeFile(file.path(), entry.data)) {
                setError("Failed to read resource for pack: " + file.path().string());
                return false;
            }

            size_t hash = std::hash<std::string>{}(entry.path) ^ (entry.data.size() << 1);
            entry.key = static_cast<uint8_t>((hash % 255) + 1);
            entry.shift = entry.data.empty() ? 0 : static_cast<uint32_t>(hash % entry.data.size());

            shiftRight(entry.data, entry.shift);
            applyXor(entry.data, entry.key);

            entries.push_back(std::move(entry));
        }
    }

    if (entries.empty()) {
        fs::remove(outputPath, ec);
        return true;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.path < b.path;
    });

    uint64_t offset = sizeof(RESOURCE_PACK_MAGIC) + 4;
    for (const auto& entry : entries) {
        if (entry.path.size() > UINT16_MAX) {
            setError("Resource path is too long for pack: " + entry.path);
            return false;
        }
        offset += 2 + entry.path.size() + 8 + 8 + 1 + 4;
    }
    for (auto& entry : entries) {
        entry.offset = offset;
        offset += entry.data.size();
    }

    fs::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        setError("Failed to create resource pack directory: " + ec.message());
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        setError("Failed to create resource pack: " + outputPath.string());
        return false;
    }

    out.write(RESOURCE_PACK_MAGIC, sizeof(RESOURCE_PACK_MAGIC));
    writeU32(out, static_cast<uint32_t>(entries.size()));

    for (const auto& entry : entries) {
        writeU16(out, static_cast<uint16_t>(entry.path.size()));
        out.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        writeU64(out, entry.offset);
        writeU64(out, static_cast<uint64_t>(entry.data.size()));
        writeU8(out, entry.key);
        writeU32(out, entry.shift);
    }

    for (const auto& entry : entries) {
        if (!entry.data.empty()) {
            out.write(reinterpret_cast<const char*>(entry.data.data()), static_cast<std::streamsize>(entry.data.size()));
        }
    }

    if (!out) {
        setError("Failed to write resource pack: " + outputPath.string());
        return false;
    }

    created = true;
    return true;
}

bool editor::Exporter::collectSourceResourcePack() {
    const fs::path projectRoot = getExportProjectRoot();
    const fs::path assetsDir = projectRoot / "assets";
    const fs::path luaDir = projectRoot / "lua";
    const fs::path packPath = assetsDir / "game.pak";

    bool packCreated = false;
    if (!writeNativeResourcePack(packPath, packCreated)) {
        return false;
    }

    std::error_code ec;
    if (!packCreated) {
        return true;
    }

    for (const auto& entry : fs::directory_iterator(assetsDir, ec)) {
        if (ec) {
            setError("Failed to read exported assets directory: " + ec.message());
            return false;
        }
        if (entry.path().filename() == "game.pak") {
            continue;
        }

        fs::remove_all(entry.path(), ec);
        if (ec) {
            setError("Failed to remove unpacked asset after packing: " + ec.message());
            return false;
        }
    }

    fs::remove_all(luaDir, ec);
    if (ec) {
        setError("Failed to remove unpacked Lua directory after packing: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::collectWebArtifacts() {
    setProgressRaw("Copying artifacts...", 0.95f);

    const fs::path buildDir = config.targetDir / "build";
    const std::string appName = getAppName();

    std::error_code ec;
    fs::create_directories(config.destinationDir, ec);
    if (ec) {
        setError("Failed to create destination directory: " + ec.message());
        return false;
    }

    for (const char* ext : {".html", ".js", ".wasm"}) {
        fs::path src = buildDir / (appName + ext);
        if (!fs::exists(src, ec)) {
            setError("Built web output not found: " + src.string());
            return false;
        }
        fs::copy_file(src, config.destinationDir / (appName + ext), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy " + src.filename().string() + ": " + ec.message());
            return false;
        }
    }

    // The .data preload bundle exists only when the project has assets or lua.
    fs::path dataSrc = buildDir / (appName + ".data");
    if (fs::exists(dataSrc, ec)) {
        fs::copy_file(dataSrc, config.destinationDir / (appName + ".data"), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy " + dataSrc.filename().string() + ": " + ec.message());
            return false;
        }
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

    editor::getEditorHost().enqueueMainThreadTask([this, &savePromise]() {
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

    // main.cpp is handled separately below. PlatformEditor.* is the GLFW host
    // the editor used to emit into every project; projects created before it
    // was dropped still carry the files, and the exported build globs every
    // *.cpp under the project root, so a stale copy would be compiled.
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

        // Remove the entry point (the exported build compiles the engine's own
        // platform main.cpp, which already defines main()). Generator brackets
        // it with markers because the block is preprocessor-conditional.
        const std::string entryBegin = "// DORIAX_ENTRY_POINT_BEGIN\n";
        const std::string entryEnd = "// DORIAX_ENTRY_POINT_END\n";
        size_t pos = content.find(entryBegin);
        if (pos != std::string::npos) {
            size_t endPos = content.find(entryEnd, pos);
            if (endPos != std::string::npos) {
                endPos += entryEnd.size();
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
        std::ifstream ifs(sceneScriptsSrc, std::ios::in | std::ios::binary);
        if (!ifs) {
            setError("Failed to read generated scene_scripts.cpp");
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        const std::string editorOnlyComment = "// Doriax API headers for this project are provided by .doriax/engine-api; see generated CMakeLists.txt for local and upstream source references.\n";
        const std::string exportComment = "// This file binds scene script metadata to compiled C++ and Lua scripts for the current build configuration.\n";
        size_t commentPos = content.find(editorOnlyComment);
        if (commentPos != std::string::npos) {
            content.replace(commentPos, editorOnlyComment.size(), exportComment);
        }

        FileUtils::writeIfChanged(generatedDst / "scene_scripts.cpp", content);
    }

    return true;
}

bool editor::Exporter::copyAssets() {
    setProgress("Copying assets...", 0.35f);

    fs::path assetsSrc = config.assetsDir;
    if (assetsSrc.empty()) {
        assetsSrc = project->getProjectPath();
    }
    if (assetsSrc.is_relative()) {
        assetsSrc = project->getProjectPath() / assetsSrc;
    }

    std::error_code ec;
    assetsSrc = fs::weakly_canonical(assetsSrc, ec);

    if (!fs::exists(assetsSrc, ec)) {
        setError("Assets directory does not exist: " + assetsSrc.string());
        return false;
    }

    fs::path assetsDst = getExportProjectRoot() / "assets";
    fs::create_directories(assetsDst, ec);

    // References are relative to this directory, so its contents become the export root
    for (auto& entry : fs::recursive_directory_iterator(assetsSrc, fs::directory_options::skip_permission_denied, ec)) {
        fs::path relPath = fs::relative(entry.path(), assetsSrc, ec);
        if (ec || relPath.empty()) continue;

        // Skip hidden directories (starting with '.') and build directories
        std::string firstComponent = relPath.begin()->string();
        if (!firstComponent.empty() && (firstComponent[0] == '.' || firstComponent == "build")) continue;

        // Skip project support files that should not ship as assets
        if (shouldSkipExportSupportFile(relPath)) continue;

        // Skip C++ source/header files; registered scripts ship via copyCppScripts
        if (!entry.is_regular_file() || isCppSourceFile(entry.path())) continue;

        // Skip Lua sources already shipped in the lua tree
        if (isLuaSourceFile(entry.path()) && luaCopiedSources.count(entry.path().lexically_normal())) continue;

        fs::path destPath = assetsDst / relPath;
        fs::create_directories(destPath.parent_path(), ec);
        fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
    }

    return true;
}

bool editor::Exporter::copyLua() {
    setProgress("Copying Lua scripts...", 0.3f);

    // Cleared before the early returns: with no lua tree, assets keeps the Lua files
    luaCopiedSources.clear();

    fs::path luaSrc = config.luaDir;
    if (luaSrc.empty()) {
        return true; // No Lua directory configured, skip
    }
    if (luaSrc.is_relative()) {
        luaSrc = project->getProjectPath() / luaSrc;
    }

    std::error_code ec;
    luaSrc = fs::weakly_canonical(luaSrc, ec);

    if (!fs::exists(luaSrc, ec)) {
        return true; // Lua directory doesn't exist, not an error
    }

    // Created by the copy below, so a project without Lua scripts has no lua directory
    fs::path luaDst = getExportProjectRoot() / "lua";

    // A directory of its own holds only what the scripts need, so all of it ships. With
    // the default "." the Lua root is the project root, where the allowlist filters.
    const bool luaRootIsProjectRoot = (luaSrc == fs::weakly_canonical(project->getProjectPath(), ec));

    for (auto it = fs::recursive_directory_iterator(luaSrc, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        auto& entry = *it;
        fs::path relPath = fs::relative(entry.path(), luaSrc, ec);
        if (ec || relPath.empty()) continue;

        // Skip hidden directories (starting with '.') and build directories
        std::string firstComponent = relPath.begin()->string();
        if (!firstComponent.empty() && (firstComponent[0] == '.' || firstComponent == "build")) { it.disable_recursion_pending(); continue; }

        // Skip project support files that should not ship in the Lua directory
        if (shouldSkipExportSupportFile(relPath)) continue;

        // Skip C++ source/header files; registered scripts ship via copyCppScripts
        if (!entry.is_regular_file() || isCppSourceFile(entry.path())) continue;

        if (luaRootIsProjectRoot && !isLuaExportFile(entry.path())) continue;

        fs::path destPath = luaDst / relPath;
        fs::create_directories(destPath.parent_path(), ec);
        fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) luaCopiedSources.insert(entry.path().lexically_normal());
    }

    return true;
}

bool editor::Exporter::copyCppScripts() {
    setProgress("Copying C++ scripts...", 0.4f);

    fs::path scriptsDst = getExportProjectRoot() / "scripts";
    const fs::path projectRoot = project->getProjectPath();

    std::error_code ec;
    std::set<std::string> copiedPaths;
    const fs::path normalizedProjectRoot = projectRoot.lexically_normal();
    const auto projectRelativePath = [&](const fs::path& path) {
        const fs::path relPath = path.lexically_normal().lexically_relative(normalizedProjectRoot);
        if (relPath.empty() || relPath.is_absolute()
            || (relPath.begin() != relPath.end() && *relPath.begin() == "..")) {
            return fs::path();
        }
        return relPath;
    };

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
                    const fs::path relPath = projectRelativePath(srcPath);
                    if (!relPath.empty()) {
                        fs::path dstPath = scriptsDst / relPath;
                        fs::create_directories(dstPath.parent_path(), ec);
                        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
                    }
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
                    const fs::path relPath = projectRelativePath(hdrPath);
                    if (!relPath.empty()) {
                        fs::path dstPath = scriptsDst / relPath;
                        fs::create_directories(dstPath.parent_path(), ec);
                        fs::copy_file(hdrPath, dstPath, fs::copy_options::overwrite_existing, ec);
                    }
                }
            }
        }
    }

    // Preserve unregistered support headers for project-root-relative includes.
    try {
        for (auto it = fs::recursive_directory_iterator(projectRoot, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& entry = *it;
            const fs::path relPath = projectRelativePath(entry.path());
            if (relPath.empty()) {
                if (entry.is_directory()) it.disable_recursion_pending();
                continue;
            }
            const std::string firstComponent = relPath.begin()->string();

            if (entry.is_directory() && !firstComponent.empty()
                && (firstComponent[0] == '.' || firstComponent == "build")) {
                it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file() || !isCppHeaderFile(entry.path())
                || !copiedPaths.insert(entry.path().string()).second) {
                continue;
            }

            const fs::path dstPath = scriptsDst / relPath;
            fs::create_directories(dstPath.parent_path());
            fs::copy_file(entry.path(), dstPath, fs::copy_options::overwrite_existing);
        }
    } catch (const fs::filesystem_error& e) {
        setError("Failed to copy project headers: " + std::string(e.what()));
        return false;
    }

    // Sources under a script root are compiled without a script component
    // referencing them, so they ship even when nothing registered pulls them in.
    for (const fs::path& scriptDir : project->getScriptDirs()) {
        const fs::path relDir = scriptDirRelativePath(scriptDir);
        if (relDir.empty()) {
            Out::warning("Script directory outside the project is not exported: %s", scriptDir.generic_string().c_str());
            continue;
        }

        const fs::path rootPath = normalizedProjectRoot / relDir;
        if (!fs::is_directory(rootPath, ec)) continue;

        try {
            for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                const std::string name = it->path().filename().string();
                if (it->is_directory() && (name.empty() || name[0] == '.' || name == "build")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file() || !isCppSourceFile(it->path())) continue;

                const fs::path relPath = projectRelativePath(it->path());
                if (relPath.empty() || !copiedPaths.insert(it->path().string()).second) continue;

                const fs::path dstPath = scriptsDst / relPath;
                fs::create_directories(dstPath.parent_path(), ec);
                fs::copy_file(it->path(), dstPath, fs::copy_options::overwrite_existing, ec);
            }
        } catch (const fs::filesystem_error& e) {
            setError("Failed to copy script directory: " + std::string(e.what()));
            return false;
        }
    }

    // Goes to the project root, not scripts/, because that is where the exported
    // CMakeLists includes it from.
    const fs::path userBuildFile = projectRoot / "ProjectBuild.cmake";
    if (fs::is_regular_file(userBuildFile, ec)) {
        fs::copy_file(userBuildFile, getExportProjectRoot() / "ProjectBuild.cmake",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy ProjectBuild.cmake: " + ec.message());
            return false;
        }
    }

    return true;
}

void editor::Exporter::copyTree(const fs::path& src, const fs::path& dst, std::error_code& ec) {
#ifdef _WIN32
    auto getWindowsLongPath = [](const fs::path& path) {
        const fs::path absolutePath = fs::absolute(path);
        const fs::path::string_type nativePath = absolutePath.native();

        if (nativePath.rfind(L"\\\\?\\", 0) == 0) {
            return absolutePath;
        }
        if (nativePath.rfind(L"\\\\", 0) == 0) {
            return fs::path(L"\\\\?\\UNC\\" + nativePath.substr(2));
        }
        return fs::path(L"\\\\?\\" + nativePath);
    };

    // Prefix absolute paths so Win32 APIs used by std::filesystem can traverse
    // directory trees beyond the legacy MAX_PATH limit.
    fs::copy(getWindowsLongPath(src),
             getWindowsLongPath(dst),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
#else
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
#endif
}

void editor::Exporter::copyTreeIfChanged(const fs::path& src, const fs::path& dst, std::error_code& ec, bool pruneStale) {
#ifdef _WIN32
    // Prefix absolute paths so Win32 APIs used by std::filesystem can traverse
    // directory trees beyond the legacy MAX_PATH limit (see copyTree).
    auto adjustPath = [](const fs::path& path) {
        const fs::path absolutePath = fs::absolute(path);
        const fs::path::string_type nativePath = absolutePath.native();

        if (nativePath.rfind(L"\\\\?\\", 0) == 0) {
            return absolutePath;
        }
        if (nativePath.rfind(L"\\\\", 0) == 0) {
            return fs::path(L"\\\\?\\UNC\\" + nativePath.substr(2));
        }
        return fs::path(L"\\\\?\\" + nativePath);
    };
#else
    auto adjustPath = [](const fs::path& path) { return path; };
#endif

    auto filesEqual = [](const fs::path& a, const fs::path& b) -> bool {
        std::ifstream fa(a, std::ios::binary);
        std::ifstream fb(b, std::ios::binary);
        if (!fa || !fb) return false;
        constexpr size_t BUFFER_SIZE = 1 << 16;
        std::vector<char> bufA(BUFFER_SIZE), bufB(BUFFER_SIZE);
        while (true) {
            fa.read(bufA.data(), BUFFER_SIZE);
            fb.read(bufB.data(), BUFFER_SIZE);
            std::streamsize readA = fa.gcount();
            std::streamsize readB = fb.gcount();
            if (readA != readB) return false;
            if (readA == 0) return true;
            if (std::memcmp(bufA.data(), bufB.data(), (size_t)readA) != 0) return false;
        }
    };

    // Unlike copyTree, unchanged destination files are left untouched so they
    // keep their mtime and incremental builds over the tree skip recompiling.
    ec.clear();
    const fs::path srcAdj = adjustPath(src);
    const fs::path dstAdj = adjustPath(dst);
    try {
        fs::create_directories(dstAdj);
        for (const auto& entry : fs::recursive_directory_iterator(srcAdj)) {
            const fs::path target = dstAdj / fs::relative(entry.path(), srcAdj);
            if (entry.is_directory()) {
                fs::create_directories(target);
            } else if (entry.is_regular_file()) {
                if (fs::exists(target)
                    && fs::file_size(target) == entry.file_size()
                    && filesEqual(entry.path(), target)) {
                    continue;
                }
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
            }
        }

        // Mirror semantics for the build cache: drop destination entries the
        // source no longer has, or files removed/renamed in an SDK update
        // would keep being compiled by the exported build's recursive globs.
        if (pruneStale) {
            std::vector<fs::path> stale;
            for (const auto& entry : fs::recursive_directory_iterator(dstAdj)) {
                if (!fs::exists(srcAdj / fs::relative(entry.path(), dstAdj))) {
                    stale.push_back(entry.path());
                }
            }
            for (const fs::path& path : stale) {
                // No-op for children of an already-removed directory.
                fs::remove_all(path);
            }
        }
    } catch (const fs::filesystem_error& e) {
        ec = e.code();
    }
}

bool editor::Exporter::copyEngine() {
    setProgress("Copying engine...", 0.5f);

    fs::path exeDir = FileUtils::getExecutableDir();

    fs::path sdkRoot;
    const std::vector<fs::path> sdkCandidates = {
        FileUtils::getEngineDir(),
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

    auto copyDir = [&](const std::string& name, bool pruneStale) -> bool {
        fs::path src = sdkRoot / name;
        fs::path dst = config.targetDir / name;

        if (!fs::exists(src, ec)) {
            setError(name + " directory not found at: " + src.string());
            return false;
        }
        // Content-compare copy: unchanged files keep their mtime so the
        // Desktop/Web build cache stays incremental across exports.
        copyTreeIfChanged(src, dst, ec, pruneStale);
        if (ec) {
            setError("Failed to copy " + name + " directory: " + ec.message());
            return false;
        }
        return true;
    };

    if (!copyDir("core", true)) return false;
    if (!copyDir("libs", true)) return false;
    if (!copyDir("platform", true)) return false;
    if (!copyDir("renders", true)) return false;
    //if (!copyDir("tools")) return false;
    if (!copyDir("workspaces", true)) return false;

    // The SDK "shaders" dir holds only stub headers (getBase64Shader returning
    // "") under the exact names buildAndSaveShaders generates into. Copy them
    // only when missing: syncing would clobber the previous export's generated
    // headers with stubs just to regenerate them, bumping their mtime and
    // recompiling every TU that includes them on each incremental export.
    {
        fs::path src = sdkRoot / "shaders";
        fs::path dst = config.targetDir / "shaders";
        if (!fs::exists(src, ec)) {
            setError("shaders directory not found at: " + src.string());
            return false;
        }
        fs::create_directories(dst, ec);
        if (ec) {
            setError("Failed to create shaders directory: " + ec.message());
            return false;
        }
        for (const auto& entry : fs::directory_iterator(src)) {
            if (!entry.is_regular_file()) continue;
            fs::path target = dst / entry.path().filename();
            if (fs::exists(target, ec)) continue;
            fs::copy_file(entry.path(), target, ec);
            if (ec) {
                setError("Failed to copy shaders directory: " + ec.message());
                return false;
            }
        }
    }

    fs::path cmakeSrc = sdkRoot / "CMakeLists.txt";
    fs::path cmakeDst = config.targetDir / "CMakeLists.txt";
    if (!fs::exists(cmakeSrc, ec)) {
        setError("CMakeLists.txt not found at: " + cmakeSrc.string());
        return false;
    }

    std::string appName = getAppName();

    // Patch from the SDK source and write with writeIfChanged so an unchanged
    // result keeps its mtime (no spurious reconfigure of the build cache).
    std::ifstream cmakeIfs(cmakeSrc, std::ios::in | std::ios::binary);
    if (!cmakeIfs) {
        setError("Failed to read SDK CMakeLists.txt");
        return false;
    }
    std::string cmakeContent((std::istreambuf_iterator<char>(cmakeIfs)), std::istreambuf_iterator<char>());
    cmakeIfs.close();

    const std::string defaultAppName = "set(APP_NAME doriax-project)";
    const std::string patchedAppName = "set(APP_NAME " + appName + ")";
    const size_t appNamePos = cmakeContent.find(defaultAppName);
    if (appNamePos == std::string::npos) {
        setError("Exported CMakeLists.txt is missing default APP_NAME");
        return false;
    }
    cmakeContent.replace(appNamePos, defaultAppName.size(), patchedAppName);

    // Generate the embedded window/executable icon before the settings marker
    // so DORIAX_WINDOW_ICON is only switched on when the files exist. A bad
    // icon degrades to an iconless export (warned), never a failed one.
    bool iconGenerated = false;
    if (!project->getWindowIcon().empty()) {
        iconGenerated = writeAppIcon();
    }

    const std::string projectSettingsMarker = "# @DORIAX_PROJECT_SETTINGS@";
    const size_t projectSettingsPos = cmakeContent.find(projectSettingsMarker);
    if (projectSettingsPos != std::string::npos) {
        const WindowSettings window = project->getWindowSettings();

        // The title crosses two quoting layers: the CMake string literal here and
        // the C string literal it becomes through add_definitions. Escape for the
        // C level first, then for the CMake level ($ stops variable expansion).
        // Control characters are sanitized upstream in Project::getWindowSettings().
        std::string title;
        for (char c : window.title) {
            if (c == '\\' || c == '"') title += '\\';
            title += c;
        }
        std::string cmakeTitle;
        for (char c : title) {
            if (c == '\\' || c == '"' || c == '$') cmakeTitle += '\\';
            cmakeTitle += c;
        }

        // First line reuses the marker's existing indentation; subsequent lines
        // match the surrounding standalone setup block (4 spaces).
        const std::string indent = "\n    ";
        std::string projectSettings = std::string("set(DORIAX_VSYNC_ENABLED ")
            + (project->isVSyncEnabled() ? "ON" : "OFF") + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_WIDTH " + std::to_string(window.width) + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_HEIGHT " + std::to_string(window.height) + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_MODE " + std::to_string(static_cast<int>(window.mode)) + ")";
        projectSettings += indent + std::string("set(DORIAX_WINDOW_RESIZABLE ") + (window.resizable ? "ON" : "OFF") + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_TITLE \"" + cmakeTitle + "\")";
        if (iconGenerated) {
            projectSettings += indent + "set(DORIAX_WINDOW_ICON ON)";
        }
        cmakeContent.replace(projectSettingsPos, projectSettingsMarker.size(), projectSettings);
    } else {
        Out::warning("Exported CMakeLists.txt is missing the project settings marker; using platform defaults");
    }

    // Script roots reach the export as include directories; their sources come in
    // through the recursive glob once copyCppScripts() has copied them.
    const std::string scriptDirsMarker = "# @DORIAX_SCRIPT_DIRS@";
    const size_t scriptDirsPos = cmakeContent.find(scriptDirsMarker);
    if (scriptDirsPos != std::string::npos) {
        cmakeContent.replace(scriptDirsPos, scriptDirsMarker.size(), buildScriptDirIncludes());
    }

    // Inject per-project HybridArray capacities so the exported build sizes its
    // fixed-capacity arrays to the larger of what the project actually uses and the
    // engine defaults in core/Engine.h (big models grow past the defaults). Missing
    // marker is non-fatal: the build simply falls back to those defaults.
    const std::string maxValuesMarker = "# @DORIAX_SCENE_MAX_VALUES@";
    const size_t maxValuesPos = cmakeContent.find(maxValuesMarker);
    if (maxValuesPos != std::string::npos) {
        cmakeContent.replace(maxValuesPos, maxValuesMarker.size(), buildSceneMaxValuesDefinitions());
    } else {
        Out::warning("Exported CMakeLists.txt is missing the SceneMaxValues marker; using engine default capacities");
    }

    FileUtils::writeIfChanged(cmakeDst, cmakeContent);

    return true;
}

fs::path editor::Exporter::scriptDirRelativePath(const fs::path& scriptDir) const {
    const fs::path projectRoot = project->getProjectPath().lexically_normal();
    const fs::path rootPath = (scriptDir.is_absolute() ? scriptDir : projectRoot / scriptDir).lexically_normal();
    const fs::path relPath = rootPath.lexically_relative(projectRoot);

    if (relPath.empty() || relPath.is_absolute() || *relPath.begin() == "..") {
        return {};
    }
    return relPath;
}

std::string editor::Exporter::buildScriptDirIncludes() const {
    // copyCppScripts() mirrors the project-relative layout under scripts/, so a
    // root keeps its path there.
    std::string dirs;
    for (const fs::path& scriptDir : project->getScriptDirs()) {
        const fs::path relDir = scriptDirRelativePath(scriptDir);
        if (relDir.empty()) {
            continue;
        }

        dirs += "\n        ${PROJECT_ROOT}/scripts";
        if (relDir != ".") {
            dirs += "/" + relDir.generic_string();
        }
    }

    if (dirs.empty()) {
        return {};
    }
    return "target_include_directories(${APP_NAME} PRIVATE" + dirs + "\n    )";
}

std::string editor::Exporter::buildSceneMaxValuesDefinitions() const {
    // Aggregate the per-scene maxima into a single project-wide capacity for each
    // HybridArray. SceneProject::maxValues is refreshed for every scene during
    // loadAndSaveAllScenes(), which always runs before copyEngine().
    SceneMaxValues agg;
    for (const SceneProject& sceneProject : project->getScenes()) {
        agg.maxSubmeshes        = std::max(agg.maxSubmeshes, sceneProject.maxValues.maxSubmeshes);
        agg.maxTilemapTilesRect = std::max(agg.maxTilemapTilesRect, sceneProject.maxValues.maxTilemapTilesRect);
        agg.maxTilemapTiles     = std::max(agg.maxTilemapTiles, sceneProject.maxValues.maxTilemapTiles);
        agg.maxExternalBuffers  = std::max(agg.maxExternalBuffers, sceneProject.maxValues.maxExternalBuffers);
        agg.maxSpriteFrames     = std::max(agg.maxSpriteFrames, sceneProject.maxValues.maxSpriteFrames);
        agg.maxBones            = std::max(agg.maxBones, sceneProject.maxValues.maxBones);
    }

    // Floor each capacity at the engine default declared in core/Engine.h: growing past the
    // default sizes big models correctly, while never shrinking below the known-safe baseline.
    // This matters because the editor-time calc can undercount model-backed meshes (e.g.
    // numExternalBuffers is regenerated at runtime, not serialized) and cannot see content
    // created dynamically by scripts. Passing the macros themselves as the floor keeps these in
    // lockstep with the engine defaults (string literals are not macro-expanded; the bare macro
    // arguments are).
    auto define = [&](const char* macro, unsigned int value, unsigned int engineDefault) {
        return std::string("add_definitions(\"-D") + macro + "=" + std::to_string(std::max(value, engineDefault)) + "\")";
    };

    // First line reuses the marker's existing indentation; subsequent lines are
    // indented to match the surrounding standalone setup block (4 spaces).
    const std::string indent = "\n    ";
    std::string out = define("MAX_SUBMESHES", agg.maxSubmeshes, MAX_SUBMESHES);
    out += indent + define("MAX_TILEMAP_TILESRECT", agg.maxTilemapTilesRect, MAX_TILEMAP_TILESRECT);
    out += indent + define("MAX_TILEMAP_TILES", agg.maxTilemapTiles, MAX_TILEMAP_TILES);
    out += indent + define("MAX_SPRITE_FRAMES", agg.maxSpriteFrames, MAX_SPRITE_FRAMES);
    out += indent + define("MAX_EXTERNAL_BUFFERS", agg.maxExternalBuffers, MAX_EXTERNAL_BUFFERS);
    out += indent + define("MAX_BONES", agg.maxBones, MAX_BONES);
    return out;
}

bool editor::Exporter::writeAppIcon() {
    fs::path iconPath = project->getWindowIcon();
    if (iconPath.is_relative()) {
        iconPath = project->getProjectPath() / iconPath;
    }

    int srcWidth = 0, srcHeight = 0, srcChannels = 0;
    unsigned char* srcPixels = stbi_load(iconPath.string().c_str(), &srcWidth, &srcHeight, &srcChannels, 4);
    if (!srcPixels) {
        Out::warning("Project icon could not be loaded, exporting without icon: %s", iconPath.string().c_str());
        return false;
    }

    const int maxDimension = std::max(srcWidth, srcHeight);

    // RGBA at a given square size, resized from the source (stretch: icons are
    // expected to be square). Gamma-aware so downscaled icons keep their tone.
    auto makeSize = [&](int size) -> std::vector<unsigned char> {
        std::vector<unsigned char> pixels((size_t)size * size * 4);
        if (size == srcWidth && size == srcHeight) {
            memcpy(pixels.data(), srcPixels, pixels.size());
        } else {
            stbir_resize_uint8_srgb(srcPixels, srcWidth, srcHeight, 0,
                                    pixels.data(), size, size, 0, STBIR_RGBA);
        }
        return pixels;
    };

    // Skip sizes larger than the source (upscaling only blurs); keep at least one.
    auto filterSizes = [&](std::initializer_list<int> wanted) {
        std::vector<int> sizes;
        for (int size : wanted) {
            if (size <= maxDimension) sizes.push_back(size);
        }
        if (sizes.empty()) sizes.push_back(*wanted.begin());
        return sizes;
    };

    // --- app_icon.h: RGBA images the window backends set at startup ---
    const std::vector<int> runtimeSizes = filterSizes({16, 32, 64, 128});
    std::string header;
    header += "// Generated by the Doriax editor export from the project icon. Do not edit.\n";
    header += "#ifndef DORIAX_APP_ICON_H\n#define DORIAX_APP_ICON_H\n\n";
    header += "typedef struct DoriaxAppIconImage { int width; int height; const unsigned char* pixels; } DoriaxAppIconImage;\n\n";
    for (int size : runtimeSizes) {
        std::vector<unsigned char> pixels = makeSize(size);
        header += "static const unsigned char doriax_app_icon_" + std::to_string(size) + "[] = {";
        char buf[8];
        for (size_t i = 0; i < pixels.size(); i++) {
            if (i % 32 == 0) header += "\n";
            snprintf(buf, sizeof(buf), "%u,", pixels[i]);
            header += buf;
        }
        header += "\n};\n\n";
    }
    header += "static const DoriaxAppIconImage DORIAX_APP_ICON_IMAGES[] = {\n";
    for (int size : runtimeSizes) {
        std::string s = std::to_string(size);
        header += "    {" + s + ", " + s + ", doriax_app_icon_" + s + "},\n";
    }
    header += "};\n\n";
    header += "#define DORIAX_APP_ICON_COUNT " + std::to_string(runtimeSizes.size()) + "\n\n";
    header += "#endif // DORIAX_APP_ICON_H\n";

    // --- app_icon.ico: PNG-compressed entries, embedded into the Windows exe ---
    const std::vector<int> icoSizes = filterSizes({16, 32, 48, 256});
    std::string ico;
    std::vector<std::string> icoPngs;
    auto appendPng = [](void* context, void* data, int size) {
        ((std::string*)context)->append((const char*)data, (size_t)size);
    };
    for (int size : icoSizes) {
        std::vector<unsigned char> pixels = makeSize(size);
        std::string png;
        if (!stbi_write_png_to_func(appendPng, &png, size, size, 4, pixels.data(), size * 4)) {
            Out::warning("Failed to encode project icon at %dx%d, exporting without icon", size, size);
            stbi_image_free(srcPixels);
            return false;
        }
        icoPngs.push_back(std::move(png));
    }

    // --- app_icon.png: launcher-size PNG for the Linux .desktop entry ---
    std::string launcherPng;
    {
        const int launcherSize = std::min(maxDimension, 256);
        std::vector<unsigned char> pixels = makeSize(launcherSize);
        if (!stbi_write_png_to_func(appendPng, &launcherPng, launcherSize, launcherSize, 4, pixels.data(), launcherSize * 4)) {
            Out::warning("Failed to encode project icon at %dx%d, exporting without icon", launcherSize, launcherSize);
            stbi_image_free(srcPixels);
            return false;
        }
    }
    stbi_image_free(srcPixels);

    auto appendU16 = [&ico](uint16_t value) {
        ico += (char)(value & 0xFF);
        ico += (char)((value >> 8) & 0xFF);
    };
    auto appendU32 = [&ico](uint32_t value) {
        ico += (char)(value & 0xFF);
        ico += (char)((value >> 8) & 0xFF);
        ico += (char)((value >> 16) & 0xFF);
        ico += (char)((value >> 24) & 0xFF);
    };

    appendU16(0);                            // reserved
    appendU16(1);                            // type: icon
    appendU16((uint16_t)icoSizes.size());    // image count
    uint32_t dataOffset = 6 + 16 * (uint32_t)icoSizes.size();
    for (size_t i = 0; i < icoSizes.size(); i++) {
        int size = icoSizes[i];
        ico += (char)(size >= 256 ? 0 : size); // width (0 = 256)
        ico += (char)(size >= 256 ? 0 : size); // height
        ico += (char)0;                        // palette colors
        ico += (char)0;                        // reserved
        appendU16(1);                          // color planes
        appendU16(32);                         // bits per pixel
        appendU32((uint32_t)icoPngs[i].size());
        appendU32(dataOffset);
        dataOffset += (uint32_t)icoPngs[i].size();
    }
    for (const std::string& png : icoPngs) {
        ico += png;
    }

    const fs::path projectRoot = getExportProjectRoot();
    std::error_code ec;
    fs::create_directories(projectRoot, ec);

    {
        std::ofstream f(projectRoot / "app_icon.h", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.h, exporting without icon");
            return false;
        }
        f << header;
    }
    {
        std::ofstream f(projectRoot / "app_icon.ico", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.ico, exporting without icon");
            return false;
        }
        f.write(ico.data(), (std::streamsize)ico.size());
    }
    {
        // Resource paths are relative to the .rc file, which sits next to the .ico.
        std::ofstream f(projectRoot / "app_icon.rc", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.rc, exporting without icon");
            return false;
        }
        f << "1 ICON \"app_icon.ico\"\n";
    }
    {
        std::ofstream f(projectRoot / "app_icon.png", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.png, exporting without icon");
            return false;
        }
        f.write(launcherPng.data(), (std::streamsize)launcherPng.size());
    }

    return true;
}

bool editor::Exporter::buildAndSaveShaders() {
    setProgress("Building shaders...", 0.6f);

    if (config.selectedShaderKeys.empty()) {
        setError("No shaders selected");
        return false;
    }

    // Drop retired/reserved property bits so keys collected before a feature bit was
    // retired collapse onto their current equivalent (the std::set dedupes them),
    // instead of exporting stale duplicate variants with '?' in their names.
    {
        std::set<ShaderKey> normalizedKeys;
        for (const ShaderKey& key : config.selectedShaderKeys) {
            normalizedKeys.insert(ShaderPool::normalizeKey(key));
        }
        config.selectedShaderKeys = std::move(normalizedKeys);
    }

    // Shader-only generation writes straight into the requested dir
    fs::path shadersDst = project ? config.targetDir / "shaders" : config.targetDir;

    std::error_code ec;
    fs::create_directories(shadersDst, ec);
    if (ec) {
        setError("Failed to create shaders directory: " + ec.message());
        return false;
    }

    shadersDst = fs::absolute(shadersDst, ec);
    if (ec) {
        setError("Failed to resolve shaders directory: " + ec.message());
        return false;
    }

    // Default to glsl410 if no backend was selected
    std::vector<ShaderBackend> backends(config.selectedBackends.begin(), config.selectedBackends.end());
    if (backends.empty()) {
        backends.push_back(ShaderBackend::GLCore);
    }

    int total = (int)config.selectedShaderKeys.size() * backends.size();
    int current = 0;
    bool hadFailure = false;
    std::map<std::string, std::vector<ShaderHeaderBuilder::HeaderShader>> headerShaders;

    for (const ShaderKey& shaderKey : config.selectedShaderKeys) {
        ShaderType type = ShaderPool::getShaderTypeFromKey(shaderKey);
        uint32_t props = ShaderPool::getPropertiesFromKey(shaderKey);
        uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);
        std::string shaderStr = ShaderPool::getShaderStr(type, props, customId);

        for (ShaderBackend backend : backends) {
            float shaderProgress = 0.6f + (0.3f * (float)current / (float)std::max(total, 1));
            std::string fmtStr = ShaderPool::getShaderLangStr(backend);
            setProgress("Building shader: " + shaderStr + " (" + fmtStr + ")", shaderProgress);

            try {
                // Synchronous build without cache
                ShaderData resultData = shaderBuilder.buildShaderForExport(shaderKey, project, backend);

                std::string basename = shaderStr + "_" + fmtStr;
                std::string err;

                // Persist with the storage key (customId stripped): the runtime assigns
                // its own session-local id, so the embedded key must not depend on it.
                ShaderKey storageKey = ShaderPool::getStorageKey(shaderKey);

                if (config.shaderOutputFormat == ShaderOutputFormat::Header) {
                    std::vector<unsigned char> sdatBytes;
                    if (!ShaderDataSerializer::writeToBytes(sdatBytes, storageKey, resultData, &err)) {
                        Out::warning("Failed to encode shader %s: %s", basename.c_str(), err.c_str());
                        hadFailure = true;
                    } else {
                        headerShaders[fmtStr].push_back({basename, Base64::encode(sdatBytes.data(), sdatBytes.size())});
                    }
                } else if (config.shaderOutputFormat == ShaderOutputFormat::Json) {
                    std::string filename = basename + ".json";
                    fs::path outputPath = shadersDst / filename;
                    if (!ShaderDataSerializer::writeJsonToFile(outputPath.string(), storageKey, resultData, &err)) {
                        Out::warning("Failed to save shader %s: %s", filename.c_str(), err.c_str());
                        hadFailure = true;
                    }
                } else {
                    std::string filename = basename + ".sdat";
                    fs::path outputPath = shadersDst / filename;
                    if (!ShaderDataSerializer::writeToFile(outputPath.string(), storageKey, resultData, &err)) {
                        Out::warning("Failed to save shader %s: %s", filename.c_str(), err.c_str());
                        hadFailure = true;
                    }
                }
            } catch (const std::exception& e) {
                Out::warning("Failed to build shader %s (%s): %s", shaderStr.c_str(), fmtStr.c_str(), e.what());
                hadFailure = true;
            }

            current++;
        }
    }

    if (config.shaderOutputFormat == ShaderOutputFormat::Header) {
        setProgress("Writing shader headers...", 0.95f);
        for (ShaderBackend backend : backends) {
            const std::string fmtStr = ShaderPool::getShaderLangStr(backend);
            std::string err;
            const auto it = headerShaders.find(fmtStr);
            const std::vector<ShaderHeaderBuilder::HeaderShader> emptyShaders;
            const std::vector<ShaderHeaderBuilder::HeaderShader>& shaders = it == headerShaders.end() ? emptyShaders : it->second;
            if (!ShaderHeaderBuilder::writeShaderHeader(shadersDst / (fmtStr + ".h"), fmtStr, shaders, err)) {
                Out::warning("Failed to write shader header %s.h: %s", fmtStr.c_str(), err.c_str());
                hadFailure = true;
            }
        }
    }

    if (hadFailure) {
        setError("One or more shaders failed to build or save");
        return false;
    }

    return true;
}

// Static helpers for UI display

std::string editor::Exporter::getShaderDisplayName(ShaderType type, uint32_t properties, uint16_t customId) {
    std::string name = ShaderPool::getShaderTypeName(type);

    // Distinguish forked shaders (same type/props, different source) in the list.
    std::string customName = ShaderPool::getCustomShaderName(customId);
    if (!customName.empty()) {
        name += " [" + customName + "]";
    }

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

std::string editor::Exporter::getCMakeGraphicBackend(::doriax::ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::GLES3:      return "gles3";
        case ShaderBackend::D3D11:      return "d3d11";
        case ShaderBackend::MetalMacOS:
        case ShaderBackend::MetalIOS:   return "metal";
        case ShaderBackend::Vulkan:     return "vulkan";
        default:                        return "glcore";
    }
}

editor::EmsdkInfo editor::Exporter::detectEmsdk(const std::string& overridePath) {
    EmsdkInfo info;

#ifdef _WIN32
    const char* emcmakeName = "emcmake.bat";
#else
    const char* emcmakeName = "emcmake";
#endif

    auto tryRoot = [&](const fs::path& root, const std::string& source) -> bool {
        if (root.empty()) return false;
        std::error_code ec;
        // Accept both the emsdk root and the emscripten dir pointed at directly.
        const fs::path candidates[] = {
            root / "upstream" / "emscripten" / emcmakeName,
            root / emcmakeName
        };
        for (const fs::path& candidate : candidates) {
            if (fs::exists(candidate, ec)) {
                info.found = true;
                info.emcmake = candidate.string();
                info.description = "from " + source;
                return true;
            }
        }
        return false;
    };

    // An explicit override is an explicit choice: when set, it alone decides.
    if (!overridePath.empty()) {
        tryRoot(fs::path(overridePath), "configured path");
        return info;
    }

    const char* emsdkEnv = std::getenv("EMSDK");
    if (emsdkEnv && tryRoot(fs::path(emsdkEnv), "EMSDK environment variable")) {
        return info;
    }

#ifdef _WIN32
    if (!CommandRunner::runCaptureNoWindow("where emcmake.bat 2>nul").empty()) {
        info.found = true;
        info.emcmake = "emcmake";
        info.description = "on PATH";
    }
#else
    if (std::system("command -v emcmake >/dev/null 2>&1") == 0) {
        info.found = true;
        info.emcmake = "emcmake";
        info.description = "on PATH";
    }
#endif

    return info;
}
