// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SystemFonts.h"

#include <climits>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #ifndef __APPLE__
        #include "ShellEnv.h"
    #endif
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

using namespace doriax::editor;

namespace {

    // Han characters are drawn differently per language
    enum class CjkLanguage { SimplifiedChinese, TraditionalChinese, Japanese, Korean };

    struct FontFile {
        std::string path;
        int faceIndex = 0;
    };

    CjkLanguage userLanguage() {
        std::string locale;
#ifdef _WIN32
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
        for (const wchar_t* c = name; *c; c++) {
            locale += static_cast<char>(*c);
        }
#else
        for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char* value = std::getenv(variable);
            if (value && *value) {
                locale = value;
                break;
            }
        }
#endif
        if (locale.rfind("ja", 0) == 0) return CjkLanguage::Japanese;
        if (locale.rfind("ko", 0) == 0) return CjkLanguage::Korean;
        if (locale.rfind("zh", 0) == 0 &&
            (locale.find("TW") != std::string::npos || locale.find("HK") != std::string::npos || locale.find("Hant") != std::string::npos)) {
            return CjkLanguage::TraditionalChinese;
        }
        return CjkLanguage::SimplifiedChinese;
    }

#if defined(_WIN32) || defined(__APPLE__)
    std::vector<FontFile> findFonts(CjkLanguage language) {
#ifdef _WIN32
        const char* windows = std::getenv("WINDIR");
        const std::string directory = std::string(windows ? windows : "C:\\Windows") + "\\Fonts\\";
        std::vector<const char*> files;
        switch (language) {
            case CjkLanguage::Japanese:           files = {"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"}; break;
            case CjkLanguage::TraditionalChinese: files = {"msjh.ttc", "mingliub.ttc"}; break;
            case CjkLanguage::Korean:             files = {"malgun.ttf", "gulim.ttc"}; break;
            default:                              files = {"msyh.ttc", "simsun.ttc"}; break;
        }
#else
        const std::string directory = "/System/Library/Fonts/";
        std::vector<const char*> files;
        switch (language) {
            case CjkLanguage::Japanese: files = {"ヒラギノ角ゴシック W3.ttc", "Hiragino Sans GB.ttc"}; break;
            case CjkLanguage::Korean:   files = {"AppleSDGothicNeo.ttc"}; break;
            default:                    files = {"Hiragino Sans GB.ttc", "STHeiti Light.ttc", "PingFang.ttc"}; break;
        }
#endif
        std::vector<FontFile> fonts;
        for (const char* file : files) {
            std::error_code ec;
            if (std::filesystem::exists(directory + file, ec)) {
                fonts.push_back({directory + file, 0});
            }
        }
        return fonts;
    }
#else
    std::vector<FontFile> findFonts(CjkLanguage language) {
        const char* lang = "zh-cn";
        if (language == CjkLanguage::Japanese) lang = "ja";
        if (language == CjkLanguage::Korean) lang = "ko";
        if (language == CjkLanguage::TraditionalChinese) lang = "zh-tw";

        // fontconfig's fonts for the language, best first, with the languages each covers
        std::istringstream lines(ShellEnv::runCapture(
            std::string("fc-match -s -f '%{file}\\t%{index}\\t%{lang}\\n' 'sans-serif:lang=") + lang + "'", 3000));
        std::vector<FontFile> fonts;
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream fields(line);
            std::string path, index, languages;
            std::getline(fields, path, '\t');
            std::getline(fields, index, '\t');
            std::getline(fields, languages);

            // the upper 16 bits name a variable font instance, which stb_truetype can't select
            const long faceIndex = std::atol(index.c_str());
            const std::string extension = std::filesystem::path(path).extension().string();
            if ((faceIndex >> 16) == 0 && (extension == ".ttf" || extension == ".ttc" || extension == ".otf") &&
                ("|" + languages + "|").find(std::string("|") + lang + "|") != std::string::npos) {
                fonts.push_back({path, static_cast<int>(faceIndex)});
            }
        }
        return fonts;
    }
#endif

    // The checks ImGui's stb_truetype loader makes, which assert on a face it can't read
    bool isLoadable(const SystemFonts::Font& font) {
        const unsigned char* data = static_cast<const unsigned char*>(font.data);
        const int offset = stbtt_GetFontOffsetForIndex(data, font.faceIndex);
        stbtt_fontinfo info;
        return offset >= 0 && stbtt_InitFont(&info, data, offset);
    }

    SystemFonts::Font mapFont(const FontFile& file) {
        SystemFonts::Font font;
        font.faceIndex = file.faceIndex;
#ifdef _WIN32
        HANDLE handle = CreateFileA(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return font;
        }
        LARGE_INTEGER size{};
        HANDLE mapping = nullptr;
        if (GetFileSizeEx(handle, &size) && size.QuadPart <= INT_MAX) {
            mapping = CreateFileMappingA(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        }
        CloseHandle(handle);
        if (mapping) {
            font.data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            font.size = static_cast<int>(size.QuadPart);
            CloseHandle(mapping);
        }
#else
        const int handle = open(file.path.c_str(), O_RDONLY);
        if (handle < 0) {
            return font;
        }
        struct stat info{};
        if (fstat(handle, &info) == 0 && info.st_size <= INT_MAX) {
            void* data = mmap(nullptr, info.st_size, PROT_READ, MAP_PRIVATE, handle, 0);
            if (data != MAP_FAILED) {
                font.data = data;
                font.size = static_cast<int>(info.st_size);
            }
        }
        close(handle);
#endif
        return font;
    }

    void unmapFont(const SystemFonts::Font& font) {
        if (!font.data) {
            return;
        }
#ifdef _WIN32
        UnmapViewOfFile(font.data);
#else
        munmap(font.data, font.size);
#endif
    }

}

const std::vector<SystemFonts::Font>& SystemFonts::cjkFonts() {
    static const std::vector<Font> fonts = []() {
        // Simplified Chinese for the Han characters a Japanese font lacks, Korean for Hangul
        const CjkLanguage languages[] = {userLanguage(), CjkLanguage::SimplifiedChinese, CjkLanguage::Korean};

        std::vector<Font> found;
        std::set<std::string> merged;
        for (CjkLanguage language : languages) {
            for (const FontFile& file : findFonts(language)) {
                // a font merged for an earlier language covers this one too
                if (merged.count(file.path)) {
                    break;
                }
                const Font font = mapFont(file);
                if (font.data && isLoadable(font)) {
                    merged.insert(file.path);
                    found.push_back(font);
                    break;
                }
                unmapFont(font);
            }
        }
        return found;
    }();
    return fonts;
}
