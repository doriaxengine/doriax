// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include "yaml-cpp/yaml.h"

namespace doriax::editor {
    // Windows text from older editors and console programs is in the legacy code pages.
    // Elsewhere these return the text unchanged.
    class TextEncoding {
    public:
        static std::string legacyToUtf8(const std::string& text);
        static std::string consoleToUtf8(const std::string& text);

        // Also converts the strings older editors wrote in the ANSI code page
        static YAML::Node loadYamlFile(const std::filesystem::path& path);
    };
}
