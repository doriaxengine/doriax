// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderHeaderBuilder.h"
#include "FileUtils.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

using namespace doriax::editor;

namespace {
    // Shader names carry variant codes that may contain characters which are not
    // valid in a C++ identifier (for example '?', emitted for an unknown/reserved
    // property bit). The name is still used verbatim as the runtime lookup key, so
    // only the emitted variable name is sanitized to a valid identifier here.
    std::string toIdentifier(const std::string& name) {
        std::string id;
        id.reserve(name.size());
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                id += c;
            } else {
                id += '_';
            }
        }
        if (id.empty() || std::isdigit(static_cast<unsigned char>(id[0]))) {
            id.insert(id.begin(), '_');
        }
        return id;
    }
}

std::string ShaderHeaderBuilder::buildShaderHeader(const std::string& suffix, std::vector<HeaderShader> shaders) {
    std::sort(shaders.begin(), shaders.end(), [](const HeaderShader& a, const HeaderShader& b) {
        return a.name < b.name;
    });

    // Precompute a valid, unique C++ identifier for each shader. The original
    // name stays as the string lookup key in getBase64Shader() so it keeps
    // matching ShaderPool::getShaderName() at runtime.
    std::vector<std::string> varNames;
    varNames.reserve(shaders.size());
    std::unordered_set<std::string> usedNames;
    for (const auto& shader : shaders) {
        std::string base = toIdentifier(shader.name);
        std::string unique = base;
        for (int n = 1; !usedNames.insert(unique).second; ++n) {
            unique = base + "_" + std::to_string(n);
        }
        varNames.push_back(unique);
    }

    std::string content;
    content += "#ifndef SHADER_" + suffix + "_h\n";
    content += "#define SHADER_" + suffix + "_h\n\n";
    content += "#include <string>\n\n";

    constexpr size_t chunkSize = 100;
    for (size_t i = 0; i < shaders.size(); ++i) {
        const auto& shader = shaders[i];
        content += "static const std::string " + varNames[i] + " = ";
        if (shader.encodedData.empty()) {
            content += "\"\"";
        } else {
            for (size_t offset = 0; offset < shader.encodedData.size(); offset += chunkSize) {
                content += "\n    \"" + shader.encodedData.substr(offset, chunkSize) + "\"";
            }
        }
        content += ";\n\n";
    }

    // MSVC fails on else-if chains longer than 128 (C1061)
    content += "std::string getBase64Shader(std::string name) {\n";
    for (size_t i = 0; i < shaders.size(); ++i) {
        content += "    if (name == \"" + shaders[i].name + "\") return " + varNames[i] + ";\n";
    }
    content += "    return \"\";\n}\n\n";
    content += "#endif //SHADER_" + suffix + "_h\n";

    return content;
}

bool ShaderHeaderBuilder::writeShaderHeader(const std::filesystem::path& outputPath, const std::string& suffix, const std::vector<HeaderShader>& shaders, std::string& err) {
    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        err = "Failed to create shader header directory: " + ec.message();
        return false;
    }

    FileUtils::writeIfChanged(outputPath, buildShaderHeader(suffix, shaders));
    return true;
}
