// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "shader/ShaderData.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace doriax::editor {

    // one declaration of a shader source, as "#pragma param float sway = 0.3 [0, 5] color runtime "Bend"",
    // "#pragma scene vec3 Wind.velocity" or "#pragma view float SeeThrough.radius"
    struct ShaderParameterDeclaration {
        ShaderParameterScope scope = ShaderParameterScope::OBJECT;
        std::string collection;  // SCENE and VIEW
        std::string name;
        ShaderUniformType type = ShaderUniformType::FLOAT;
        Vector4 defaultValue = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        bool hasRange = false;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        bool color = false;
        bool runtime = false;
        // "#pragma param texture2D detail = white linear repeat"
        bool texture = false;
        TextureType textureType = TextureType::TEXTURE_2D;
        ShaderTextureDefault textureDefault = ShaderTextureDefault::WHITE;
        TextureFilter filter = TextureFilter::LINEAR;
        TextureWrap wrap = TextureWrap::REPEAT;
        std::string description;
        std::string sourceName;
        int line = 0;
    };

    struct ShaderEffectAnnotations {
        std::vector<ShaderParameterDeclaration> parameters;
        // "#pragma shader mesh"
        std::string shaderType;
        // hooks the code defines
        bool vertexOffset = false;
        bool coverage = false;
        bool surface = false;
        bool color = false;
        bool varyingsHook = false;
        bool clipPosition = false;
        bool depthOffset = false;
        bool underwaterColor = false;
        // fields they read of their context
        std::set<std::string> contextReads;
        // post-process inputs they sample: sceneDepth, sceneNormal, sceneOcclusion
        std::set<std::string> sceneReads;
        // "#pragma coverage_shadows apply|keep"
        bool hasCoverageShadows = false;
        bool coverageShadows = false;
        // "#pragma shading unlit"
        bool unlit = false;
        // "#pragma varying vec3 tipColor", type and name
        std::vector<std::pair<std::string, std::string>> varyings;
        // "#pragma surface_replaces baseColor alpha", fields the surface hook sets from scratch
        std::set<std::string> surfaceReplaces;
        // the code reads Engine.time or Engine.resolution
        bool engineTime = false;
        bool engineResolution = false;
        bool enginePass = false;
        std::vector<std::string> errors;
    };

    // engine values a stage's hooks read
    enum HookContext : uint32_t {
        HookContext_Vertex = 1 << 0,     // worldToModel and offsetMax
        HookContext_Fragment = 1 << 1,   // eye, forward and pass
        HookContext_View = 1 << 2,       // the camera's view matrix
        HookContext_Projection = 1 << 3, // the depth terms of the projection
        HookContext_Clip = 1 << 4,       // the y flip of the clip position
        HookContext_Model = 1 << 5       // the model matrix
    };

    class ShaderParameterParser {
    public:
        static void parse(const std::string& source, const std::string& sourceName, ShaderEffectAnnotations& out);
        // a source and its includes, as one stage reads them
        static void parseTree(const std::string& rootKey, const std::function<const std::string*(const std::string&)>& lookup,
                              ShaderStageType stage, ShaderEffectAnnotations& out);

        // the other stage's section blanked
        static std::string getStageSource(const std::string& source, ShaderStageType stage);
        // "#pragma stage" lines as #if on SHADER_STAGE_VERTEX and SHADER_STAGE_FRAGMENT
        static std::string getStageConditions(const std::string& source);

        static std::string getTypeName(ShaderUniformType type);
        static bool parseTypeName(const std::string& name, ShaderUniformType& type);

        // member of the generated block: p_name, s_Collection_name, v_Collection_name
        static std::string getMemberName(const ShaderParameterDeclaration& declaration);

        // the "#pragma shader" name of a program; the mesh passes share "mesh", the underwater fade "water"
        static std::string getShaderFileType(ShaderType shaderType);
        // shader_params_vs.glsl or shader_params_fs.glsl, with the context the effect hooks read
        static std::string generateHeader(ShaderStageType stage, const ShaderEffectAnnotations& annotations, uint32_t context,
                                          std::vector<ShaderParameterInfo>& parameters, std::vector<ShaderTextureInfo>& textures);
        // the Varyings struct and its outputs or inputs
        static std::string generateVaryings(ShaderStageType stage, const std::vector<std::pair<std::string, std::string>>& varyings);
        // defines of the texture names, or their undefs
        static std::string generateTextureNames(ShaderStageType stage, const std::vector<ShaderTextureInfo>& textures, bool define);
        // a name with two types, a collection in two scopes; given both stages, as one value feeds them
        static void checkDeclarations(const std::vector<ShaderParameterDeclaration>& declarations, std::vector<std::string>& errors);
        // the clashes between sources, which none of them shows alone
        static std::vector<std::string> checkSources(const std::vector<const ShaderEffectAnnotations*>& sources);
    };

}
