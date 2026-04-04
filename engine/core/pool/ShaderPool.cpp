//
// (c) 2024 Eduardo Doria.
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
#if SOKOL_METAL || SUPERNOVA_APPLE
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "msl21ios.h"
#elif TARGET_OS_MAC
#include "msl21macos.h"
#endif
#endif

using namespace Supernova;

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

std::string ShaderPool::getShaderStr(ShaderType shaderType, uint32_t properties){

	std::string str;
	std::string propOut;

	if (shaderType == ShaderType::MESH){
		str = "mesh";

		if (properties & (1 << 0))  propOut += "Ult";
		if (properties & (1 << 1))  propOut += "Uv1";
		if (properties & (1 << 2))  propOut += "Uv2";
		if (properties & (1 << 3))  propOut += "Puc";
		if (properties & (1 << 4))  propOut += "Shw";
		if (properties & (1 << 5))  propOut += "Pcf";
		if (properties & (1 << 6))  propOut += "Nor";
		if (properties & (1 << 7))  propOut += "Nmp";
		if (properties & (1 << 8))  propOut += "Tan";
		if (properties & (1 << 9))  propOut += "Vc3";
		if (properties & (1 << 10)) propOut += "Vc4";
		if (properties & (1 << 11)) propOut += "Txr";
		if (properties & (1 << 12)) propOut += "Fog";
		if (properties & (1 << 13)) propOut += "Ski";
		if (properties & (1 << 14)) propOut += "Mta";
		if (properties & (1 << 15)) propOut += "Mnr";
		if (properties & (1 << 16)) propOut += "Mtg";
		if (properties & (1 << 17)) propOut += "Ter";
		if (properties & (1 << 18)) propOut += "Ist";

	}else if (shaderType == ShaderType::DEPTH){
		str = "depth";

		if (properties & (1 << 0))  propOut += "Tex";
		if (properties & (1 << 1))  propOut += "Ski";
		if (properties & (1 << 2))  propOut += "Mta";
		if (properties & (1 << 3))  propOut += "Mnr";
		if (properties & (1 << 4))  propOut += "Mtg";
		if (properties & (1 << 5))  propOut += "Ter";
		if (properties & (1 << 6))  propOut += "Ist";

	}else if (shaderType == ShaderType::SKYBOX){
		str = "sky";

	}else if (shaderType == ShaderType::UI){
		str = "ui";

		if (properties & (1 << 0))  propOut += "Tex";
		if (properties & (1 << 1))  propOut += "Ftx";
		if (properties & (1 << 2))  propOut += "Vc3";
		if (properties & (1 << 3))  propOut += "Vc4";

	}else if (shaderType == ShaderType::POINTS){
		str = "points";

		if (properties & (1 << 0))  propOut += "Tex";
		if (properties & (1 << 1))  propOut += "Vc3";
		if (properties & (1 << 2))  propOut += "Vc4";
		if (properties & (1 << 3))  propOut += "Txr";

	}else if (shaderType == ShaderType::LINES){
		str = "lines";

		if (properties & (1 << 0))  propOut += "Vc3";
		if (properties & (1 << 1))  propOut += "Vc4";
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
