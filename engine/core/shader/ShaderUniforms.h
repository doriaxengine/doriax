// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERUNIFORMS_H
#define SHADERUNIFORMS_H

#include "Export.h"
#include "math/Vector4.h"
#include <string>
#include <vector>
#include <utility>

namespace doriax{

    // shader parameter values by name; a vector keeps the serialized order stable
    typedef std::vector<std::pair<std::string, Vector4>> ShaderUniformValues;

    class DORIAX_API ShaderUniforms{
    public:
        static void set(ShaderUniformValues& values, const std::string& name, const Vector4& value);
        static Vector4 get(const ShaderUniformValues& values, const std::string& name);
        static bool has(const ShaderUniformValues& values, const std::string& name);
        static bool remove(ShaderUniformValues& values, const std::string& name);
    };

}

#endif //SHADERUNIFORMS_H
