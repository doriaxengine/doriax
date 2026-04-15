//
// (c) 2026 Eduardo Doria.
//

#ifndef SHADERDATA_H
#define SHADERDATA_H

#include <string>
#include <vector>
#include <cstdint>
#include "render/Render.h"

namespace doriax {

    struct ShaderAttr{
        std::string name;
        std::string semanticName;
        unsigned int semanticIndex;
        int location;
        ShaderVertexType type;
    };

    struct ShaderUniform {
        std::string name;
        ShaderUniformType type;
        unsigned int arrayCount;
        unsigned int offset;
    };

    struct ShaderUniformBlock {
        std::string name;
        std::string instName;
        int set;
        int binding;
        unsigned int slot;
        unsigned int sizeBytes;
        bool flattened;

        std::vector<ShaderUniform> uniforms;
    };

    struct ShaderStorageBuffer {
        std::string name;
        std::string instName;
        int set;
        int binding;
        unsigned int slot;
        unsigned int sizeBytes;
        bool readonly;

        ShaderStorageBufferType type;
    };

    struct ShaderTexture {
        std::string name;
        int set;
        int binding;
        unsigned int slot;
        TextureType type;
        TextureSamplerType samplerType;
    };

    struct ShaderSampler {
        std::string name;
        int set;
        int binding;
        unsigned int slot;
        SamplerType type;
    };

    struct ShaderTextureSamplerPair {
        std::string name;
        std::string textureName;
        std::string samplerName;
        unsigned int slot;
    };

    struct ShaderBinSource{
        unsigned char* data = NULL;
        uint32_t size;
    };

    struct ShaderStage{
        ShaderStageType type;
        std::string name;

        std::string source;
        ShaderBinSource bytecode;

        std::vector<ShaderAttr> attributes;
        std::vector<ShaderUniformBlock> uniformblocks;
        std::vector<ShaderStorageBuffer> storagebuffers;
        std::vector<ShaderTexture> textures;
        std::vector<ShaderSampler> samplers;
        std::vector<ShaderTextureSamplerPair> textureSamplerPairs;
    };

    class DORIAX_API ShaderData {
    public:
        ShaderLang lang;
        unsigned int version;
        bool es;
        std::vector<ShaderStage> stages;


        ShaderData();
        virtual ~ShaderData();

        ShaderData(const ShaderData& d);
        ShaderData& operator = (const ShaderData& d);

        int getAttrIndex(AttributeType type);
        int getUniformBlockIndex(UniformBlockType type);
        int getStorageBufferIndex(StorageBufferType type);
        std::pair<int, int> getTextureIndex(TextureShaderType type);

        void releaseSourceData();
    };

}


#endif //SHADERDATA_H
