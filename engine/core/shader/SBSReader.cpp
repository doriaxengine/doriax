//
// (c) 2026 Eduardo Doria.
//

#include "SBSReader.h"

#include "io/File.h"
#include "io/Data.h"
#include "Log.h"

#define makefourcc(_a, _b, _c, _d) (((uint32_t)(_a) | ((uint32_t)(_b) << 8) | ((uint32_t)(_c) << 16) | ((uint32_t)(_d) << 24)))

#define SBS_NAME_SIZE 64

//----------Begin SBS format---------------
#pragma pack(push, 1)

#define SBS_CHUNK               makefourcc('S', 'B', 'S', ' ')
#define SBS_CHUNK_STAG          makefourcc('S', 'T', 'A', 'G')
#define SBS_CHUNK_CODE          makefourcc('C', 'O', 'D', 'E')
#define SBS_CHUNK_DATA          makefourcc('D', 'A', 'T', 'A')
#define SBS_CHUNK_REFL          makefourcc('R', 'E', 'F', 'L')

#define SBS_STAGE_VERTEX        makefourcc('V', 'E', 'R', 'T')
#define SBS_STAGE_FRAGMENT      makefourcc('F', 'R', 'A', 'G')

#define SBS_LANG_HLSL           makefourcc('H', 'L', 'S', 'L')
#define SBS_LANG_GLSL           makefourcc('G', 'L', 'S', 'L')
#define SBS_LANG_MSL            makefourcc('M', 'S', 'L', ' ')

#define SBS_VERTEXTYPE_FLOAT    makefourcc('F', 'L', 'T', '1')
#define SBS_VERTEXTYPE_FLOAT2   makefourcc('F', 'L', 'T', '2')
#define SBS_VERTEXTYPE_FLOAT3   makefourcc('F', 'L', 'T', '3')
#define SBS_VERTEXTYPE_FLOAT4   makefourcc('F', 'L', 'T', '4')
#define SBS_VERTEXTYPE_INT      makefourcc('I', 'N', 'T', '1')
#define SBS_VERTEXTYPE_INT2     makefourcc('I', 'N', 'T', '2')
#define SBS_VERTEXTYPE_INT3     makefourcc('I', 'N', 'T', '3')
#define SBS_VERTEXTYPE_INT4     makefourcc('I', 'N', 'T', '4')

#define SBS_UNIFORMTYPE_FLOAT    makefourcc('F', 'L', 'T', '1')
#define SBS_UNIFORMTYPE_FLOAT2   makefourcc('F', 'L', 'T', '2')
#define SBS_UNIFORMTYPE_FLOAT3   makefourcc('F', 'L', 'T', '3')
#define SBS_UNIFORMTYPE_FLOAT4   makefourcc('F', 'L', 'T', '4')
#define SBS_UNIFORMTYPE_INT      makefourcc('I', 'N', 'T', '1')
#define SBS_UNIFORMTYPE_INT2     makefourcc('I', 'N', 'T', '2')
#define SBS_UNIFORMTYPE_INT3     makefourcc('I', 'N', 'T', '3')
#define SBS_UNIFORMTYPE_INT4     makefourcc('I', 'N', 'T', '4')
#define SBS_UNIFORMTYPE_MAT3     makefourcc('M', 'A', 'T', '3')
#define SBS_UNIFORMTYPE_MAT4     makefourcc('M', 'A', 'T', '4')

#define SBS_STORAGEBUFFERTYPE_STRUCT     makefourcc('S', 'T', 'R', 'C')

#define SBS_TEXTURE_2D          makefourcc('2', 'D', ' ', ' ')
#define SBS_TEXTURE_3D          makefourcc('3', 'D', ' ', ' ')
#define SBS_TEXTURE_CUBE        makefourcc('C', 'U', 'B', 'E')
#define SBS_TEXTURE_ARRAY       makefourcc('A', 'R', 'R', 'A')

#define SBS_TEXTURE_SAMPLERTYPE_FLOAT   makefourcc('T', 'F', 'L', 'T')
#define SBS_TEXTURE_SAMPLERTYPE_SINT    makefourcc('T', 'I', 'N', 'T')
#define SBS_TEXTURE_SAMPLERTYPE_UINT    makefourcc('T', 'U', 'I', 'T')
#define SBS_TEXTURE_SAMPLERTYPE_DEPTH   makefourcc('T', 'D', 'P', 'H')

#define SBS_SAMPLERTYPE_FILTERING     makefourcc('S', 'F', 'I', 'L')
#define SBS_SAMPLERTYPE_COMPARISON    makefourcc('S', 'C', 'O', 'M')

struct sbs_chunk {
    uint32_t sbs_version;
    uint32_t lang;
    uint32_t version;
    uint16_t es;
};

// STAG
struct sbs_stage {
    uint32_t type;
};

// REFL
struct sbs_chunk_refl {
    char     name[SBS_NAME_SIZE];
    uint32_t num_inputs;
    uint32_t num_textures;
    uint32_t num_samplers;
    uint32_t num_texture_sampler_pairs;
    uint32_t num_uniform_blocks;
    uint32_t num_uniforms;
    uint32_t num_storage_buffers;
};

struct sbs_refl_input {
    char     name[SBS_NAME_SIZE];
    int32_t  location;
    char     semantic_name[SBS_NAME_SIZE];
    uint32_t semantic_index;
    uint32_t type;
};

struct sbs_refl_texture {
    char     name[SBS_NAME_SIZE];
    uint32_t set;
    int32_t  binding;
    uint32_t type;
    uint32_t sampler_type;
};

struct sbs_refl_sampler {
    char     name[SBS_NAME_SIZE];
    uint32_t set;
    int32_t  binding;
    uint32_t type;
}; 

struct sbs_refl_texture_sampler_pair {
    char     name[SBS_NAME_SIZE];
    char     texture_name[SBS_NAME_SIZE];
    char     sampler_name[SBS_NAME_SIZE];
}; 

struct sbs_refl_uniformblock {
    uint32_t num_uniforms;
    char     name[SBS_NAME_SIZE];
    char     inst_name[SBS_NAME_SIZE];
    uint32_t set;
    int32_t  binding;
    uint32_t size_bytes;
    bool     flattened;
};

struct sbs_refl_uniform {
    char     name[SBS_NAME_SIZE];
    uint32_t type;
    uint32_t array_count;
    uint32_t offset;
};

struct sbs_refl_storagebuffer {
    char     name[SBS_NAME_SIZE];
    char     inst_name[SBS_NAME_SIZE];
    uint32_t set;
    int32_t  binding;
    uint32_t size_bytes;
    bool     readonly;
    uint32_t type;
};

#pragma pack(pop)
//----------End SBS format---------------


using namespace doriax;


SBSReader::SBSReader(){

}

SBSReader::~SBSReader(){
    for (int i = 0; i < shaderData.stages.size(); i++){
        if (shaderData.stages[i].bytecode.data)
            delete shaderData.stages[i].bytecode.data;
    }
}

bool SBSReader::read(const std::string& filepath){
    File file;

    if (file.open(filepath.c_str()) != FileErrors::FILEDATA_OK){
        Log::error("Cannot open SBS file: %s", filepath.c_str());
        return false;
    }

    return read(file);
}

bool SBSReader::read(std::vector<unsigned char> datashader){
    Data data;

    if (data.open(&(datashader.front()), datashader.size(), false, false) != FileErrors::FILEDATA_OK){
        Log::error("Cannot read SBS file");
        return false;
    }

    return read(data);
}

bool SBSReader::read(FileData& file){
    uint32_t sbs = file.read32();
    if (sbs != SBS_CHUNK) {
        Log::error("Invalid sbs file format");
        return false;
    }

    file.seek(file.pos()+sizeof(uint32_t));

    sbs_chunk sinfo;
    file.read((unsigned char*)&sinfo, sizeof(sinfo));

    if (sinfo.sbs_version != 130){
        Log::error("Invalid sbs file version");
        return false;
    }

    shaderData.version = sinfo.version;
    shaderData.es = sinfo.es;

    if (sinfo.lang == SBS_LANG_GLSL){
        shaderData.lang = ShaderLang::GLSL;
    }else if (sinfo.lang == SBS_LANG_HLSL){
        shaderData.lang = ShaderLang::HLSL;
    }else if (sinfo.lang == SBS_LANG_MSL){
        shaderData.lang = ShaderLang::MSL;
    }

    uint32_t stag = file.read32();
    uint32_t stagSize = file.read32();

    ShaderStage* shaderStage;

    unsigned int numTexs = 0;
    unsigned int numSams = 0;
    unsigned int numPairs = 0;
    unsigned int numUBs = 0;
    unsigned int numSBs = 0;

    while (stag == SBS_CHUNK_STAG){
        uint32_t stagPos = file.pos();

        sbs_stage sbsstage;
        file.read((unsigned char*)&sbsstage, sizeof(sbs_stage));

        shaderData.stages.push_back({});
        shaderStage = &shaderData.stages.back();
        if (sbsstage.type == SBS_STAGE_VERTEX) {
            shaderStage->type = ShaderStageType::VERTEX;
        } else if (sbsstage.type == SBS_STAGE_FRAGMENT) {
            shaderStage->type = ShaderStageType::FRAGMENT;
        } else {
            Log::error("SBS Stage not implemented");
            return false;
        }

        uint32_t code = file.read32();
        uint32_t codeSize = file.read32();

        if (code == SBS_CHUNK_CODE){
            shaderStage->source = file.readString(codeSize);
        }else if (code == SBS_CHUNK_DATA){
            shaderStage->bytecode.data = new unsigned char[codeSize];
            shaderStage->bytecode.size = codeSize;
            file.read((unsigned char*)&shaderStage->bytecode.data, codeSize);
        }

        uint32_t refl = file.read32();
        uint32_t reflSize = file.read32();

        if (refl == SBS_CHUNK_REFL){
            sbs_chunk_refl refl_chunk;
            file.read((unsigned char*)&refl_chunk, sizeof(refl_chunk));

            shaderStage->name = std::string(refl_chunk.name);

            for (uint32_t i = 0; i < refl_chunk.num_inputs; i++) {
                sbs_refl_input in;
                file.read((unsigned char*)&in, sizeof(in));

                ShaderAttr attr;
                attr.location = in.location;
                attr.name = std::string(in.name);
                attr.semanticName = std::string(in.semantic_name);
                attr.semanticIndex = in.semantic_index;
                if (in.type == SBS_VERTEXTYPE_FLOAT){
                    attr.type = ShaderVertexType::FLOAT;
                }else if (in.type == SBS_VERTEXTYPE_FLOAT2){
                    attr.type = ShaderVertexType::FLOAT2;
                }else if (in.type == SBS_VERTEXTYPE_FLOAT3){
                    attr.type = ShaderVertexType::FLOAT3;
                }else if (in.type == SBS_VERTEXTYPE_FLOAT4){
                    attr.type = ShaderVertexType::FLOAT4;
                }else if (in.type == SBS_VERTEXTYPE_INT){
                    attr.type = ShaderVertexType::INT;
                }else if (in.type == SBS_VERTEXTYPE_INT2){
                    attr.type = ShaderVertexType::INT2;
                }else if (in.type == SBS_VERTEXTYPE_INT3){
                    attr.type = ShaderVertexType::INT3;
                }else if (in.type == SBS_VERTEXTYPE_INT4){
                    attr.type = ShaderVertexType::INT4;
                }

                shaderStage->attributes.push_back(attr);
            }

            for (uint32_t i = 0; i < refl_chunk.num_textures; i++) {
                sbs_refl_texture t;
                file.read((unsigned char*)&t, sizeof(t));

                ShaderTexture texture;
                texture.name = std::string(t.name);
                texture.set = t.set;
                texture.binding = t.binding;
                texture.slot = numTexs++;

                if (t.type == SBS_TEXTURE_2D){
                    texture.type = TextureType::TEXTURE_2D;
                }else if (t.type == SBS_TEXTURE_3D){
                    texture.type = TextureType::TEXTURE_3D;
                }else if (t.type == SBS_TEXTURE_CUBE){
                    texture.type = TextureType::TEXTURE_CUBE;
                }else if (t.type == SBS_TEXTURE_ARRAY){
                    texture.type = TextureType::TEXTURE_ARRAY;
                }else{
                    texture.type = TextureType::TEXTURE_2D;
                }

                if (t.sampler_type == SBS_TEXTURE_SAMPLERTYPE_FLOAT){
                    texture.samplerType = TextureSamplerType::FLOAT;
                }else if (t.sampler_type == SBS_TEXTURE_SAMPLERTYPE_UINT){
                    texture.samplerType = TextureSamplerType::UINT;
                }else if (t.sampler_type == SBS_TEXTURE_SAMPLERTYPE_SINT){
                    texture.samplerType = TextureSamplerType::SINT;
                }else if (t.sampler_type == SBS_TEXTURE_SAMPLERTYPE_DEPTH){
                    texture.samplerType = TextureSamplerType::DEPTH;
                }else{
                    texture.samplerType = TextureSamplerType::FLOAT;
                }

                shaderStage->textures.push_back(texture);
            }

            for (uint32_t i = 0; i < refl_chunk.num_samplers; i++) {
                sbs_refl_sampler sm;
                file.read((unsigned char*)&sm, sizeof(sm));

                ShaderSampler sampler;
                sampler.name = std::string(sm.name);
                sampler.set = sm.set;
                sampler.binding = sm.binding;
                sampler.slot = numSams++;

                if (sm.type == SBS_SAMPLERTYPE_COMPARISON){
                    sampler.type = SamplerType::COMPARISON;
                }else if (sm.type == SBS_SAMPLERTYPE_FILTERING){
                    sampler.type = SamplerType::FILTERING;
                }

                shaderStage->samplers.push_back(sampler);
            }

            for (uint32_t i = 0; i < refl_chunk.num_texture_sampler_pairs; i++) {
                sbs_refl_texture_sampler_pair tsm;
                file.read((unsigned char*)&tsm, sizeof(tsm));

                ShaderTextureSamplerPair texturesamplerpair;
                texturesamplerpair.name = std::string(tsm.name);
                texturesamplerpair.textureName = std::string(tsm.texture_name);
                texturesamplerpair.samplerName = std::string(tsm.sampler_name);
                texturesamplerpair.slot = numPairs++;

                shaderStage->textureSamplerPairs.push_back(texturesamplerpair);
            }

            for (uint32_t i = 0; i < refl_chunk.num_uniform_blocks; i++) {
                sbs_refl_uniformblock ub;
                file.read((unsigned char*)&ub, sizeof(ub));

                ShaderUniformBlock uniformblock;
                uniformblock.name = std::string(ub.name);
                uniformblock.instName = std::string(ub.inst_name);
                uniformblock.set = ub.set;
                uniformblock.binding = ub.binding;
                uniformblock.slot = numUBs++;
                uniformblock.sizeBytes = ub.size_bytes;
                uniformblock.flattened = ub.flattened;

                for (uint32_t j = 0; j < ub.num_uniforms; j++) {
                    sbs_refl_uniform u;
                    file.read((unsigned char*)&u, sizeof(u));

                    ShaderUniform uniform;
                    uniform.name = std::string(ub.inst_name) + "." + std::string(u.name);
                    uniform.arrayCount = u.array_count;
                    uniform.offset = u.offset;
                    if (u.type == SBS_UNIFORMTYPE_FLOAT){
                        uniform.type = ShaderUniformType::FLOAT;
                    }else if (u.type == SBS_UNIFORMTYPE_FLOAT2){
                        uniform.type = ShaderUniformType::FLOAT2;
                    }else if (u.type == SBS_UNIFORMTYPE_FLOAT3){
                        uniform.type = ShaderUniformType::FLOAT3;
                    }else if (u.type == SBS_UNIFORMTYPE_FLOAT4){
                        uniform.type = ShaderUniformType::FLOAT4;
                    }else if (u.type == SBS_UNIFORMTYPE_INT){
                        uniform.type = ShaderUniformType::INT;
                    }else if (u.type == SBS_UNIFORMTYPE_INT2){
                        uniform.type = ShaderUniformType::INT2;
                    }else if (u.type == SBS_UNIFORMTYPE_INT3){
                        uniform.type = ShaderUniformType::INT3;
                    }else if (u.type == SBS_UNIFORMTYPE_INT4){
                        uniform.type = ShaderUniformType::INT4;
                    }else if (u.type == SBS_UNIFORMTYPE_MAT3){
                        uniform.type = ShaderUniformType::MAT3;
                    }else if (u.type == SBS_UNIFORMTYPE_MAT4){
                        uniform.type = ShaderUniformType::MAT4;
                    }

                    uniformblock.uniforms.push_back(uniform);
                }

                shaderStage->uniformblocks.push_back(uniformblock);
            }

            for (uint32_t i = 0; i < refl_chunk.num_storage_buffers; i++) {
                sbs_refl_storagebuffer sb;
                file.read((unsigned char*)&sb, sizeof(sb));

                ShaderStorageBuffer storagebuffer;
                storagebuffer.name = std::string(sb.name);
                storagebuffer.instName = std::string(sb.inst_name);
                storagebuffer.set = sb.set;
                storagebuffer.binding = sb.binding;
                storagebuffer.slot = numSBs++;
                storagebuffer.sizeBytes = sb.size_bytes;
                storagebuffer.readonly = sb.readonly;
                if (sb.type == SBS_STORAGEBUFFERTYPE_STRUCT){
                    storagebuffer.type = ShaderStorageBufferType::STRUCT;
                }

                shaderStage->storagebuffers.push_back(storagebuffer);
            }
        }

        //Next STAG
        file.seek(stagPos + stagSize);
        stag = file.read32();
        stagSize = file.read32();
    }

    return true;
}

ShaderData& SBSReader::getShaderData(){
    return shaderData;
}