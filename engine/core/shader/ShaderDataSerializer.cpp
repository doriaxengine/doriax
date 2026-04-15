//
// (c) 2026 Eduardo Doria.
//

#include "ShaderDataSerializer.h"

#include "io/File.h"

#include <limits>
#include <vector>

namespace {

constexpr uint32_t SDAT_MAGIC = 0x54414453; // 'S''D''A''T' little-endian
constexpr uint32_t SDAT_VERSION = 1;

// Sanity limits to guard against malformed files
constexpr uint32_t kMaxString = 32u * 1024u * 1024u; // 32MB
constexpr uint32_t kMaxStages = 16;
constexpr uint32_t kMaxAttributes = 64;
constexpr uint32_t kMaxUniformBlocks = 32;
constexpr uint32_t kMaxUniforms = 128;
constexpr uint32_t kMaxStorageBuffers = 32;
constexpr uint32_t kMaxTextures = 64;
constexpr uint32_t kMaxSamplers = 64;
constexpr uint32_t kMaxTextureSamplerPairs = 64;

static bool setErr(std::string* err, const std::string& msg) {
    if (err) {
        *err = msg;
    }
    return false;
}

// Little-endian write helpers (portable across architectures)
static void writeU32(doriax::File& out, uint32_t v) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(v),
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 24)
    };
    out.write(buf, 4);
}

static void writeU64(doriax::File& out, uint64_t v) {
    uint8_t buf[8] = {
        static_cast<uint8_t>(v),
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 24),
        static_cast<uint8_t>(v >> 32),
        static_cast<uint8_t>(v >> 40),
        static_cast<uint8_t>(v >> 48),
        static_cast<uint8_t>(v >> 56)
    };
    out.write(buf, 8);
}

static void writeI32(doriax::File& out, int32_t v) {
    writeU32(out, static_cast<uint32_t>(v));
}

static void writeU8(doriax::File& out, uint8_t v) {
    out.write(&v, sizeof(v));
}

static void writeBool(doriax::File& out, bool v) {
    uint8_t b = v ? 1 : 0;
    writeU8(out, b);
}

static void writeString(doriax::File& out, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    writeU32(out, len);
    if (len > 0) {
        out.write((unsigned char*)s.data(), len);
    }
}

// Little-endian read helpers (portable across architectures)
static bool readU32(doriax::File& in, uint32_t& v) {
    uint8_t buf[4];
    if (in.read(buf, 4) != 4) return false;
    v = static_cast<uint32_t>(buf[0])
      | (static_cast<uint32_t>(buf[1]) << 8)
      | (static_cast<uint32_t>(buf[2]) << 16)
      | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

static bool readU64(doriax::File& in, uint64_t& v) {
    uint8_t buf[8];
    if (in.read(buf, 8) != 8) return false;
    v = static_cast<uint64_t>(buf[0])
      | (static_cast<uint64_t>(buf[1]) << 8)
      | (static_cast<uint64_t>(buf[2]) << 16)
      | (static_cast<uint64_t>(buf[3]) << 24)
      | (static_cast<uint64_t>(buf[4]) << 32)
      | (static_cast<uint64_t>(buf[5]) << 40)
      | (static_cast<uint64_t>(buf[6]) << 48)
      | (static_cast<uint64_t>(buf[7]) << 56);
    return true;
}

static bool readI32(doriax::File& in, int32_t& v) {
    uint32_t u = 0;
    if (!readU32(in, u)) return false;
    v = static_cast<int32_t>(u);
    return true;
}

static bool readU8(doriax::File& in, uint8_t& v) {
    return in.read(&v, sizeof(v)) == sizeof(v);
}

static bool readBool(doriax::File& in, bool& v) {
    uint8_t b = 0;
    if (!readU8(in, b)) {
        return false;
    }
    v = (b != 0);
    return true;
}

static bool readString(doriax::File& in, std::string& s) {
    uint32_t len = 0;
    if (!readU32(in, len)) {
        return false;
    }

    if (len > kMaxString) {
        return false;
    }

    s.clear();
    if (len == 0) {
        return true;
    }

    s.resize(len);
    return in.read((unsigned char*)&s[0], len) == len;
}

} // namespace

namespace doriax {

bool ShaderDataSerializer::writeToFile(const std::string& filepath, uint64_t shaderKey, const ShaderData& shaderData, std::string* err) {
    File out;
    if (out.open(filepath.c_str(), true) != FileErrors::FILEDATA_OK) {
        return setErr(err, "Cannot open file for writing: " + filepath);
    }

    writeU32(out, SDAT_MAGIC);
    writeU32(out, SDAT_VERSION);
    writeU64(out, shaderKey);

    writeU32(out, static_cast<uint32_t>(shaderData.lang));
    writeU32(out, static_cast<uint32_t>(shaderData.version));
    writeBool(out, shaderData.es);

    writeU32(out, static_cast<uint32_t>(shaderData.stages.size()));

    for (const auto& stage : shaderData.stages) {
        writeU32(out, static_cast<uint32_t>(stage.type));
        writeString(out, stage.name);
        writeString(out, stage.source);

        // Intentionally do not serialize bytecode (engine uses stage.source for GLSL).
        writeU32(out, 0);

        writeU32(out, static_cast<uint32_t>(stage.attributes.size()));
        for (const auto& a : stage.attributes) {
            writeString(out, a.name);
            writeString(out, a.semanticName);
            writeU32(out, static_cast<uint32_t>(a.semanticIndex));
            writeI32(out, static_cast<int32_t>(a.location));
            writeU32(out, static_cast<uint32_t>(a.type));
        }

        writeU32(out, static_cast<uint32_t>(stage.uniformblocks.size()));
        for (const auto& ub : stage.uniformblocks) {
            writeString(out, ub.name);
            writeString(out, ub.instName);
            writeI32(out, static_cast<int32_t>(ub.set));
            writeI32(out, static_cast<int32_t>(ub.binding));
            writeU32(out, static_cast<uint32_t>(ub.slot));
            writeU32(out, static_cast<uint32_t>(ub.sizeBytes));
            writeBool(out, ub.flattened);

            writeU32(out, static_cast<uint32_t>(ub.uniforms.size()));
            for (const auto& u : ub.uniforms) {
                writeString(out, u.name);
                writeU32(out, static_cast<uint32_t>(u.type));
                writeU32(out, static_cast<uint32_t>(u.arrayCount));
                writeU32(out, static_cast<uint32_t>(u.offset));
            }
        }

        writeU32(out, static_cast<uint32_t>(stage.storagebuffers.size()));
        for (const auto& sb : stage.storagebuffers) {
            writeString(out, sb.name);
            writeString(out, sb.instName);
            writeI32(out, static_cast<int32_t>(sb.set));
            writeI32(out, static_cast<int32_t>(sb.binding));
            writeU32(out, static_cast<uint32_t>(sb.slot));
            writeU32(out, static_cast<uint32_t>(sb.sizeBytes));
            writeBool(out, sb.readonly);
            writeU32(out, static_cast<uint32_t>(sb.type));
        }

        writeU32(out, static_cast<uint32_t>(stage.textures.size()));
        for (const auto& t : stage.textures) {
            writeString(out, t.name);
            writeI32(out, static_cast<int32_t>(t.set));
            writeI32(out, static_cast<int32_t>(t.binding));
            writeU32(out, static_cast<uint32_t>(t.slot));
            writeU32(out, static_cast<uint32_t>(t.type));
            writeU32(out, static_cast<uint32_t>(t.samplerType));
        }

        writeU32(out, static_cast<uint32_t>(stage.samplers.size()));
        for (const auto& s : stage.samplers) {
            writeString(out, s.name);
            writeI32(out, static_cast<int32_t>(s.set));
            writeI32(out, static_cast<int32_t>(s.binding));
            writeU32(out, static_cast<uint32_t>(s.slot));
            writeU32(out, static_cast<uint32_t>(s.type));
        }

        writeU32(out, static_cast<uint32_t>(stage.textureSamplerPairs.size()));
        for (const auto& p : stage.textureSamplerPairs) {
            writeString(out, p.name);
            writeString(out, p.textureName);
            writeString(out, p.samplerName);
            writeU32(out, static_cast<uint32_t>(p.slot));
        }
    }

    out.flush();

    return true;
}

bool ShaderDataSerializer::readFromFile(const std::string& filepath, uint64_t expectedShaderKey, ShaderData& outShaderData, std::string* err) {
    File in;
    if (in.open(filepath.c_str()) != FileErrors::FILEDATA_OK) {
        return setErr(err, "Cannot open file for reading: " + filepath);
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!readU32(in, magic) || !readU32(in, version)) {
        return setErr(err, "Failed to read header: " + filepath);
    }

    if (magic != SDAT_MAGIC) {
        return setErr(err, "Invalid SDAT magic: " + filepath);
    }

    if (version != SDAT_VERSION) {
        return setErr(err, "Unsupported SDAT version: " + filepath);
    }

    uint64_t fileShaderKey = 0;
    if (!readU64(in, fileShaderKey)) {
        return setErr(err, "Failed to read shader key: " + filepath);
    }

    if (fileShaderKey != expectedShaderKey) {
        return setErr(err, "Shader key mismatch: " + filepath);
    }

    uint32_t lang = 0;
    uint32_t shaderVersion = 0;
    bool es = false;

    if (!readU32(in, lang) || !readU32(in, shaderVersion) || !readBool(in, es)) {
        return setErr(err, "Failed to read ShaderData header: " + filepath);
    }

    ShaderData tmp;
    tmp.lang = static_cast<ShaderLang>(lang);
    tmp.version = shaderVersion;
    tmp.es = es;

    uint32_t stageCount = 0;
    if (!readU32(in, stageCount)) {
        return setErr(err, "Failed to read stage count: " + filepath);
    }

    if (stageCount > kMaxStages) {
        return setErr(err, "Too many stages in file: " + filepath);
    }

    tmp.stages.clear();
    tmp.stages.reserve(stageCount);

    for (uint32_t i = 0; i < stageCount; i++) {
        ShaderStage stage;

        uint32_t stageType = 0;
        if (!readU32(in, stageType)) {
            return setErr(err, "Failed to read stage type: " + filepath);
        }
        stage.type = static_cast<ShaderStageType>(stageType);

        if (!readString(in, stage.name) || !readString(in, stage.source)) {
            return setErr(err, "Failed to read stage strings: " + filepath);
        }

        // bytecodeSize (we do not support serialized bytecode here)
        uint32_t bytecodeSize = 0;
        if (!readU32(in, bytecodeSize)) {
            return setErr(err, "Failed to read bytecode size: " + filepath);
        }
        if (bytecodeSize != 0) {
            return setErr(err, "SDAT bytecode not supported (expected 0): " + filepath);
        }
        stage.bytecode.data = nullptr;
        stage.bytecode.size = 0;

        uint32_t attrCount = 0;
        if (!readU32(in, attrCount)) {
            return setErr(err, "Failed to read attribute count: " + filepath);
        }
        if (attrCount > kMaxAttributes) {
            return setErr(err, "Too many attributes in stage: " + filepath);
        }
        stage.attributes.reserve(attrCount);
        for (uint32_t a = 0; a < attrCount; a++) {
            ShaderAttr attr;
            uint32_t semanticIndex = 0;
            int32_t location = 0;
            uint32_t attrType = 0;

            if (!readString(in, attr.name) || !readString(in, attr.semanticName) || !readU32(in, semanticIndex) || !readI32(in, location) || !readU32(in, attrType)) {
                return setErr(err, "Failed to read attribute: " + filepath);
            }

            attr.semanticIndex = semanticIndex;
            attr.location = location;
            attr.type = static_cast<ShaderVertexType>(attrType);

            stage.attributes.push_back(attr);
        }

        uint32_t ubCount = 0;
        if (!readU32(in, ubCount)) {
            return setErr(err, "Failed to read uniform block count: " + filepath);
        }
        if (ubCount > kMaxUniformBlocks) {
            return setErr(err, "Too many uniform blocks in stage: " + filepath);
        }
        stage.uniformblocks.reserve(ubCount);
        for (uint32_t ub = 0; ub < ubCount; ub++) {
            ShaderUniformBlock block;
            int32_t set = 0;
            int32_t binding = 0;
            uint32_t slot = 0;
            uint32_t sizeBytes = 0;
            bool flattened = false;

            if (!readString(in, block.name) || !readString(in, block.instName) || !readI32(in, set) || !readI32(in, binding) || !readU32(in, slot) || !readU32(in, sizeBytes) || !readBool(in, flattened)) {
                return setErr(err, "Failed to read uniform block: " + filepath);
            }

            block.set = set;
            block.binding = binding;
            block.slot = slot;
            block.sizeBytes = sizeBytes;
            block.flattened = flattened;

            uint32_t uniformCount = 0;
            if (!readU32(in, uniformCount)) {
                return setErr(err, "Failed to read uniform count: " + filepath);
            }
            if (uniformCount > kMaxUniforms) {
                return setErr(err, "Too many uniforms in block: " + filepath);
            }

            block.uniforms.reserve(uniformCount);
            for (uint32_t u = 0; u < uniformCount; u++) {
                ShaderUniform uniform;
                uint32_t uniformType = 0;
                uint32_t arrayCount = 0;
                uint32_t offset = 0;
                if (!readString(in, uniform.name) || !readU32(in, uniformType) || !readU32(in, arrayCount) || !readU32(in, offset)) {
                    return setErr(err, "Failed to read uniform: " + filepath);
                }

                uniform.type = static_cast<ShaderUniformType>(uniformType);
                uniform.arrayCount = arrayCount;
                uniform.offset = offset;

                block.uniforms.push_back(uniform);
            }

            stage.uniformblocks.push_back(block);
        }

        uint32_t sbCount = 0;
        if (!readU32(in, sbCount)) {
            return setErr(err, "Failed to read storage buffer count: " + filepath);
        }
        if (sbCount > kMaxStorageBuffers) {
            return setErr(err, "Too many storage buffers in stage: " + filepath);
        }
        stage.storagebuffers.reserve(sbCount);
        for (uint32_t sb = 0; sb < sbCount; sb++) {
            ShaderStorageBuffer storage;
            int32_t set = 0;
            int32_t binding = 0;
            uint32_t slot = 0;
            uint32_t sizeBytes = 0;
            bool readonly = false;
            uint32_t sbType = 0;

            if (!readString(in, storage.name) || !readString(in, storage.instName) || !readI32(in, set) || !readI32(in, binding) || !readU32(in, slot) || !readU32(in, sizeBytes) || !readBool(in, readonly) || !readU32(in, sbType)) {
                return setErr(err, "Failed to read storage buffer: " + filepath);
            }

            storage.set = set;
            storage.binding = binding;
            storage.slot = slot;
            storage.sizeBytes = sizeBytes;
            storage.readonly = readonly;
            storage.type = static_cast<ShaderStorageBufferType>(sbType);

            stage.storagebuffers.push_back(storage);
        }

        uint32_t texCount = 0;
        if (!readU32(in, texCount)) {
            return setErr(err, "Failed to read texture count: " + filepath);
        }
        if (texCount > kMaxTextures) {
            return setErr(err, "Too many textures in stage: " + filepath);
        }
        stage.textures.reserve(texCount);
        for (uint32_t t = 0; t < texCount; t++) {
            ShaderTexture tex;
            int32_t set = 0;
            int32_t binding = 0;
            uint32_t slot = 0;
            uint32_t type = 0;
            uint32_t samplerType = 0;

            if (!readString(in, tex.name) || !readI32(in, set) || !readI32(in, binding) || !readU32(in, slot) || !readU32(in, type) || !readU32(in, samplerType)) {
                return setErr(err, "Failed to read texture: " + filepath);
            }

            tex.set = set;
            tex.binding = binding;
            tex.slot = slot;
            tex.type = static_cast<TextureType>(type);
            tex.samplerType = static_cast<TextureSamplerType>(samplerType);

            stage.textures.push_back(tex);
        }

        uint32_t samplerCount = 0;
        if (!readU32(in, samplerCount)) {
            return setErr(err, "Failed to read sampler count: " + filepath);
        }
        if (samplerCount > kMaxSamplers) {
            return setErr(err, "Too many samplers in stage: " + filepath);
        }
        stage.samplers.reserve(samplerCount);
        for (uint32_t s = 0; s < samplerCount; s++) {
            ShaderSampler sampler;
            int32_t set = 0;
            int32_t binding = 0;
            uint32_t slot = 0;
            uint32_t type = 0;

            if (!readString(in, sampler.name) || !readI32(in, set) || !readI32(in, binding) || !readU32(in, slot) || !readU32(in, type)) {
                return setErr(err, "Failed to read sampler: " + filepath);
            }

            sampler.set = set;
            sampler.binding = binding;
            sampler.slot = slot;
            sampler.type = static_cast<SamplerType>(type);

            stage.samplers.push_back(sampler);
        }

        uint32_t pairCount = 0;
        if (!readU32(in, pairCount)) {
            return setErr(err, "Failed to read texture sampler pair count: " + filepath);
        }
        if (pairCount > kMaxTextureSamplerPairs) {
            return setErr(err, "Too many texture sampler pairs in stage: " + filepath);
        }
        stage.textureSamplerPairs.reserve(pairCount);
        for (uint32_t p = 0; p < pairCount; p++) {
            ShaderTextureSamplerPair pair;
            uint32_t slot = 0;

            if (!readString(in, pair.name) || !readString(in, pair.textureName) || !readString(in, pair.samplerName) || !readU32(in, slot)) {
                return setErr(err, "Failed to read texture sampler pair: " + filepath);
            }

            pair.slot = slot;
            stage.textureSamplerPairs.push_back(pair);
        }

        tmp.stages.push_back(stage);
    }

    outShaderData = tmp;
    return true;
}

} // namespace doriax