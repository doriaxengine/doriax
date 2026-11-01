// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"

#include "shader/ShaderBuildTypes.h"
#include "ShaderData.h"
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

        // Sets entry-point files + defines for the key. For custom shaders, resolves only
        // the exact transitive dependency graph from the fork's .vert/.frag entrypoints.
        // Throws on failure.
        void setupBuildArgs(shadercompiler::args_t& args, ShaderKey shaderKey, Project* project);

        ShaderData buildShaderInternal(ShaderKey shaderKey, Project* project, bool trackProgress);
        std::string getShaderDisplayName(ShaderKey key);

        static std::filesystem::path getShaderCachePath(ShaderKey shaderKey, Project* project);

        // Compares the cached source signature with the entry points and the exact
        // transitive include graph used by this fork. No directory walk is required.
        static bool isCustomCacheStale(ShaderKey shaderKey, Project* project, const std::filesystem::path& cachePath);

    public:
        ShaderBuilder();
        virtual ~ShaderBuilder();

        ShaderBuildResult buildShader(ShaderKey shaderKey, Project* project);
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

        static void requestShutdown();

        // Serialize ShaderData cache on demand (no disk I/O inside buildShaderInternal).
        static bool saveShaderDataCache(ShaderKey shaderKey, Project* project, const ShaderData& shaderData, std::string* err = nullptr);

        ShaderData& getShaderData(ShaderKey shaderKey);
    };

}
