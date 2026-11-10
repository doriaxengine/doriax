// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERDATASERIALIZER_H
#define SHADERDATASERIALIZER_H

#include <cstdint>
#include <string>
#include <vector>

#include "ShaderData.h"

namespace doriax {

    class DORIAX_API ShaderDataSerializer {
    public:
        // Simple on-disk cache format for ShaderData.
        // - Stores stage GLSL/HLSL/MSL source and reflection data.
        // - Does NOT serialize stage bytecode (kept empty).
        //
        // Returns true on success. On failure, returns false and optionally fills err.
        // within the limits the readers accept
        static bool validate(const ShaderData& shaderData, std::string* err = nullptr);
        static bool writeToFile(const std::string& filepath, uint64_t shaderKey, const ShaderData& shaderData, std::string* err = nullptr);
        static bool writeToBytes(std::vector<unsigned char>& outBytes, uint64_t shaderKey, const ShaderData& shaderData, std::string* err = nullptr);
        static bool writeJsonToFile(const std::string& filepath, uint64_t shaderKey, const ShaderData& shaderData, std::string* err = nullptr);
        static bool readFromFile(const std::string& filepath, uint64_t expectedShaderKey, ShaderData& outShaderData, std::string* err = nullptr);
        static bool readFromBytes(const std::vector<unsigned char>& bytes, uint64_t expectedShaderKey, ShaderData& outShaderData, std::string* err = nullptr);
    };

}

#endif // SHADERDATASERIALIZER_H
