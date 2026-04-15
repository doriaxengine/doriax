#include "ShaderBuilder.h"

#include "ShaderDataSerializer.h"
#include "App.h"

#include "thread/ResourceProgress.h"
#include "thread/ThreadPoolManager.h"

#include <cstring>
#include <cstdint>
#include <filesystem>

//#include <thread>
//#include <chrono>

using namespace doriax;


std::unordered_map<ShaderKey, ShaderData> editor::ShaderBuilder::shaderDataCache;
std::unordered_map<ShaderKey, std::future<ShaderData>> editor::ShaderBuilder::pendingBuilds;
std::mutex editor::ShaderBuilder::cacheMutex;
std::atomic<bool> editor::ShaderBuilder::shutdownRequested{false};

editor::ShaderBuilder::ShaderBuilder(){
}

editor::ShaderBuilder::~ShaderBuilder(){
}

// Mapping functions implementation with camelCase
ShaderVertexType editor::ShaderBuilder::mapVertexType(shadercompiler::attribute_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case attribute_type_t::FLOAT:  return ShaderVertexType::FLOAT;
        case attribute_type_t::FLOAT2: return ShaderVertexType::FLOAT2;
        case attribute_type_t::FLOAT3: return ShaderVertexType::FLOAT3;
        case attribute_type_t::FLOAT4: return ShaderVertexType::FLOAT4;
        case attribute_type_t::INT:    return ShaderVertexType::INT;
        case attribute_type_t::INT2:   return ShaderVertexType::INT2;
        case attribute_type_t::INT3:   return ShaderVertexType::INT3;
        case attribute_type_t::INT4:   return ShaderVertexType::INT4;
        default:                       return ShaderVertexType::FLOAT;
    }
}

ShaderUniformType editor::ShaderBuilder::mapUniformType(shadercompiler::uniform_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case uniform_type_t::FLOAT:  return ShaderUniformType::FLOAT;
        case uniform_type_t::FLOAT2: return ShaderUniformType::FLOAT2;
        case uniform_type_t::FLOAT3: return ShaderUniformType::FLOAT3;
        case uniform_type_t::FLOAT4: return ShaderUniformType::FLOAT4;
        case uniform_type_t::INT:    return ShaderUniformType::INT;
        case uniform_type_t::INT2:   return ShaderUniformType::INT2;
        case uniform_type_t::INT3:   return ShaderUniformType::INT3;
        case uniform_type_t::INT4:   return ShaderUniformType::INT4;
        case uniform_type_t::MAT3:   return ShaderUniformType::MAT3;
        case uniform_type_t::MAT4:   return ShaderUniformType::MAT4;
        default:                     return ShaderUniformType::FLOAT;
    }
}

TextureType editor::ShaderBuilder::mapTextureType(shadercompiler::texture_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case texture_type_t::TEXTURE_2D:    return TextureType::TEXTURE_2D;
        case texture_type_t::TEXTURE_3D:    return TextureType::TEXTURE_3D;
        case texture_type_t::TEXTURE_CUBE:  return TextureType::TEXTURE_CUBE;
        case texture_type_t::TEXTURE_ARRAY: return TextureType::TEXTURE_ARRAY;
        default:                            return TextureType::TEXTURE_2D;
    }
}

TextureSamplerType editor::ShaderBuilder::mapSamplerType(shadercompiler::texture_samplertype_t type) {
    using namespace shadercompiler;
    switch(type) {
        case texture_samplertype_t::FLOAT: return TextureSamplerType::FLOAT;
        case texture_samplertype_t::SINT:  return TextureSamplerType::SINT;
        case texture_samplertype_t::UINT:  return TextureSamplerType::UINT;
        case texture_samplertype_t::DEPTH: return TextureSamplerType::DEPTH;
        default:                           return TextureSamplerType::FLOAT;
    }
}

SamplerType editor::ShaderBuilder::mapSamplerFilterType(shadercompiler::sampler_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case sampler_type_t::FILTERING:   return SamplerType::FILTERING;
        case sampler_type_t::COMPARISON:  return SamplerType::COMPARISON;
        default:                          return SamplerType::FILTERING;
    }
}

ShaderStorageBufferType editor::ShaderBuilder::mapStorageType(shadercompiler::storage_buffer_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case storage_buffer_type_t::STRUCT: return ShaderStorageBufferType::STRUCT;
        default:                            return ShaderStorageBufferType::STRUCT;
    }
}

ShaderStageType editor::ShaderBuilder::mapStageType(shadercompiler::stage_type_t type) {
    using namespace shadercompiler;
    switch(type) {
        case STAGE_VERTEX:   return ShaderStageType::VERTEX;
        case STAGE_FRAGMENT: return ShaderStageType::FRAGMENT;
        default:             return ShaderStageType::VERTEX;
    }
}

ShaderLang editor::ShaderBuilder::mapLang(shadercompiler::lang_type_t lang) {
    using namespace shadercompiler;
    switch(lang) {
        case LANG_GLSL: return ShaderLang::GLSL;
        case LANG_HLSL: return ShaderLang::HLSL;
        case LANG_MSL:  return ShaderLang::MSL;
        default:        return ShaderLang::GLSL;
    }
}

// Implementation of convertToShaderData
ShaderData editor::ShaderBuilder::convertToShaderData(
    const std::vector<shadercompiler::spirvcross_t>& spirvcrossvec,
    const std::vector<shadercompiler::input_t>& inputs,
    const shadercompiler::args_t& args) {

    ShaderData shaderData;

    shaderData.lang = mapLang(args.lang);
    shaderData.version = args.version;
    shaderData.es = args.es;

    unsigned int texCount = 0, samplerCount = 0, ubCount = 0, sbCount = 0, pairCount = 0;

    for (const auto& cross : spirvcrossvec) {
        ShaderStage stage;
        stage.type = mapStageType(cross.stage_type);
        stage.name = args.output_basename;  // Using output basename for stage name
        stage.source = cross.source;

        // Attributes
        for (const auto& attr : cross.inputs) {
            ShaderAttr sa;
            sa.name = attr.name;
            sa.semanticName = attr.semantic_name;
            sa.semanticIndex = attr.semantic_index;
            sa.location = attr.location;
            sa.type = mapVertexType(attr.type);
            stage.attributes.push_back(sa);
        }

        // Uniform Blocks
        for (const auto& ub : cross.uniform_blocks) {
            ShaderUniformBlock sub;
            sub.name = ub.name;
            sub.instName = ub.inst_name;
            sub.set = ub.set;
            sub.binding = ub.binding;
            sub.slot = ubCount++;
            sub.sizeBytes = ub.size_bytes;
            sub.flattened = ub.flattened;

            for (const auto& u : ub.uniforms) {
                ShaderUniform su;
                su.name = ub.inst_name + "." + u.name;  // Qualified name
                su.type = mapUniformType(u.type);
                su.arrayCount = u.array_count;
                su.offset = u.offset;
                sub.uniforms.push_back(su);
            }
            stage.uniformblocks.push_back(sub);
        }

        // Storage Buffers
        for (const auto& sb : cross.storage_buffers) {
            ShaderStorageBuffer ssb;
            ssb.name = sb.name;
            ssb.instName = sb.inst_name;
            ssb.set = sb.set;
            ssb.binding = sb.binding;
            ssb.slot = sbCount++;
            ssb.sizeBytes = sb.size_bytes;
            ssb.readonly = sb.readonly;
            ssb.type = mapStorageType(sb.type);
            stage.storagebuffers.push_back(ssb);
        }

        // Textures
        for (const auto& tex : cross.textures) {
            ShaderTexture st;
            st.name = tex.name;
            st.set = tex.set;
            st.binding = tex.binding;
            st.slot = texCount++;
            st.type = mapTextureType(tex.type);
            st.samplerType = mapSamplerType(tex.sampler_type);
            stage.textures.push_back(st);
        }

        // Samplers
        for (const auto& s : cross.samplers) {
            ShaderSampler ss;
            ss.name = s.name;
            ss.set = s.set;
            ss.binding = s.binding;
            ss.slot = samplerCount++;
            ss.type = mapSamplerFilterType(s.type);
            stage.samplers.push_back(ss);
        }

        // Texture-Sampler Pairs
        for (const auto& tsp : cross.texture_sampler_pairs) {
            ShaderTextureSamplerPair pair;
            pair.name = tsp.name;
            pair.textureName = tsp.texture_name;
            pair.samplerName = tsp.sampler_name;
            pair.slot = pairCount++;
            stage.textureSamplerPairs.push_back(pair);
        }

        shaderData.stages.push_back(stage);
    }

    return shaderData;
}

void editor::ShaderBuilder::addMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"MATERIAL_UNLIT", "1"});            // 'Ult'
    if (prop & (1 << 1))  defs.push_back({"HAS_UV_SET1", "1"});               // 'Uv1'
    if (prop & (1 << 2))  defs.push_back({"HAS_UV_SET2", "1"});               // 'Uv2'
    if (prop & (1 << 3))  defs.push_back({"USE_PUNCTUAL", "1"});              // 'Puc'
    if (prop & (1 << 4))  defs.push_back({"USE_SHADOWS", "1"});               // 'Shw'
    if (prop & (1 << 5))  defs.push_back({"USE_SHADOWS_PCF", "1"});           // 'Pcf'
    if (prop & (1 << 6))  defs.push_back({"HAS_NORMALS", "1"});               // 'Nor'
    if (prop & (1 << 7))  defs.push_back({"HAS_NORMAL_MAP", "1"});            // 'Nmp'
    if (prop & (1 << 8))  defs.push_back({"HAS_TANGENTS", "1"});              // 'Tan'
    if (prop & (1 << 9))  defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"});     // 'Vc3'
    if (prop & (1 << 10)) defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"});     // 'Vc4'
    if (prop & (1 << 11)) defs.push_back({"HAS_TEXTURERECT", "1"});           // 'Txr'
    if (prop & (1 << 12)) defs.push_back({"HAS_FOG", "1"});                   // 'Fog'
    if (prop & (1 << 13)) defs.push_back({"HAS_SKINNING", "1"});              // 'Ski'
    if (prop & (1 << 14)) defs.push_back({"HAS_MORPHTARGET", "1"});           // 'Mta'
    if (prop & (1 << 15)) defs.push_back({"HAS_MORPHNORMAL", "1"});           // 'Mnr'
    if (prop & (1 << 16)) defs.push_back({"HAS_MORPHTANGENT", "1"});          // 'Mtg'
    if (prop & (1 << 17)) defs.push_back({"HAS_TERRAIN", "1"});               // 'Ter'
    if (prop & (1 << 18)) defs.push_back({"HAS_INSTANCING", "1"});            // 'Ist'
}

void editor::ShaderBuilder::addDepthMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_TEXTURE", "1"});       // 'Tex'
    if (prop & (1 << 1))  defs.push_back({"HAS_SKINNING", "1"});      // 'Ski'
    if (prop & (1 << 2))  defs.push_back({"HAS_MORPHTARGET", "1"});   // 'Mta'
    if (prop & (1 << 3))  defs.push_back({"HAS_MORPHNORMAL", "1"});   // 'Mnr'
    if (prop & (1 << 4))  defs.push_back({"HAS_MORPHTANGENT", "1"});  // 'Mtg'
    if (prop & (1 << 5))  defs.push_back({"HAS_TERRAIN", "1"});       // 'Ter'
    if (prop & (1 << 6))  defs.push_back({"HAS_INSTANCING", "1"});    // 'Ist'
}

void editor::ShaderBuilder::addUIPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_TEXTURE", "1"});              // 'Tex'
    if (prop & (1 << 1))  defs.push_back({"HAS_FONTATLAS_TEXTURE", "1"});    // 'Ftx'
    if (prop & (1 << 2))  defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"});    // 'Vc3'
    if (prop & (1 << 3))  defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"});    // 'Vc4'
}

void editor::ShaderBuilder::addPointsPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_TEXTURE", "1"});              // 'Tex'
    if (prop & (1 << 1))  defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"});    // 'Vc3'
    if (prop & (1 << 2))  defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"});    // 'Vc4'
    if (prop & (1 << 3))  defs.push_back({"HAS_TEXTURERECT", "1"});          // 'Txr'
}

void editor::ShaderBuilder::addLinesPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"});    // 'Vc3'
    if (prop & (1 << 1))  defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"});    // 'Vc4'
}

ShaderBuildResult editor::ShaderBuilder::buildShader(ShaderKey shaderKey, Project* project) {
    std::unique_lock<std::mutex> lock(cacheMutex);

    // Check if already in cache
    if (shaderDataCache.count(shaderKey)){
        return ShaderBuildResult(shaderDataCache[shaderKey], ResourceLoadState::Finished);
    }

    // Try disk cache (<cache>/doriax/shaders/<version>/<basename>.sdat)
    if (project) {
        lock.unlock();
        ShaderData diskData;
        std::string err;
        const std::filesystem::path cachePath = getShaderCachePath(shaderKey, project);
        if (ShaderDataSerializer::readFromFile(cachePath.string(), shaderKey, diskData, &err)) {
            lock.lock();
            shaderDataCache[shaderKey] = diskData;
            printf("Shader loaded from disk cache: %s\n", cachePath.string().c_str());
            return ShaderBuildResult(diskData, ResourceLoadState::Finished);
        }
        lock.lock();
    }

    // Check if already building
    if (pendingBuilds.count(shaderKey)) {
        auto& future = pendingBuilds[shaderKey];
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            // Build finished, move to cache
            try {
                ShaderData data = future.get();
                shaderDataCache[shaderKey] = data;
                pendingBuilds.erase(shaderKey);

                lock.unlock();
                // Persist cache after build finishes (outside lock).
                (void)saveShaderDataCache(shaderKey, project, data);

                return ShaderBuildResult(data, ResourceLoadState::Finished);
            } catch (const std::exception& e) {
                pendingBuilds.erase(shaderKey);
                return ShaderBuildResult({}, ResourceLoadState::Failed);
            }
        } else {
            // Still building
            return ShaderBuildResult({}, ResourceLoadState::Loading);
        }
    }

    // Start new async build
    std::string shaderName = getShaderDisplayName(shaderKey);
    ResourceProgress::startBuild(shaderKey, ResourceType::Shader, shaderName);

    // Use thread pool instead of std::async
    pendingBuilds[shaderKey] = ThreadPoolManager::getInstance().enqueue(
        [this, shaderKey, project]() {
            return buildShaderInternal(shaderKey, project);
        }
    );

    return ShaderBuildResult({}, ResourceLoadState::Loading);
}

void editor::ShaderBuilder::requestShutdown() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    shutdownRequested = true;

    // Wait for all pending builds to complete
    for (auto& [key, future] : pendingBuilds) {
        if (future.valid()) {
            future.wait(); // Wait for completion
        }
    }
    pendingBuilds.clear();
}

bool editor::ShaderBuilder::setupShaderArgs(shadercompiler::args_t& args, ShaderType shaderType, uint32_t properties) {
    if (shaderType == ShaderType::MESH){
        args.vert_file = "mesh.vert";
        args.frag_file = "mesh.frag";
        addMeshPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::DEPTH){
        args.vert_file = "depth.vert";
        args.frag_file = "depth.frag";
        addDepthMeshPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::UI){
        args.vert_file = "ui.vert";
        args.frag_file = "ui.frag";
        addUIPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::POINTS){
        args.vert_file = "points.vert";
        args.frag_file = "points.frag";
        addPointsPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::LINES){
        args.vert_file = "lines.vert";
        args.frag_file = "lines.frag";
        addLinesPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::SKYBOX){
        args.vert_file = "sky.vert";
        args.frag_file = "sky.frag";
    }else{
        return false;
    }

    if (shaderType == ShaderType::MESH){
        args.defines.push_back({"MAX_LIGHTS", "6"});
        args.defines.push_back({"MAX_SHADOWSMAP", "6"});
        args.defines.push_back({"MAX_SHADOWSCUBEMAP", "1"});
        args.defines.push_back({"MAX_SHADOWCASCADES", "4"});
        args.defines.push_back({"MAX_BONES", "70"});
    }
    if (shaderType == ShaderType::DEPTH){
        args.defines.push_back({"MAX_BONES", "70"});
    }

    return true;
}

std::string editor::ShaderBuilder::getLangSuffix(shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform) {
    if (lang == shadercompiler::LANG_GLSL) {
        return es ? "_glsl" + std::to_string(version) + "es" : "_glsl" + std::to_string(version);
    } else if (lang == shadercompiler::LANG_HLSL) {
        return "_hlsl" + std::to_string(version);
    } else if (lang == shadercompiler::LANG_MSL) {
        return (platform == shadercompiler::SHADER_IOS) ? "_msl" + std::to_string(version) + "ios" : "_msl" + std::to_string(version) + "macos";
    }
    return "";
}

ShaderData editor::ShaderBuilder::buildShaderInternal(ShaderKey shaderKey, Project* project){
    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 0.1f); // Starting

    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);

    std::vector<shadercompiler::input_t> inputs;
    shadercompiler::args_t args = shadercompiler::initialize_args();
    args.isValid = true;
    args.useBuffers = true;
    args.fileBuffers = editor::shaderMap;
    args.lang = shadercompiler::LANG_GLSL;
    args.version = 410;

    if (!setupShaderArgs(args, shaderType, properties)) {
        ResourceProgress::failBuild(shaderKey);
        throw std::runtime_error("Unknown shader type");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 0.3f); // Setup complete

    if (!shadercompiler::load_input(inputs, args)) {
        //printf("Error loading shader input\n");
        ResourceProgress::failBuild(shaderKey);
        throw std::runtime_error("Error loading shader input");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 0.5f); // Input loaded

    std::vector<shadercompiler::spirv_t> spirvvec;
    spirvvec.resize(inputs.size());
    if (!shadercompiler::compile_to_spirv(spirvvec, inputs, args)) {
        //printf("Error compiling to SPIRV\n");
        ResourceProgress::failBuild(shaderKey);
        throw std::runtime_error("Error compiling to SPIRV");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 0.8f); // SPIRV compiled

    std::vector<shadercompiler::spirvcross_t> spirvcrossvec;
    spirvcrossvec.resize(inputs.size());
    if (!shadercompiler::compile_to_lang(spirvcrossvec, spirvvec, inputs, args)) {
        //printf("Error cross-compiling\n");
        ResourceProgress::failBuild(shaderKey);
        throw std::runtime_error("Error cross-compiling");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 0.9f); // Cross-compilation done

    // Use a deterministic basename for stage naming and disk cache
    args.output_basename = ShaderPool::getShaderStr(shaderType, properties) + getLangSuffix(args.lang, args.version, args.es, args.platform);
    ShaderData shaderData = convertToShaderData(spirvcrossvec, inputs, args);

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    ResourceProgress::updateProgress(shaderKey, 1.0f); // Complete
    ResourceProgress::completeBuild(shaderKey);

    printf("Shader (%s, %s, %u) generated successfully\n", args.vert_file.c_str(), args.frag_file.c_str(), properties);

    return shaderData;
}

std::filesystem::path editor::ShaderBuilder::getShaderCachePath(ShaderKey shaderKey, Project* project) {
    if (!project) {
        return {};
    }

    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);
    std::string basename = ShaderPool::getShaderStr(shaderType, properties) + "_glsl410";

    return App::getUserShaderCacheDir() / (basename + ".sdat");
}

bool editor::ShaderBuilder::saveShaderDataCache(ShaderKey shaderKey, Project* project, const ShaderData& shaderData, std::string* err) {
    if (!project) {
        return false;
    }

    const std::filesystem::path cachePath = getShaderCachePath(shaderKey, project);
    if (cachePath.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        if (err) {
            *err = "Failed to create shader cache directory: " + cachePath.parent_path().string();
        }
        return false;
    }

    return ShaderDataSerializer::writeToFile(cachePath.string(), shaderKey, shaderData, err);
}

std::string editor::ShaderBuilder::getShaderDisplayName(ShaderKey key) {
    ShaderType type = ShaderPool::getShaderTypeFromKey(key);
    uint32_t properties = ShaderPool::getPropertiesFromKey(key);
    std::string shaderStr = ShaderPool::getShaderStr(type, properties);

    // Find the underscore separating type and properties
    size_t underscorePos = shaderStr.find('_');
    std::string props;
    std::string typeStr = shaderStr; // Use a separate string variable

    if (underscorePos != std::string::npos) {
        typeStr = shaderStr.substr(0, underscorePos);  // Assign to string, not enum
        props = shaderStr.substr(underscorePos + 1);
    }

    // Capitalize the first character of type, lowercase the rest
    if (!typeStr.empty()) {
        typeStr[0] = std::toupper(typeStr[0]);
        for (size_t i = 1; i < typeStr.size(); ++i)
            typeStr[i] = std::tolower(typeStr[i]);
    }

    // Format as "Type (Props)"
    std::string result = typeStr;
    if (!props.empty()) {
        result += " (" + props + ")";
    }
    return result;
}

ShaderData& editor::ShaderBuilder::getShaderData(ShaderKey shaderKey) { 
    std::lock_guard<std::mutex> lock(cacheMutex);
    return shaderDataCache[shaderKey]; 
}

ShaderData editor::ShaderBuilder::buildShaderForExport(ShaderKey shaderKey, shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform) {
    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);

    std::vector<shadercompiler::input_t> inputs;
    shadercompiler::args_t args = shadercompiler::initialize_args();
    args.isValid = true;
    args.useBuffers = true;
    args.fileBuffers = editor::shaderMap;
    args.lang = lang;
    args.version = version;
    args.es = es;
    args.platform = platform;

    if (!setupShaderArgs(args, shaderType, properties)) {
        throw std::runtime_error("Unknown shader type");
    }

    if (!shadercompiler::load_input(inputs, args)) {
        throw std::runtime_error("Error loading shader input");
    }

    std::vector<shadercompiler::spirv_t> spirvvec;
    spirvvec.resize(inputs.size());
    if (!shadercompiler::compile_to_spirv(spirvvec, inputs, args)) {
        throw std::runtime_error("Error compiling to SPIRV");
    }

    std::vector<shadercompiler::spirvcross_t> spirvcrossvec;
    spirvcrossvec.resize(inputs.size());
    if (!shadercompiler::compile_to_lang(spirvcrossvec, spirvvec, inputs, args)) {
        throw std::runtime_error("Error cross-compiling");
    }

    args.output_basename = ShaderPool::getShaderStr(shaderType, properties) + getLangSuffix(lang, version, es, platform);
    ShaderData shaderData = convertToShaderData(spirvcrossvec, inputs, args);

    return shaderData;
}
