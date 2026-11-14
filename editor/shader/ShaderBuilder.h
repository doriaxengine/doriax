// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"

#include "shader/ShaderBuildTypes.h"
#include "ShaderData.h"
#include "ShaderParameterParser.h"
#include "graph/NodeRegistry.h"
#include <vector>
#include <set>
#include <unordered_map>
#include <mutex>
#include <future>
#include <filesystem>

#include "shadercompiler.h"
#include "shaders.h"

namespace shadercompiler {
    struct spirvcross_t;
    struct input_t;
    struct args_t;
}

namespace doriax::editor {

    // what a shader's sources declare
    struct ShaderDeclarations {
        std::vector<ShaderParameterInfo> parameters;
        std::vector<ShaderTextureInfo> textures;
        uint32_t flags = 0;
    };

    // the sources a build takes
    enum class ShaderSourceUse {
        EDITOR,     // edits, live pin numbers while open
        RUN,        // edits, pin numbers in the code
        SAVED       // the files, as exports take them
    };

    class ShaderBuilder {
    private:
        static std::unordered_map<ShaderKey, ShaderData> shaderDataCache;
        static std::unordered_map<ShaderKey, std::future<ShaderData>> pendingBuilds;
        static std::mutex cacheMutex;

        static std::atomic<bool> shutdownRequested;

        // Mapping functions declarations with camelCase
        ShaderVertexType mapVertexType(shadercompiler::attribute_type_t type);
        ShaderUniformType mapUniformType(shadercompiler::uniform_type_t type);
        TextureType mapTextureType(shadercompiler::texture_type_t type);
        TextureSamplerType mapSamplerType(shadercompiler::texture_samplertype_t type);
        SamplerType mapSamplerFilterType(shadercompiler::sampler_type_t type);
        ShaderStorageBufferType mapStorageType(shadercompiler::storage_buffer_type_t type);
        ShaderStageType mapStageType(shadercompiler::stage_type_t type);
        ShaderLang mapLang(shadercompiler::lang_type_t lang);
        static void applyShaderBackend(shadercompiler::args_t& args, ShaderBackend backend);
        static void setBackendLang(shadercompiler::args_t& args);

        ShaderData convertToShaderData(
            const std::vector<shadercompiler::spirvcross_t>& spirvcrossvec,
            const std::vector<shadercompiler::input_t>& inputs,
            const shadercompiler::args_t& args);

        void addMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addDepthMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addGBufferMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addUIPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addPointsPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addLinesPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addWaterPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);

        bool setupShaderArgs(shadercompiler::args_t& args, ShaderType shaderType, uint32_t properties);
        std::string getLangSuffix(shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform);

        // Sets entry-point files + defines for the key, and for a shader file the exact
        // graph of files it includes. Throws on failure.
        // declarations takes what the shader file declares, for ShaderData
        void setupBuildArgs(shadercompiler::args_t& args, ShaderKey shaderKey, Project* project, ShaderDeclarations& declarations,
                            ShaderSourceUse use = ShaderSourceUse::EDITOR);

        ShaderData buildShaderInternal(ShaderKey shaderKey, Project* project, bool trackProgress, ShaderSourceUse use = ShaderSourceUse::EDITOR);
        std::string getShaderDisplayName(ShaderKey key);

        // live for a build with pin numbers as parameters
        static std::filesystem::path getShaderCachePath(ShaderKey shaderKey, Project* project, bool live = false);
        // whether an open graph builds it with live pin numbers
        static bool isLiveShader(ShaderKey shaderKey);

        // Compares the cached source signature with the shader file and the exact
        // graph of files it includes. No directory walk is required.
        static bool isCustomCacheStale(ShaderKey shaderKey, Project* project, const std::filesystem::path& cachePath,
                                       ShaderSourceUse use = ShaderSourceUse::EDITOR);

    public:
        ShaderBuilder();
        virtual ~ShaderBuilder();

        ShaderBuildResult buildShader(ShaderKey shaderKey, Project* project);
        // annotations of a shader file, with its includes
        static ShaderEffectAnnotations getShaderAnnotations(Project* project, const std::string& shader);
        ShaderData buildShaderForExport(ShaderKey shaderKey, Project* project, ShaderBackend backend);

        // Replaces HLSL sources with the bytecode D3D11 would compile at every game start.
        // Windows only: elsewhere, or without d3dcompiler_47.dll, the sources stay.
        static void compileHlslBytecode(ShaderData& shaderData);

        // Compiles the keys absent from the disk cache, synchronously (buildShader hands
        // async builds to the pool and returns before they land).
        void buildMissingShaders(const std::set<ShaderKey>& shaderKeys, Project* project);

        // Drops cached builds and dependency snapshots for all custom variants so the
        // next get() recompiles. Called after source mutations and project switches.
        static void invalidateCustomShaders();
        // one shader file or graph and its callers; returns them
        static std::vector<std::string> invalidateCustomShader(const std::string& shader);
        // bumped by the calls above
        static uint32_t getCustomShaderGeneration();

        // an open graph's edits, built until saved or closed
        static void setGraphSource(const std::string& shader, const std::string& text);
        static void clearGraphSource(const std::string& shader);
        // open graphs build pin numbers as parameters; true when changed
        static bool setGraphLive(const std::string& shader, bool live);
        // writes a graph's code to the generated files
        static void writeGraphCode(Project* project, const std::string& shader, const std::string& code);
        // a graph as the editor holds it, edited or saved
        static bool readGraphSource(Project* project, const std::string& path, std::string& text);
        // a function graph's pins, as edited; false for no function
        static bool getFunctionPins(Project* project, const std::string& path, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs);

        static void requestShutdown();

        // Serialize ShaderData cache on demand (no disk I/O inside buildShaderInternal).
        static bool saveShaderDataCache(ShaderKey shaderKey, Project* project, const ShaderData& shaderData, std::string* err = nullptr,
                                        ShaderSourceUse use = ShaderSourceUse::EDITOR);

        ShaderData& getShaderData(ShaderKey shaderKey);
    };

}
