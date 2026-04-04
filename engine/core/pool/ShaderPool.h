//
// (c) 2024 Eduardo Doria.
//

#ifndef SHADERPOOL_H
#define SHADERPOOL_H

#include "Engine.h"
#include "render/ShaderRender.h"
#include <map>
#include <memory>
#include <functional>

namespace Supernova{

    typedef uint64_t ShaderKey;

    typedef std::map<ShaderKey, std::shared_ptr<ShaderRender>> shaders_t;

    enum class ShaderBuildState {
        Finished,
        Running,
        Failed,
        NotStarted
    };

    struct ShaderBuildResult {
        ShaderData data;
        ResourceLoadState state;

        ShaderBuildResult() : state(ResourceLoadState::NotStarted) {}
        ShaderBuildResult(const ShaderData& shaderData, ResourceLoadState buildState) 
            : data(shaderData), state(buildState) {}

        explicit operator bool() const {
            return state == ResourceLoadState::Finished;
        }
    };

    using ShaderBuilderFn = std::function<ShaderBuildResult(ShaderKey)>;

    class SUPERNOVA_API ShaderPool{
    private:
        static shaders_t& getMap();

        static ShaderBuilderFn shaderBuilderFn;

        static std::string getShaderFile(const std::string& shaderStr, const std::string& extension);
        static std::string getShaderName(const std::string& shaderStr);

        static void addMissingShader(const std::string& shaderStr);

    public:
        static ShaderKey getShaderKey(ShaderType shaderType, uint32_t properties);

        static std::shared_ptr<ShaderRender> get(ShaderType shaderType, uint32_t properties);
        static void remove(ShaderType shaderType, uint32_t properties);

        static std::string getShaderStr(ShaderType shaderType, uint32_t properties);
        static ShaderType getShaderTypeFromKey(ShaderKey key);
        static uint32_t getPropertiesFromKey(ShaderKey key);

        static std::string getShaderLangStr();
        static std::vector<std::string>& getMissingShaders();

        static void setShaderBuilder(ShaderBuilderFn fn);

        static uint32_t getMeshProperties(bool unlit, bool uv1, bool uv2, 
						bool punctual, bool shadows, bool shadowsPCF, bool normals, bool normalMap, 
						bool tangents, bool vertexColorVec3, bool vertexColorVec4, bool textureRect, 
                        bool fog, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent,
                        bool terrain, bool instanced);
        static uint32_t getDepthMeshProperties(bool texture, bool skinning, bool morphTarget, bool morphNormal, bool morphTangent, bool terrain, bool instanced);
        static uint32_t getUIProperties(bool texture, bool fontAtlasTexture, bool vertexColorVec3, bool vertexColorVec4);
        static uint32_t getPointsProperties(bool texture, bool vertexColorVec3, bool vertexColorVec4, bool textureRect);
        static uint32_t getLinesProperties(bool vertexColorVec3, bool vertexColorVec4);

		// necessary for engine shutdown
		static void clear();
    };
}

#endif /* SHADERPOOL_H */
