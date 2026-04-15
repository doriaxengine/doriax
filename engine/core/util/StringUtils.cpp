//
// (c) 2026 Eduardo Doria.
//

#include "StringUtils.h"

#include <cstdint>
#include <limits>

using namespace doriax;

std::string StringUtils::toUTF8(wchar_t codepoint){
    uint32_t cp = static_cast<uint32_t>(codepoint);

    // Replace invalid Unicode scalar values.
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        cp = 0xFFFDu;
    }

    std::string out;
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    return out;
}

std::vector<uint32_t> StringUtils::decodeUtf8ToCodepoints(const std::string& text, bool& hadInvalid) {
    hadInvalid = false;

    std::vector<uint32_t> out;
    out.reserve(text.size());

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        unsigned char b0 = bytes[i];

        if (b0 <= 0x7F) {
            cp = b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= text.size()) {
                hadInvalid = true;
                cp = 0xFFFD;
                i += 1;
            } else {
                unsigned char b1 = bytes[i + 1];
                uint32_t v = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
                if ((b1 & 0xC0) != 0x80 || v < 0x80) {
                    hadInvalid = true;
                    cp = 0xFFFD;
                    i += 1;
                } else {
                    cp = v;
                    i += 2;
                }
            }
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= text.size()) {
                hadInvalid = true;
                cp = 0xFFFD;
                i += 1;
            } else {
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                uint32_t v = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
                if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) {
                    hadInvalid = true;
                    cp = 0xFFFD;
                    i += 1;
                } else {
                    cp = v;
                    i += 3;
                }
            }
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= text.size()) {
                hadInvalid = true;
                cp = 0xFFFD;
                i += 1;
            } else {
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                unsigned char b3 = bytes[i + 3];
                uint32_t v = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) |
                             ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
                if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || v < 0x10000 || v > 0x10FFFF) {
                    hadInvalid = true;
                    cp = 0xFFFD;
                    i += 1;
                } else {
                    cp = v;
                    i += 4;
                }
            }
        } else {
            hadInvalid = true;
            cp = 0xFFFD;
            i += 1;
        }

        out.push_back(cp);
    }

    return out;
}

std::wstring StringUtils::utf8ToWString(const std::string& text, bool& hadInvalid) {
    std::vector<uint32_t> cps = decodeUtf8ToCodepoints(text, hadInvalid);

    std::wstring out;
    out.reserve(cps.size());

    if constexpr (sizeof(wchar_t) == 2) {
        for (uint32_t cp : cps) {
            if (cp <= 0xFFFFu) {
                out.push_back(static_cast<wchar_t>(cp));
            } else {
                cp -= 0x10000u;
                wchar_t high = static_cast<wchar_t>(0xD800u + (cp >> 10));
                wchar_t low = static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu));
                out.push_back(high);
                out.push_back(low);
            }
        }
    } else {
        for (uint32_t cp : cps) {
            if (cp > static_cast<uint32_t>(std::numeric_limits<wchar_t>::max())) {
                out.push_back(static_cast<wchar_t>(0xFFFDu));
                hadInvalid = true;
            } else {
                out.push_back(static_cast<wchar_t>(cp));
            }
        }
    }

    return out;
}

void StringUtils::eraseLastCodepointUtf8(std::string& text) {
    if (text.empty()) {
        return;
    }

    // Remove trailing UTF-8 continuation bytes (10xxxxxx)
    size_t i = text.size();
    while (i > 0) {
        unsigned char c = static_cast<unsigned char>(text[i - 1]);
        if ((c & 0xC0) != 0x80) {
            break;
        }
        --i;
    }

    // If we ended up at 0, the string was all continuation bytes; clear it.
    if (i == 0) {
        text.clear();
        return;
    }

    text.erase(i - 1);
}
