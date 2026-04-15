#include "ScriptParser.h"
#include "Out.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <algorithm>
#include <unordered_set>

using namespace doriax;


ScriptPropertyType editor::ScriptParser::inferTypeFromCppType(const std::string& cppType, std::string& ptrTypeName) {
    // Remove const, &, and whitespace for comparison
    std::string cleanType = cppType;
    cleanType.erase(std::remove_if(cleanType.begin(), cleanType.end(), ::isspace), cleanType.end());

    // Remove const qualifier
    size_t constPos = cleanType.find("const");
    if (constPos != std::string::npos) {
        cleanType.erase(constPos, 5);
    }

    // Check if it's a pointer type (has *)
    bool isPointer = (cleanType.find('*') != std::string::npos);

    // Remove references and pointers for type matching
    std::string bareType = cleanType;
    bareType.erase(std::remove(bareType.begin(), bareType.end(), '&'), bareType.end());
    bareType.erase(std::remove(bareType.begin(), bareType.end(), '*'), bareType.end());

    // If it's a pointer type, store the type name without * and return Pointer
    if (isPointer) {
        ptrTypeName = bareType; // Store "Mesh", "Object", etc. (without *)
        return ScriptPropertyType::EntityReference;
    }

    // Map C++ types to ScriptPropertyType
    if (bareType == "bool") {
        return ScriptPropertyType::Bool;
    }

    if (bareType == "int" || bareType == "int32_t" || bareType == "uint32_t" ||
        bareType == "short" || bareType == "long") {
        return ScriptPropertyType::Int;
    }

    if (bareType == "float" || bareType == "double") {
        return ScriptPropertyType::Float;
    }

    if (bareType == "std::string" || bareType == "string") {
        return ScriptPropertyType::String;
    }

    if (bareType == "Vector2" || bareType == "doriax::Vector2") {
        return ScriptPropertyType::Vector2;
    }

    if (bareType == "Vector3" || bareType == "doriax::Vector3") {
        return ScriptPropertyType::Vector3;
    }

    if (bareType == "Vector4" || bareType == "doriax::Vector4") {
        return ScriptPropertyType::Vector4;
    }

    // Default to int if unknown
    Out::warning("Unknown C++ type '%s', defaulting to Int", cppType.c_str());
    return ScriptPropertyType::Int;
}

ScriptPropertyType editor::ScriptParser::parseExplicitType(const std::string& typeStr, const std::string& cppType) {
    std::string cleanType = typeStr;
    cleanType.erase(std::remove_if(cleanType.begin(), cleanType.end(), ::isspace), cleanType.end());

    if (cleanType == "Bool") return ScriptPropertyType::Bool;
    if (cleanType == "Int") return ScriptPropertyType::Int;
    if (cleanType == "Float") return ScriptPropertyType::Float;
    if (cleanType == "String") return ScriptPropertyType::String;
    if (cleanType == "Vector2") return ScriptPropertyType::Vector2;
    if (cleanType == "Vector3") return ScriptPropertyType::Vector3;
    if (cleanType == "Vector4") return ScriptPropertyType::Vector4;
    if (cleanType == "Color3") return ScriptPropertyType::Color3;
    if (cleanType == "Color4") return ScriptPropertyType::Color4;
    //if (cleanType == "Pointer") return ScriptPropertyType::Pointer;

    // Handle "Color" - infer based on C++ type
    if (cleanType == "Color") {
        // Remove whitespace from C++ type for comparison
        std::string cleanCppType = cppType;
        cleanCppType.erase(std::remove_if(cleanCppType.begin(), cleanCppType.end(), ::isspace), cleanCppType.end());

        // Remove const, &, * for bare type
        std::string bareCppType = cleanCppType;
        size_t constPos = bareCppType.find("const");
        if (constPos != std::string::npos) {
            bareCppType.erase(constPos, 5);
        }
        bareCppType.erase(std::remove(bareCppType.begin(), bareCppType.end(), '&'), bareCppType.end());
        bareCppType.erase(std::remove(bareCppType.begin(), bareCppType.end(), '*'), bareCppType.end());

        // Check if it's Vector4 or Vector3
        if (bareCppType == "Vector4" || bareCppType == "doriax::Vector4") {
            return ScriptPropertyType::Color4;
        } else {
            // Default to Color3 for Vector3 or any other type
            return ScriptPropertyType::Color3;
        }
    }

    Out::warning("Unknown explicit type '%s', will fall back to inferred type", typeStr.c_str());
    return ScriptPropertyType::Int; // Placeholder, will be overridden
}

std::string editor::ScriptParser::removeComments(const std::string& content) {
    std::string result;
    result.reserve(content.size());

    enum class State { Normal, InLineComment, InBlockComment, InString, InChar };
    State state = State::Normal;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        char next = (i + 1 < content.size()) ? content[i + 1] : '\0';

        switch (state) {
            case State::Normal:
                if (c == '/' && next == '/') {
                    state = State::InLineComment;
                    i++; // Skip next char
                } else if (c == '/' && next == '*') {
                    state = State::InBlockComment;
                    i++; // Skip next char
                } else if (c == '"') {
                    state = State::InString;
                    result += c;
                } else if (c == '\'') {
                    state = State::InChar;
                    result += c;
                } else {
                    result += c;
                }
                break;

            case State::InLineComment:
                if (c == '\n') {
                    state = State::Normal;
                    result += c; // Keep newline
                }
                break;

            case State::InBlockComment:
                if (c == '*' && next == '/') {
                    state = State::Normal;
                    i++; // Skip next char
                }
                break;

            case State::InString:
                result += c;
                if (c == '\\' && next != '\0') {
                    result += next;
                    i++; // Skip escaped char
                } else if (c == '"') {
                    state = State::Normal;
                }
                break;

            case State::InChar:
                result += c;
                if (c == '\\' && next != '\0') {
                    result += next;
                    i++; // Skip escaped char
                } else if (c == '\'') {
                    state = State::Normal;
                }
                break;
        }
    }

    return result;
}

std::vector<ScriptProperty> editor::ScriptParser::parseScriptProperties(const std::filesystem::path& scriptPath) {
    std::vector<ScriptProperty> properties;

    if (!std::filesystem::exists(scriptPath)) {
        return properties;
    }

    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        Out::error("Failed to open script file for parsing: %s", scriptPath.string().c_str());
        return properties;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    content = removeComments(content);

    // Updated pattern to capture optional type parameter and type annotation comment
    // Pattern: SPROPERTY("Display Name") or SPROPERTY("Display Name", Type) followed by Type varName = defaultValue;
    std::regex propertyRegex(
        "SPROPERTY\\s*\\(\\s*"                    // SPROPERTY(
        "\"([^\"]+)\"\\s*"                         // "Display Name"
        "(?:,\\s*([\\w]+))?\\s*"                   // optional , Type
        "\\)\\s*"                                  // )
        "(?:/\\*[^*]*@SPROPERTY_TYPE:\\s*([\\w]+)[^*]*\\*/\\s*)?" // optional /* @SPROPERTY_TYPE: Type */
        "([\\w:*]+(?:\\s*<[^>]+>)?)\\s+"          // C++ Type (with templates and pointers)
        "(\\w+)\\s*"                               // varName
        "(?:=\\s*([^;]+?))?\\s*;"                 // optional = defaultValue
    );

    std::sregex_iterator it(content.begin(), content.end(), propertyRegex);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        std::smatch match = *it;
        size_t position = match.position();

        std::string displayName = match[1].str();
        std::string explicitType = match[2].str();        // From SPROPERTY(..., Type)
        std::string typeAnnotation = match[3].str();      // From /* @SPROPERTY_TYPE: Type */
        std::string cppType = match[4].str();
        std::string varName = match[5].str();
        std::string defaultValueStr = match[6].str();     // May be empty

        // Remove whitespace from type (but keep * for pointers)
        std::string cleanCppType;
        for (char c : cppType) {
            if (!std::isspace(c)) {
                cleanCppType += c;
            }
        }
        cppType = cleanCppType;

        // Remove whitespace from default value if present
        if (!defaultValueStr.empty()) {
            defaultValueStr.erase(std::remove_if(defaultValueStr.begin(), defaultValueStr.end(), ::isspace), defaultValueStr.end());
        }

        // Determine the property type
        ScriptPropertyType type;
        std::string ptrTypeName;

        // Priority: explicit type parameter > type annotation > inferred from C++ type
        if (!explicitType.empty()) {
            type = parseExplicitType(explicitType, cppType);
        } else if (!typeAnnotation.empty()) {
            type = parseExplicitType(typeAnnotation, cppType);
        } else {
            type = inferTypeFromCppType(cppType, ptrTypeName);
        }

        ScriptProperty prop;
        prop.name = varName;
        prop.displayName = displayName;
        prop.type = type;
        prop.ptrTypeName = ptrTypeName; // Will be empty for non-pointer types

        // Parse and set default value based on type
        try {
            switch (type) {
                case ScriptPropertyType::Bool: {
                    bool val = false; // default value
                    if (!defaultValueStr.empty()) {
                        val = (defaultValueStr == "true" || defaultValueStr == "1");
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::Int: {
                    int val = 0; // default value
                    if (!defaultValueStr.empty()) {
                        val = std::stoi(defaultValueStr);
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::Float: {
                    float val = 0.0f; // default value
                    if (!defaultValueStr.empty()) {
                        // Handle 'f' suffix
                        std::string cleanFloat = defaultValueStr;
                        if (!cleanFloat.empty() && cleanFloat.back() == 'f') {
                            cleanFloat.pop_back();
                        }
                        val = std::stof(cleanFloat);
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::String: {
                    std::string val = ""; // default value
                    if (!defaultValueStr.empty()) {
                        // Remove quotes if present
                        val = defaultValueStr;
                        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                            val = val.substr(1, val.size() - 2);
                        }
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::Vector2: {
                    Vector2 val = Vector2(); // default value
                    if (!defaultValueStr.empty()) {
                        // Parse Vector2(x, y) or doriax::Vector2(x, y)
                        std::regex vecRegex("(?:doriax::)?Vector2\\(([^,]+),([^)]+)\\)");
                        std::smatch vecMatch;
                        if (std::regex_search(defaultValueStr, vecMatch, vecRegex)) {
                            std::string xStr = vecMatch[1].str();
                            std::string yStr = vecMatch[2].str();
                            // Remove 'f' suffix if present
                            if (!xStr.empty() && xStr.back() == 'f') xStr.pop_back();
                            if (!yStr.empty() && yStr.back() == 'f') yStr.pop_back();
                            float x = std::stof(xStr);
                            float y = std::stof(yStr);
                            val = Vector2(x, y);
                        }
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3: {
                    Vector3 val = Vector3(); // default value
                    if (!defaultValueStr.empty()) {
                        // Parse Vector3(x, y, z) or doriax::Vector3(x, y, z)
                        std::regex vecRegex("(?:doriax::)?Vector3\\(([^,]+),([^,]+),([^)]+)\\)");
                        std::smatch vecMatch;
                        if (std::regex_search(defaultValueStr, vecMatch, vecRegex)) {
                            std::string xStr = vecMatch[1].str();
                            std::string yStr = vecMatch[2].str();
                            std::string zStr = vecMatch[3].str();
                            // Remove 'f' suffix if present
                            if (!xStr.empty() && xStr.back() == 'f') xStr.pop_back();
                            if (!yStr.empty() && yStr.back() == 'f') yStr.pop_back();
                            if (!zStr.empty() && zStr.back() == 'f') zStr.pop_back();
                            float x = std::stof(xStr);
                            float y = std::stof(yStr);
                            float z = std::stof(zStr);
                            val = Vector3(x, y, z);
                        }
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4: {
                    Vector4 val = Vector4(); // default value
                    if (!defaultValueStr.empty()) {
                        // Parse Vector4(x, y, z, w) or doriax::Vector4(x, y, z, w)
                        std::regex vecRegex("(?:doriax::)?Vector4\\(([^,]+),([^,]+),([^,]+),([^)]+)\\)");
                        std::smatch vecMatch;
                        if (std::regex_search(defaultValueStr, vecMatch, vecRegex)) {
                            std::string xStr = vecMatch[1].str();
                            std::string yStr = vecMatch[2].str();
                            std::string zStr = vecMatch[3].str();
                            std::string wStr = vecMatch[4].str();
                            // Remove 'f' suffix if present
                            if (!xStr.empty() && xStr.back() == 'f') xStr.pop_back();
                            if (!yStr.empty() && yStr.back() == 'f') yStr.pop_back();
                            if (!zStr.empty() && zStr.back() == 'f') zStr.pop_back();
                            if (!wStr.empty() && wStr.back() == 'f') wStr.pop_back();
                            float x = std::stof(xStr);
                            float y = std::stof(yStr);
                            float z = std::stof(zStr);
                            float w = std::stof(wStr);
                            val = Vector4(x, y, z, w);
                        }
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }

                case ScriptPropertyType::EntityReference: {
                    // Pointers default to null entity
                    EntityReference val{NULL_ENTITY, 0};
                    if (!defaultValueStr.empty() && defaultValueStr != "nullptr" && defaultValueStr != "NULL") {
                        Out::warning("Non-null entity pointer default values are not supported, using null entity for '%s'", varName.c_str());
                    }
                    prop.value = val;
                    prop.defaultValue = val;
                    break;
                }
            }
        } catch (const std::exception& e) {
            size_t lineNum = std::count(content.begin(), content.begin() + position, '\n') + 1;
            Out::warning("Failed to parse property at line %zu: %s", lineNum, e.what());
            // Initialize with default values on parse error
            switch (type) {
                case ScriptPropertyType::Bool:
                    prop.value = false;
                    prop.defaultValue = false;
                    break;
                case ScriptPropertyType::Int:
                    prop.value = 0;
                    prop.defaultValue = 0;
                    break;
                case ScriptPropertyType::Float:
                    prop.value = 0.0f;
                    prop.defaultValue = 0.0f;
                    break;
                case ScriptPropertyType::String:
                    prop.value = std::string("");
                    prop.defaultValue = std::string("");
                    break;
                case ScriptPropertyType::Vector2:
                    prop.value = Vector2();
                    prop.defaultValue = Vector2();
                    break;
                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3:
                    prop.value = Vector3();
                    prop.defaultValue = Vector3();
                    break;
                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4:
                    prop.value = Vector4();
                    prop.defaultValue = Vector4();
                    break;
                case ScriptPropertyType::EntityReference:
                    prop.value = EntityReference{NULL_ENTITY, 0};
                    prop.defaultValue = EntityReference{NULL_ENTITY, 0};
                    break;
            }
        }

        properties.push_back(prop);
    }

    std::unordered_set<std::string> seenNames;
    for (const auto& prop : properties) {
        if (!seenNames.insert(prop.name).second) {
            Out::warning("Duplicate property name '%s' in %s", 
                         prop.name.c_str(), scriptPath.string().c_str());
        }
    }

    return properties;
}