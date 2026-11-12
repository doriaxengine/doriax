// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include "Backend.h"
#include "imgui.h"

namespace doriax::editor{
    class Util{

    public:

        inline static std::string getImageExtensions() {
             return "png,jpg,jpeg,bmp,tga,gif,hdr,psd,pic,pnm,svg";
        }

        inline static std::string getFontExtensions() {
             return "ttf,otf";
        }

        inline static std::string getSceneExtensions() {
             return "scene";
        }

        inline static std::string getMaterialExtensions() {
             return "material";
        }

        inline static std::string getModelExtensions() {
             return "gltf,glb,obj,fbx";
        }

        inline static std::string getBundleExtensions() {
             return "bundle";
        }

        inline static std::string getAudioExtensions() {
             return "wav,ogg,mp3,flac";
        }

        inline static std::string getShaderExtensions() {
             return "glsl,shader";
        }

        inline static std::string getScriptExtensions() {
             return "lua,cpp,cc,cxx,h,hh,hpp,hxx";
        }

        inline static bool isImageFile(const std::string& path) {
            static const std::unordered_set<std::string> imageExtensions = {
                ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr", ".psd", ".pic", ".pnm", ".svg"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return imageExtensions.find(ext) != imageExtensions.end();
        }

        inline static bool isFontFile(const std::string& path) {
             static const std::unordered_set<std::string> fontExtensions = {
                ".ttf", ".otf"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return fontExtensions.find(ext) != fontExtensions.end();
        }

        inline static bool isSceneFile(const std::string& path) {
             static const std::unordered_set<std::string> sceneExtensions = {
                ".scene"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return sceneExtensions.find(ext) != sceneExtensions.end();
        }

        inline static bool isMaterialFile(const std::string& path) {
             static const std::unordered_set<std::string> materialExtensions = {
                ".material"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return materialExtensions.find(ext) != materialExtensions.end();
        }

        inline static bool isShaderFile(const std::string& path) {
             static const std::unordered_set<std::string> shaderExtensions = {
                ".glsl", ".shader"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return shaderExtensions.find(ext) != shaderExtensions.end();
        }

        // True for a relative path that cannot escape the directory it is resolved
        // against. Guards every project-relative path that reaches the filesystem.
        inline static bool isSafeRelativePath(const std::filesystem::path& path) {
            if (path.empty() || path.is_absolute())
                return false;

            std::filesystem::path normalized = path.lexically_normal();
            return !normalized.empty() && *normalized.begin() != "..";
        }

        // True when path is root itself or sits below it. Lexical: it runs per folder
        // per frame in the creation dialogs.
        inline static bool isInsidePath(const std::filesystem::path& path, const std::filesystem::path& root) {
            std::filesystem::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
            return !relative.empty() && *relative.begin() != "..";
        }

        struct ShaderInclude {
            std::string key;
            size_t position = 0; // of the key in the source
        };

        // Every '#include "..."' in a GLSL source, in order.
        inline static std::vector<ShaderInclude> getShaderIncludes(const std::string& source) {
            auto skipBlanks = [](std::string_view line, size_t i) {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
                return i;
            };

            std::vector<ShaderInclude> includes;
            for (size_t lineStart = 0; lineStart <= source.size(); ) {
                size_t lineEnd = source.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = source.size();

                const size_t lineOffset = lineStart;
                std::string_view line(source.data() + lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 1;

                size_t i = skipBlanks(line, 0);
                if (i >= line.size() || line[i] != '#') continue;
                i = skipBlanks(line, i + 1);
                if (i + 7 > line.size() || line.compare(i, 7, "include") != 0) continue;
                i = skipBlanks(line, i + 7);
                if (i >= line.size() || line[i] != '"') continue;

                size_t close = line.find('"', ++i);
                if (close != std::string_view::npos && close > i)
                    includes.push_back({std::string(line.substr(i, close - i)), lineOffset + i});
            }
            return includes;
        }

        // The paths quoted by every '#include "..."' in a GLSL source, in order.
        inline static std::vector<std::string> getShaderIncludeKeys(const std::string& source) {
            std::vector<std::string> keys;
            for (ShaderInclude& include : getShaderIncludes(source))
                keys.push_back(std::move(include.key));
            return keys;
        }

        inline static bool isBundleFile(const std::string& path) {
             static const std::unordered_set<std::string> bundleExtensions = {
                ".bundle"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return bundleExtensions.find(ext) != bundleExtensions.end();
        }

        inline static bool isModelFile(const std::string& path) {
             static const std::unordered_set<std::string> modelExtensions = {
                ".gltf", ".glb", ".obj", ".fbx"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return modelExtensions.find(ext) != modelExtensions.end();
        }

        inline static bool isAudioFile(const std::string& path) {
             static const std::unordered_set<std::string> audioExtensions = {
                ".wav", ".ogg", ".mp3", ".flac"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return audioExtensions.find(ext) != audioExtensions.end();
        }

        // Files referenced relative to the assets directory
        inline static bool isAssetFile(const std::string& path) {
            return isImageFile(path) || isModelFile(path) || isFontFile(path) || isAudioFile(path);
        }

        inline static bool isScriptFile(const std::string& path) {
             static const std::unordered_set<std::string> scriptExtensions = {
                ".lua", ".cpp", ".cc", ".cxx", ".h", ".hh", ".hpp", ".hxx"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return scriptExtensions.find(ext) != scriptExtensions.end();
        }

        inline static bool isLuaFile(const std::string& path) {
             static const std::unordered_set<std::string> luaExtensions = {
                ".lua"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return luaExtensions.find(ext) != luaExtensions.end();
        }

        inline static bool isHeaderFile(const std::string& path) {
             static const std::unordered_set<std::string> headerExtensions = {
                ".h", ".hh", ".hpp", ".hxx"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return headerExtensions.find(ext) != headerExtensions.end();
        }

        inline static bool isSourceFile(const std::string& path) {
             static const std::unordered_set<std::string> sourceExtensions = {
                ".cpp", ".cc", ".cxx"
            };

            std::string ext = std::filesystem::path(path).extension().string();
            if (ext.empty() && !path.empty() && path[0] == '.') {
                ext = path;
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return sourceExtensions.find(ext) != sourceExtensions.end();
        }

        // String-list drag payloads ("external_files", "resource_files") are
        // strings packed back-to-back, each terminated by '\0'. Encoder and
        // decoder live together so every producer/consumer shares one format.
        inline static std::vector<char> encodeStringsPayload(const std::vector<std::string>& strings){
            std::vector<char> payload;
            for (const std::string& value : strings) {
                payload.insert(payload.end(), value.begin(), value.end());
                payload.push_back('\0');
            }
            return payload;
        }

        inline static std::vector<std::string> getStringsFromPayload(const ImGuiPayload* payload){
            std::vector<std::string> receivedStrings;
            if (!payload || !payload->Data || payload->DataSize <= 0) {
                return receivedStrings;
            }

            const char* current = static_cast<const char*>(payload->Data);
            const char* end = current + payload->DataSize;
            while (current < end) {
                // Bounded scan: never run past DataSize even if the final
                // string is missing its terminator.
                const void* terminator = std::memchr(current, '\0', static_cast<size_t>(end - current));
                const char* valueEnd = terminator ? static_cast<const char*>(terminator) : end;
                if (valueEnd != current) {
                    receivedStrings.emplace_back(current, valueEnd);
                }
                current = valueEnd + 1;
            }

            return receivedStrings;
        }
    };
}
