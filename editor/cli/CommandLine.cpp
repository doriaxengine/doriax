// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CommandLine.h"

#include "AppSettings.h"
#include "Doriax.h"
#include "EditorHost.h"
#include "Exporter.h"
#include "Out.h"
#include "Platform.h"
#include "Project.h"
#include "pool/ShaderPool.h"
#include "shader/ShaderBuilder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
    #include <cstdio>
#endif

namespace doriax::editor {

using ::doriax::Engine;
using ::doriax::ShaderBackend;
using ::doriax::ShaderBuildResult;
using ::doriax::ShaderKey;
using ::doriax::ShaderPool;
using ::doriax::ShaderType;

namespace fs = std::filesystem;

struct ExportCliOptions {
    fs::path projectPath;
    fs::path targetDir;
    fs::path assetsDir;
    fs::path luaDir;
    std::string startScene;
    std::set<ShaderBackend> backends;
    std::set<ShaderKey> shaderKeys;
    bool help = false;
    bool listScenes = false;
};

struct ShaderCliOptions {
    fs::path targetDir;
    std::set<ShaderBackend> backends;
    std::set<ShaderKey> shaderKeys;
    ShaderOutputFormat outputFormat = ShaderOutputFormat::Binary;
    bool help = false;
};

struct EngineRuntimeGuard {
    bool initialized = false;
    ~EngineRuntimeGuard() {
        if (!initialized) {
            return;
        }
        ShaderPool::setShaderBuilder(nullptr);
        ShaderBuilder::requestShutdown();
        Engine::systemShutdown();
    }
};

struct EditorHostOverrideGuard {
    EditorHost* previousHost = nullptr;
    explicit EditorHostOverrideGuard(EditorHost* host)
        : previousHost(&getEditorHost()) {
        setEditorHost(host);
    }
    ~EditorHostOverrideGuard() {
        setEditorHost(previousHost);
    }
};

static std::string normalizeToken(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

static std::vector<std::string> splitList(const std::string& value) {
    std::string normalized = value;
    for (char& ch : normalized) {
        if (ch == '+' || ch == '|' || ch == ';') {
            ch = ',';
        }
    }

    std::vector<std::string> parts;
    std::stringstream stream(normalized);
    std::string part;
    while (std::getline(stream, part, ',')) {
        size_t start = part.find_first_not_of(" \t\n\r");
        size_t end = part.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) {
            continue;
        }
        parts.push_back(part.substr(start, end - start + 1));
    }
    return parts;
}

static bool parseUInt32(const std::string& value, uint32_t& out) {
    if (value.empty() || value[0] == '+' || value[0] == '-') {
        return false;
    }

    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value.c_str(), &end, 0);
    if (!end || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    out = static_cast<uint32_t>(parsed);
    return true;
}

static bool isAllBackendsToken(const std::string& value) {
    return normalizeToken(value) == "all";
}

static bool parseShaderOutputFormat(const std::string& value, ShaderOutputFormat& out) {
    const std::string token = normalizeToken(value);
    if (token == "binary" || token == "sdat") {
        out = ShaderOutputFormat::Binary;
        return true;
    }
    if (token == "header") {
        out = ShaderOutputFormat::Header;
        return true;
    }
    if (token == "json") {
        out = ShaderOutputFormat::Json;
        return true;
    }
    return false;
}

static bool parseShaderTypeName(const std::string& value, ShaderType& out) {
    const std::string token = normalizeToken(value);
    if (token == "points" || token == "point")  { out = ShaderType::POINTS; return true; }
    if (token == "lines"  || token == "line")   { out = ShaderType::LINES;  return true; }
    if (token == "mesh")                        { out = ShaderType::MESH;   return true; }
    if (token == "sky"    || token == "skybox") { out = ShaderType::SKYBOX; return true; }
    if (token == "depth")                       { out = ShaderType::DEPTH;  return true; }
    if (token == "gbuffer")                     { out = ShaderType::GBUFFER; return true; }
    if (token == "ui")                          { out = ShaderType::UI;     return true; }
    if (token == "ssao")                        { out = ShaderType::SSAO;   return true; }
    if (token == "ssaoblur")                    { out = ShaderType::SSAO_BLUR; return true; }
    if (token == "ssr")                         { out = ShaderType::SSR;    return true; }
    if (token == "ssrblur")                     { out = ShaderType::SSR_BLUR; return true; }
    if (token == "composite")                   { out = ShaderType::COMPOSITE; return true; }
    if (token == "shadow2d")                    { out = ShaderType::SHADOW2D; return true; }
    if (token == "blit")                        { out = ShaderType::BLIT;   return true; }
    if (token == "postprocess" || token == "post") { out = ShaderType::POSTPROCESS; return true; }
    if (token == "water")                       { out = ShaderType::WATER;  return true; }
    if (token == "underwater")                  { out = ShaderType::UNDERWATER; return true; }
    if (token == "probefilter")                 { out = ShaderType::PROBE_FILTER; return true; }
    return false;
}

static bool parseShaderSpec(const std::string& value, ShaderKey& out, std::string& error) {
    const size_t separator = value.find(':');
    const std::string typePart  = separator == std::string::npos ? value : value.substr(0, separator);
    const std::string propsPart = separator == std::string::npos ? ""    : value.substr(separator + 1);

    ShaderType type;
    if (!parseShaderTypeName(typePart, type)) {
        error = "Unknown shader type: " + typePart;
        return false;
    }

    uint32_t properties = 0;
    if (!propsPart.empty()) {
        const bool numericMask =
            std::isdigit(static_cast<unsigned char>(propsPart[0])) ||
            propsPart.rfind("0x", 0) == 0 ||
            propsPart.rfind("0X", 0) == 0;

        if (numericMask) {
            if (!parseUInt32(propsPart, properties)) {
                error = "Invalid shader property mask: " + propsPart;
                return false;
            }
        } else {
            for (const std::string& prop : splitList(propsPart)) {
                const int bit = ShaderPool::getShaderPropertyBit(type, prop);
                if (bit < 0) {
                    error = "Unknown shader property '" + prop + "' for shader type " + typePart;
                    return false;
                }
                properties |= (1u << bit);
            }
        }
    }

    out = ShaderPool::getShaderKey(type, properties);
    return true;
}

static void addAllSupportedBackends(std::set<ShaderBackend>& backends) {
    const std::vector<ShaderBackend>& all = ShaderPool::getShaderBackends();
    backends.insert(all.begin(), all.end());
}

static bool parseBackendList(const std::string& value, std::set<ShaderBackend>& backends, std::string& error) {
    for (const std::string& name : splitList(value)) {
        if (isAllBackendsToken(name)) {
            addAllSupportedBackends(backends);
            continue;
        }

        ShaderBackend backend;
        if (!ShaderPool::parseShaderBackend(name, backend)) {
            error = "Unknown graphic backend: " + name;
            return false;
        }
        backends.insert(backend);
    }
    return true;
}

static std::string getCommandName(const char* executableName) {
    if (!executableName || !*executableName) {
        return "doriax-editor";
    }

    const fs::path executablePath(executableName);
    const std::string filename = executablePath.filename().string();
    return filename.empty() ? std::string("doriax-editor") : filename;
}

static void printMainUsage(const std::string& commandName) {
    std::cout
        << "Usage:\n"
        << "  " << commandName << "                          Open the editor.\n"
        << "  " << commandName << " export [options]         Export a project.\n"
        << "  " << commandName << " shaders [options]        Generate shaders.\n"
        << "  " << commandName << " benchmark [options]      Measure scene FPS with vsync off.\n\n"
        << "Run a subcommand with --help for its options.\n";
}

static void printUsage(const std::string& commandName) {
    std::cout
        << "Usage:\n"
        << "  " << commandName << " export --project <project-dir|project.yaml> --out <dir> [options]\n\n"
        << "Options:\n"
        << "  -p, --project <path>        Project directory or project.yaml file.\n"
        << "  -o, --out <path>            Destination directory. Must be empty.\n"
        << "      --assets <path>         Asset directory, relative to the project or absolute.\n"
        << "      --lua <path>            Lua directory, relative to the project or absolute.\n"
        << "      --start-scene <id|name> Start scene override.\n"
        << "      --backend <list>        opengl, opengles, d3d11, metal-macos, metal-ios, vulkan, all. Can repeat.\n"
        << "      --shader <spec>         Shader type and optional properties, e.g. mesh:Uv1,Nor. Can repeat.\n"
        << "      --list-scenes           Print project scenes and exit.\n"
        << "  -h, --help                  Show this help.\n\n"
        << "If no --backend is provided, all supported backends are used.\n"
        << "If no --shader is provided, shaders discovered while regenerating scenes are exported.\n"
        << "For standalone shader generation, use `" << commandName << " shaders`.\n";
}

static void printBenchmarkUsage(const std::string& commandName) {
    std::cout
        << "Usage:\n"
        << "  " << commandName << " benchmark --project <project-dir> --scene <scene> [options]\n\n"
        << "Options:\n"
        << "  -p, --project <path>   Project directory or project.yaml file.\n"
        << "  -s, --scene <scene>    Scene file or scene name. Default: the project's start scene.\n"
        << "      --frames <n>       Measured frames after warmup. Default: 180.\n"
        << "      --warmup <n>       Warmup frames after resources load. Default: 90.\n"
        << "      --mesh-lod <on|off>      Override the scene's mesh LOD setting.\n"
        << "      --depth-prepass <on|off> Override the scene's depth prepass setting.\n"
        << "      --name <label>     Label written into the JSON result.\n"
        << "      --out <path>       Write JSON to this file in addition to stdout.\n"
        << "  -h, --help             Show this help.\n\n"
        << "Vsync is forced off. The scene camera is used for the capture.\n";
}

static void printShadersUsage(const std::string& commandName) {
    std::cout
        << "Usage:\n"
        << "  " << commandName << " shaders --out <dir> --shader <spec> [options]\n\n"
        << "Options:\n"
        << "  -o, --out <path>            Destination directory for generated shader files.\n"
        << "      --format <format>       binary, header, or json. Default: binary.\n"
        << "      --backend <list>        opengl, opengles, d3d11, metal-macos, metal-ios, vulkan, all. Can repeat.\n"
        << "      --shader <spec>         Shader type and optional properties, e.g. mesh:Uv1,Nor. Can repeat.\n"
        << "  -h, --help                  Show this help.\n\n"
        << "If no --backend is provided, all supported backends are used.\n"
        << "Shader output is written directly to <out>.\n";
}

static bool requireValue(int argc, char** argv, int& index, std::string& value, std::string& error) {
    if (index + 1 >= argc) {
        error = std::string("Missing value for ") + argv[index];
        return false;
    }
    value = argv[++index];
    return true;
}

static bool parseArgs(int argc, char** argv, ExportCliOptions& options, std::string& error) {
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else if (arg == "-p" || arg == "--project") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.projectPath = value;
        } else if (arg == "-o" || arg == "--out" || arg == "--target") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.targetDir = value;
        } else if (arg == "--assets") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.assetsDir = value;
        } else if (arg == "--lua") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.luaDir = value;
        } else if (arg == "--start-scene") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.startScene = value;
        } else if (arg == "--backend") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            if (!parseBackendList(value, options.backends, error)) return false;
        } else if (arg == "--shader") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            ShaderKey key;
            if (!parseShaderSpec(value, key, error)) {
                return false;
            }
            options.shaderKeys.insert(key);
        } else if (arg == "--list-scenes") {
            options.listScenes = true;
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }
    }

    if (options.help) {
        return true;
    }
    if (options.projectPath.empty()) {
        error = "Missing required --project path.";
        return false;
    }
    if (options.targetDir.empty() && !options.listScenes) {
        error = "Missing required --out path.";
        return false;
    }
    if (options.backends.empty()) {
        addAllSupportedBackends(options.backends);
    }

    return true;
}

static bool parseShadersArgs(int argc, char** argv, ShaderCliOptions& options, std::string& error) {
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else if (arg == "-o" || arg == "--out" || arg == "--target") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            options.targetDir = value;
        } else if (arg == "--format" || arg == "--output-format") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            if (!parseShaderOutputFormat(value, options.outputFormat)) {
                error = "Unknown shader output format: " + value;
                return false;
            }
        } else if (arg == "--backend") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            if (!parseBackendList(value, options.backends, error)) return false;
        } else if (arg == "--shader") {
            if (!requireValue(argc, argv, i, value, error)) return false;
            ShaderKey key;
            if (!parseShaderSpec(value, key, error)) {
                return false;
            }
            options.shaderKeys.insert(key);
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }
    }

    if (options.help) {
        return true;
    }
    if (options.targetDir.empty()) {
        error = "Missing required --out path.";
        return false;
    }
    if (options.shaderKeys.empty()) {
        error = "Missing required --shader spec.";
        return false;
    }
    if (options.backends.empty()) {
        addAllSupportedBackends(options.backends);
    }

    return true;
}

// absolute, as the project joins relative paths onto its folder
static fs::path normalizeProjectPath(const fs::path& path) {
    fs::path projectPath = fs::absolute(path);
    if (projectPath.filename() == "project.yaml") {
        return projectPath.parent_path();
    }
    return projectPath;
}

static uint32_t resolveStartSceneId(const Project& project, const std::string& requested) {
    if (requested.empty()) {
        return project.getStartSceneId();
    }

    uint32_t parsedId = 0;
    if (parseUInt32(requested, parsedId)) {
        return parsedId;
    }

    const std::string requestedToken = normalizeToken(requested);
    for (const auto& scene : project.getScenes()) {
        if (normalizeToken(scene.name) == requestedToken ||
            normalizeToken(scene.filepath.stem().string()) == requestedToken ||
            normalizeToken(scene.filepath.string()) == requestedToken) {
            return scene.id;
        }
    }

    return 0;
}

static void printScenes(const Project& project) {
    for (const auto& scene : project.getScenes()) {
        std::cout << scene.id << "\t" << scene.name << "\t"
                  << scene.filepath.generic_string() << "\n";
    }
}

// On Windows the editor binary is linked with the GUI subsystem, so stdout
// and stderr are not connected to the parent console. Attaching to the parent
// console when running a CLI subcommand restores normal terminal behavior.
static void attachHostConsoleIfNeeded() {
#if defined(_WIN32)
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stdoutFile = nullptr;
        FILE* stderrFile = nullptr;
        FILE* stdinFile = nullptr;
        (void)freopen_s(&stdoutFile, "CONOUT$", "w", stdout);
        (void)freopen_s(&stderrFile, "CONOUT$", "w", stderr);
        (void)freopen_s(&stdinFile,  "CONIN$",  "r", stdin);
    }

    // The output is UTF-8, the shell gets its code page back on exit
    static const UINT shellCodePage = GetConsoleOutputCP();
    if (shellCodePage != CP_UTF8 && SetConsoleOutputCP(CP_UTF8)) {
        std::atexit([]() { SetConsoleOutputCP(shellCodePage); });
    }
#endif
}

int CommandLine::runExportCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    // Backend translation units define a static `App` instance that registers
    // itself as the active EditorHost before main(). The export subcommand must
    // override that and use the headless default host so project loading does
    // not touch GUI-only windows like ResourcesWindow.
    EditorHostOverrideGuard hostOverride(nullptr);

    const std::string commandName = getCommandName(executableName);

    ExportCliOptions options;
    std::string error;
    if (!parseArgs(argc, argv, options, error)) {
        std::cerr << commandName << " export: " << error << "\n\n";
        printUsage(commandName);
        return 2;
    }

    if (options.help) {
        printUsage(commandName);
        return 0;
    }

    Out::setOutputWindow(nullptr);
    AppSettings::initialize();

    Project project;
    auto platform = std::make_unique<Platform>(&project);
    EngineRuntimeGuard engineRuntime;
    Engine::systemInit(argc, argv, platform.get());
    engineRuntime.initialized = true;
    Engine::pauseGameEvents(true);
    Engine::setAsyncLoading(true);

    ShaderPool::setShaderBuilder([projectPtr = &project](ShaderKey shaderKey) -> ShaderBuildResult {
        static ShaderBuilder builder;
        return builder.buildShader(shaderKey, projectPtr);
    });

    const fs::path projectPath = normalizeProjectPath(options.projectPath);
    if (!project.loadProject(projectPath, false)) {
        return 1;
    }

    if (options.listScenes) {
        printScenes(project);
        return 0;
    }

    ExportConfig config;
    config.targetDir = fs::absolute(options.targetDir);
    config.assetsDir = options.assetsDir.empty() ? project.getAssetsDir() : options.assetsDir;
    config.luaDir    = options.luaDir.empty()    ? project.getLuaDir()    : options.luaDir;
    config.startSceneId = resolveStartSceneId(project, options.startScene);
    config.packNativeResources = project.shouldPackNativeResources();
    config.selectedBackends   = options.backends;
    config.shaderKeysOverride = options.shaderKeys;
    // saved overrides apply unless --shader gives the exact list
    if (options.shaderKeys.empty()) {
        config.shaderAdditions = project.getShaderOverrides().additions;
        config.shaderExclusions = project.getShaderOverrides().exclusions;
    }

    if (!options.startScene.empty() && config.startSceneId == 0) {
        Out::warning("Start scene not found: %s", options.startScene.c_str());
    }

    Exporter exporter;
    const bool success = exporter.exportProject(&project, config);
    const ExportProgress progress = exporter.getProgress();

    if (!success) {
        if (!progress.errorMessage.empty()) {
            std::cerr << commandName << " export: " << progress.errorMessage << "\n";
        }
        return 1;
    }

    return 0;
}

int CommandLine::runShadersCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    EditorHostOverrideGuard hostOverride(nullptr);

    const std::string commandName = getCommandName(executableName);

    ShaderCliOptions options;
    std::string error;
    if (!parseShadersArgs(argc, argv, options, error)) {
        std::cerr << commandName << " shaders: " << error << "\n\n";
        printShadersUsage(commandName);
        return 2;
    }

    if (options.help) {
        printShadersUsage(commandName);
        return 0;
    }

    Out::setOutputWindow(nullptr);

    Project project;
    auto platform = std::make_unique<Platform>(&project);
    EngineRuntimeGuard engineRuntime;
    Engine::systemInit(argc, argv, platform.get());
    engineRuntime.initialized = true;
    Engine::pauseGameEvents(true);
    Engine::setAsyncLoading(true);

    ExportConfig config;
    config.targetDir = options.targetDir;
    config.shaderOutputFormat = options.outputFormat;
    config.selectedBackends = options.backends;
    config.selectedShaderKeys = options.shaderKeys;

    Exporter exporter;
    const bool success = exporter.generateShaders(config);
    const ExportProgress progress = exporter.getProgress();

    if (!success) {
        if (!progress.errorMessage.empty()) {
            std::cerr << commandName << " shaders: " << progress.errorMessage << "\n";
        }
        return 1;
    }

    return 0;
}

int CommandLine::runBenchmarkHelp(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();
    printBenchmarkUsage(getCommandName(executableName));
    (void)argc;
    (void)argv;
    return 0;
}

int CommandLine::runHelpCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    const std::string commandName = getCommandName(executableName);
    const std::string argument = argc >= 1 ? argv[0] : "";

    if (argument == "-h" || argument == "--help" || argument == "help") {
        printMainUsage(commandName);
        return 0;
    }

    std::cerr << commandName << ": unknown argument '" << argument << "'\n\n";
    printMainUsage(commandName);
    return 2;
}

}
