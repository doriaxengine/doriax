// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderPool.h"

#include "Log.h"
#include "Engine.h"
#include "shader/ShaderDataSerializer.h"
#include "util/Base64.h"
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>
#include <mutex>
#include <cctype>
#include <set>
#include <bitset>

#ifdef SOKOL_GLCORE
#include "glsl410.h"
#endif
#ifdef SOKOL_GLES3
#include "glsl300es.h"
#endif
#ifdef SOKOL_D3D11
#include "hlsl5.h"
#endif
#ifdef SOKOL_VULKAN
#include "spirv10.h"
#endif
// Metal shader cache. SOKOL_METAL covers cmake Metal builds. DORIAX_APPLE is
// also needed: the Xcode engine targets define it without SOKOL_METAL (that
// flag is only on the sokol gfx unit), and Engine::getGraphicBackend() treats
// DORIAX_APPLE as Metal. Exclude SOKOL_GLCORE so an Apple+OpenGL build does
// not pull in both headers and redefine getBase64Shader.
#if defined(SOKOL_METAL) || (defined(DORIAX_APPLE) && !defined(SOKOL_GLCORE))
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "msl21ios.h"
#elif TARGET_OS_MAC
#include "msl21macos.h"
#endif
#endif

using namespace doriax;

ShaderBuilderFn ShaderPool::shaderBuilderFn = nullptr;

bool ShaderPool::parseShaderTypeToken(const std::string& typeToken, ShaderType& shaderType) {
    if (typeToken == "mesh") {
        shaderType = ShaderType::MESH;
        return true;
    }
    if (typeToken == "depth") {
        shaderType = ShaderType::DEPTH;
        return true;
    }
    if (typeToken == "gbuffer") {
        shaderType = ShaderType::GBUFFER;
        return true;
    }
    if (typeToken == "sky") {
        shaderType = ShaderType::SKYBOX;
        return true;
    }
    if (typeToken == "ui") {
        shaderType = ShaderType::UI;
        return true;
    }
    if (typeToken == "points") {
        shaderType = ShaderType::POINTS;
        return true;
    }
    if (typeToken == "lines") {
        shaderType = ShaderType::LINES;
        return true;
    }
    if (typeToken == "ssao") {
        shaderType = ShaderType::SSAO;
        return true;
    }
    if (typeToken == "ssaoblur") {
        shaderType = ShaderType::SSAO_BLUR;
        return true;
    }
    if (typeToken == "ssr") {
        shaderType = ShaderType::SSR;
        return true;
    }
    if (typeToken == "ssrblur") {
        shaderType = ShaderType::SSR_BLUR;
        return true;
    }
    if (typeToken == "composite") {
        shaderType = ShaderType::COMPOSITE;
        return true;
    }
    if (typeToken == "shadow2d") {
        shaderType = ShaderType::SHADOW2D;
        return true;
    }
    if (typeToken == "blit") {
        shaderType = ShaderType::BLIT;
        return true;
    }
    if (typeToken == "postprocess") {
        shaderType = ShaderType::POSTPROCESS;
        return true;
    }
    if (typeToken == "water") {
        shaderType = ShaderType::WATER;
        return true;
    }
    if (typeToken == "underwater") {
        shaderType = ShaderType::UNDERWATER;
        return true;
    }
    if (typeToken == "probefilter") {
        shaderType = ShaderType::PROBE_FILTER;
        return true;
    }

    return false;
}

shaders_t& ShaderPool::getMap(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static shaders_t* map = new shaders_t();
    return *map;
};

std::vector<std::string>& ShaderPool::getMissingShaders(){
    static std::vector<std::string>* missingshaders = new std::vector<std::string>();
    return *missingshaders;
};

// Keys whose last build or backend resource creation failed. The editor pool stops
// re-invoking the builder for these (the compile is expensive and spams errors every
// frame) until the failure is cleared by remove()/destroyCustomShaders().
static std::set<ShaderKey>& failedShaders(){
    static std::set<ShaderKey>* set = new std::set<ShaderKey>();
    return *set;
};

// Custom shader registry: index = customId-1, value = project-relative base path.
static std::mutex& customShaderMutex(){
    static std::mutex* m = new std::mutex();
    return *m;
}
static std::vector<std::string>& customShaderNames(){
    static std::vector<std::string>* names = new std::vector<std::string>();
    return *names;
}

uint16_t ShaderPool::registerCustomShader(const std::string& baseName){
    if (baseName.empty())
        return 0;

    std::lock_guard<std::mutex> lock(customShaderMutex());
    auto& names = customShaderNames();
    for (size_t i = 0; i < names.size(); i++){
        if (names[i] == baseName)
            return (uint16_t)(i + 1);
    }
    names.push_back(baseName);
    return (uint16_t)names.size();
}

std::string ShaderPool::getCustomShaderName(uint16_t customId){
    if (customId == 0)
        return "";

    std::lock_guard<std::mutex> lock(customShaderMutex());
    auto& names = customShaderNames();
    if (customId <= names.size())
        return names[customId - 1];
    return "";
}

std::string ShaderPool::getCustomShaderBasename(uint16_t customId){
    std::string base = getCustomShaderName(customId);
    // Sanitize the project-relative base into a filesystem-safe, deterministic token
    // (e.g. "shaders/myMesh" -> "shaders_myMesh") used in .sdat filenames.
    for (char& c : base){
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            c = '_';
    }
    return base;
}

void ShaderPool::setShaderBuilder(ShaderBuilderFn fn) {
    shaderBuilderFn = fn;
}

std::string ShaderPool::getShaderLangStr(ShaderLang lang, int version, bool es, Platform platform) {
    if (lang == ShaderLang::GLSL) {
        return es ? "glsl" + std::to_string(version) + "es" : "glsl" + std::to_string(version);
    }
    if (lang == ShaderLang::HLSL) {
        return "hlsl" + std::to_string(version / 10);
    }
    if (lang == ShaderLang::MSL) {
        if (platform == Platform::iOS) {
            return "msl" + std::to_string(version) + "ios";
        }
        if (platform == Platform::MacOS) {
            return "msl" + std::to_string(version) + "macos";
        }
    }
    if (lang == ShaderLang::SPIRV) {
        return "spirv" + std::to_string(version);
    }

    return "<unknown>";
}

const std::vector<ShaderBackend>& ShaderPool::getShaderBackends() {
    static const std::vector<ShaderBackend>* backends = new std::vector<ShaderBackend>{
        ShaderBackend::GLCore,
        ShaderBackend::GLES3,
        ShaderBackend::D3D11,
        ShaderBackend::MetalMacOS,
        ShaderBackend::MetalIOS,
        ShaderBackend::Vulkan
    };
    return *backends;
}

ShaderBackend ShaderPool::getShaderBackend() {
    switch (Engine::getGraphicBackend()) {
        case GraphicBackend::GLES3:  return ShaderBackend::GLES3;
        case GraphicBackend::D3D11:  return ShaderBackend::D3D11;
        case GraphicBackend::VULKAN: return ShaderBackend::Vulkan;
        // only place the OS takes part: MSL differs between macOS and iOS
        case GraphicBackend::METAL:  return (Engine::getPlatform() == Platform::iOS) ? ShaderBackend::MetalIOS : ShaderBackend::MetalMacOS;
        default:                     return ShaderBackend::GLCore;
    }
}

std::string ShaderPool::getShaderBackendName(ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::GLCore:     return "OpenGL";
        case ShaderBackend::GLES3:      return "OpenGL ES 3";
        case ShaderBackend::D3D11:      return "Direct3D 11";
        case ShaderBackend::MetalMacOS: return "Metal (macOS)";
        case ShaderBackend::MetalIOS:   return "Metal (iOS)";
        case ShaderBackend::Vulkan:     return "Vulkan";
        default:                        return "<unknown>";
    }
}

std::string ShaderPool::getShaderBackendCliToken(ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::GLCore:     return "opengl";
        case ShaderBackend::GLES3:      return "opengles";
        case ShaderBackend::D3D11:      return "d3d11";
        case ShaderBackend::MetalMacOS: return "metal-macos";
        case ShaderBackend::MetalIOS:   return "metal-ios";
        case ShaderBackend::Vulkan:     return "vulkan";
        default:                        return "<target-backend>";
    }
}

// Accepts the CLI token and the GRAPHIC_BACKEND value users see in the build settings
bool ShaderPool::parseShaderBackend(const std::string& value, ShaderBackend& out) {
    std::string token;
    for (char c : value) {
        if (std::isalnum((unsigned char)c))
            token += std::tolower((unsigned char)c);
    }

    if (token == "opengl"   || token == "glcore") { out = ShaderBackend::GLCore;     return true; }
    if (token == "opengles" || token == "gles3")  { out = ShaderBackend::GLES3;      return true; }
    if (token == "d3d11")                         { out = ShaderBackend::D3D11;      return true; }
    if (token == "metal"    || token == "metalmacos") { out = ShaderBackend::MetalMacOS; return true; }
    if (token == "metalios")                      { out = ShaderBackend::MetalIOS;   return true; }
    if (token == "vulkan")                        { out = ShaderBackend::Vulkan;     return true; }

    return false;
}

std::string ShaderPool::getShaderLangStr(ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::GLCore:     return getShaderLangStr(ShaderLang::GLSL, 410, false);
        case ShaderBackend::GLES3:      return getShaderLangStr(ShaderLang::GLSL, 300, true);
        case ShaderBackend::D3D11:      return getShaderLangStr(ShaderLang::HLSL, 50);
        case ShaderBackend::MetalMacOS: return getShaderLangStr(ShaderLang::MSL, 21, false, Platform::MacOS);
        case ShaderBackend::MetalIOS:   return getShaderLangStr(ShaderLang::MSL, 21, false, Platform::iOS);
        case ShaderBackend::Vulkan:     return getShaderLangStr(ShaderLang::SPIRV, 10);
        default:                        return "<unknown>";
    }
}

std::string ShaderPool::getShaderLangStr(){
    return getShaderLangStr(getShaderBackend());
}

bool ShaderPool::getShaderCliSpec(const std::string& shaderStr, std::string& cliSpec) {
    const size_t underscorePos = shaderStr.find('_');
    const std::string typeToken = underscorePos == std::string::npos ? shaderStr : shaderStr.substr(0, underscorePos);

    ShaderType shaderType;
    if (!parseShaderTypeToken(typeToken, shaderType)) {
        return false;
    }

    cliSpec = typeToken;
    if (underscorePos == std::string::npos) {
        return true;
    }

    const std::string propsToken = shaderStr.substr(underscorePos + 1);
    if (propsToken.empty()) {
        return true;
    }

    std::string propsList;
    size_t parsedLength = 0;
    const int propCount = getShaderPropertyCount(shaderType);
    for (int bit = 0; bit < propCount; bit++) {
        const std::string propName = getShaderPropertyName(shaderType, bit, true);
        if (propName.empty() || propName == "?") {
            continue;
        }

        if (propsToken.compare(parsedLength, propName.size(), propName) == 0) {
            if (!propsList.empty()) {
                propsList += ",";
            }
            propsList += propName;
            parsedLength += propName.size();
        }
    }

    if (parsedLength != propsToken.size()) {
        return false;
    }

    if (!propsList.empty()) {
        cliSpec += ":" + propsList;
    }

    return true;
}

std::string ShaderPool::getSuggestedCliBackend() {
    return getShaderBackendCliToken(getShaderBackend());
}

std::string ShaderPool::getMissingShadersCliArgs() {
    std::string cliArgs;

    for (const std::string& shaderName : getMissingShaders()) {
        std::string cliSpec;
        if (!getShaderCliSpec(shaderName, cliSpec)) {
            continue;
        }

        cliArgs += " --shader \"" + cliSpec + "\"";
    }

    return cliArgs;
}

std::string ShaderPool::getMissingShadersDisplayList() {
    std::string shaderList;

    for (const std::string& shaderName : getMissingShaders()) {
        std::string displayName = shaderName;
        std::string cliSpec;
        if (getShaderCliSpec(shaderName, cliSpec)) {
            displayName = cliSpec;
        }

        if (!shaderList.empty()) {
            shaderList += "; ";
        }
        shaderList += displayName;
    }

    return shaderList;
}


std::string ShaderPool::getShaderFile(const std::string& shaderStr, const std::string& extension){
    std::string filename = getShaderName(shaderStr);

    filename += extension;

    return filename;
}

std::string ShaderPool::getShaderName(const std::string& shaderStr){
    std::string name = shaderStr;

    name += "_" + getShaderLangStr();

    return name;
}

std::string ShaderPool::getShaderTypeName(ShaderType shaderType, bool lowerCase){
    switch (shaderType) {
        case ShaderType::MESH:   return lowerCase ? "mesh"   : "Mesh";
        case ShaderType::DEPTH:  return lowerCase ? "depth"  : "Depth";
        case ShaderType::GBUFFER: return lowerCase ? "gbuffer" : "GBuffer";
        case ShaderType::SKYBOX: return lowerCase ? "sky"    : "Skybox";
        case ShaderType::UI:     return lowerCase ? "ui"     : "UI";
        case ShaderType::POINTS: return lowerCase ? "points" : "Points";
        case ShaderType::LINES:  return lowerCase ? "lines"  : "Lines";
        case ShaderType::SSAO:   return lowerCase ? "ssao"   : "SSAO";
        case ShaderType::SSAO_BLUR: return lowerCase ? "ssaoblur" : "SSAO Blur";
        case ShaderType::SSR:    return lowerCase ? "ssr"    : "SSR";
        case ShaderType::SSR_BLUR: return lowerCase ? "ssrblur" : "SSR Blur";
        case ShaderType::COMPOSITE: return lowerCase ? "composite" : "Composite";
        case ShaderType::SHADOW2D: return lowerCase ? "shadow2d" : "Shadow 2D";
        case ShaderType::BLIT:   return lowerCase ? "blit"   : "Blit";
        case ShaderType::POSTPROCESS: return lowerCase ? "postprocess" : "Post-process";
        case ShaderType::WATER:  return lowerCase ? "water"  : "Water";
        case ShaderType::UNDERWATER: return lowerCase ? "underwater" : "Underwater";
        case ShaderType::PROBE_FILTER: return lowerCase ? "probefilter" : "Probe Filter";
        default:                 return lowerCase ? "unknown": "Unknown";
    }
}

int ShaderPool::getShaderPropertyCount(ShaderType shaderType){
    switch (shaderType) {
        case ShaderType::MESH:   return 29;
        case ShaderType::DEPTH:  return 9;
        case ShaderType::GBUFFER: return 10;
        case ShaderType::UI:     return 4;
        case ShaderType::POINTS: return 4;
        case ShaderType::LINES:  return 2;
        case ShaderType::SKYBOX: return 0;
        case ShaderType::SSAO:   return 0;
        case ShaderType::SSAO_BLUR: return 0;
        case ShaderType::SSR:    return 0;
        case ShaderType::SSR_BLUR: return 0;
        case ShaderType::COMPOSITE: return 0;
        case ShaderType::SHADOW2D: return 0;
        case ShaderType::BLIT:   return 0;
        case ShaderType::WATER:  return 5;
        default:                 return 0;
    }
}

std::string ShaderPool::getShaderPropertyName(ShaderType shaderType, int bit, bool shortName){
    if (shaderType == ShaderType::MESH) {
        switch (bit) {
            case 0:  return shortName ? "Ult" : "Unlit";
            case 1:  return shortName ? "Uv1" : "UV1";
            case 2:  return shortName ? "Uv2" : "UV2";
            case 3:  return shortName ? "Puc" : "Punctual";
            case 4:  return shortName ? "Shw" : "Shadow";
            // case 5 reserved (was 'Pcf'; the PCF filter is uniform-driven now)
            case 6:  return shortName ? "Nor" : "Normals";
            case 7:  return shortName ? "Nmp" : "Normal Map";
            case 8:  return shortName ? "Tan" : "Tangents";
            case 9:  return shortName ? "Vc3" : "Vertex Color 3";
            case 10: return shortName ? "Vc4" : "Vertex Color 4";
            case 11: return shortName ? "Txr" : "Texture Rect";
            case 12: return shortName ? "Fog" : "Fog";
            case 13: return shortName ? "Ski" : "Skinning";
            case 14: return shortName ? "Mta" : "Morph Target";
            case 15: return shortName ? "Mnr" : "Morph Normal";
            case 16: return shortName ? "Mtg" : "Morph Tangent";
            case 17: return shortName ? "Ter" : "Terrain";
            case 18: return shortName ? "Ist" : "Instancing";
            case 19: return shortName ? "Ibl" : "IBL";
            case 20: return shortName ? "Mir" : "Mirror";
            case 21: return shortName ? "Sao" : "SSAO";
            case 22: return shortName ? "L2d" : "Light 2D";
            case 23: return shortName ? "S2d" : "Shadow 2D";
            case 24: return shortName ? "Ams" : "Alpha Mask";
            case 25: return shortName ? "Aop" : "Alpha Opaque";
            case 26: return shortName ? "Ifd" : "Instance Fade";
            case 27: return shortName ? "Tpb" : "Terrain PBR";
            case 28: return shortName ? "Dlo" : "Directional Lights Only";
        }
    } else if (shaderType == ShaderType::DEPTH) {
        switch (bit) {
            case 0: return shortName ? "Tex" : "Texture";
            case 1: return shortName ? "Ski" : "Skinning";
            case 2: return shortName ? "Mta" : "Morph Target";
            case 3: return shortName ? "Mnr" : "Morph Normal";
            case 4: return shortName ? "Mtg" : "Morph Tangent";
            case 5: return shortName ? "Ter" : "Terrain";
            case 6: return shortName ? "Ist" : "Instancing";
            case 7: return shortName ? "Ams" : "Alpha Mask";
            case 8: return shortName ? "Ifd" : "Instance Fade";
        }
    } else if (shaderType == ShaderType::GBUFFER) {
        switch (bit) {
            case 0: return shortName ? "Bct" : "Base Color Texture";
            case 1: return shortName ? "Nor" : "Normals";
            case 2: return shortName ? "Ski" : "Skinning";
            case 3: return shortName ? "Mta" : "Morph Target";
            case 4: return shortName ? "Mnr" : "Morph Normal";
            case 5: return shortName ? "Mtg" : "Morph Tangent";
            case 6: return shortName ? "Ter" : "Terrain";
            case 7: return shortName ? "Ist" : "Instancing";
            case 8: return shortName ? "Mrt" : "MetalRough Texture";
            case 9: return shortName ? "Tpb" : "Terrain PBR";
        }
    } else if (shaderType == ShaderType::UI) {
        switch (bit) {
            case 0: return shortName ? "Tex" : "Texture";
            case 1: return shortName ? "Ftx" : "Font Texture";
            case 2: return shortName ? "Vc3" : "Vertex Color 3";
            case 3: return shortName ? "Vc4" : "Vertex Color 4";
        }
    } else if (shaderType == ShaderType::POINTS) {
        switch (bit) {
            case 0: return shortName ? "Tex" : "Texture";
            case 1: return shortName ? "Vc3" : "Vertex Color 3";
            case 2: return shortName ? "Vc4" : "Vertex Color 4";
            case 3: return shortName ? "Txr" : "Texture Rect";
        }
    } else if (shaderType == ShaderType::LINES) {
        switch (bit) {
            case 0: return shortName ? "Vc3" : "Vertex Color 3";
            case 1: return shortName ? "Vc4" : "Vertex Color 4";
        }
    } else if (shaderType == ShaderType::WATER) {
        switch (bit) {
            case 0: return shortName ? "Fog" : "Fog";
            case 1: return shortName ? "Dep" : "Scene Depth";
            case 2: return shortName ? "Prf" : "Planar Reflection";
            case 3: return shortName ? "Shw" : "Shadows";
            case 4: return shortName ? "Rfr" : "Refraction";
        }
    }
    return shortName ? "?" : "Unknown";
}

int ShaderPool::getShaderPropertyBit(ShaderType shaderType, const std::string& name){
    auto token = [](const std::string& value){
        std::string result;
        for (char c : value) {
            if (std::isalnum((unsigned char)c))
                result += std::tolower((unsigned char)c);
        }
        return result;
    };
    const std::string wanted = token(name);
    for (int bit = 0; bit < getShaderPropertyCount(shaderType); bit++) {
        const std::string shortName = getShaderPropertyName(shaderType, bit, true);
        if (shortName == "?") // retired
            continue;
        if (wanted == token(shortName) || wanted == token(getShaderPropertyName(shaderType, bit, false)))
            return bit;
    }
    return -1;
}

std::string ShaderPool::getShaderStr(ShaderType shaderType, uint32_t properties, uint16_t customId){

    std::string str = getShaderTypeName(shaderType, true);
    std::string propOut;

    // Custom (forked) shaders get a unique basename so their compiled .sdat does not
    // collide with the built-in shader of the same type/variant.
    if (customId != 0) {
        std::string customBase = getCustomShaderBasename(customId);
        if (!customBase.empty())
            str += "_" + customBase;
    }

    int propCount = getShaderPropertyCount(shaderType);
    for (int i = 0; i < propCount; i++) {
        if (properties & (1 << i))
            propOut += getShaderPropertyName(shaderType, i, true);
    }

    if (str.empty())
        Log::error("Erro mapping shader type to string");

    if (!propOut.empty())
        str += "_" + propOut;

    return str;
}

ShaderKey ShaderPool::getShaderKey(ShaderType shaderType, uint32_t properties, uint16_t customId) {
    return ((uint64_t)customId << 48) | (((uint64_t)shaderType & 0xFFFF) << 32) | (properties & 0xFFFFFFFF);
}

bool ShaderPool::loadPrecompiled(ShaderType shaderType, uint32_t properties, uint16_t customId, ShaderData& shaderData){
    std::string shaderStr = getShaderStr(shaderType, properties, customId);
    std::string base64Shd = getBase64Shader(getShaderName(shaderStr));

    // Validate against the storage key (customId stripped): the unique shaderStr
    // already selected the forked source; the embedded key only checks type+props.
    ShaderKey storageKey = getStorageKey(getShaderKey(shaderType, properties, customId));

    if (!base64Shd.empty() && ShaderDataSerializer::readFromBytes(Base64::decode(base64Shd), storageKey, shaderData)){
        return true;
    }
    return ShaderDataSerializer::readFromFile("shader://"+getShaderFile(shaderStr, ".sdat"), storageKey, shaderData);
}

uint32_t ShaderPool::getFallbackProperties(ShaderType shaderType, uint32_t properties, uint16_t customId){
    // optional features, in the order they are given up
    std::vector<int> optionalBits;
    if (shaderType == ShaderType::MESH){
        optionalBits = {28, 21, 12, 23, 4, 20, 19, 7, 8, 2, 1}; // Dlo Sao Fog S2d Shw Mir Ibl Nmp Tan Uv2 Uv1
    }else if (shaderType == ShaderType::DEPTH){
        optionalBits = {7, 0}; // Ams Tex
    }else if (shaderType == ShaderType::GBUFFER){
        optionalBits = {8, 0}; // Mrt Bct
    }

    std::vector<uint32_t> present;
    for (int bit : optionalBits){
        if (properties & (1u << bit)){
            present.push_back(1u << bit);
        }
    }

    // fewest features dropped first
    const uint32_t combinations = 1u << present.size();
    for (size_t dropped = 1; dropped <= present.size(); dropped++){
        for (uint32_t combination = 1; combination < combinations; combination++){
            if (std::bitset<32>(combination).count() != dropped){
                continue;
            }
            uint32_t candidate = properties;
            for (size_t i = 0; i < present.size(); i++){
                if (combination & (1u << i)){
                    candidate &= ~present[i];
                }
            }
            ShaderData shaderData;
            if (loadPrecompiled(shaderType, candidate, customId, shaderData)){
                return candidate;
            }
        }
    }

    return properties;
}

void ShaderPool::addMissingShader(const std::string& shaderStr) {
    auto& missingShaders = getMissingShaders();
    if (std::find(missingShaders.begin(), missingShaders.end(), shaderStr) == missingShaders.end()) {
        missingShaders.push_back(shaderStr);
    }
}

std::shared_ptr<ShaderRender> ShaderPool::get(ShaderType shaderType, uint32_t properties, uint16_t customId){
    ShaderKey shaderKey = getShaderKey(shaderType, properties, customId);
    auto& shared = getMap()[shaderKey];

    if (shared && shared->isCreated()){
        return shared;
    }

    // The build can succeed and the backend still reject the shader (Metal compiles
    // the native library here). Retire the handle so its pool slot is released.
    if (shared && shared->isFailed()) {
        shared->destroyShader();
        failedShaders().insert(shaderKey);
        Log::error("Shader resource creation failed: %s",
                   getShaderStr(shaderType, properties, customId).c_str());
        return shared;
    }

    if (!shared) {
        shared = std::make_shared<ShaderRender>();
    }

    if (!shared->isCreated()) {
        // A previous attempt failed: don't retry every frame (it recompiles and spams
        // errors). Retried once remove()/destroyCustomShaders() clears the failure.
        if (failedShaders().count(shaderKey)) {
            return shared;
        }
        if (shaderBuilderFn) {
            ShaderBuildResult result = shaderBuilderFn(shaderKey);
            if (result.state == ResourceLoadState::Finished) {
                shared->createShader(result.data);
                failedShaders().erase(shaderKey);
            } else if (result.state == ResourceLoadState::Loading) {
                // Shader is still building, do nothing
            } else if (result.state == ResourceLoadState::Failed) {
                failedShaders().insert(shaderKey);
                Log::error("Shader build failed: %s", getShaderStr(shaderType, properties, customId).c_str());
            }
        } else {
            ShaderData tempShaderData;

            if (loadPrecompiled(shaderType, properties, customId, tempShaderData)){
                shared->createShader(tempShaderData);
            } else {
                // A set exported from stale scene keys can lack this variant
                std::string shaderStr = getShaderStr(shaderType, properties, customId);
                uint32_t fallbackProperties = getFallbackProperties(shaderType, properties, customId);
                if (fallbackProperties != properties){
                    Log::warn("Shader %s is missing, using %s", shaderStr.c_str(), getShaderStr(shaderType, fallbackProperties, customId).c_str());
                    // kept under the missing key too, so the search runs once
                    shared = get(shaderType, fallbackProperties, customId);
                    return shared;
                }
                addMissingShader(shaderStr);
            }
        }
    }

    return shared;
}

bool ShaderPool::isShaderBuildFailed(ShaderType shaderType, uint32_t properties, uint16_t customId){
    return failedShaders().count(getShaderKey(shaderType, properties, customId)) > 0;
}

void ShaderPool::remove(ShaderType shaderType, uint32_t properties, uint16_t customId){
    ShaderKey shaderKey = getShaderKey(shaderType, properties, customId);
    failedShaders().erase(shaderKey);
    auto& map = getMap();
    auto it = map.find(shaderKey);
    if (it != map.end()){
        if (!it->second || it->second.use_count() <= 1){
            if (it->second){
                it->second->destroyShader();
            }
            //Log::debug("Remove shader %s", shaderStr.c_str());
            map.erase(it);
        }
    }else{
        if (Engine::isViewLoaded()){
            Log::debug("Trying to destroy a non existent shader");
        }
    }
}

void ShaderPool::destroyCustomShaders(){
    for (auto& entry : getMap()){
        if (getCustomIdFromKey(entry.first) != 0 && entry.second && entry.second->isCreated()){
            entry.second->destroyShader();
        }
    }
    // Forget custom-shader build failures so an edited source is retried on the next get().
    for (auto it = failedShaders().begin(); it != failedShaders().end();){
        if (getCustomIdFromKey(*it) != 0){
            it = failedShaders().erase(it);
        } else {
            ++it;
        }
    }
}

ShaderType ShaderPool::getShaderTypeFromKey(ShaderKey key) {
    return (ShaderType)((key >> 32) & 0xFFFF);
}

uint32_t ShaderPool::getPropertiesFromKey(ShaderKey key) {
    return (uint32_t)(key & 0xFFFFFFFF);
}

uint16_t ShaderPool::getCustomIdFromKey(ShaderKey key) {
    return (uint16_t)((key >> 48) & 0xFFFF);
}

ShaderKey ShaderPool::getStorageKey(ShaderKey key) {
    // Strip the session-local customShaderId (bits 48..63); keep type + properties.
    return key & 0x0000FFFFFFFFFFFFULL;
}

uint32_t ShaderPool::getValidPropertyMask(ShaderType shaderType) {
    // Derived from the property-name table so a retired bit (e.g. MESH bit 5, the
    // old 'Pcf') is excluded automatically: it has no short name ("?").
    uint32_t mask = 0;
    int propCount = getShaderPropertyCount(shaderType);
    for (int i = 0; i < propCount; i++) {
        if (getShaderPropertyName(shaderType, i, true) != "?")
            mask |= (1u << i);
    }
    return mask;
}

ShaderKey ShaderPool::normalizeKey(ShaderKey key) {
    ShaderType type = getShaderTypeFromKey(key);
    uint16_t customId = getCustomIdFromKey(key);
    uint32_t properties = getPropertiesFromKey(key) & getValidPropertyMask(type);
    // Ult with L2d used to take Tan without Nmp
    if (type == ShaderType::MESH && (properties & 1u) && (properties & (1u << 22)) && !(properties & (1u << 7))){
        properties &= ~(1u << 8);
    }
    return getShaderKey(type, properties, customId);
}

uint32_t ShaderPool::getMeshProperties(
    bool unlit, bool uv1, bool uv2,
    bool punctual, bool shadows, bool normals, bool normalMap,
    bool tangents, bool vertexColorVec3, bool vertexColorVec4, bool textureRect,
    bool fog, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent,
    bool terrain, bool instanced, bool ibl, bool mirror, bool ssao, bool light2d, bool shadows2d,
    bool alphaMask, bool alphaOpaque, bool instanceFade, bool terrainPBR){
    uint32_t prop = 0;

    prop |= unlit            ? (1 <<  0) : 0;
    prop |= uv1              ? (1 <<  1) : 0;
    prop |= uv2              ? (1 <<  2) : 0;
    prop |= punctual         ? (1 <<  3) : 0;
    prop |= shadows          ? (1 <<  4) : 0;
    // bit 5 was USE_SHADOWS_PCF; the PCF filter is uniform-driven now (Scene
    // shadowQuality). Kept reserved so the other bit positions stay stable.
    prop |= normals          ? (1 <<  6) : 0;
    prop |= normalMap        ? (1 <<  7) : 0;
    prop |= tangents         ? (1 <<  8) : 0;
    prop |= vertexColorVec3  ? (1 <<  9) : 0;
    prop |= vertexColorVec4  ? (1 << 10) : 0;
    prop |= textureRect      ? (1 << 11) : 0;
    prop |= fog              ? (1 << 12) : 0;
    prop |= skinning         ? (1 << 13) : 0;
    prop |= morphTarget      ? (1 << 14) : 0;
    prop |= morphNormal      ? (1 << 15) : 0;
    prop |= morphTangent     ? (1 << 16) : 0;
    prop |= terrain          ? (1 << 17) : 0;
    prop |= instanced        ? (1 << 18) : 0;
    prop |= ibl              ? (1 << 19) : 0;
    prop |= mirror           ? (1 << 20) : 0;
    prop |= ssao             ? (1 << 21) : 0;
    prop |= light2d          ? (1 << 22) : 0;
    prop |= shadows2d        ? (1 << 23) : 0;
    prop |= alphaMask        ? (1 << 24) : 0;
    prop |= alphaOpaque      ? (1 << 25) : 0;
    prop |= instanceFade     ? (1 << 26) : 0;
    prop |= terrainPBR       ? (1 << 27) : 0;

    return prop;
}

uint32_t ShaderPool::getDepthMeshProperties(bool texture, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced, bool alphaMask, bool instanceFade){
    uint32_t prop = 0;

    prop |= texture          ? (1 <<  0) : 0;
    prop |= skinning         ? (1 <<  1) : 0;
    prop |= morphTarget      ? (1 <<  2) : 0;
    prop |= morphNormal      ? (1 <<  3) : 0;
    prop |= morphTangent     ? (1 <<  4) : 0;
    prop |= terrain          ? (1 <<  5) : 0;
    prop |= instanced        ? (1 <<  6) : 0;
    prop |= alphaMask        ? (1 <<  7) : 0;
    prop |= instanceFade     ? (1 <<  8) : 0;

    return prop;
}

uint32_t ShaderPool::getGBufferMeshProperties(bool baseColorTexture, bool normals, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced, bool metallicRoughnessTexture, bool terrainPBR){
    uint32_t prop = 0;

    prop |= baseColorTexture        ? (1 <<  0) : 0;
    prop |= normals                 ? (1 <<  1) : 0;
    prop |= skinning                ? (1 <<  2) : 0;
    prop |= morphTarget             ? (1 <<  3) : 0;
    prop |= morphNormal             ? (1 <<  4) : 0;
    prop |= morphTangent            ? (1 <<  5) : 0;
    prop |= terrain                 ? (1 <<  6) : 0;
    prop |= instanced               ? (1 <<  7) : 0;
    prop |= metallicRoughnessTexture ? (1 << 8) : 0;
    prop |= terrainPBR              ? (1 <<  9) : 0;

    return prop;
}

uint32_t ShaderPool::getUIProperties(bool texture, bool fontAtlasTexture, bool vertexColorVec3, bool vertexColorVec4){
    uint32_t prop = 0;

    prop |= texture          ? (1 <<  0) : 0;
    prop |= fontAtlasTexture ? (1 <<  1) : 0;
    prop |= vertexColorVec3  ? (1 <<  2) : 0;
    prop |= vertexColorVec4  ? (1 <<  3) : 0;

    return prop;
}

uint32_t ShaderPool::getPointsProperties(bool texture, bool vertexColorVec3, bool vertexColorVec4, bool textureRect){
    uint32_t prop = 0;

    prop |= texture          ? (1 <<  0) : 0;
    prop |= vertexColorVec3	 ? (1 <<  1) : 0;
    prop |= vertexColorVec4  ? (1 <<  2) : 0;
    prop |= textureRect      ? (1 <<  3) : 0;

    return prop;
}

uint32_t ShaderPool::getLinesProperties(bool vertexColorVec3, bool vertexColorVec4){
    uint32_t prop = 0;

    prop |= vertexColorVec3	 ? (1 <<  0) : 0;
    prop |= vertexColorVec4  ? (1 <<  1) : 0;

    return prop;
}

uint32_t ShaderPool::getWaterProperties(bool fog, bool sceneDepth, bool planarReflection, bool shadows, bool refraction){
    uint32_t prop = 0;

    prop |= fog              ? (1 <<  0) : 0;
    prop |= sceneDepth       ? (1 <<  1) : 0;
    prop |= planarReflection ? (1 <<  2) : 0;
    prop |= shadows          ? (1 <<  3) : 0;
    prop |= refraction       ? (1 <<  4) : 0;

    return prop;
}

void ShaderPool::clear(){
    for (auto& it: getMap()) {
        if (it.second)
            it.second->destroyShader();
    }
    //Log::debug("Remove all shaders");
    getMap().clear();
    getMissingShaders().clear();
    failedShaders().clear();

    std::lock_guard<std::mutex> lock(customShaderMutex());
    customShaderNames().clear();
}

void ShaderPool::clearUnused(){
    std::vector<ShaderKey> keys;
    keys.reserve(getMap().size());
    for (const auto& entry : getMap()) {
        keys.push_back(entry.first);
    }
    for (ShaderKey key : keys) {
        remove(getShaderTypeFromKey(key), getPropertiesFromKey(key), getCustomIdFromKey(key));
    }
    // Per-project bookkeeping: always reset so the next project does not inherit
    // missing/failed entries or custom-shader name registrations.
    getMissingShaders().clear();
    failedShaders().clear();
    std::lock_guard<std::mutex> lock(customShaderMutex());
    customShaderNames().clear();
}
