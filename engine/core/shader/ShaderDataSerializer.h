//
// (c) 2026 Eduardo Doria.
//

#ifndef SHADERDATASERIALIZER_H
#define SHADERDATASERIALIZER_H

#include <cstdint>
#include <string>

#include "ShaderData.h"

namespace doriax {

    class DORIAX_API ShaderDataSerializer {
    public:
        // Simple on-disk cache format for ShaderData.
        // - Stores stage GLSL/HLSL/MSL source and reflection data.
        // - Does NOT serialize stage bytecode (kept empty).
        //
        // Returns true on success. On failure, returns false and optionally fills err.
        static bool writeToFile(const std::string& filepath, uint64_t shaderKey, const ShaderData& shaderData, std::string* err = nullptr);
        static bool readFromFile(const std::string& filepath, uint64_t expectedShaderKey, ShaderData& outShaderData, std::string* err = nullptr);
    };

}

#endif // SHADERDATASERIALIZER_H
