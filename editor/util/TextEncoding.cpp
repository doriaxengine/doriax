// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TextEncoding.h"
#include "FileUtils.h"

#include <fstream>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

using namespace doriax::editor;

#ifdef _WIN32
namespace {

    // The manifest makes GetACP() and GetOEMCP() return UTF-8, the system locale keeps the old ones
    UINT systemCodePage(LCTYPE type) {
        DWORD codePage = 0;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, type | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&codePage), sizeof(codePage) / sizeof(WCHAR));
        return codePage ? codePage : 1252;
    }

    std::string toUtf8(const std::string& text, UINT codePage) {
        if (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0) {
            return text;
        }
        const int wideSize = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(wideSize, L'\0');
        MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideSize);
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, utf8.data(), size, nullptr, nullptr);
        return utf8;
    }

    void convertLegacyStrings(YAML::Node node) {
        if (node.IsScalar()) {
            const std::string utf8 = TextEncoding::legacyToUtf8(node.Scalar());
            if (utf8 != node.Scalar()) {
                node = utf8;
            }
        } else if (node.IsSequence()) {
            for (YAML::Node child : node) {
                convertLegacyStrings(child);
            }
        } else if (node.IsMap()) {
            for (auto it = node.begin(); it != node.end(); ++it) {
                convertLegacyStrings(it->first);
                convertLegacyStrings(it->second);
            }
        }
    }

}
#endif

std::string TextEncoding::legacyToUtf8(const std::string& text) {
#ifdef _WIN32
    static const UINT codePage = systemCodePage(LOCALE_IDEFAULTANSICODEPAGE);
    return toUtf8(text, codePage);
#else
    return text;
#endif
}

std::string TextEncoding::consoleToUtf8(const std::string& text) {
#ifdef _WIN32
    static const UINT codePage = systemCodePage(LOCALE_IDEFAULTCODEPAGE);
    return toUtf8(text, codePage);
#else
    return text;
#endif
}

YAML::Node TextEncoding::loadYamlFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw YAML::BadFile(FileUtils::pathToUtf8(path));
    }
    YAML::Node root = YAML::Load(file);
#ifdef _WIN32
    // before Windows 10 1903 the process is still in the ANSI code page
    if (GetACP() == CP_UTF8) {
        convertLegacyStrings(root);
    }
#endif
    return root;
}
