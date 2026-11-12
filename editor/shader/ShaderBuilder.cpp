// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderBuilder.h"
#include "ShaderParameterParser.h"

#include "ShaderDataSerializer.h"
#include "App.h"
#include "Engine.h"
#include "Out.h"

#include "thread/ResourceProgress.h"
#include "thread/ThreadPoolManager.h"

#include "component/TerrainComponent.h"
#include "component/WaterExclusionComponent.h"
#include "pool/ShaderPool.h"
#include "util/SHA1.h"
#include "util/Util.h"
#include "util/ProjectUtils.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <d3dcompiler.h>
#endif

//#include <thread>
//#include <chrono>

using namespace doriax;

namespace {

struct CustomSourceSnapshot {
    bool valid = false;
    std::string error;
    std::unordered_map<std::string, std::string> projectBuffers;
    std::string signature;
};

std::mutex customSourceCacheMutex;
std::unordered_map<std::string, std::shared_ptr<const CustomSourceSnapshot>> customSourceCache;

bool readTextFile(const std::filesystem::path& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    content = stream.str();
    return true;
}

// Length prefixes keep the concatenation unambiguous, so two different graphs
// cannot hash to the same signature.
void appendSignaturePart(std::string& input, const std::string& label, const std::string& content) {
    input += std::to_string(label.size()) + ':' + label;
    input += ':' + std::to_string(content.size()) + ':' + content + '\n';
}

std::string customSourceCacheKey(editor::Project* project, const std::string& customShader) {
    return project->getProjectPath().lexically_normal().generic_string() + '\n' + customShader;
}

std::shared_ptr<const CustomSourceSnapshot> resolveCustomSources(editor::Project* project,
                                                                 const std::string& customShader) {
    if (!project || customShader.empty()) {
        auto invalid = std::make_shared<CustomSourceSnapshot>();
        invalid->error = "Custom shader project or path is unavailable.";
        return invalid;
    }

    std::string cacheKey = customSourceCacheKey(project, customShader);
    std::lock_guard<std::mutex> lock(customSourceCacheMutex);
    if (auto it = customSourceCache.find(cacheKey); it != customSourceCache.end())
        return it->second;

    auto snapshot = std::make_shared<CustomSourceSnapshot>();
    std::filesystem::path projectRoot = project->getProjectPath();
    const std::string forkShader = ShaderPool::getCustomShaderFork(customShader);
    const std::string shaderAsset = ShaderPool::getCustomShaderAsset(customShader);
    editor::Util::CustomShaderPaths paths = editor::Util::resolveCustomShaderPaths(forkShader);
    std::string signatureInput;

    auto loadEntryPoint = [&](const std::string& rel, const char* stage) -> bool {
        std::string content;
        if (!editor::Util::isSafeRelativePath(rel)) {
            snapshot->error = std::string("Unsafe custom ") + stage + " shader path: " + rel;
            return false;
        }
        if (!readTextFile(projectRoot / rel, content)) {
            snapshot->error = std::string("Could not open custom ") + stage + " shader: " + rel;
            return false;
        }

        snapshot->projectBuffers[rel] = content;
        appendSignaturePart(signatureInput, std::string("entry:") + rel, content);
        return true;
    };

    // Failures are not cached: the file may be created or restored at any moment.
    if (!forkShader.empty() && (!loadEntryPoint(paths.vert, "vertex") || !loadEntryPoint(paths.frag, "fragment")))
        return snapshot;
    // a project file, else an engine one, pulled in by its own path so errors name it
    std::string assetSource;
    if (!shaderAsset.empty()) {
        std::error_code ec;
        if (editor::ProjectUtils::isEngineShader(shaderAsset) && !std::filesystem::exists(projectRoot / shaderAsset, ec)) {
            assetSource = editor::shaderMap.at(shaderAsset);
            appendSignaturePart(signatureInput, "engine:" + shaderAsset, assetSource);
        } else {
            if (!loadEntryPoint(shaderAsset, "mesh"))
                return snapshot;
            assetSource = std::move(snapshot->projectBuffers[shaderAsset]);
            snapshot->projectBuffers.erase(shaderAsset);
        }
        // texture names are defines, kept to the file
        snapshot->projectBuffers["shader_hooks_vs.glsl"] = "#define SHADER_STAGE_VERTEX\n#include \"shader_textures_vs.glsl\"\n#include \"/" +
            shaderAsset + "\"\n#include \"shader_textures_end_vs.glsl\"\n";
        snapshot->projectBuffers["shader_hooks_fs.glsl"] = "#define SHADER_STAGE_FRAGMENT\n#include \"shader_textures_fs.glsl\"\n#include \"/" +
            shaderAsset + "\"\n#include \"shader_textures_end_fs.glsl\"\n";
    }

    auto readCandidate = [](const std::filesystem::path& path, std::string& content) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec) && readTextFile(path, content);
    };

    // Includes share one namespace, so a shader file keys its files by '/' and their project
    // path, which no fork include can name, even at the project root
    std::unordered_set<std::string> visited;
    std::unordered_map<std::string, std::string> effectPaths;
    std::function<std::string(const std::string&, const std::filesystem::path&, bool)> visitSource =
            [&](const std::string& source, const std::filesystem::path& includeRootRel, bool effect) {
        // An empty include root means the entry point sits at the project root, where the
        // first two candidates would be the same file.
        bool hasLocalRoot = !includeRootRel.empty() && includeRootRel != ".";

        std::string renamed;
        size_t copied = 0;
        for (const editor::Util::ShaderInclude& include : editor::Util::getShaderIncludes(source)) {
            const std::string& key = include.key;
            // effect files resolve from their own folder, where one name can be another file
            const std::string visitKey = effect ? "effect:" + includeRootRel.generic_string() + ":" + key : key;
            if (visited.insert(visitKey).second) {
                if (!editor::Util::isSafeRelativePath(key)) {
                    appendSignaturePart(signatureInput, "unsafe:" + key, "");
                    continue;
                }

                std::string content;
                std::string origin;
                std::string path;
                if (hasLocalRoot && readCandidate(projectRoot / includeRootRel / key, content)) {
                    origin = "local:" + key;
                    path = effect ? "/" + (includeRootRel / key).lexically_normal().generic_string() : key;
                } else if (readCandidate(projectRoot / key, content)) {
                    origin = "project:" + key;
                    path = effect ? "/" + std::filesystem::path(key).lexically_normal().generic_string() : key;
                } else if (auto engineIt = editor::shaderMap.find(key); engineIt != editor::shaderMap.end()) {
                    origin = "engine:" + key;
                    content = engineIt->second;
                } else {
                    appendSignaturePart(signatureInput, "missing:" + key, "");
                    continue;
                }

                appendSignaturePart(signatureInput, origin, content);
                if (effect && !path.empty())
                    effectPaths[visitKey] = path;
                content = visitSource(content, includeRootRel, effect);
                // the fork's file wins a name both use, as it does over the engine's
                if (!path.empty())
                    snapshot->projectBuffers.emplace(path, std::move(content));
            }

            auto effectPath = effectPaths.find(visitKey);
            if (effect && effectPath != effectPaths.end() && effectPath->second != key) {
                renamed.append(source, copied, include.position - copied);
                renamed += effectPath->second;
                copied = include.position + key.size();
            }
        }

        if (copied == 0)
            return source;
        renamed.append(source, copied, std::string::npos);
        return renamed;
    };

    if (!forkShader.empty()){
        const std::filesystem::path forkRoot = editor::Util::getCustomShaderIncludeRoot(forkShader);
        visitSource(snapshot->projectBuffers[paths.vert], forkRoot, false);
        visitSource(snapshot->projectBuffers[paths.frag], forkRoot, false);
    }
    if (!shaderAsset.empty())
        snapshot->projectBuffers["/" + shaderAsset] = visitSource(assetSource, std::filesystem::path(shaderAsset).parent_path(), true);
    snapshot->signature = editor::SHA1::hash(signatureInput);
    snapshot->valid = true;
    customSourceCache[cacheKey] = snapshot;
    return snapshot;
}

std::filesystem::path sourceSignaturePath(const std::filesystem::path& shaderCachePath) {
    return std::filesystem::path(shaderCachePath.string() + ".sources.sha1");
}

bool readSourceSignature(const std::filesystem::path& path, std::string& signature) {
    if (!readTextFile(path, signature))
        return false;
    while (!signature.empty() && (signature.back() == '\n' || signature.back() == '\r'))
        signature.pop_back();
    return !signature.empty();
}

void clearCustomSourceCache() {
    std::lock_guard<std::mutex> lock(customSourceCacheMutex);
    customSourceCache.clear();
}

// parameter headers, hook defines and flags; throws on a bad annotation
void setupShaderParameters(shadercompiler::args_t& args, const std::string& shaderAsset, ShaderType passType,
                           editor::ShaderDeclarations& declared) {
    const std::string shaderType = editor::ShaderParameterParser::getShaderFileType(passType);
    const bool meshPass = shaderType == "mesh";
    // UI, points and lines move vertices and color pixels
    const bool primitive = passType == ShaderType::UI || passType == ShaderType::POINTS || passType == ShaderType::LINES;
    auto defined = [&](const char* name) {
        return std::any_of(args.defines.begin(), args.defines.end(), [&](const shadercompiler::define_t& define) { return define.def == name; });
    };

    auto file = [&](const std::string& key) -> const std::string* {
        auto it = args.fileBuffers.find(key);
        return it != args.fileBuffers.end() ? &it->second : nullptr;
    };

    // only the hooks the file defines are compiled in
    editor::ShaderEffectAnnotations vertexHooks;
    editor::ShaderEffectAnnotations fragmentHooks;
    if (!shaderAsset.empty()) {
        editor::ShaderParameterParser::parseTree("/" + shaderAsset, file, ShaderStageType::VERTEX, vertexHooks);
        editor::ShaderParameterParser::parseTree("/" + shaderAsset, file, ShaderStageType::FRAGMENT, fragmentHooks);
        const std::string& type = vertexHooks.shaderType.empty() ? fragmentHooks.shaderType : vertexHooks.shaderType;
        if (type.empty())
            throw std::runtime_error(shaderAsset + " has no \"#pragma shader " + shaderType + "\" line");
        if (type != shaderType)
            throw std::runtime_error(shaderAsset + " is a " + type + " shader, not a " + shaderType + " one");
    }
    // the hooks each type runs
    const struct { const char* name; bool defined; bool runs; } hooks[] = {
        {"getVertexOffset", vertexHooks.vertexOffset, meshPass || primitive},
        {"getVaryings", vertexHooks.varyingsHook, meshPass},
        {"getClipPosition", vertexHooks.clipPosition, meshPass},
        {"getCoverage", fragmentHooks.coverage, meshPass},
        {"getSurface", fragmentHooks.surface, meshPass},
        {"getColor", fragmentHooks.color, true},
        {"getDepthOffset", fragmentHooks.depthOffset, meshPass},
        {"getUnderwaterColor", fragmentHooks.underwaterColor, shaderType == "water"}
    };
    bool anyHook = false;
    for (const auto& hook : hooks) {
        if (hook.defined && !hook.runs)
            throw std::runtime_error(shaderAsset + " defines " + hook.name + "(), which " + shaderType + " shaders do not have");
        anyHook = anyHook || hook.defined;
    }
    if (!shaderAsset.empty() && !anyHook && vertexHooks.errors.empty() && fragmentHooks.errors.empty())
        throw std::runtime_error(shaderAsset + " defines no hook function");

    const bool useVertexOffset = vertexHooks.vertexOffset;
    const bool useClipPosition = vertexHooks.clipPosition;
    const bool useCoverage = fragmentHooks.coverage;
    // the depth passes take only the surface alpha, of masked materials
    const bool useSurface = fragmentHooks.surface && (passType != ShaderType::DEPTH || defined("ALPHA_MASK"));
    const bool useColor = fragmentHooks.color && passType != ShaderType::DEPTH && passType != ShaderType::GBUFFER && passType != ShaderType::UNDERWATER;
    const bool useDepthOffset = fragmentHooks.depthOffset;
    const bool useUnderwaterColor = fragmentHooks.underwaterColor && passType == ShaderType::UNDERWATER;
    const bool useFragmentHooks = useCoverage || useSurface || useColor || useDepthOffset || useUnderwaterColor;
    // only where a fragment hook can read them
    const bool useVaryings = vertexHooks.varyingsHook && useFragmentHooks;
    const bool useVertexHooks = useVertexOffset || useClipPosition || useVaryings;
    if (vertexHooks.varyingsHook && vertexHooks.varyings.empty())
        throw std::runtime_error(shaderAsset + " fills varyings without declaring any with #pragma varying");
    // GLES3 guarantees 15 vectors between the stages, the engine's own take most of them
    if (vertexHooks.varyings.size() > 4)
        throw std::runtime_error(shaderAsset + " declares " + std::to_string(vertexHooks.varyings.size()) + " varyings, the limit is 4");
    if (useVertexHooks)
        args.defines.push_back({"USE_VERTEX_HOOKS", "1"});
    if (useVertexOffset)
        args.defines.push_back({"USE_VERTEX_OFFSET", "1"});
    if (useClipPosition)
        args.defines.push_back({"USE_CLIP_POSITION", "1"});
    if (useVaryings)
        args.defines.push_back({"USE_VARYINGS", "1"});
    if (useDepthOffset)
        args.defines.push_back({"USE_DEPTH_OFFSET", "1"});
    if (useCoverage)
        args.defines.push_back({"USE_COVERAGE", "1"});
    if (useSurface)
        args.defines.push_back({"USE_SURFACE", "1"});
    if (useColor)
        args.defines.push_back({"USE_COLOR", "1"});
    if (useFragmentHooks)
        args.defines.push_back({"USE_FRAGMENT_HOOKS", "1"});
    if (!shaderAsset.empty() && meshPass)
        args.defines.push_back({passType == ShaderType::DEPTH ? "IN_DEPTH_PASS" : passType == ShaderType::GBUFFER ? "IN_GBUFFER_PASS" : "IN_COLOR_PASS", "1"});

    // the material maps a surface hook makes unnecessary
    const std::set<std::string>& replaces = fragmentHooks.surfaceReplaces;
    if (useSurface && replaces.count("baseColor") && replaces.count("alpha"))
        args.defines.push_back({"SURFACE_REPLACES_BASECOLOR_MAP", "1"});
    if (useSurface && replaces.count("metallic") && replaces.count("roughness"))
        args.defines.push_back({"SURFACE_REPLACES_METALLICROUGHNESS_MAP", "1"});
    if (useSurface && replaces.count("normal"))
        args.defines.push_back({"SURFACE_REPLACES_NORMAL_MAP", "1"});
    if (useSurface && replaces.count("occlusion"))
        args.defines.push_back({"SURFACE_REPLACES_OCCLUSION_MAP", "1"});
    if (useSurface && replaces.count("emissive"))
        args.defines.push_back({"SURFACE_REPLACES_EMISSIVE_MAP", "1"});

    // an unlit shader compiles the unlit variant under whatever key the lights gave the mesh
    if ((vertexHooks.unlit || fragmentHooks.unlit) && passType == ShaderType::MESH) {
        const bool light2D = defined("USE_LIGHT2D");
        const bool normals = defined("HAS_NORMALS");
        args.defines.erase(std::remove_if(args.defines.begin(), args.defines.end(), [&](const shadercompiler::define_t& define) {
            static const std::set<std::string> lighting = {"MATERIAL_UNLIT", "USE_PUNCTUAL", "USE_SHADOWS", "USE_IBL", "USE_SSAO", "DIRECTIONAL_LIGHTS_ONLY"};
            static const std::set<std::string> surface = {"HAS_NORMALS", "HAS_TANGENTS", "HAS_NORMAL_MAP"};
            return lighting.count(define.def) || (!light2D && surface.count(define.def));
        }), args.defines.end());
        args.defines.push_back({"MATERIAL_UNLIT", "1"});
        // the hooks keep the normals the lit key gave
        if (normals && !light2D && !defined("HAS_HOOK_NORMALS"))
            args.defines.push_back({"HAS_HOOK_NORMALS", "1"});
        declared.flags |= ShaderFlag_Unlit;
    }

    // what the hooks read of the vertices, where this pass has it; a UI always has UVs,
    // and the other types compute what they give in place
    const struct { const char* field; const char* name; bool available; } inputs[] = {
        {"uv1", "UV1", passType == ShaderType::UI || (meshPass && (defined("HAS_UV_SET1") || defined("HAS_HOOK_UV1")) && !defined("HAS_TERRAIN"))},
        {"normal", "NORMAL", meshPass && (defined("HAS_NORMALS") || defined("HAS_HOOK_NORMALS"))},
        {"color", "COLOR", (meshPass || primitive) && (defined("HAS_VERTEX_COLOR_VEC3") || defined("HAS_VERTEX_COLOR_VEC4") || defined("HAS_INSTANCING"))},
        {"worldPosition", "POSITION", primitive}
    };
    for (const auto& input : inputs) {
        const bool vertex = useVertexHooks && vertexHooks.contextReads.count(input.field);
        const bool fragment = useFragmentHooks && fragmentHooks.contextReads.count(input.field);
        if (input.available && (vertex || fragment))
            args.defines.push_back({std::string("HOOK_") + input.name, "1"});
        if (input.available && fragment)
            args.defines.push_back({std::string("HOOK_FRAGMENT_") + input.name, "1"});
    }

    // the files each stage reaches; an inactive hook is not read
    std::unordered_set<std::string> vertexFiles;
    std::unordered_set<std::string> fragmentFiles;
    auto lookup = [&](std::unordered_set<std::string>& reached) {
        return [&](const std::string& key) -> const std::string* {
            reached.insert(key);
            if ((key == "shader_hooks_vs.glsl" && !useVertexHooks) || (key == "shader_hooks_fs.glsl" && !useFragmentHooks))
                return nullptr;
            return file(key);
        };
    };

    editor::ShaderEffectAnnotations vertexAnnotations;
    editor::ShaderEffectAnnotations fragmentAnnotations;
    editor::ShaderParameterParser::parseTree(args.vert_file, lookup(vertexFiles), ShaderStageType::VERTEX, vertexAnnotations);
    editor::ShaderParameterParser::parseTree(args.frag_file, lookup(fragmentFiles), ShaderStageType::FRAGMENT, fragmentAnnotations);

    std::vector<std::string> errors = vertexAnnotations.errors;
    errors.insert(errors.end(), fragmentAnnotations.errors.begin(), fragmentAnnotations.errors.end());
    // of a section no hook reaches
    for (const editor::ShaderEffectAnnotations* hooks : {&vertexHooks, &fragmentHooks}) {
        for (const std::string& error : hooks->errors) {
            if (std::find(errors.begin(), errors.end(), error) == errors.end())
                errors.push_back(error);
        }
    }

    // a fork from before the hooks existed cannot run them
    if (useVertexHooks && !vertexFiles.count("shader_hooks_vs.glsl"))
        editor::Out::warning("%s does not include includes/hooks_vs.glsl, so the vertex hooks of %s do nothing in it", args.vert_file.c_str(), shaderAsset.c_str());
    if (useFragmentHooks && !fragmentFiles.count("shader_hooks_fs.glsl"))
        editor::Out::warning("%s does not include includes/hooks_fs.glsl, so the hooks of %s do nothing in it", args.frag_file.c_str(), shaderAsset.c_str());

    if (useCoverage && fragmentAnnotations.hasCoverageShadows && fragmentAnnotations.coverageShadows)
        declared.flags |= ShaderFlag_CoverageShadows;

    // both stages read one value by name, so they are checked together
    std::vector<editor::ShaderParameterDeclaration> declarations = vertexAnnotations.parameters;
    declarations.insert(declarations.end(), fragmentAnnotations.parameters.begin(), fragmentAnnotations.parameters.end());
    editor::ShaderParameterParser::checkDeclarations(declarations, errors);

    // shadow maps and the camera depth share the depth program, the clip position tells them apart
    if (useClipPosition && passType == ShaderType::DEPTH)
        vertexAnnotations.enginePass = true;
    if (useVaryings) {
        args.fileBuffers["shader_varyings_vs.glsl"] = editor::ShaderParameterParser::generateVaryings(ShaderStageType::VERTEX, vertexHooks.varyings);
        args.fileBuffers["shader_varyings_fs.glsl"] = editor::ShaderParameterParser::generateVaryings(ShaderStageType::FRAGMENT, vertexHooks.varyings);
    }

    // the scene inputs a post-process pass samples
    const bool sceneDepth = useColor && passType == ShaderType::POSTPROCESS && fragmentHooks.sceneReads.count("sceneDepth");
    const bool sceneNormal = useColor && passType == ShaderType::POSTPROCESS && fragmentHooks.sceneReads.count("sceneNormal");
    if (sceneDepth)
        args.defines.push_back({"HOOK_SCENE_DEPTH", "1"});
    if (sceneNormal)
        args.defines.push_back({"HOOK_SCENE_NORMAL", "1"});
    if (useColor && passType == ShaderType::POSTPROCESS && fragmentHooks.sceneReads.count("sceneOcclusion"))
        args.defines.push_back({"HOOK_SCENE_OCCLUSION", "1"});

    // the model matrix of the types whose own blocks lack it
    const bool modelMatrix = primitive && (useVertexHooks || defined("HOOK_FRAGMENT_POSITION"));
    args.fileBuffers["shader_params_vs.glsl"] = editor::ShaderParameterParser::generateHeader(
        ShaderStageType::VERTEX, vertexAnnotations, (useVertexOffset ? editor::HookContext_Vertex : 0) | (useClipPosition ? editor::HookContext_Clip : 0) |
        (modelMatrix ? editor::HookContext_Model : 0), declared.parameters, declared.textures);
    args.fileBuffers["shader_params_fs.glsl"] = editor::ShaderParameterParser::generateHeader(
        ShaderStageType::FRAGMENT, fragmentAnnotations, ((useFragmentHooks && meshPass) ? editor::HookContext_Fragment : 0) |
        ((useSurface && passType == ShaderType::GBUFFER) || sceneNormal ? editor::HookContext_View : 0) |
        (useDepthOffset || sceneDepth ? editor::HookContext_Projection : 0),
        declared.parameters, declared.textures);

    args.fileBuffers["shader_textures_vs.glsl"] = editor::ShaderParameterParser::generateTextureNames(ShaderStageType::VERTEX, declared.textures, true);
    args.fileBuffers["shader_textures_end_vs.glsl"] = editor::ShaderParameterParser::generateTextureNames(ShaderStageType::VERTEX, declared.textures, false);
    args.fileBuffers["shader_textures_fs.glsl"] = editor::ShaderParameterParser::generateTextureNames(ShaderStageType::FRAGMENT, declared.textures, true);
    args.fileBuffers["shader_textures_end_fs.glsl"] = editor::ShaderParameterParser::generateTextureNames(ShaderStageType::FRAGMENT, declared.textures, false);

    // a fork that dropped the call would read zeros
    auto warnMissingLoad = [&](const std::string& file, const char* header) {
        auto source = args.fileBuffers.find(file);
        if (args.fileBuffers[header].find(" = block.") != std::string::npos && source != args.fileBuffers.end() &&
                source->second.find("loadShaderParameters();") == std::string::npos)
            editor::Out::warning("%s declares shader parameters but never calls loadShaderParameters(), so they read zero", file.c_str());
    };
    warnMissingLoad(args.vert_file, "shader_params_vs.glsl");
    warnMissingLoad(args.frag_file, "shader_params_fs.glsl");

    if (!errors.empty()){
        std::string message = "Shader parameter annotations:";
        for (const std::string& error : errors)
            message += "\n" + error;
        throw std::runtime_error(message);
    }
}

bool hasAnnotation(const shadercompiler::args_t& args, const char* tag,
                   const std::string& name, const char* value) {
    const std::string annotation = std::string("#pragma ") + tag + " " + name + " " + value;
    for (const auto& file : args.fileBuffers)
        if (file.second.find(annotation) != std::string::npos)
            return true;
    return false;
}

// SG_MAX_UNIFORMBLOCK_MEMBERS: on GL, a block of mixed member types declares them one by one
constexpr size_t maxMixedBlockMembers = 16;
// SG_MAX_SAMPLER_BINDSLOTS and SG_MAX_PORTABLE_TEXTURE_BINDINGS_PER_STAGE
constexpr size_t maxSamplers = 16;
constexpr size_t maxStageTextures = 16;

// a shader past what the runtime loads
std::string getShaderDataError(const ShaderData& shaderData, uint16_t customId) {
    if (customId == 0)
        return "";

    std::string name = ShaderPool::getCustomShaderName(customId);
    if (name[0] == '#')
        name.erase(0, 1);
    size_t samplers = 0;
    for (const ShaderStage& stage : shaderData.stages) {
        samplers += stage.samplers.size();
        if (stage.textures.size() > maxStageTextures)
            return name + ": " + std::to_string(stage.textures.size()) + " textures in one stage, the limit is " + std::to_string(maxStageTextures);
    }
    if (samplers > maxSamplers)
        return name + ": " + std::to_string(samplers) + " samplers, the limit is " + std::to_string(maxSamplers) +
            "; textures of one filter and wrap share a sampler";

    for (const ShaderStage& stage : shaderData.stages) {
        for (const ShaderUniformBlock& block : stage.uniformblocks) {
            if (shaderData.lang == ShaderLang::GLSL && !block.flattened && block.uniforms.size() > maxMixedBlockMembers)
                return name + ": " + block.name + " mixes member types and has " + std::to_string(block.uniforms.size()) +
                    " members, more than the " + std::to_string(maxMixedBlockMembers) + " OpenGL loads";
        }
    }

    std::string error;
    if (!ShaderDataSerializer::validate(shaderData, &error))
        return name + ": " + error;
    return "";
}

} // namespace


// read from the sources the build resolves, so both see the same includes
editor::ShaderEffectAnnotations editor::ShaderBuilder::getShaderAnnotations(Project* project, const std::string& shader, bool fork) {
    ShaderEffectAnnotations annotations;
    const std::shared_ptr<const CustomSourceSnapshot> sources = resolveCustomSources(project, fork ? shader : "#" + shader);
    if (!sources->valid)
        return annotations;

    auto lookup = [&](const std::string& key) -> const std::string* {
        auto projectFile = sources->projectBuffers.find(key);
        if (projectFile != sources->projectBuffers.end())
            return &projectFile->second;
        auto engineFile = editor::shaderMap.find(key);
        return engineFile != editor::shaderMap.end() ? &engineFile->second : nullptr;
    };
    const Util::CustomShaderPaths paths = fork ? Util::resolveCustomShaderPaths(shader) : Util::CustomShaderPaths{"/" + shader, "/" + shader};
    ShaderParameterParser::parseTree(paths.vert, lookup, ShaderStageType::VERTEX, annotations);
    ShaderParameterParser::parseTree(paths.frag, lookup, ShaderStageType::FRAGMENT, annotations);
    // the clashes the build would stop at
    ShaderParameterParser::checkDeclarations(annotations.parameters, annotations.errors);
    return annotations;
}

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
        case LANG_GLSL:  return ShaderLang::GLSL;
        case LANG_HLSL:  return ShaderLang::HLSL;
        case LANG_MSL:   return ShaderLang::MSL;
        case LANG_SPIRV: return ShaderLang::SPIRV;
        default:         return ShaderLang::GLSL;
    }
}

// Target selection must stay in step with ShaderPool::getShaderLangStr(backend), which
// names the file the result is stored under.
void editor::ShaderBuilder::applyShaderBackend(shadercompiler::args_t& args, ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::MetalMacOS:
        case ShaderBackend::MetalIOS:
            args.lang = shadercompiler::LANG_MSL;
            args.version = 20100; // SPIRV-Cross MSL 2.1, as MMmmpp
            args.platform = (backend == ShaderBackend::MetalIOS)
                ? shadercompiler::SHADER_IOS : shadercompiler::SHADER_MACOS;
            break;
        case ShaderBackend::D3D11:
            args.lang = shadercompiler::LANG_HLSL;
            args.version = 50;
            break;
        case ShaderBackend::Vulkan:
            args.lang = shadercompiler::LANG_SPIRV;
            args.version = 10;
            break;
        case ShaderBackend::GLES3:
            args.lang = shadercompiler::LANG_GLSL;
            args.version = 300;
            args.es = true;
            break;
        default:
            args.lang = shadercompiler::LANG_GLSL;
            args.version = 410;
            break;
    }
}

void editor::ShaderBuilder::setBackendLang(shadercompiler::args_t& args) {
    applyShaderBackend(args, ShaderPool::getShaderBackend());
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

        // SPIRV (Vulkan) outputs bytecode instead of source
        if (!cross.bytecode.empty()) {
            stage.bytecode.size = static_cast<uint32_t>(cross.bytecode.size() * sizeof(uint32_t));
            stage.bytecode.data = new unsigned char[stage.bytecode.size];
            memcpy(stage.bytecode.data, cross.bytecode.data(), stage.bytecode.size);
        }

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
            st.samplerType = hasAnnotation(args, "image_sample_type", tex.name, "unfilterable_float")
                ? TextureSamplerType::UNFILTERABLE_FLOAT : mapSamplerType(tex.sampler_type);
            stage.textures.push_back(st);
        }

        // Samplers
        for (const auto& s : cross.samplers) {
            ShaderSampler ss;
            ss.name = s.name;
            ss.set = s.set;
            ss.binding = s.binding;
            ss.slot = samplerCount++;
            ss.type = hasAnnotation(args, "sampler_type", s.name, "nonfiltering")
                ? SamplerType::NONFILTERING : mapSamplerFilterType(s.type);
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
    // bit 5 reserved (was USE_SHADOWS_PCF; the PCF filter is uniform-driven now)
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
    if (prop & (1 << 19)) defs.push_back({"USE_IBL", "1"});                   // 'Ibl'
    if (prop & (1 << 20)) defs.push_back({"USE_MIRROR", "1"});                // 'Mir'
    if (prop & (1 << 21)) defs.push_back({"USE_SSAO", "1"});                  // 'Sao'
    if (prop & (1 << 22)) defs.push_back({"USE_LIGHT2D", "1"});               // 'L2d'
    if (prop & (1 << 23)) defs.push_back({"USE_SHADOWS_2D", "1"});            // 'S2d'
    if (prop & (1 << 24)) defs.push_back({"ALPHA_MASK", "1"});                 // 'Ams'
    if (prop & (1 << 25)) defs.push_back({"ALPHA_OPAQUE", "1"});               // 'Aop'
    if (prop & (1 << 26)) defs.push_back({"USE_INSTANCE_FADE", "1"});          // 'Ifd'
    if (prop & (1 << 27)) defs.push_back({"HAS_TERRAIN_PBR", "1"});            // 'Tpb'
    if (prop & (1 << 29)) defs.push_back({"HAS_HOOK_UV1", "1"});               // 'Hu1'
    if (prop & (1 << 30)) defs.push_back({"HAS_HOOK_NORMALS", "1"});           // 'Hnr'
}

void editor::ShaderBuilder::addDepthMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_TEXTURE", "1"});       // 'Tex'
    if (prop & (1 << 1))  defs.push_back({"HAS_SKINNING", "1"});      // 'Ski'
    if (prop & (1 << 2))  defs.push_back({"HAS_MORPHTARGET", "1"});   // 'Mta'
    if (prop & (1 << 3))  defs.push_back({"HAS_MORPHNORMAL", "1"});   // 'Mnr'
    if (prop & (1 << 4))  defs.push_back({"HAS_MORPHTANGENT", "1"});  // 'Mtg'
    if (prop & (1 << 5))  defs.push_back({"HAS_TERRAIN", "1"});       // 'Ter'
    if (prop & (1 << 6))  defs.push_back({"HAS_INSTANCING", "1"});    // 'Ist'
    if (prop & (1 << 7))  defs.push_back({"ALPHA_MASK", "1"});         // 'Ams'
    if (prop & (1 << 8))  defs.push_back({"USE_INSTANCE_FADE", "1"}); // 'Ifd'
    if (prop & (1 << 9))  defs.push_back({"HAS_UV_SET1", "1"});       // 'Uv1'
    if (prop & (1 << 10)) defs.push_back({"HAS_NORMALS", "1"});       // 'Nor'
    if (prop & (1 << 11)) defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"}); // 'Vc3'
    if (prop & (1 << 12)) defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"}); // 'Vc4'
}

void editor::ShaderBuilder::addGBufferMeshPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_BASECOLOR_TEXTURE", "1"});         // 'Bct'
    if (prop & (1 << 1))  defs.push_back({"HAS_NORMALS", "1"});                   // 'Nor'
    if (prop & (1 << 2))  defs.push_back({"HAS_SKINNING", "1"});                  // 'Ski'
    if (prop & (1 << 3))  defs.push_back({"HAS_MORPHTARGET", "1"});               // 'Mta'
    if (prop & (1 << 4))  defs.push_back({"HAS_MORPHNORMAL", "1"});               // 'Mnr'
    if (prop & (1 << 5))  defs.push_back({"HAS_MORPHTANGENT", "1"});              // 'Mtg'
    if (prop & (1 << 6))  defs.push_back({"HAS_TERRAIN", "1"});                   // 'Ter'
    if (prop & (1 << 7))  defs.push_back({"HAS_INSTANCING", "1"});                // 'Ist'
    if (prop & (1 << 8))  defs.push_back({"HAS_METALLICROUGHNESS_TEXTURE", "1"}); // 'Mrt'
    if (prop & (1 << 9))  defs.push_back({"HAS_TERRAIN_PBR", "1"});               // 'Tpb'
    if (prop & (1 << 10)) defs.push_back({"HAS_OCCLUSION_TEXTURE", "1"});         // 'Occ'
    if (prop & (1 << 11)) defs.push_back({"OCCLUSION_UV2", "1"});                 // 'Ou2'
    if (prop & (1 << 12)) defs.push_back({"HAS_UV_SET1", "1"});                   // 'Uv1'
    if (prop & (1 << 13)) defs.push_back({"HAS_VERTEX_COLOR_VEC3", "1"});         // 'Vc3'
    if (prop & (1 << 14)) defs.push_back({"HAS_VERTEX_COLOR_VEC4", "1"});         // 'Vc4'
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

void editor::ShaderBuilder::addWaterPropertyDefinitions(std::vector<shadercompiler::define_t>& defs, const uint32_t prop) {
    if (prop & (1 << 0))  defs.push_back({"HAS_FOG", "1"});                  // 'Fog'
    if (prop & (1 << 1))  defs.push_back({"USE_SCENE_DEPTH", "1"});          // 'Dep'
    if (prop & (1 << 2))  defs.push_back({"USE_PLANAR_REFLECTION", "1"});    // 'Prf'
    if (prop & (1 << 3))  defs.push_back({"USE_SHADOWS", "1"});              // 'Shw'
    if (prop & (1 << 4))  defs.push_back({"USE_REFRACTION", "1"});           // 'Rfr'
}

ShaderBuildResult editor::ShaderBuilder::buildShader(ShaderKey shaderKey, Project* project) {
    std::unique_lock<std::mutex> lock(cacheMutex);

    // Check if already in cache
    if (shaderDataCache.count(shaderKey)){
        return ShaderBuildResult(shaderDataCache[shaderKey], ResourceLoadState::Finished);
    }

    // Try disk cache (<cache>/doriax/shaders/<version>/<basename>.sdat). Forked shaders
    // are cached too (the run-build / standalone runtime loads shaders from this same
    // dir and cannot compile). A persisted source signature covers both entry points and
    // their exact resolved include graph. The validation key is the storage key
    // (customId stripped) because that id is session-local and would not match across
    // the editor and a separate run process.
    const bool isCustom = ShaderPool::getCustomIdFromKey(shaderKey) != 0;
    if (project) {
        lock.unlock();
        const std::filesystem::path cachePath = getShaderCachePath(shaderKey, project);
        const bool fresh = !isCustom || !isCustomCacheStale(shaderKey, project, cachePath);
        if (fresh) {
            ShaderData diskData;
            std::string err;
            if (ShaderDataSerializer::readFromFile(cachePath.string(), ShaderPool::getStorageKey(shaderKey), diskData, &err)) {
                lock.lock();
                shaderDataCache[shaderKey] = diskData;
                printf("Shader loaded from disk cache: %s\n", cachePath.string().c_str());
                return ShaderBuildResult(diskData, ResourceLoadState::Finished);
            }
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

    if (!Engine::isAsyncLoading()) {
        try {
            lock.unlock();
            ShaderData data = buildShaderInternal(shaderKey, project, false);
            lock.lock();
            shaderDataCache[shaderKey] = data;
            lock.unlock();

            (void)saveShaderDataCache(shaderKey, project, data);

            return ShaderBuildResult(data, ResourceLoadState::Finished);
        } catch (const std::exception& e) {
            return ShaderBuildResult({}, ResourceLoadState::Failed);
        }
    }

    // Start new async build
    std::string shaderName = getShaderDisplayName(shaderKey);
    ResourceProgress::startBuild(shaderKey, ResourceType::Shader, shaderName);

    // Use thread pool instead of std::async
    pendingBuilds[shaderKey] = ThreadPoolManager::getInstance().enqueue(
        [this, shaderKey, project]() {
            return buildShaderInternal(shaderKey, project, true);
        }
    );

    return ShaderBuildResult({}, ResourceLoadState::Loading);
}

void editor::ShaderBuilder::buildMissingShaders(const std::set<ShaderKey>& shaderKeys, Project* project) {
    if (!project) {
        return;
    }

    for (const ShaderKey& shaderKey : shaderKeys) {
        const std::filesystem::path cachePath = getShaderCachePath(shaderKey, project);
        const bool isCustom = ShaderPool::getCustomIdFromKey(shaderKey) != 0;
        const bool fresh = !isCustom || !isCustomCacheStale(shaderKey, project, cachePath);

        ShaderData diskData;
        if (fresh && ShaderDataSerializer::readFromFile(cachePath.string(), ShaderPool::getStorageKey(shaderKey), diskData)) {
            continue;
        }

        try {
            const ShaderData data = buildShaderInternal(shaderKey, project, false);
            (void)saveShaderDataCache(shaderKey, project, data);
        } catch (const std::exception&) {
            // buildShaderInternal reports it; the remaining keys still build
        }
    }
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
    }else if (shaderType == ShaderType::GBUFFER){
        args.vert_file = "gbuffer.vert";
        args.frag_file = "gbuffer.frag";
        addGBufferMeshPropertyDefinitions(args.defines, properties);
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
    }else if (shaderType == ShaderType::SSAO){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "ssao.frag";
    }else if (shaderType == ShaderType::SSAO_BLUR){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "ssao_blur.frag";
    }else if (shaderType == ShaderType::SSR){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "ssr.frag";
    }else if (shaderType == ShaderType::SSR_BLUR){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "ssr_blur.frag";
    }else if (shaderType == ShaderType::COMPOSITE){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "composite.frag";
    }else if (shaderType == ShaderType::SHADOW2D){
        args.vert_file = "shadow2d.vert";
        args.frag_file = "shadow2d.frag";
    }else if (shaderType == ShaderType::BLIT){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "blit.frag";
    }else if (shaderType == ShaderType::POSTPROCESS){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "postprocess.frag";
    }else if (shaderType == ShaderType::WATER){
        args.vert_file = "water.vert";
        args.frag_file = "water.frag";
        addWaterPropertyDefinitions(args.defines, properties);
    }else if (shaderType == ShaderType::UNDERWATER){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "underwater.frag";
    }else if (shaderType == ShaderType::PROBE_FILTER){
        args.vert_file = "fullscreen.vert";
        args.frag_file = "probe_filter.frag";
    }else{
        return false;
    }

    if (shaderType == ShaderType::MESH || shaderType == ShaderType::GBUFFER){
        args.defines.push_back({"MAX_TERRAIN_LAYERS", std::to_string(MAX_TERRAIN_LAYERS)});
    }
    if (shaderType == ShaderType::MESH || shaderType == ShaderType::WATER){
        args.defines.push_back({"MAX_LIGHTS", std::to_string(MAX_LIGHTS)});
        args.defines.push_back({"MAX_SHADOW_ATLAS_SLOTS", std::to_string(MAX_SHADOW_ATLAS_SLOTS)});
        args.defines.push_back({"MAX_POINT_SHADOW_ATLAS_SLOTS", std::to_string(MAX_POINT_SHADOW_ATLAS_SLOTS)});
        args.defines.push_back({"MAX_SHADOWCASCADES", std::to_string(MAX_SHADOWCASCADES)});
    }
    if (shaderType == ShaderType::MESH){
        args.defines.push_back({"MAX_LIGHTS_2D", std::to_string(MAX_LIGHTS_2D)});
        if (properties & (1u << 28)){
            args.defines.push_back({"DIRECTIONAL_LIGHTS_ONLY", "1"});
        }
    }
    if (shaderType == ShaderType::WATER){
        args.defines.push_back({"MAX_WATER_EXCLUSIONS", std::to_string(MAX_WATER_EXCLUSIONS)});
    }
    if (shaderType == ShaderType::GBUFFER){
        // suppresses the terrain texture-coordinate varyings in the shared includes
        // (the G-buffer fragment does not consume them); mirrors depth.vert
        args.defines.push_back({"DEPTH_SHADER", "1"});
    }
    if (shaderType == ShaderType::SSAO){
        args.defines.push_back({"SSAO_KERNEL_SIZE", "32"});
    }
    if (shaderType == ShaderType::SSR){
        args.defines.push_back({"SSR_MAX_STEPS", "128"});
    }
    if (shaderType == ShaderType::SSR_BLUR){
        args.defines.push_back({"SSR_BLUR_TAPS", "6"});
    }

    return true;
}

void editor::ShaderBuilder::setupBuildArgs(shadercompiler::args_t& args, ShaderKey shaderKey, Project* project, ShaderDeclarations& declarations) {
    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);
    uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);

    // Built-in entry-point files + property #defines (the variant system is identical
    // for forked shaders).
    if (!setupShaderArgs(args, shaderType, properties)) {
        throw std::runtime_error("Unknown shader type");
    }

    const bool meshPass = shaderType == ShaderType::MESH || shaderType == ShaderType::DEPTH || shaderType == ShaderType::GBUFFER;
    if (args.lang == shadercompiler::LANG_GLSL && meshPass)
        args.defines.push_back({"SKINNING_TEXTURE", "1"});

    // the built-in shaders declare no parameters
    args.fileBuffers["shader_params_vs.glsl"] = "// no parameters\n";
    args.fileBuffers["shader_params_fs.glsl"] = "// no parameters\n";

    if (customId == 0)
        return;

    // Overlay the project's forked entry points and every include they reach onto the
    // embedded engine file buffers.
    if (!project) {
        throw std::runtime_error("Custom shader requested without a project");
    }

    std::string customShader = ShaderPool::getCustomShaderName(customId);
    if (customShader.empty()) {
        throw std::runtime_error("Unknown custom shader id");
    }

    const std::shared_ptr<const CustomSourceSnapshot> sources = resolveCustomSources(project, customShader);
    if (!sources->valid)
        throw std::runtime_error(sources->error.empty() ? "Could not resolve custom shader sources" : sources->error);

    // The .vert/.frag entry points may share a base name or live in separate files.
    // Only project files that are actually reachable through #include are overlaid.
    // Resolution order is the entry point's own directory, the project root, then the
    // embedded engine shader library already present in args.fileBuffers.
    for (const auto& [key, content] : sources->projectBuffers)
        args.fileBuffers[key] = content;

    // a shader file alone keeps the built-in entry points
    const std::string forkShader = ShaderPool::getCustomShaderFork(customShader);
    if (!forkShader.empty()) {
        const editor::Util::CustomShaderPaths customPaths = editor::Util::resolveCustomShaderPaths(forkShader);
        args.vert_file = customPaths.vert;
        args.frag_file = customPaths.frag;
    }

    setupShaderParameters(args, ShaderPool::getCustomShaderAsset(customShader), shaderType, declarations);

    // compiled with its stages as conditions
    for (const auto& [key, content] : sources->projectBuffers)
        args.fileBuffers[key] = ShaderParameterParser::getStageConditions(content);
}

std::string editor::ShaderBuilder::getLangSuffix(shadercompiler::lang_type_t lang, int version, bool es, shadercompiler::platform_t platform) {
    if (lang == shadercompiler::LANG_GLSL) {
        return es ? "_glsl" + std::to_string(version) + "es" : "_glsl" + std::to_string(version);
    } else if (lang == shadercompiler::LANG_HLSL) {
        return "_hlsl" + std::to_string(version);
    } else if (lang == shadercompiler::LANG_MSL) {
        return (platform == shadercompiler::SHADER_IOS) ? "_msl" + std::to_string(version) + "ios" : "_msl" + std::to_string(version) + "macos";
    } else if (lang == shadercompiler::LANG_SPIRV) {
        return "_spirv" + std::to_string(version);
    }
    return "";
}

ShaderData editor::ShaderBuilder::buildShaderInternal(ShaderKey shaderKey, Project* project, bool trackProgress){
    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 0.1f); // Starting
    }

    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);
    uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);

    std::vector<shadercompiler::input_t> inputs;
    shadercompiler::args_t args = shadercompiler::initialize_args();
    args.isValid = true;
    args.useBuffers = true;
    args.fileBuffers = editor::shaderMap;
    setBackendLang(args);

    // Capture the GLSL compiler log so failures surface in the editor output window
    // (otherwise the details only reach stderr/app output).
    std::string compileLog;
    args.error_output = &compileLog;

    ShaderDeclarations declarations;
    try {
        setupBuildArgs(args, shaderKey, project, declarations);
    } catch (const std::exception& e) {
        if (trackProgress) {
            ResourceProgress::failBuild(shaderKey);
        }
        Out::error("Shader %s failed: %s", getShaderDisplayName(shaderKey).c_str(), e.what());
        throw;
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 0.3f); // Setup complete
    }

    if (!shadercompiler::load_input(inputs, args)) {
        //printf("Error loading shader input\n");
        if (trackProgress) {
            ResourceProgress::failBuild(shaderKey);
        }
        throw std::runtime_error("Error loading shader input");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 0.5f); // Input loaded
    }

    std::vector<shadercompiler::spirv_t> spirvvec;
    spirvvec.resize(inputs.size());
    if (!shadercompiler::compile_to_spirv(spirvvec, inputs, args)) {
        if (trackProgress) {
            ResourceProgress::failBuild(shaderKey);
        }
        Out::error("Shader %s failed to compile:\n%s", getShaderDisplayName(shaderKey).c_str(),
                   compileLog.empty() ? "(no compiler output)" : compileLog.c_str());
        throw std::runtime_error("Error compiling to SPIRV");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 0.8f); // SPIRV compiled
    }

    std::vector<shadercompiler::spirvcross_t> spirvcrossvec;
    spirvcrossvec.resize(inputs.size());
    if (!shadercompiler::compile_to_lang(spirvcrossvec, spirvvec, inputs, args)) {
        if (trackProgress) {
            ResourceProgress::failBuild(shaderKey);
        }
        Out::error("Shader %s failed to cross-compile:\n%s", getShaderDisplayName(shaderKey).c_str(),
                   compileLog.empty() ? "(no compiler output)" : compileLog.c_str());
        throw std::runtime_error("Error cross-compiling");
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 0.9f); // Cross-compilation done
    }

    // Use a deterministic basename for stage naming and disk cache
    args.output_basename = ShaderPool::getShaderStr(shaderType, properties, customId) + getLangSuffix(args.lang, args.version, args.es, args.platform);
    ShaderData shaderData = convertToShaderData(spirvcrossvec, inputs, args);
    shaderData.parameters = std::move(declarations.parameters);
    shaderData.textureParameters = std::move(declarations.textures);
    shaderData.flags = declarations.flags;

    const std::string blockError = getShaderDataError(shaderData, customId);
    if (!blockError.empty()) {
        if (trackProgress) {
            ResourceProgress::failBuild(shaderKey);
        }
        Out::error("Shader %s failed: %s", getShaderDisplayName(shaderKey).c_str(), blockError.c_str());
        throw std::runtime_error(blockError);
    }

    if (shutdownRequested) {
        throw std::runtime_error("Shutdown requested");
    }
    if (trackProgress) {
        ResourceProgress::updateProgress(shaderKey, 1.0f); // Complete
        ResourceProgress::completeBuild(shaderKey);
    }

    Out::build(
        "Shader %s (%s) generated successfully",
        getShaderDisplayName(shaderKey).c_str(),
        ShaderPool::getShaderLangStr(ShaderPool::getShaderBackend()).c_str());

    return shaderData;
}

std::filesystem::path editor::ShaderBuilder::getShaderCachePath(ShaderKey shaderKey, Project* project) {
    if (!project) {
        return {};
    }

    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);
    uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);
    std::string basename = ShaderPool::getShaderStr(shaderType, properties, customId) +
                           "_" + ShaderPool::getShaderLangStr();

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

    // Validate/serialize with the storage key (customId stripped): a separate run
    // process assigns its own session-local customId, so the on-disk key must not
    // depend on it (the unique basename in the filename identifies the forked source).
    if (!ShaderDataSerializer::writeToFile(cachePath.string(), ShaderPool::getStorageKey(shaderKey), shaderData, err))
        return false;

    const uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);
    if (customId == 0)
        return true;

    const std::string customShader = ShaderPool::getCustomShaderName(customId);
    const std::shared_ptr<const CustomSourceSnapshot> sources = resolveCustomSources(project, customShader);
    if (!sources->valid) {
        if (err)
            *err = sources->error;
        return false;
    }

    std::ofstream signatureFile(sourceSignaturePath(cachePath), std::ios::binary | std::ios::trunc);
    if (!signatureFile) {
        if (err)
            *err = "Failed to write custom shader source signature.";
        return false;
    }
    signatureFile << sources->signature << '\n';
    if (!signatureFile) {
        if (err)
            *err = "Failed to write custom shader source signature.";
        return false;
    }
    return true;
}

bool editor::ShaderBuilder::isCustomCacheStale(ShaderKey shaderKey, Project* project, const std::filesystem::path& cachePath) {
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec))
        return true;

    const std::string customShader = ShaderPool::getCustomShaderName(ShaderPool::getCustomIdFromKey(shaderKey));
    if (customShader.empty())
        return true;

    const std::shared_ptr<const CustomSourceSnapshot> sources = resolveCustomSources(project, customShader);
    if (!sources->valid)
        return true;

    std::string cachedSignature;
    return !readSourceSignature(sourceSignaturePath(cachePath), cachedSignature) ||
           cachedSignature != sources->signature;
}

std::string editor::ShaderBuilder::getShaderDisplayName(ShaderKey key) {
    ShaderType type = ShaderPool::getShaderTypeFromKey(key);
    uint32_t properties = ShaderPool::getPropertiesFromKey(key);
    uint16_t customId = ShaderPool::getCustomIdFromKey(key);
    std::string shaderStr = ShaderPool::getShaderStr(type, properties, customId);

    std::string cliSpec;
    if (ShaderPool::getShaderCliSpec(shaderStr, cliSpec)) {
        return cliSpec;
    }

    return shaderStr;
}

void editor::ShaderBuilder::invalidateCustomShaders() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (auto it = shaderDataCache.begin(); it != shaderDataCache.end(); ) {
        if (ShaderPool::getCustomIdFromKey(it->first) != 0)
            it = shaderDataCache.erase(it);
        else
            ++it;
    }
    // Drop pending custom builds too, otherwise buildShader would reap a stale (pre-edit)
    // future into the cache. ThreadPoolManager futures don't block on destruction; an
    // in-flight task simply discards its result and the next get() restarts the build.
    for (auto it = pendingBuilds.begin(); it != pendingBuilds.end(); ) {
        if (ShaderPool::getCustomIdFromKey(it->first) != 0)
            it = pendingBuilds.erase(it);
        else
            ++it;
    }
    clearCustomSourceCache();
}

ShaderData& editor::ShaderBuilder::getShaderData(ShaderKey shaderKey) { 
    std::lock_guard<std::mutex> lock(cacheMutex);
    return shaderDataCache[shaderKey]; 
}

ShaderData editor::ShaderBuilder::buildShaderForExport(ShaderKey shaderKey, Project* project, ShaderBackend backend) {
    ShaderType shaderType = ShaderPool::getShaderTypeFromKey(shaderKey);
    uint32_t properties = ShaderPool::getPropertiesFromKey(shaderKey);
    uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);

    std::vector<shadercompiler::input_t> inputs;
    shadercompiler::args_t args = shadercompiler::initialize_args();
    args.isValid = true;
    args.useBuffers = true;
    args.fileBuffers = editor::shaderMap;
    applyShaderBackend(args, backend);

    ShaderDeclarations declarations;
    setupBuildArgs(args, shaderKey, project, declarations);

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

    args.output_basename = ShaderPool::getShaderStr(shaderType, properties, customId) +
                           getLangSuffix(args.lang, args.version, args.es, args.platform);
    ShaderData shaderData = convertToShaderData(spirvcrossvec, inputs, args);
    shaderData.parameters = std::move(declarations.parameters);
    shaderData.textureParameters = std::move(declarations.textures);
    shaderData.flags = declarations.flags;

    const std::string blockError = getShaderDataError(shaderData, customId);
    if (!blockError.empty())
        throw std::runtime_error(blockError);

    Out::build(
        "Shader %s (%s) generated successfully",
        getShaderDisplayName(shaderKey).c_str(),
        ShaderPool::getShaderLangStr(backend).c_str());

    return shaderData;
}

void editor::ShaderBuilder::compileHlslBytecode(ShaderData& shaderData) {
#ifdef _WIN32
    static const pD3DCompile d3dCompile = []() -> pD3DCompile {
        HMODULE dll = LoadLibraryA("d3dcompiler_47.dll");
        return dll ? reinterpret_cast<pD3DCompile>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    }();
    if (!d3dCompile) {
        return;
    }

    for (ShaderStage& stage : shaderData.stages) {
        // Target and flags of sokol_gfx compiling the source
        const char* target = (stage.type == ShaderStageType::VERTEX) ? "vs_5_0" : "ps_5_0";
        ID3DBlob* code = nullptr;
        ID3DBlob* messages = nullptr;
        HRESULT hr = d3dCompile(stage.source.c_str(), stage.source.size(), nullptr, nullptr, nullptr, "main", target,
            D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
        std::string error = (FAILED(hr) && messages) ? static_cast<const char*>(messages->GetBufferPointer()) : "";
        if (messages) messages->Release();
        if (FAILED(hr)) {
            if (code) code->Release();
            throw std::runtime_error("Error compiling HLSL: " + error);
        }

        stage.bytecode.size = static_cast<uint32_t>(code->GetBufferSize());
        stage.bytecode.data = new unsigned char[stage.bytecode.size];
        memcpy(stage.bytecode.data, code->GetBufferPointer(), stage.bytecode.size);
        stage.source.clear();
        code->Release();
    }
#endif
}
