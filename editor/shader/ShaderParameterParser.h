// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "shader/ShaderData.h"

#include <functional>
#include <string>
#include <vector>

namespace doriax::editor {

    // one annotation of a shader source, as "// @param float sway = 0.3 [0, 5] color runtime "Bend"",
    // "// @scene vec3 Wind.velocity" or "// @view float SeeThrough.radius"
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
        std::string description;
        std::string sourceName;
        int line = 0;
    };

    struct ShaderEffectAnnotations {
        std::vector<ShaderParameterDeclaration> parameters;
        // "@coverage_shadows apply|keep"
        bool hasCoverageShadows = false;
        bool coverageShadows = false;
        // the code reads Engine.time or Engine.resolution
        bool engineTime = false;
        bool engineResolution = false;
        std::vector<std::string> errors;
    };

    class ShaderParameterParser {
    public:
        static void parse(const std::string& source, const std::string& sourceName, ShaderEffectAnnotations& out);
        // a source and everything it includes, as lookup resolves them
        static void parseTree(const std::string& rootKey, const std::function<const std::string*(const std::string&)>& lookup,
                              ShaderEffectAnnotations& out);

        static std::string getTypeName(ShaderUniformType type);
        static bool parseTypeName(const std::string& name, ShaderUniformType& type);

        // member of the generated block: p_name, s_Collection_name, v_Collection_name
        static std::string getMemberName(const ShaderParameterDeclaration& declaration);

        // shader_params_vs.glsl or shader_params_fs.glsl, adding the effect context the hooks read
        static std::string generateHeader(ShaderStageType stage, const ShaderEffectAnnotations& annotations,
                                          bool vertexContext, bool fragmentContext,
                                          std::vector<ShaderParameterInfo>& parameters, std::vector<std::string>& errors);
    };

}
