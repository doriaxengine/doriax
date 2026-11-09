// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderParameters.h"

#include "ShaderData.h"
#include "Scene.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <unordered_map>

using namespace doriax;

static std::mutex& internMutex(){
    static std::mutex* m = new std::mutex();
    return *m;
}

static std::unordered_map<std::string, uint32_t>& internedKeys(){
    static std::unordered_map<std::string, uint32_t>* keys = new std::unordered_map<std::string, uint32_t>();
    return *keys;
}

static void writeFloats(std::vector<uint8_t>& data, unsigned int offset, const float* values, int count){
    if (count <= 0 || (size_t)offset + (size_t)count * sizeof(float) > data.size())
        return;
    memcpy(data.data() + offset, values, (size_t)count * sizeof(float));
}

static void writeValue(std::vector<uint8_t>& data, const ShaderParameterBlock::Binding& binding, const Vector4& value){
    const float components[4] = {value.x, value.y, value.z, value.w};
    writeFloats(data, binding.offset, components, ShaderParameters::getComponentCount(binding.type));
}

void ShaderParameterTable::clear(){
    std::fill(present.begin(), present.end(), 0);
}

void ShaderParameterTable::set(uint32_t key, const Vector4& value){
    if (key >= values.size()){
        values.resize(key + 1, Vector4(0.0f, 0.0f, 0.0f, 0.0f));
        present.resize(key + 1, 0);
    }
    values[key] = value;
    present[key] = 1;
}

void ShaderParameterTable::apply(const ShaderUniformValues& entries){
    for (const auto& entry : entries){
        set(ShaderParameters::intern(entry.first), entry.second);
    }
}

const Vector4* ShaderParameterTable::find(uint32_t key) const{
    if (key < present.size() && present[key])
        return &values[key];
    return nullptr;
}

std::string ShaderParameters::getKey(const std::string& collection, const std::string& name){
    return collection + "." + name;
}

bool ShaderParameters::splitKey(const std::string& key, std::string& collection, std::string& name){
    const size_t dot = key.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size())
        return false;
    collection = key.substr(0, dot);
    name = key.substr(dot + 1);
    return true;
}

bool ShaderParameters::isValidName(const std::string& name){
    if (name.empty() || std::isdigit((unsigned char)name[0]))
        return false;
    // GLSL reserves both
    if (name.find("__") != std::string::npos || name.rfind("gl_", 0) == 0)
        return false;
    for (char c : name){
        if (!std::isalnum((unsigned char)c) && c != '_')
            return false;
    }
    return true;
}

bool ShaderParameters::isValidKey(const std::string& collection, const std::string& name){
    return isValidName(collection) && isValidName(name);
}

bool ShaderParameters::checkKey(const std::string& collection, const std::string& name){
    if (isValidKey(collection, name))
        return true;
    Log::error("Invalid shader parameter '%s.%s': collection and name take letters, digits and underscores", collection.c_str(), name.c_str());
    return false;
}

uint32_t ShaderParameters::intern(const std::string& key){
    std::lock_guard<std::mutex> lock(internMutex());
    auto& keys = internedKeys();
    auto it = keys.find(key);
    if (it != keys.end())
        return it->second;
    const uint32_t id = (uint32_t)keys.size();
    keys.emplace(key, id);
    return id;
}

const char* ShaderParameters::getScopeName(ShaderParameterScope scope){
    switch (scope){
        case ShaderParameterScope::OBJECT: return "object";
        case ShaderParameterScope::SCENE:  return "scene";
        case ShaderParameterScope::VIEW:   return "view";
        case ShaderParameterScope::ENGINE: return "engine";
    }
    return "object";
}

int ShaderParameters::getComponentCount(ShaderUniformType type){
    switch (type){
        case ShaderUniformType::FLOAT:  return 1;
        case ShaderUniformType::FLOAT2: return 2;
        case ShaderUniformType::FLOAT3: return 3;
        case ShaderUniformType::FLOAT4: return 4;
        case ShaderUniformType::INT:    return 1;
        case ShaderUniformType::INT2:   return 2;
        case ShaderUniformType::INT3:   return 3;
        case ShaderUniformType::INT4:   return 4;
        case ShaderUniformType::MAT4:   return 16;
        default:                        return 0;
    }
}

bool ShaderParameterBlock::resolve(ShaderData& shaderData, const std::string& blockName, ShaderStageType stage){
    clear();

    if (shaderData.parameters.empty())
        return false;

    unsigned int sizeBytes = 0;
    const std::vector<ShaderUniform>* members = shaderData.getUniformBlockMembers(blockName, sizeBytes);
    if (!members || sizeBytes == 0)
        return false;

    for (const ShaderUniform& member : *members){
        const std::string shortName = ShaderData::getUniformShortName(member.name);
        const ShaderParameterInfo* info = nullptr;
        for (const ShaderParameterInfo& candidate : shaderData.parameters){
            if (candidate.stage == stage && candidate.member == shortName){
                info = &candidate;
                break;
            }
        }
        if (!info || member.arrayCount > 1)
            continue;

        Binding binding;
        binding.offset = member.offset;
        binding.type = member.type;
        binding.scope = info->scope;
        binding.name = info->name;
        binding.defaultValue = info->defaultValue;

        if (info->scope == ShaderParameterScope::ENGINE){
            const int index = (int)bindings.size();
            if (info->name == "time"){
                engineTime = index;
            }else if (info->name == "resolution"){
                engineResolution = index;
            }else if (info->name == "worldToModel" && member.type == ShaderUniformType::MAT4){
                worldToModel = index;
            }else if (info->name == "offsetMax"){
                offsetMax = index;
            }else if (info->name == "eye"){
                eye = index;
            }else if (info->name == "forward"){
                forward = index;
            }else if (info->name == "pass"){
                pass = index;
            }else{
                continue;
            }
        }else{
            // int parameters are packed as floats too
            if (member.type != ShaderUniformType::FLOAT && member.type != ShaderUniformType::FLOAT2 &&
                    member.type != ShaderUniformType::FLOAT3 && member.type != ShaderUniformType::FLOAT4)
                continue;
            if (info->scope == ShaderParameterScope::SCENE || info->scope == ShaderParameterScope::VIEW){
                binding.key = ShaderParameters::intern(ShaderParameters::getKey(info->collection, info->name));
                shared = true;
            }
        }

        bindings.push_back(binding);
    }

    if (bindings.empty()){
        clear();
        return false;
    }

    slot = shaderData.getUniformBlockIndexByName(blockName);
    size = ((sizeBytes + 15) / 16) * 16;
    data.assign(size, 0);
    firstOffset = size;
    for (const Binding& binding : bindings){
        firstOffset = std::min(firstOffset, binding.offset);
    }

    // declared defaults until something writes
    writeObject(ShaderUniformValues());
    writeFrame(ShaderEffectView(), Vector2(0.0f, 0.0f));

    return slot != -1;
}

void ShaderParameterBlock::clear(){
    slot = -1;
    size = 0;
    firstOffset = 0;
    data.clear();
    bindings.clear();
    engineTime = -1;
    engineResolution = -1;
    worldToModel = -1;
    offsetMax = -1;
    eye = -1;
    forward = -1;
    pass = -1;
    shared = false;
}

void ShaderParameterBlock::writeObject(const ShaderUniformValues& values, const ShaderUniformValues* inherited){
    for (const Binding& binding : bindings){
        if (binding.scope != ShaderParameterScope::OBJECT)
            continue;
        if (ShaderUniforms::has(values, binding.name)){
            writeValue(data, binding, ShaderUniforms::get(values, binding.name));
        }else if (inherited && ShaderUniforms::has(*inherited, binding.name)){
            writeValue(data, binding, ShaderUniforms::get(*inherited, binding.name));
        }else{
            writeValue(data, binding, binding.defaultValue);
        }
    }
}

void ShaderParameterBlock::writeFrame(const ShaderEffectView& view, const Vector2& resolution){
    if (engineTime != -1){
        writeValue(data, bindings[engineTime], Vector4(view.time, 0.0f, 0.0f, 0.0f));
    }
    if (engineResolution != -1){
        const float invWidth = resolution.x > 0.0f ? 1.0f / resolution.x : 0.0f;
        const float invHeight = resolution.y > 0.0f ? 1.0f / resolution.y : 0.0f;
        writeValue(data, bindings[engineResolution], Vector4(resolution.x, resolution.y, invWidth, invHeight));
    }
    if (!shared)
        return;
    for (const Binding& binding : bindings){
        const ShaderParameterTable* table = nullptr;
        if (binding.scope == ShaderParameterScope::SCENE){
            table = view.scene;
        }else if (binding.scope == ShaderParameterScope::VIEW){
            table = view.view;
        }else{
            continue;
        }
        const Vector4* value = table ? table->find(binding.key) : nullptr;
        writeValue(data, binding, value ? *value : binding.defaultValue);
    }
}

void ShaderParameterBlock::writeVertexContext(const Matrix4& modelMatrix, float maxOffset){
    if (worldToModel != -1){
        const Matrix4 inverse = modelMatrix.inverse();
        writeFloats(data, bindings[worldToModel].offset, &inverse[0][0], 16);
    }
    if (offsetMax != -1){
        writeValue(data, bindings[offsetMax], Vector4(maxOffset, 0.0f, 0.0f, 0.0f));
    }
}

void ShaderParameterBlock::writeFragmentContext(const ShaderEffectView& view, bool coverage, bool fade){
    if (eye != -1){
        writeValue(data, bindings[eye], Vector4(view.eye.x, view.eye.y, view.eye.z, view.perspective ? 1.0f : 0.0f));
    }
    if (forward != -1){
        writeValue(data, bindings[forward], Vector4(view.forward.x, view.forward.y, view.forward.z, 0.0f));
    }
    if (pass != -1){
        writeValue(data, bindings[pass], Vector4(view.ditherOrigin.x, view.ditherOrigin.y, coverage ? 1.0f : 0.0f, fade ? 1.0f : 0.0f));
    }
}

void WindParameterProvider::update(Scene* scene, double dt, bool paused){
    if (paused || dt <= 0.0)
        return;

    const Vector4 velocity = scene->getShaderParameter("Wind", "velocity");
    const double speed = Vector3(velocity.x, velocity.y, velocity.z).length();
    if (speed <= 0.0)
        return;

    travel += speed * dt;
    scene->setShaderParameter("Wind", "travel", (float)travel);
}

void WindParameterProvider::reset(){
    travel = 0.0;
}
