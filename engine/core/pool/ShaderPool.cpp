//
// (c) 2026 Eduardo Doria.
//

#include "ShaderPool.h"

#include "Log.h"
#include "Engine.h"
#include "shader/SBSReader.h"
#include "shader/ShaderDataSerializer.h"
#include "util/Base64.h"
#include <cstdint>
#include <algorithm>

#ifdef SOKOL_GLCORE
#include "glsl410.h"
#endif
#ifdef SOKOL_GLES3
#include "glsl300es.h"
#endif
#ifdef SOKOL_D3D11
#include "hlsl5.h"
#endif
#if SOKOL_METAL || DORIAX_APPLE
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "msl21ios.h"
#elif TARGET_OS_MAC
#include "msl21macos.h"
#endif
#endif

using namespace doriax;

ShaderBuilderFn ShaderPool::shaderBuilderFn = nullptr;

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

void ShaderPool::setShaderBuilder(ShaderBuilderFn fn) {
    shaderBuilderFn = fn;
}

std::string ShaderPool::getShaderLangStr(){
	if (Engine::getGraphicBackend() == GraphicBackend::GLCORE){
		return "glsl410";
	}else if (Engine::getGraphicBackend() == GraphicBackend::GLES3){
		return "glsl300es";
	}else if (Engine::getGraphicBackend() == GraphicBackend::METAL){
		if (Engine::getPlatform() == Platform::MacOS){
			return "msl21macos";
		}else if (Engine::getPlatform() == Platform::iOS){
			return "msl21ios";
		}
	}else if (Engine::getGraphicBackend() == GraphicBackend::D3D11){
		return "hlsl5";
	}

	return "<unknown>";
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
		case ShaderType::SKYBOX: return lowerCase ? "sky"    : "Skybox";
		case ShaderType::UI:     return lowerCase ? "ui"     : "UI";
		case ShaderType::POINTS: return lowerCase ? "points" : "Points";
		case ShaderType::LINES:  return lowerCase ? "lines"  : "Lines";
		default:                 return lowerCase ? "unknown": "Unknown";
	}
}

int ShaderPool::getShaderPropertyCount(ShaderType shaderType){
	switch (shaderType) {
		case ShaderType::MESH:   return 19;
		case ShaderType::DEPTH:  return 7;
		case ShaderType::UI:     return 4;
		case ShaderType::POINTS: return 4;
		case ShaderType::LINES:  return 2;
		case ShaderType::SKYBOX: return 0;
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
			case 5:  return shortName ? "Pcf" : "PCF";
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
	}
	return shortName ? "?" : "Unknown";
}

std::string ShaderPool::getShaderStr(ShaderType shaderType, uint32_t properties){

	std::string str = getShaderTypeName(shaderType, true);
	std::string propOut;

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

ShaderKey ShaderPool::getShaderKey(ShaderType shaderType, uint32_t properties) {
    return ((uint64_t)shaderType << 32) | (properties & 0xFFFFFFFF);
}

void ShaderPool::addMissingShader(const std::string& shaderStr) {
    auto& missingShaders = getMissingShaders();
    if (std::find(missingShaders.begin(), missingShaders.end(), shaderStr) == missingShaders.end()) {
        missingShaders.push_back(shaderStr);
    }
}

std::shared_ptr<ShaderRender> ShaderPool::get(ShaderType shaderType, uint32_t properties){
    ShaderKey shaderKey = getShaderKey(shaderType, properties);
    auto& shared = getMap()[shaderKey];

    if (shared && shared->isCreated()){
        return shared;
    }

    if (!shared) {
        shared = std::make_shared<ShaderRender>();
    }

    if (!shared->isCreated()) {
        if (shaderBuilderFn) {
            ShaderBuildResult result = shaderBuilderFn(shaderKey);
            if (result.state == ResourceLoadState::Finished) {
                shared->createShader(result.data);
            } else if (result.state == ResourceLoadState::Loading) {
                // Shader is still building, do nothing
            } else if (result.state == ResourceLoadState::Failed) {
                Log::error("Shader build failed");
            }
        } else {
            SBSReader sbs;
            ShaderData tempShaderData;

            std::string shaderStr = getShaderStr(shaderType, properties);
            std::string base64Shd = getBase64Shader(getShaderName(shaderStr));

            if (!base64Shd.empty() && sbs.read(Base64::decode(base64Shd))){
                shared->createShader(sbs.getShaderData());
            } else if (ShaderDataSerializer::readFromFile("shader://"+getShaderFile(shaderStr, ".sdat"), shaderKey, tempShaderData)){
                shared->createShader(tempShaderData);
            } else if (sbs.read("shader://"+getShaderFile(shaderStr, ".sbs"))){
                shared->createShader(sbs.getShaderData());
            } else {
                addMissingShader(shaderStr);
            }
        }
    }

    return shared;
}

void ShaderPool::remove(ShaderType shaderType, uint32_t properties){
	ShaderKey shaderKey = getShaderKey(shaderType, properties);
	if (getMap().count(shaderKey)){
		auto& shared = getMap()[shaderKey];
		if (shared.use_count() <= 1){
			shared->destroyShader();
			//Log::debug("Remove shader %s", shaderStr.c_str());
			getMap().erase(shaderKey);
		}
	}else{
		if (Engine::isViewLoaded()){
			Log::debug("Trying to destroy a non existent shader");
		}
	}
}

ShaderType ShaderPool::getShaderTypeFromKey(ShaderKey key) {
    return (ShaderType)(key >> 32);
}

uint32_t ShaderPool::getPropertiesFromKey(ShaderKey key) {
    return (uint32_t)(key & 0xFFFFFFFF);
}

uint32_t ShaderPool::getMeshProperties(
    bool unlit, bool uv1, bool uv2, 
    bool punctual, bool shadows, bool shadowsPCF, bool normals, bool normalMap, 
    bool tangents, bool vertexColorVec3, bool vertexColorVec4, bool textureRect, 
    bool fog, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent,
    bool terrain, bool instanced){
    uint32_t prop = 0;

    prop |= unlit            ? (1 <<  0) : 0;
    prop |= uv1              ? (1 <<  1) : 0;
    prop |= uv2              ? (1 <<  2) : 0;
    prop |= punctual         ? (1 <<  3) : 0;
    prop |= shadows          ? (1 <<  4) : 0;
    prop |= shadowsPCF       ? (1 <<  5) : 0;
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

    return prop;
}

uint32_t ShaderPool::getDepthMeshProperties(bool texture, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced){
    uint32_t prop = 0;

    prop |= texture          ? (1 <<  0) : 0;
    prop |= skinning         ? (1 <<  1) : 0;
    prop |= morphTarget      ? (1 <<  2) : 0;
    prop |= morphNormal      ? (1 <<  3) : 0;
    prop |= morphTangent     ? (1 <<  4) : 0;
    prop |= terrain          ? (1 <<  5) : 0;
    prop |= instanced        ? (1 <<  6) : 0;

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

void ShaderPool::clear(){
	for (auto& it: getMap()) {
		if (it.second)
			it.second->destroyShader();
	}
	//Log::debug("Remove all shaders");
	getMap().clear();
}
