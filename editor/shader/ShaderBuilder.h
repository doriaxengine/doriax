#pragma once

#include "Project.h"

#include "pool/ShaderPool.h"
#include "ShaderData.h"
#include <vector>
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

        ShaderData convertToShaderData(
            const std::vector<shadercompiler::spirvcross_t>& spirvcrossvec,
            const std::vector<shadercompiler::input_t>& inputs,
            const shadercompiler::args_t& args);

        void addMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addDepthMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addUIPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addPointsPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);
        void addLinesPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop);

        bool setupShaderArgs(shadercompiler::args_t& args, ShaderType shaderType, uint32_t properties);
        std::string getLangSuffix(shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform);

        ShaderData buildShaderInternal(ShaderKey shaderKey, Project* project);
        std::string getShaderDisplayName(ShaderKey key);

        static std::filesystem::path getShaderCachePath(ShaderKey shaderKey, Project* project);

    public:
        ShaderBuilder();
        virtual ~ShaderBuilder();

        ShaderBuildResult buildShader(ShaderKey shaderKey, Project* project);
        ShaderData buildShaderForExport(ShaderKey shaderKey, shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform = shadercompiler::SHADER_DEFAULT);

        static void requestShutdown();

        // Serialize ShaderData cache on demand (no disk I/O inside buildShaderInternal).
        static bool saveShaderDataCache(ShaderKey shaderKey, Project* project, const ShaderData& shaderData, std::string* err = nullptr);

        ShaderData& getShaderData(ShaderKey shaderKey);
    };

}