#pragma once

#include <vector>
#include <string>
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
             return "png,jpg,jpeg,bmp,tga,gif,hdr,psd,pic,pnm";
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

        inline static std::string getEntityExtensions() {
             return "entity";
        }

        inline static std::string getModelExtensions() {
             return "gltf,glb,obj";
        }

        inline static std::string getAudioExtensions() {
             return "wav,ogg,mp3,flac";
        }

        inline static bool isImageFile(const std::string& path) {
            static const std::unordered_set<std::string> imageExtensions = {
                ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr", ".psd", ".pic", ".pnm"
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
                ".gltf", ".glb", ".obj"
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

        inline static std::vector<std::string> getStringsFromPayload(const ImGuiPayload* payload){
            const char* data = static_cast<const char*>(payload->Data);
            size_t dataSize = payload->DataSize;

            std::vector<std::string> receivedStrings;
            size_t offset = 0;

            while (offset < dataSize) {
                // Read null-terminated strings from the payload
                const char* str = &data[offset];
                receivedStrings.push_back(std::string(str));
                offset += strlen(str) + 1; // Move past the string and null terminator
            }

            return receivedStrings;
        }
    };
}