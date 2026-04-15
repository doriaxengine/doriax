//
// (c) 2021 Eduardo Doria.
//

#include "shadercompiler.h"

#include <iostream>
#include <fstream>
#include <set>
#include <list>
#include <memory>

#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"
#include "glslang/MachineIndependent/localintermediate.h"

#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/SpvTools.h"
#include "SPIRV/disassemble.h"
#include "SPIRV/spirv.hpp"
#if ENABLE_OPT
#include "spirv-tools/libspirv.h"
#include "spirv-tools/optimizer.hpp"
#endif

using namespace shadercompiler;


// TODO: can be replaced with glslang/MachineIndependent/DirStackFileIncluder.h
class FileIncluder : public glslang::TShader::Includer {
private:
    std::vector<std::string> localDirectories;
    std::set<std::string> includedFiles;

    IncludeResult* newIncludeResult(const std::string& path, std::ifstream& file, int length) const {
        char* content = new char [length];
        file.seekg(0, file.beg);
        file.read(content, length);
        return new IncludeResult(path, content, length, content);
    }

    std::string getDirectory(const std::string path) const {
        size_t last = path.find_last_of("/\\");
        return last == std::string::npos ? "." : path.substr(0, last);
    }

public:
    virtual IncludeResult* includeLocal(const char* headerName, 
                                        const char* includerName, 
                                        size_t inclusionDepth) override {
        
        localDirectories.resize((int)inclusionDepth + (int)localDirectories.size());
        if (inclusionDepth == 1)
            localDirectories.back() = getDirectory(includerName);

        for (auto it = localDirectories.rbegin(); it != localDirectories.rend(); ++it) {
            std::string path = *it + '/' + headerName;
            std::replace(path.begin(), path.end(), '\\', '/');
            std::ifstream file(path, std::ios_base::binary | std::ios_base::ate);
            if (file) {
                includedFiles.insert(path);
                return newIncludeResult(path, file, (int)file.tellg());
            }
        }

        return nullptr;
    }

     // Not using search for a <system> path.
    virtual IncludeResult* includeSystem(const char* headerName,
                                         const char* /*includerName*/,
                                         size_t /*inclusionDepth*/) override {
        return nullptr;
    }

    virtual void releaseInclude(IncludeResult* result) override {
        if (result != nullptr) {
            delete [] static_cast<char*>(result->userData);
            delete result;
        }
    }

    virtual ~FileIncluder() override { }

    void addLocalDirectory(const std::string& dir) {
        localDirectories.push_back(dir);
    }

    virtual std::set<std::string> getIncludedFiles() {
        return includedFiles;
    }
};

class BufferIncluder : public glslang::TShader::Includer {
private:
    const std::unordered_map<std::string, std::string>& fileBuffers;
    std::set<std::string> includedFiles;

    IncludeResult* newIncludeResult(const std::string& path, const std::string& content) const {
        char* data = new char[content.size()];
        memcpy(data, content.data(), content.size());
        return new IncludeResult(path, data, content.size(), data);
    }

public:
    BufferIncluder(const std::unordered_map<std::string, std::string>& fileBuffers_)
        : fileBuffers(fileBuffers_) {}

    virtual IncludeResult* includeLocal(const char* headerName, 
                                        const char* includerName, 
                                        size_t /*inclusionDepth*/) override {
        std::string path(headerName);
        auto it = fileBuffers.find(path);
        if (it != fileBuffers.end()) {
            includedFiles.insert(path);
            return newIncludeResult(path, it->second);
        }
        return nullptr;
    }

    // Not using search for a <system> path.
    virtual IncludeResult* includeSystem(const char* headerName,
                                         const char* /*includerName*/,
                                         size_t /*inclusionDepth*/) override {
        return nullptr;
    }

    virtual void releaseInclude(IncludeResult* result) override {
        if (result != nullptr) {
            delete [] static_cast<char*>(result->userData);
            delete result;
        }
    }

    virtual ~BufferIncluder() override { }

    virtual std::set<std::string> getIncludedFiles() {
        return includedFiles;
    }
};


extern const TBuiltInResource DefaultTBuiltInResource;

static void output_error(const char* str, const char* header){
    if (str && str[0]){
        fprintf(stderr, "%s\n", header);
        fprintf(stderr, "%s\n", str);
    }
}

static void output_included_files(const std::set<std::string>& includedFiles){
    fprintf(stdout, "Included files:\n");
    for(auto f : includedFiles) {
        fprintf(stdout, "%s\n", f.c_str());
    }
}


static void cleanup_program_shaders(glslang::TProgram* program, std::list<glslang::TShader*>& shaders){
    // Program has to go before the shaders, look glslang StandAlone.cpp
    delete program;
    while (shaders.size() > 0) {
        delete shaders.back();
        shaders.pop_back();
    }
}

static EShLanguage get_stage(stage_type_t stage_type){
    if (stage_type == STAGE_VERTEX){
        return EShLangVertex;
    }else if (stage_type == STAGE_FRAGMENT){
        return EShLangFragment;
    }else{
        fprintf(stderr, "Not a valid stage input type");
        return EShLangVertex;
    }
}

static void add_defines(glslang::TShader* shader, const args_t& args, std::string& def){
    std::vector<std::string> processes;

    for (int i = 0; i < args.defines.size(); i++) {
        const define_t& d = args.defines[i];
        def += "#define " + d.def;
        if (!d.value.empty()) {
            def += std::string(" ") + d.value;
        }
        def += std::string("\n");

        processes.push_back("define-macro " + d.def);
        if (!d.value.empty()){
            processes.back().append("=" + d.value);
        }
    }

    shader->setPreamble(def.c_str());
    shader->addProcesses(processes);
}

#if ENABLE_OPT
//
// Start modified part of SpvTools.cpp/SpirvToolsTransform to work with WEBGL1 and HLSL shaders
// Check this if changed glslang version
//

// Callback passed to spvtools::Optimizer::SetMessageConsumer
void OptimizerMesssageConsumer(spv_message_level_t level, const char *source,
        const spv_position_t &position, const char *message)
{
    auto &out = std::cerr;
    switch (level)
    {
    case SPV_MSG_FATAL:
    case SPV_MSG_INTERNAL_ERROR:
    case SPV_MSG_ERROR:
        out << "error: ";
        break;
    case SPV_MSG_WARNING:
        out << "warning: ";
        break;
    case SPV_MSG_INFO:
    case SPV_MSG_DEBUG:
        out << "info: ";
        break;
    default:
        break;
    }
    if (source)
    {
        out << source << ":";
    }
    out << position.line << ":" << position.column << ":" << position.index << ":";
    if (message)
    {
        out << " " << message;
    }
    out << std::endl;
}

// Apply the SPIRV-Tools optimizer to generated SPIR-V.  HLSL SPIR-V is legalized in the process.
void spirv_optimize(const glslang::TIntermediate& intermediate, std::vector<unsigned int>& spirv,
                         spv::SpvBuildLogger* logger, const glslang::SpvOptions* options)
{
    spv_target_env target_env = glslang::MapToSpirvToolsEnv(intermediate.getSpv(), logger);

    spvtools::Optimizer optimizer(target_env);
    optimizer.SetMessageConsumer(OptimizerMesssageConsumer);

    // If debug (specifically source line info) is being generated, propagate
    // line information into all SPIR-V instructions. This avoids loss of
    // information when instructions are deleted or moved. Later, remove
    // redundant information to minimize final SPRIR-V size.
    if (options->stripDebugInfo) {
        optimizer.RegisterPass(spvtools::CreateStripDebugInfoPass());
    }
    optimizer.RegisterPass(spvtools::CreateWrapOpKillPass());
    optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
    //optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
    //optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
    optimizer.RegisterPass(spvtools::CreateEliminateDeadFunctionsPass());
    optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
    optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
    optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
    optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
    optimizer.RegisterPass(spvtools::CreateSimplificationPass());
    optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
    optimizer.RegisterPass(spvtools::CreateVectorDCEPass());
    optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
    optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
    optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
    //optimizer.RegisterPass(spvtools::CreateBlockMergePass());
    //optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
    optimizer.RegisterPass(spvtools::CreateIfConversionPass());
    optimizer.RegisterPass(spvtools::CreateSimplificationPass());
    optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
    optimizer.RegisterPass(spvtools::CreateVectorDCEPass());
    optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
    optimizer.RegisterPass(spvtools::CreateInterpolateFixupPass());
    if (options->optimizeSize) {
        optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
        optimizer.RegisterPass(spvtools::CreateEliminateDeadInputComponentsSafePass());
    }
    optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
    optimizer.RegisterPass(spvtools::CreateCFGCleanupPass());

    spvtools::OptimizerOptions spvOptOptions;
    if (options->optimizerAllowExpandedIDBound)
        spvOptOptions.set_max_id_bound(0x3FFFFFFF);
    optimizer.SetTargetEnv(glslang::MapToSpirvToolsEnv(intermediate.getSpv(), logger));
    spvOptOptions.set_run_validator(false); // The validator may run as a separate step later on
    optimizer.Run(spirv.data(), spirv.size(), &spirv, spvOptOptions);

    if (options->optimizerAllowExpandedIDBound) {
        if (spirv.size() > 3 && spirv[3] > kDefaultMaxIdBound) {
            spvtools::Optimizer optimizer2(target_env);
            optimizer2.SetMessageConsumer(OptimizerMesssageConsumer);
            optimizer2.RegisterPass(spvtools::CreateCompactIdsPass());
            optimizer2.Run(spirv.data(), spirv.size(), &spirv, spvOptOptions);
        }
    }
}
//
// End modified part of SpvTools.cpp/SpirvToolsTransform
//
#endif

//
// Check this if changed in glslang/ResourceLimits/ResourceLimits.cpp
//

const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,
    /* .maxMeshOutputVerticesEXT = */ 256,
    /* .maxMeshOutputPrimitivesEXT = */ 256,
    /* .maxMeshWorkGroupSizeX_EXT = */ 128,
    /* .maxMeshWorkGroupSizeY_EXT = */ 128,
    /* .maxMeshWorkGroupSizeZ_EXT = */ 128,
    /* .maxTaskWorkGroupSizeX_EXT = */ 128,
    /* .maxTaskWorkGroupSizeY_EXT = */ 128,
    /* .maxTaskWorkGroupSizeZ_EXT = */ 128,
    /* .maxMeshViewCountEXT = */ 4,
    /* .maxDualSourceDrawBuffersEXT = */ 1,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }};

bool shadercompiler::compile_to_spirv(std::vector<spirv_t>& spirvvec, const std::vector<input_t>& inputs, const args_t& args){
    glslang::InitializeProcess();

    std::list<glslang::TShader*> shaders;

    std::unique_ptr<glslang::TShader::Includer> includer;
    if (args.useBuffers) {
        includer = std::make_unique<BufferIncluder>(args.fileBuffers);
    } else {
        includer = std::make_unique<FileIncluder>();
        if (!args.include_dir.empty())
            static_cast<FileIncluder*>(includer.get())->addLocalDirectory(args.include_dir);
    }

    glslang::TProgram* program = new glslang::TProgram;

    EShMessages messages = EShMsgDefault;
    int default_version = 100; // 110 for desktop

    // To be used in layout(location = SEMANTIC) for HLSL
    std::string semantics_def;
    for (int i = 0; i < VERTEX_ATTRIB_COUNT; i++) {
        semantics_def += std::string("#define " + std::string(k_attrib_names[i]) + " " + std::to_string(i) + "\n");
    }

    // For more HLSL compatibility
    for (int i = 0; i < 8; i++) {
        semantics_def += std::string("#define SV_Target" + std::to_string(i) + " " + std::to_string(i) + "\n");
    }

    for (int i = 0; i < inputs.size(); i++){
        std::string def("#extension GL_GOOGLE_include_directive : require\n");
        def += semantics_def;

        if (args.lang == LANG_GLSL && args.version == 100) {
            def += std::string("#define flat\n");
        }

        if (args.lang == LANG_GLSL){
            def += std::string("#define IS_GLSL\n");
            if (args.es){
                def += std::string("#define IS_GLES\n");
            }
        }else if (args.lang == LANG_HLSL){
            def += std::string("#define IS_HLSL\n");
        }else if (args.lang == LANG_MSL){
            def += std::string("#define IS_MSL\n");
        }

        EShLanguage stage = get_stage(inputs[i].stage_type);

        glslang::TShader* shader = new glslang::TShader(stage);

        const char* sources[1] = { inputs[i].source.c_str() };
        const int sourcesLen[1] = { (int) inputs[i].source.length() };
        const char* sourcesNames[1] = { inputs[i].filename.c_str() };

        shader->setStringsWithLengthsAndNames(sources, sourcesLen, sourcesNames, 1);
        shader->setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, default_version);
        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader->setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_0);

        shader->setAutoMapLocations(true);
        shader->setAutoMapBindings(true);

        add_defines(shader, args, def);

        shaders.push_back(shader);

        bool parse_success = shader->parse(&DefaultTBuiltInResource, default_version, false, messages, *includer);
        output_error(shader->getInfoLog(), ("File: " + inputs[i].filename).c_str());
        output_error(shader->getInfoDebugLog(), ("File: " + inputs[i].filename).c_str());
        if (!parse_success) {
            cleanup_program_shaders(program, shaders);
            return false;
        }

        program->addShader(shader);
    }

    bool link_success = program->link(messages);
    output_error(program->getInfoLog(), "Link failed:");
    output_error(program->getInfoDebugLog(), "Link failed:");
    if (!link_success) {
        cleanup_program_shaders(program, shaders);
        return false;
    }

    bool map_success = program->mapIO();
    output_error(program->getInfoLog(), "MapIO failed:");
    output_error(program->getInfoDebugLog(), "MapIO failed:");
    if (!map_success) {
        cleanup_program_shaders(program, shaders);
        return false;
    }

    for (int i = 0; i < inputs.size(); i++){

        glslang::SpvOptions spv_opts;
        spv_opts.validate = true;
        spv_opts.optimizeSize = true;
        // Disable this glslang internal optimizer that is broken with WEBGL1 and HLSL shaders
        spv_opts.disableOptimizer = true;
        spv::SpvBuildLogger logger;
        const glslang::TIntermediate* im = program->getIntermediate(get_stage(inputs[i].stage_type));
        if (im){
            glslang::GlslangToSpv(*im, spirvvec[i].bytecode, &logger, &spv_opts);
            // It is the same of glslang optimizer with some parts removed
            #if ENABLE_OPT
            if (args.optimization){
                spirv_optimize(*im, spirvvec[i].bytecode, &logger, &spv_opts);
            }
            #endif
            if (!logger.getAllMessages().empty())
                puts(logger.getAllMessages().c_str());
        }
    }

    if (args.list_includes)
        output_included_files(args.useBuffers 
            ? static_cast<BufferIncluder*>(includer.get())->getIncludedFiles()
            : static_cast<FileIncluder*>(includer.get())->getIncludedFiles());

    cleanup_program_shaders(program, shaders);
    glslang::FinalizeProcess();
    return true;
}
