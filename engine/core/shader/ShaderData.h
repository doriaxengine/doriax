// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERDATA_H
#define SHADERDATA_H

#include <string>
#include <vector>
#include <cstdint>
#include "render/Render.h"
#include "math/Vector4.h"

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

    // OBJECT: the component's shader uniforms, SCENE and VIEW: shared by a scene or a camera,
    // ENGINE: the effect context the renderer writes
    enum class ShaderParameterScope{
        OBJECT,
        SCENE,
        VIEW,
        ENGINE
    };

    // a parameter a shader declares, member being its name in the generated block
    struct ShaderParameterInfo{
        ShaderStageType stage = ShaderStageType::VERTEX;
        std::string member;
        ShaderParameterScope scope = ShaderParameterScope::OBJECT;
        std::string collection;
        std::string name;
        ShaderUniformType type = ShaderUniformType::FLOAT4;
        Vector4 defaultValue = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    enum class ShaderTextureDefault{
        WHITE,
        BLACK,
        NORMAL
    };

    // a declared texture; those of one filter and wrap share a sampler
    struct ShaderTextureInfo{
        ShaderStageType stage = ShaderStageType::FRAGMENT;
        std::string name;
        TextureType type = TextureType::TEXTURE_2D;
        ShaderTextureDefault defaultTexture = ShaderTextureDefault::WHITE;
        TextureFilter filter = TextureFilter::LINEAR;
        TextureWrap wrap = TextureWrap::REPEAT;

        // uniform names
        std::string getTextureName() const { return name + "_texture"; }
        std::string getSamplerName() const;
    };

    // what a shader's hooks ask of the renderer
    enum ShaderFlags : uint32_t {
        ShaderFlag_CoverageShadows = 1 << 0, // shadow maps follow getCoverage()
        ShaderFlag_Unlit = 1 << 1            // compiled unlit whatever its key says
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
        std::vector<ShaderParameterInfo> parameters;
        std::vector<ShaderTextureInfo> textureParameters;
        uint32_t flags = 0;

        ShaderData();
        virtual ~ShaderData();

        ShaderData(const ShaderData& d);
        ShaderData& operator = (const ShaderData& d);

        int getAttrIndex(AttributeType type);
        int getUniformBlockIndex(UniformBlockType type);
        int getStorageBufferIndex(StorageBufferType type);
        std::pair<int, int> getTextureIndex(TextureShaderType type);

        // Same lookups by reflected name, for shaders whose bindings are not a fixed
        // enum (user post-process passes). Missing names give -1.
        int getUniformBlockIndexByName(const std::string& name);
        std::pair<int, int> getTextureIndexByName(const std::string& name);
        // view and sampler slots of a declared texture
        std::pair<int, int> getTextureParameterIndex(const ShaderTextureInfo& texture);

        // block members, to build its byte blob by name
        const std::vector<ShaderUniform>* getUniformBlockMembers(const std::string& name, unsigned int& sizeBytes);
        // members are reflected as "instance.member"; the short name is what is stored
        static std::string getUniformShortName(const std::string& name);

        void releaseSourceData();
    };

}


#endif //SHADERDATA_H
