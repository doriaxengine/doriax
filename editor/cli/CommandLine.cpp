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
#include "logic/LogicGraphCompiler.h"
#include "logic/LogicGraphJson.h"
#include "shader/graph/ShaderGraphJson.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    // parsed again once the project has loaded, which resets the custom shader registry
    std::vector<std::string> shaderSpecs;
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
    if (token == "bloomdown")                   { out = ShaderType::BLOOM_DOWNSAMPLE; return true; }
    if (token == "bloomup")                     { out = ShaderType::BLOOM_UPSAMPLE; return true; }
    if (token == "tonemap")                     { out = ShaderType::TONEMAP; return true; }
    return false;
}

static bool parseShaderSpec(const std::string& spec, ShaderKey& out, std::string& error) {
    // a shader file follows '@'
    const size_t customSeparator = spec.find('@');
    const std::string value = spec.substr(0, customSeparator);
    const std::string shaderFile = customSeparator == std::string::npos ? "" : spec.substr(customSeparator + 1);
    if (customSeparator != std::string::npos && shaderFile.empty()) {
        error = "Missing shader file after '@' in " + spec;
        return false;
    }

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

    out = ShaderPool::getShaderKey(type, properties, ShaderPool::registerCustomShader(shaderFile));
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
        << "  " << commandName << " --project <path>         Open the editor on a project.\n"
        << "  " << commandName << " export [options]         Export a project.\n"
        << "  " << commandName << " ide --project <path>     Write the CMake and IDE files of a project.\n"
        << "  " << commandName << " shaders [options]        Generate shaders.\n"
        << "  " << commandName << " benchmark [options]      Measure scene FPS with vsync off.\n"
        << "  " << commandName << " play --project <path>    Play a scene in another editor, --scene picks it.\n"
        << "  " << commandName << " shader-nodes             Print the shader graph node types as JSON.\n"
        << "  " << commandName << " logic-nodes              Print the logic graph node types as JSON.\n"
        << "  " << commandName << " logic <file> [options]   Write the Lua script of a logic graph.\n\n"
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
        << "      --shader <spec>         Shader type and optional properties, e.g. mesh:Uv1,Nor. A shader\n"
        << "                              file follows @, e.g. mesh:Uv1,Nor@shaders/wind.glsl. Can repeat.\n"
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
            options.shaderSpecs.push_back(value);
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

    // headless host, so loading skips the editor windows
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
    options.shaderKeys.clear();
    for (const std::string& spec : options.shaderSpecs) {
        ShaderKey key;
        if (parseShaderSpec(spec, key, error))
            options.shaderKeys.insert(key);
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

bool CommandLine::checkPlayArgs(int argc, char** argv) {
    bool hasProject = false;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if ((arg == "-p" || arg == "--project") && hasValue) {
            hasProject = true;
            i++;
        } else if ((arg == "-s" || arg == "--scene") && hasValue) {
            i++;
        } else {
            std::cerr << "play: unknown or incomplete argument '" << arg << "'\n";
            return false;
        }
    }

    if (!hasProject) {
        std::cerr << "play: --project is required\n";
    }
    return hasProject;
}

int CommandLine::runBenchmarkHelp(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();
    printBenchmarkUsage(getCommandName(executableName));
    (void)argc;
    (void)argv;
    return 0;
}

int CommandLine::runShaderNodesCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();
    const std::string commandName = getCommandName(executableName);
    for (int i = 1; i < argc; i++) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::cout << "Usage:\n  " << commandName << " shader-nodes\n\n"
                      << "Prints every shader graph node type as JSON: pins, settings, where it runs and the shaders it fits.\n";
            return 0;
        }
        std::cerr << commandName << " shader-nodes: unknown argument '" << argument << "'\n";
        return 2;
    }

    nlohmann::json list = nlohmann::json::array();
    for (const NodeType* base : ShaderNodes::get().getTypes())
        list.push_back(shaderNodeTypeJson(*static_cast<const ShaderNodeType*>(base)));
    std::cout << list.dump(2) << "\n";
    return 0;
}

int CommandLine::runLogicNodesCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    const std::string commandName = getCommandName(executableName);
    bool check = false;
    bool cpp = false;
    std::string dump;
    for (int i = 1; i < argc; i++) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::cout << "Usage:\n  " << commandName << " logic-nodes [--check] [--cpp] [--dump <dir>]\n\n"
                      << "Prints every logic graph node type as JSON: pins, settings and whether it runs in execution order.\n"
                      << "With --check, compiles a graph around each node type instead and prints the ones that fail.\n"
                      << "With --cpp, the check writes C++ scripts; with --dump, each script goes to the directory.\n";
            return 0;
        }
        if (argument == "--cpp") {
            cpp = true;
            continue;
        }
        if (argument == "--check") {
            check = true;
            continue;
        }
        if (argument == "--dump" && i + 1 < argc) {
            dump = argv[++i];
            continue;
        }
        std::cerr << commandName << " logic-nodes: unknown argument '" << argument << "'\n";
        return 2;
    }

    if (check) {
        int graphs = 0;
        int failed = 0;
        for (const NodeType* base : LogicNodes::get().getTypes()) {
            const LogicNodeType& type = *static_cast<const LogicNodeType*>(base);
            // a value node is read once through each output
            size_t values = 0;
            for (const NodePin& pin : type.outputs)
                values += pin.kind == PinKind::DATA ? 1 : 0;
            const size_t runs = type.runs() ? 1 : std::max<size_t>(values, 1);
            for (size_t run = 0; run < runs; run++) {
                GraphDocument graph;
                graph.kind = "logic";
                graph.type = "script";
                GraphParameter variable;
                variable.name = "value1";
                variable.type = "float";
                const int variableId = graph.addParameter(variable);

                const int node = graph.addNode(type.id, Vector2(200.0f, 0.0f));
                GraphNode& added = *graph.findNode(node);
                for (const NodeSetting& setting : type.settings) {
                    if (setting.type == "parameter")
                        added.settings[setting.name] = GraphValue((float)variableId);
                    else if (setting.name == "method")
                        added.settings[setting.name] = GraphValue("update");
                    else if (setting.name == "body")
                        added.settings[setting.name] = GraphValue(cpp ? "return a;" : "return a");
                }
                // an object the script has none of comes from somewhere
                if (const NodePin* target = type.findInput("self"); target && !LogicNodes::isEntityClass(target->type) && target->type != "any") {
                    const int source = graph.addNode("utility/expression", Vector2(0.0f, 100.0f));
                    const std::string object = "static_cast<" + target->type + "*>(nullptr)";
                    graph.findNode(source)->settings["code"] = GraphValue(!cpp ? "self" : (LogicNodes::isPointerClass(target->type) ? object : "*" + object));
                    graph.connect(source, "value", node, "self");
                }
                if (type.flow == LogicFlow::FOR_EACH) {
                    const int list = graph.addNode("utility/expression", Vector2(0.0f, 200.0f));
                    graph.findNode(list)->settings["code"] = GraphValue(cpp ? "std::vector<int>()" : "{}");
                    graph.connect(list, "value", node, "list");
                }
                // C++ only takes the value, it may not print it
                const int print = graph.addNode(cpp ? "utility/statement" : "utility/print", Vector2(400.0f, 0.0f));
                if (cpp)
                    graph.findNode(print)->settings["body"] = GraphValue("(void)a;");
                const char* taken = cpp ? "a" : "text";
                if (type.flow == LogicFlow::EVENT) {
                    graph.connectExecution(node, "out", print, "in");
                } else {
                    const int start = graph.addNode("event/start", Vector2(0.0f, 0.0f));
                    if (type.runs()) {
                        graph.connectExecution(start, "out", node, "in");
                        // what it leads to, and what it gives
                        for (const NodePin& pin : type.outputs) {
                            if (pin.kind == PinKind::EXECUTION && !graph.findLinkInto(print, "in"))
                                graph.connectExecution(node, pin.name, print, "in");
                            else if (pin.kind == PinKind::DATA && !graph.findLinkInto(print, taken))
                                graph.connect(node, pin.name, print, taken);
                        }
                    } else {
                        graph.connectExecution(start, "out", print, "in");
                        size_t index = 0;
                        for (const NodePin& pin : type.outputs)
                            if (pin.kind == PinKind::DATA && index++ == run)
                                graph.connect(node, pin.name, print, taken);
                    }
                }
                graphs++;
                const std::string name = "Check" + std::to_string(graphs);
                // a C++ script says in its first line which node it checks
                const GraphResult result = cpp ? LogicGraphCompiler::compile(graph, type.id, name, LogicLanguage::CPP)
                                               : LogicGraphCompiler::compile(graph, "Check.logic", "Check");
                if (!result.success) {
                    failed++;
                    std::cout << type.id << ": " << result.describeErrors() << "\n";
                } else if (!dump.empty()) {
                    std::ofstream(fs::path(dump) / (name + (cpp ? ".cpp" : ".lua")), std::ios::binary) << result.code;
                    if (cpp)
                        std::ofstream(fs::path(dump) / (name + ".h"), std::ios::binary) << result.header;
                }
            }
        }
        std::cout << graphs << " graphs, " << failed << " failed\n";
        return failed ? 1 : 0;
    }

    nlohmann::json list = nlohmann::json::array();
    for (const NodeType* base : LogicNodes::get().getTypes())
        list.push_back(logicNodeTypeJson(*static_cast<const LogicNodeType*>(base)));
    std::cout << list.dump(2) << "\n";
    return 0;
}

int CommandLine::runLogicCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    const std::string commandName = getCommandName(executableName);
    std::string input;
    std::string output;
    std::string asked;
    for (int i = 1; i < argc; i++) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::cout << "Usage:\n  " << commandName << " logic <file.logic> [--language lua|cpp] [--out <file>]\n\n"
                      << "Writes the script a logic graph stands for, to the file or to the output: Lua, or a C++ class\n"
                      << "with its header beside the file. Without --language it is the graph's own, else Lua.\n";
            return 0;
        }
        if ((argument == "-o" || argument == "--out") && i + 1 < argc) {
            output = argv[++i];
        } else if (argument == "--language" && i + 1 < argc) {
            asked = argv[++i];
        } else if (input.empty() && argument[0] != '-') {
            input = argument;
        } else {
            std::cerr << commandName << " logic: unknown argument '" << argument << "'\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << commandName << " logic: no graph file given\n";
        return 2;
    }

    std::ifstream file(input, std::ios::binary);
    if (!file) {
        std::cerr << commandName << " logic: cannot read " << input << "\n";
        return 1;
    }
    std::stringstream text;
    text << file.rdbuf();

    GraphDocument graph;
    std::string error;
    if (!graph.read(text.str(), error)) {
        std::cerr << commandName << " logic: " << input << ": " << error << "\n";
        return 1;
    }
    const std::string name = std::filesystem::path(input).filename().string();
    // the graph's own language, unless one is asked for
    LogicLanguage language = LogicLanguage::LUA;
    LogicGraphCompiler::findLanguage(graph.language, language);
    if (!asked.empty() && !LogicGraphCompiler::findLanguage(asked, language)) {
        std::cerr << commandName << " logic: --language is lua or cpp\n";
        return 2;
    }
    const GraphResult result = LogicGraphCompiler::compile(graph, name, LogicGraphCompiler::getModuleName(input), language);
    if (!result.success) {
        std::cerr << input << ": " << result.describeErrors() << "\n";
        return 1;
    }
    if (output.empty()) {
        std::cout << result.header << result.code;
        return 0;
    }
    std::ofstream written(output, std::ios::binary | std::ios::trunc);
    written << result.code;
    // a C++ script has its header beside it
    if (language == LogicLanguage::CPP)
        std::ofstream(std::filesystem::path(output).replace_extension(".h"), std::ios::binary | std::ios::trunc) << result.header;
    return written ? 0 : 1;
}

int CommandLine::runIdeCommand(int argc, char** argv, const char* executableName) {
    attachHostConsoleIfNeeded();

    // headless host, as for export
    EditorHostOverrideGuard hostOverride(nullptr);

    const std::string commandName = getCommandName(executableName);
    std::string input;
    for (int i = 1; i < argc; i++) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::cout << "Usage:\n  " << commandName << " ide --project <project-dir|project.yaml>\n\n"
                      << "Writes the CMake and IDE files of a project, as the editor does on save: CMakeLists.txt,\n"
                      << "CMakeUserPresets.json, .vscode, .luarc.json and the engine API in .doriax. Also compiles\n"
                      << "the shaders its scenes use, which the project built from these files needs to run.\n";
            return 0;
        }
        if ((argument == "-p" || argument == "--project") && i + 1 < argc) {
            input = argv[++i];
        } else {
            std::cerr << commandName << " ide: unknown argument '" << argument << "'\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << commandName << " ide: no project given\n";
        return 2;
    }

    Out::setOutputWindow(nullptr);
    AppSettings::initialize();

    Project project;
    auto platform = std::make_unique<Platform>(&project);
    EngineRuntimeGuard engineRuntime;
    Engine::systemInit(argc, argv, platform.get());
    engineRuntime.initialized = true;
    Engine::pauseGameEvents(true);

    if (!project.loadProject(normalizeProjectPath(input), false)) {
        return 1;
    }
    project.copyEngineApiToProject();
    // scene files may have changed outside the editor
    if (!project.writeGeneratedSources(true)) {
        return 1;
    }
    // the project built from these files only reads compiled shaders
    project.buildStandaloneShaderCache();
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
