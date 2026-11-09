// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderParameterParser.h"
#include "shader/ShaderParameters.h"
#include "util/Util.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

using namespace doriax;

namespace {

std::string trim(const std::string& value){
    size_t begin = 0;
    while (begin < value.size() && std::isspace((unsigned char)value[begin]))
        begin++;
    size_t end = value.size();
    while (end > begin && std::isspace((unsigned char)value[end - 1]))
        end--;
    return value.substr(begin, end - begin);
}

bool parseNumbers(const std::string& text, std::vector<float>& numbers){
    std::string cleaned;
    for (char c : text){
        cleaned += (c == ',' || c == '(' || c == ')') ? ' ' : c;
    }
    std::istringstream stream(cleaned);
    std::string token;
    while (stream >> token){
        char* end = nullptr;
        const float value = std::strtof(token.c_str(), &end);
        if (!end || *end != '\0')
            return false;
        numbers.push_back(value);
    }
    return true;
}

// the source without its comments
std::string stripComments(const std::string& source){
    std::string code;
    code.reserve(source.size());
    for (size_t i = 0; i < source.size(); i++){
        if (source.compare(i, 2, "//") == 0){
            while (i < source.size() && source[i] != '\n')
                i++;
        }else if (source.compare(i, 2, "/*") == 0){
            const size_t end = source.find("*/", i + 2);
            i = end == std::string::npos ? source.size() : end + 1;
            continue;
        }
        if (i < source.size())
            code += source[i];
    }
    return code;
}

void scanEngineUses(const std::string& source, editor::ShaderEffectAnnotations& out){
    const std::string code = stripComments(source);
    for (size_t at = code.find("Engine"); at != std::string::npos; at = code.find("Engine", at + 6)){
        if (at > 0 && (std::isalnum((unsigned char)code[at - 1]) || code[at - 1] == '_'))
            continue;
        size_t i = at + 6;
        while (i < code.size() && std::isspace((unsigned char)code[i]))
            i++;
        if (i >= code.size() || code[i] != '.')
            continue;
        i++;
        while (i < code.size() && std::isspace((unsigned char)code[i]))
            i++;
        size_t end = i;
        while (end < code.size() && (std::isalnum((unsigned char)code[end]) || code[end] == '_'))
            end++;
        const std::string member = code.substr(i, end - i);
        if (member == "time")
            out.engineTime = true;
        else if (member == "resolution")
            out.engineResolution = true;
    }
}

// ints are packed as floats in the block
std::string packedTypeName(ShaderUniformType type){
    switch (type){
        case ShaderUniformType::INT:  return "float";
        case ShaderUniformType::INT2: return "vec2";
        case ShaderUniformType::INT3: return "vec3";
        case ShaderUniformType::INT4: return "vec4";
        default:                      return editor::ShaderParameterParser::getTypeName(type);
    }
}

std::string displayName(const editor::ShaderParameterDeclaration& declaration){
    return declaration.collection.empty() ? declaration.name : declaration.collection + "." + declaration.name;
}

// "file:line: name", the start of an error
std::string errorPrefix(const editor::ShaderParameterDeclaration& declaration){
    return declaration.sourceName + ":" + std::to_string(declaration.line) + ": " + displayName(declaration);
}

struct Member {
    std::string declaration;  // GLSL member line
    std::string load;         // copy into the global
};

}

std::string editor::ShaderParameterParser::getTypeName(ShaderUniformType type){
    switch (type){
        case ShaderUniformType::FLOAT:  return "float";
        case ShaderUniformType::FLOAT2: return "vec2";
        case ShaderUniformType::FLOAT3: return "vec3";
        case ShaderUniformType::FLOAT4: return "vec4";
        case ShaderUniformType::INT:    return "int";
        case ShaderUniformType::INT2:   return "ivec2";
        case ShaderUniformType::INT3:   return "ivec3";
        case ShaderUniformType::INT4:   return "ivec4";
        default:                        return "";
    }
}

bool editor::ShaderParameterParser::parseTypeName(const std::string& name, ShaderUniformType& type){
    if (name == "float"){ type = ShaderUniformType::FLOAT; return true; }
    if (name == "vec2"){ type = ShaderUniformType::FLOAT2; return true; }
    if (name == "vec3"){ type = ShaderUniformType::FLOAT3; return true; }
    if (name == "vec4"){ type = ShaderUniformType::FLOAT4; return true; }
    if (name == "int"){ type = ShaderUniformType::INT; return true; }
    if (name == "ivec2"){ type = ShaderUniformType::INT2; return true; }
    if (name == "ivec3"){ type = ShaderUniformType::INT3; return true; }
    if (name == "ivec4"){ type = ShaderUniformType::INT4; return true; }
    return false;
}

std::string editor::ShaderParameterParser::getMemberName(const ShaderParameterDeclaration& declaration){
    switch (declaration.scope){
        case ShaderParameterScope::SCENE: return "s_" + declaration.collection + "_" + declaration.name;
        case ShaderParameterScope::VIEW:  return "v_" + declaration.collection + "_" + declaration.name;
        default:                          return "p_" + declaration.name;
    }
}

void editor::ShaderParameterParser::parse(const std::string& source, const std::string& sourceName, ShaderEffectAnnotations& out){
    scanEngineUses(source, out);

    std::istringstream stream(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)){
        lineNumber++;
        std::string text = trim(line);
        if (text.rfind("//", 0) != 0)
            continue;
        text = trim(text.substr(2));
        if (text.empty() || text[0] != '@')
            continue;

        const size_t tagEnd = text.find_first_of(" \t");
        const std::string tag = text.substr(0, tagEnd);
        std::string rest = tagEnd == std::string::npos ? std::string() : trim(text.substr(tagEnd));
        const std::string where = sourceName + ":" + std::to_string(lineNumber) + ": ";

        if (tag == "@coverage_shadows"){
            if (rest == "apply" || rest == "keep"){
                out.hasCoverageShadows = true;
                out.coverageShadows = rest == "apply";
            }else{
                out.errors.push_back(where + "@coverage_shadows takes apply or keep");
            }
            continue;
        }

        ShaderParameterDeclaration declaration;
        if (tag == "@param"){
            declaration.scope = ShaderParameterScope::OBJECT;
        }else if (tag == "@scene"){
            declaration.scope = ShaderParameterScope::SCENE;
        }else if (tag == "@view"){
            declaration.scope = ShaderParameterScope::VIEW;
        }else{
            continue; // another tool's annotation, like @image_sample_type
        }
        declaration.sourceName = sourceName;
        declaration.line = lineNumber;

        // the quoted description and the range come out first, wherever they are
        const size_t quote = rest.find('"');
        if (quote != std::string::npos){
            const size_t endQuote = rest.rfind('"');
            if (endQuote == quote){
                out.errors.push_back(where + "unterminated description");
                continue;
            }
            declaration.description = rest.substr(quote + 1, endQuote - quote - 1);
            rest = rest.substr(0, quote) + " " + rest.substr(endQuote + 1);
        }
        const size_t openRange = rest.find('[');
        if (openRange != std::string::npos){
            const size_t closeRange = rest.find(']', openRange);
            std::vector<float> range;
            if (closeRange == std::string::npos || !parseNumbers(rest.substr(openRange + 1, closeRange - openRange - 1), range) || range.size() != 2){
                out.errors.push_back(where + "a range is written [min, max]");
                continue;
            }
            declaration.hasRange = true;
            declaration.minValue = range[0];
            declaration.maxValue = range[1];
            rest = rest.substr(0, openRange) + " " + rest.substr(closeRange + 1);
        }

        std::string defaults;
        const size_t equals = rest.find('=');
        if (equals != std::string::npos){
            // flags may follow the numbers
            std::istringstream defaultWords(rest.substr(equals + 1));
            std::string word, flags;
            while (defaultWords >> word){
                if (std::isalpha((unsigned char)word[0]))
                    flags += " " + word;
                else
                    defaults += " " + word;
            }
            rest = rest.substr(0, equals) + flags;
        }

        std::istringstream words(rest);
        std::string typeName, name, flag;
        words >> typeName >> name;
        if (!parseTypeName(typeName, declaration.type)){
            out.errors.push_back(where + "'" + typeName + "' is not a parameter type (float, vec2-4, int or ivec2-4)");
            continue;
        }
        if (declaration.scope == ShaderParameterScope::OBJECT){
            declaration.name = name;
            if (!ShaderParameters::isValidName(name)){
                out.errors.push_back(where + "'" + name + "' is not a valid parameter name");
                continue;
            }
        }else{
            const size_t dot = name.find('.');
            if (dot == std::string::npos){
                out.errors.push_back(where + "a shared parameter is named Collection.name, not '" + name + "'");
                continue;
            }
            declaration.collection = name.substr(0, dot);
            declaration.name = name.substr(dot + 1);
            if (!ShaderParameters::isValidKey(declaration.collection, declaration.name) || declaration.collection == "params" ||
                    declaration.collection == "Engine"){
                out.errors.push_back(where + "'" + name + "' is not a valid shared parameter name");
                continue;
            }
        }
        bool badFlag = false;
        while (words >> flag){
            if (flag == "color"){
                declaration.color = true;
            }else if (flag == "runtime"){
                declaration.runtime = true;
            }else{
                out.errors.push_back(where + "unknown word '" + flag + "'");
                badFlag = true;
                break;
            }
        }
        if (badFlag)
            continue;

        if (!defaults.empty()){
            std::vector<float> values;
            const int components = ShaderParameters::getComponentCount(declaration.type);
            if (!parseNumbers(defaults, values) || values.empty() || (int)values.size() > components){
                out.errors.push_back(where + "the default of a " + typeName + " takes up to " + std::to_string(components) + " numbers");
                continue;
            }
            float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (size_t i = 0; i < values.size(); i++)
                value[i] = values[i];
            // a single number fills a vector
            if (values.size() == 1){
                for (int i = 1; i < components; i++)
                    value[i] = values[0];
            }
            declaration.defaultValue = Vector4(value[0], value[1], value[2], value[3]);
        }

        out.parameters.push_back(declaration);
    }
}

void editor::ShaderParameterParser::parseTree(const std::string& rootKey, const std::function<const std::string*(const std::string&)>& lookup,
                                              ShaderEffectAnnotations& out){
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit = [&](const std::string& key){
        if (!visited.insert(key).second)
            return;
        const std::string* source = lookup(key);
        if (!source)
            return;
        // effect files are keyed with a leading '/'
        parse(*source, (!key.empty() && key[0] == '/') ? key.substr(1) : key, out);
        for (const std::string& include : Util::getShaderIncludeKeys(*source))
            visit(include);
    };
    visit(rootKey);
}

std::string editor::ShaderParameterParser::generateHeader(ShaderStageType stage, const ShaderEffectAnnotations& annotations,
                                                          bool vertexContext, bool fragmentContext,
                                                          std::vector<ShaderParameterInfo>& parameters, std::vector<std::string>& errors){
    const std::string suffix = stage == ShaderStageType::VERTEX ? "VS" : "FS";

    std::vector<Member> members;
    auto addEngine = [&](ShaderUniformType type, const std::string& member, const std::string& name, const std::string& load){
        ShaderParameterInfo info;
        info.stage = stage;
        info.member = member;
        info.scope = ShaderParameterScope::ENGINE;
        info.name = name;
        info.type = type;
        parameters.push_back(info);
        members.push_back({(type == ShaderUniformType::MAT4 ? "mat4" : getTypeName(type)) + " " + member + ";", load});
    };
    // the context of the effect hooks
    if (vertexContext){
        addEngine(ShaderUniformType::MAT4, "effect_worldToModel", "worldToModel", "");
        addEngine(ShaderUniformType::FLOAT, "effect_offsetMax", "offsetMax", "");
    }
    if (fragmentContext){
        addEngine(ShaderUniformType::FLOAT4, "effect_eye", "eye", "");
        addEngine(ShaderUniformType::FLOAT3, "effect_forward", "forward", "");
        addEngine(ShaderUniformType::FLOAT4, "effect_pass", "pass", "");
    }
    // Engine.time and Engine.resolution, only where the code reads them
    std::string engineStruct;
    if (annotations.engineTime){
        addEngine(ShaderUniformType::FLOAT, "engine_time", "time", "Engine.time = block.engine_time;");
        engineStruct += " float time;";
    }
    if (annotations.engineResolution){
        addEngine(ShaderUniformType::FLOAT4, "engine_resolution", "resolution", "Engine.resolution = block.engine_resolution;");
        engineStruct += " vec4 resolution;";
    }

    // one declaration per parameter: repeats of the same type merge, others clash
    std::vector<ShaderParameterDeclaration> merged;
    std::map<std::string, ShaderParameterScope> collectionScopes;
    std::set<std::string> memberNames;
    for (const ShaderParameterDeclaration& declaration : annotations.parameters){
        bool repeated = false;
        for (ShaderParameterDeclaration& existing : merged){
            if (existing.scope == declaration.scope && existing.collection == declaration.collection && existing.name == declaration.name){
                if (existing.type != declaration.type){
                    errors.push_back(errorPrefix(declaration) + " is a " + getTypeName(declaration.type) +
                        " here but a " + getTypeName(existing.type) + " in " + existing.sourceName);
                }else if (existing.description.empty()){
                    existing.description = declaration.description;
                }
                repeated = true;
                break;
            }
        }
        if (repeated)
            continue;

        if (!declaration.collection.empty()){
            auto scope = collectionScopes.find(declaration.collection);
            if (scope != collectionScopes.end() && scope->second != declaration.scope){
                errors.push_back(errorPrefix(declaration) + " is in a " + ShaderParameters::getScopeName(scope->second) +
                    " collection, not a " + ShaderParameters::getScopeName(declaration.scope) + " one");
                continue;
            }
            collectionScopes[declaration.collection] = declaration.scope;
        }

        const std::string member = getMemberName(declaration);
        if (!memberNames.insert(member).second){
            errors.push_back(errorPrefix(declaration) + " clashes with another parameter");
            continue;
        }
        merged.push_back(declaration);
    }

    std::ostringstream structs;
    if (!engineStruct.empty())
        structs << "struct ShaderParams_Engine {" << engineStruct << " };\nShaderParams_Engine Engine;\n";

    // the object parameters, then each collection, as globals the load fills
    bool hasObject = false;
    for (const ShaderParameterDeclaration& declaration : merged)
        hasObject = hasObject || declaration.scope == ShaderParameterScope::OBJECT;
    if (hasObject){
        structs << "struct ShaderParamsObject {";
        for (const ShaderParameterDeclaration& declaration : merged){
            if (declaration.scope == ShaderParameterScope::OBJECT)
                structs << " " << getTypeName(declaration.type) << " " << declaration.name << ";";
        }
        structs << " };\nShaderParamsObject params;\n";
    }
    for (const auto& [collection, scope] : collectionScopes){
        structs << "struct ShaderParams_" << collection << " {";
        for (const ShaderParameterDeclaration& declaration : merged){
            if (declaration.collection == collection)
                structs << " " << getTypeName(declaration.type) << " " << declaration.name << ";";
        }
        structs << " };\nShaderParams_" << collection << " " << collection << ";\n";
    }

    for (const ShaderParameterDeclaration& declaration : merged){
        ShaderParameterInfo info;
        info.stage = stage;
        info.member = getMemberName(declaration);
        info.scope = declaration.scope;
        info.collection = declaration.collection;
        info.name = declaration.name;
        info.type = declaration.type;
        info.defaultValue = declaration.defaultValue;
        parameters.push_back(info);

        const std::string target = declaration.scope == ShaderParameterScope::OBJECT ? "params." + declaration.name : declaration.collection + "." + declaration.name;
        const std::string packed = packedTypeName(declaration.type);
        const std::string value = packed == getTypeName(declaration.type) ? "block." + info.member : getTypeName(declaration.type) + "(block." + info.member + ")";
        members.push_back({packed + " " + info.member + ";", target + " = " + value + ";"});
    }

    if (members.empty())
        return "// no parameters\n";

    std::ostringstream header;
    header << "// generated from the parameter annotations of this stage\n";
    header << "#ifndef SHADER_PARAMS_" << suffix << "_GENERATED\n";
    header << "#define SHADER_PARAMS_" << suffix << "_GENERATED\n";
    header << "#define HAS_SHADER_PARAMS_" << suffix << "\n";
    header << "#define SHADER_PARAMS_" << suffix << "_MEMBERS";
    for (const Member& member : members)
        header << " " << member.declaration;
    header << "\n";
    header << structs.str();
    header << "#define SHADER_PARAMS_" << suffix << "_LOAD(block)";
    for (const Member& member : members){
        if (!member.load.empty())
            header << " " << member.load;
    }
    header << "\n#endif\n";
    return header.str();
}
