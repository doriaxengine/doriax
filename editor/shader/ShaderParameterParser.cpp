// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderParameterParser.h"
#include "shader/ShaderParameters.h"
#include "util/Util.h"

#include <algorithm>
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

// what follows "#pragma" on a line, without a comment after it
std::string pragmaText(const std::string& line){
    std::string text = trim(line);
    if (text.empty() || text[0] != '#')
        return "";
    text = trim(text.substr(1));
    if (text.rfind("pragma", 0) != 0 || text.size() < 7 || !std::isspace((unsigned char)text[6]))
        return "";

    bool quoted = false;
    for (size_t i = 6; i + 1 < text.size(); i++){
        if (text[i] == '"')
            quoted = !quoted;
        else if (!quoted && text[i] == '/' && text[i + 1] == '/')
            return trim(text.substr(6, i - 6));
    }
    return trim(text.substr(6));
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

// default, filter or wrap of a texture declaration
bool parseTextureWord(const std::string& word, editor::ShaderParameterDeclaration& declaration){
    if (word == "white"){ declaration.textureDefault = ShaderTextureDefault::WHITE; return true; }
    if (word == "black"){ declaration.textureDefault = ShaderTextureDefault::BLACK; return true; }
    if (word == "normal"){ declaration.textureDefault = ShaderTextureDefault::NORMAL; return true; }
    if (word == "nearest"){ declaration.filter = TextureFilter::NEAREST; return true; }
    if (word == "linear"){ declaration.filter = TextureFilter::LINEAR; return true; }
    if (word == "trilinear"){ declaration.filter = TextureFilter::LINEAR_MIPMAP_LINEAR; return true; }
    if (word == "repeat"){ declaration.wrap = TextureWrap::REPEAT; return true; }
    if (word == "clamp"){ declaration.wrap = TextureWrap::CLAMP_TO_EDGE; return true; }
    if (word == "mirror"){ declaration.wrap = TextureWrap::MIRRORED_REPEAT; return true; }
    return false;
}

// a hook is defined where its name is followed by its context type
bool definesHook(const std::string& code, const std::string& name, const std::string& contextType){
    for (size_t at = code.find(name); at != std::string::npos; at = code.find(name, at + name.size())){
        if (at > 0 && (std::isalnum((unsigned char)code[at - 1]) || code[at - 1] == '_'))
            continue;
        size_t i = at + name.size();
        while (i < code.size() && std::isspace((unsigned char)code[i]))
            i++;
        if (i >= code.size() || code[i] != '(')
            continue;
        i++;
        while (i < code.size() && std::isspace((unsigned char)code[i]))
            i++;
        if (code.compare(i, contextType.size(), contextType) == 0)
            return true;
    }
    return false;
}

void scanCode(const std::string& source, editor::ShaderEffectAnnotations& out){
    const std::string code = stripComments(source);
    out.vertexOffset = out.vertexOffset || definesHook(code, "getVertexOffset", "VertexContext");
    out.coverage = out.coverage || definesHook(code, "getCoverage", "FragmentContext");
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

// one declaration per parameter: repeats of the same type merge, others clash
std::vector<editor::ShaderParameterDeclaration> mergeDeclarations(const std::vector<editor::ShaderParameterDeclaration>& declarations, std::vector<std::string>* errors){
    using editor::ShaderParameterParser;
    // an include both stages reach would report its clash twice
    auto report = [&](const std::string& error){
        if (errors && std::find(errors->begin(), errors->end(), error) == errors->end())
            errors->push_back(error);
    };

    std::vector<editor::ShaderParameterDeclaration> merged;
    std::map<std::string, ShaderParameterScope> collectionScopes;
    std::set<std::string> memberNames;
    for (const editor::ShaderParameterDeclaration& declaration : declarations){
        bool repeated = false;
        for (editor::ShaderParameterDeclaration& existing : merged){
            if (existing.scope == declaration.scope && existing.collection == declaration.collection && existing.name == declaration.name){
                if (existing.texture != declaration.texture || (declaration.texture && existing.textureType != declaration.textureType)){
                    report(errorPrefix(declaration) + " is another kind of parameter in " + existing.sourceName);
                }else if (declaration.texture && (existing.filter != declaration.filter || existing.wrap != declaration.wrap ||
                        existing.textureDefault != declaration.textureDefault)){
                    report(errorPrefix(declaration) + " has another default, filter or wrap in " + existing.sourceName);
                }else if (!declaration.texture && existing.type != declaration.type){
                    report(errorPrefix(declaration) + " is a " + ShaderParameterParser::getTypeName(declaration.type) +
                        " here but a " + ShaderParameterParser::getTypeName(existing.type) + " in " + existing.sourceName);
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
                report(errorPrefix(declaration) + " is in a " + ShaderParameters::getScopeName(scope->second) +
                    " collection, not a " + ShaderParameters::getScopeName(declaration.scope) + " one");
                continue;
            }
            collectionScopes[declaration.collection] = declaration.scope;
        }

        if (!memberNames.insert(ShaderParameterParser::getMemberName(declaration)).second){
            report(errorPrefix(declaration) + " clashes with another parameter");
            continue;
        }
        merged.push_back(declaration);
    }
    return merged;
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
    scanCode(source, out);


    std::set<std::string> stages;
    std::istringstream stream(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)){
        lineNumber++;
        std::string text = pragmaText(line);
        if (text.empty())
            continue;

        const size_t tagEnd = text.find_first_of(" \t");
        const std::string tag = text.substr(0, tagEnd);
        std::string rest = tagEnd == std::string::npos ? std::string() : trim(text.substr(tagEnd));
        const std::string where = sourceName + ":" + std::to_string(lineNumber) + ": ";

        if (tag == "shader"){
            if (rest.empty() || !ShaderParameters::isValidName(rest))
                out.errors.push_back(where + "shader takes the type it extends, as in \"#pragma shader mesh\"");
            else
                out.shaderType = rest;
            continue;
        }
        if (tag == "stage"){
            if (rest != "vertex" && rest != "fragment")
                out.errors.push_back(where + "stage takes vertex or fragment");
            else if (!stages.insert(rest).second)
                out.errors.push_back(where + "the " + rest + " stage has a section already");
            continue;
        }
        if (tag == "coverage_shadows"){
            if (rest == "apply" || rest == "keep"){
                out.hasCoverageShadows = true;
                out.coverageShadows = rest == "apply";
            }else{
                out.errors.push_back(where + "coverage_shadows takes apply or keep");
            }
            continue;
        }

        ShaderParameterDeclaration declaration;
        if (tag == "param"){
            declaration.scope = ShaderParameterScope::OBJECT;
        }else if (tag == "scene"){
            declaration.scope = ShaderParameterScope::SCENE;
        }else if (tag == "view"){
            declaration.scope = ShaderParameterScope::VIEW;
        }else{
            continue; // the compiler's own, like optimize
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
        if (typeName == "texture2D" || typeName == "textureCube"){
            declaration.texture = true;
            declaration.textureType = typeName == "texture2D" ? TextureType::TEXTURE_2D : TextureType::TEXTURE_CUBE;
            declaration.textureDefault = typeName == "texture2D" ? ShaderTextureDefault::WHITE : ShaderTextureDefault::BLACK;
            if (declaration.scope != ShaderParameterScope::OBJECT){
                out.errors.push_back(where + "a texture is declared with #pragma param");
                continue;
            }
        }else if (!parseTypeName(typeName, declaration.type)){
            out.errors.push_back(where + "'" + typeName + "' is not a parameter type (float, vec2-4, int, ivec2-4, texture2D or textureCube)");
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
            if (declaration.texture && parseTextureWord(flag, declaration)){
                continue;
            }else if (flag == "color" && !declaration.texture){
                declaration.color = true;
            }else if (flag == "runtime" && !declaration.texture){
                declaration.runtime = true;
            }else{
                out.errors.push_back(where + "unknown word '" + flag + "'");
                badFlag = true;
                break;
            }
        }
        if (badFlag)
            continue;

        if (declaration.texture && (!defaults.empty() || declaration.hasRange)){
            out.errors.push_back(where + "a texture takes white, black or normal, a filter (nearest, linear, trilinear) and a wrap (repeat, clamp, mirror)");
            continue;
        }
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
                                              ShaderStageType stage, ShaderEffectAnnotations& out){
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit = [&](const std::string& key){
        if (!visited.insert(key).second)
            return;
        const std::string* source = lookup(key);
        if (!source)
            return;
        const std::string staged = getStageSource(*source, stage);
        // shader files are keyed with a leading '/'
        parse(staged, (!key.empty() && key[0] == '/') ? key.substr(1) : key, out);
        for (const std::string& include : Util::getShaderIncludeKeys(staged))
            visit(include);
    };
    visit(rootKey);
}

std::string editor::ShaderParameterParser::getStageSource(const std::string& source, ShaderStageType stage){
    if (source.find("stage") == std::string::npos)
        return source;

    const std::string own = stage == ShaderStageType::VERTEX ? "vertex" : "fragment";
    std::string staged;
    staged.reserve(source.size());
    bool skip = false;
    size_t start = 0;
    while (start < source.size()){
        size_t end = source.find('\n', start);
        end = end == std::string::npos ? source.size() : end;
        const std::string line = source.substr(start, end - start);
        const std::string text = pragmaText(line);
        if (text.rfind("stage", 0) == 0 && (text.size() == 5 || std::isspace((unsigned char)text[5]))){
            skip = trim(text.substr(5)) != own;
            staged += line;
        }else if (!skip){
            staged += line;
        }
        if (end < source.size())
            staged += '\n';
        start = end + 1;
    }
    return staged;
}

std::string editor::ShaderParameterParser::getStageConditions(const std::string& source){
    if (source.find("stage") == std::string::npos)
        return source;

    std::string conditions;
    conditions.reserve(source.size() + 64);
    bool open = false;
    size_t start = 0;
    while (start < source.size()){
        size_t end = source.find('\n', start);
        end = end == std::string::npos ? source.size() : end;
        const std::string line = source.substr(start, end - start);
        const std::string text = pragmaText(line);
        if (text.rfind("stage", 0) == 0 && (text.size() == 5 || std::isspace((unsigned char)text[5]))){
            const std::string name = trim(text.substr(5)) == "vertex" ? "SHADER_STAGE_VERTEX" : "SHADER_STAGE_FRAGMENT";
            conditions += (open ? "#elif defined(" : "#if defined(") + name + ")";
            open = true;
        }else{
            conditions += line;
        }
        if (end < source.size())
            conditions += '\n';
        start = end + 1;
    }
    if (open)
        conditions += "\n#endif\n";
    return conditions;
}

std::string editor::ShaderParameterParser::generateTextureNames(ShaderStageType stage, const std::vector<ShaderTextureInfo>& textures, bool define){
    std::ostringstream names;
    for (const ShaderTextureInfo& texture : textures){
        if (texture.stage != stage)
            continue;
        if (!define){
            names << "#undef " << texture.name << "\n#undef " << texture.name << "_sampler\n";
            continue;
        }
        const char* combined = texture.type == TextureType::TEXTURE_CUBE ? " samplerCube(" : " sampler2D(";
        names << "#define " << texture.name << "_sampler " << texture.getSamplerName() << "\n";
        names << "#define " << texture.name << combined << texture.getTextureName() << ", " << texture.getSamplerName() << ")\n";
    }
    return names.str();
}

void editor::ShaderParameterParser::checkDeclarations(const std::vector<ShaderParameterDeclaration>& declarations, std::vector<std::string>& errors){
    mergeDeclarations(declarations, &errors);
}

std::vector<std::string> editor::ShaderParameterParser::checkSources(const std::vector<const ShaderEffectAnnotations*>& sources){
    std::vector<ShaderParameterDeclaration> declarations;
    std::vector<std::string> known;
    for (const ShaderEffectAnnotations* annotations : sources){
        declarations.insert(declarations.end(), annotations->parameters.begin(), annotations->parameters.end());
        known.insert(known.end(), annotations->errors.begin(), annotations->errors.end());
    }

    std::vector<std::string> errors;
    checkDeclarations(declarations, errors);
    errors.erase(std::remove_if(errors.begin(), errors.end(), [&](const std::string& error){
        return std::find(known.begin(), known.end(), error) != known.end();
    }), errors.end());

    return errors;
}

std::string editor::ShaderParameterParser::generateHeader(ShaderStageType stage, const ShaderEffectAnnotations& annotations,
                                                          bool vertexContext, bool fragmentContext,
                                                          std::vector<ShaderParameterInfo>& parameters, std::vector<ShaderTextureInfo>& textures){
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

    const std::vector<ShaderParameterDeclaration> merged = mergeDeclarations(annotations.parameters, nullptr);
    std::set<std::string> collections;
    for (const ShaderParameterDeclaration& declaration : merged){
        if (!declaration.collection.empty())
            collections.insert(declaration.collection);
    }

    std::ostringstream structs;
    if (!engineStruct.empty())
        structs << "struct ShaderParams_Engine {" << engineStruct << " };\nShaderParams_Engine Engine;\n";

    // the object parameters, then each collection, as globals the load fills
    bool hasObject = false;
    for (const ShaderParameterDeclaration& declaration : merged)
        hasObject = hasObject || (declaration.scope == ShaderParameterScope::OBJECT && !declaration.texture);
    if (hasObject){
        structs << "struct ShaderParamsObject {";
        for (const ShaderParameterDeclaration& declaration : merged){
            if (declaration.scope == ShaderParameterScope::OBJECT && !declaration.texture)
                structs << " " << getTypeName(declaration.type) << " " << declaration.name << ";";
        }
        structs << " };\nShaderParamsObject params;\n";
    }

    // textures of one filter and wrap share a sampler
    std::ostringstream textureLines;
    std::set<std::string> samplers;
    for (const ShaderParameterDeclaration& declaration : merged){
        if (!declaration.texture)
            continue;
        ShaderTextureInfo info;
        info.stage = stage;
        info.name = declaration.name;
        info.type = declaration.textureType;
        info.defaultTexture = declaration.textureDefault;
        info.filter = declaration.filter;
        info.wrap = declaration.wrap;
        textures.push_back(info);

        const bool cube = info.type == TextureType::TEXTURE_CUBE;
        if (samplers.insert(info.getSamplerName()).second)
            textureLines << "uniform sampler " << info.getSamplerName() << ";\n";
        textureLines << "uniform " << (cube ? "textureCube " : "texture2D ") << info.getTextureName() << ";\n";
    }
    for (const std::string& collection : collections){
        structs << "struct ShaderParams_" << collection << " {";
        for (const ShaderParameterDeclaration& declaration : merged){
            if (declaration.collection == collection)
                structs << " " << getTypeName(declaration.type) << " " << declaration.name << ";";
        }
        structs << " };\nShaderParams_" << collection << " " << collection << ";\n";
    }

    for (const ShaderParameterDeclaration& declaration : merged){
        if (declaration.texture)
            continue;
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

    if (members.empty() && samplers.empty())
        return "// no parameters\n";

    std::ostringstream header;
    header << "// generated from the parameter annotations of this stage\n";
    header << "#ifndef SHADER_PARAMS_" << suffix << "_GENERATED\n";
    header << "#define SHADER_PARAMS_" << suffix << "_GENERATED\n";
    header << textureLines.str();
    if (!members.empty()){
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
        header << "\n";
    }
    header << "#endif\n";
    return header.str();
}
