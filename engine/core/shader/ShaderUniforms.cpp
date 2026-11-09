// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderUniforms.h"

using namespace doriax;

void ShaderUniforms::set(ShaderUniformValues& values, const std::string& name, const Vector4& value){
    for (size_t i = 0; i < values.size(); i++){
        if (values[i].first == name){
            values[i].second = value;
            return;
        }
    }
    values.push_back({name, value});
}

Vector4 ShaderUniforms::get(const ShaderUniformValues& values, const std::string& name){
    for (size_t i = 0; i < values.size(); i++){
        if (values[i].first == name)
            return values[i].second;
    }
    return Vector4::ZERO;
}

bool ShaderUniforms::has(const ShaderUniformValues& values, const std::string& name){
    for (size_t i = 0; i < values.size(); i++){
        if (values[i].first == name)
            return true;
    }
    return false;
}

bool ShaderUniforms::remove(ShaderUniformValues& values, const std::string& name){
    for (size_t i = 0; i < values.size(); i++){
        if (values[i].first == name){
            values.erase(values.begin() + i);
            return true;
        }
    }
    return false;
}
