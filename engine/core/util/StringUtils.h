//
// (c) 2026 Eduardo Doria.
//

#ifndef StringUtils_h
#define StringUtils_h

#include "Export.h"
#include <cstdint>
#include <string>
#include <vector>

namespace doriax{
    class DORIAX_API StringUtils{
    public:
        static std::string toUTF8(wchar_t codepoint);

        // Decodes UTF-8 into Unicode codepoints. Invalid sequences are replaced with U+FFFD and hadInvalid is set.
        static std::vector<uint32_t> decodeUtf8ToCodepoints(const std::string& text, bool& hadInvalid);

        // Converts UTF-8 to std::wstring (UTF-16 on Windows; UTF-32 elsewhere). Invalid sequences become U+FFFD.
        static std::wstring utf8ToWString(const std::string& text, bool& hadInvalid);

        // Removes the last UTF-8 encoded Unicode scalar (safe for multi-byte characters).
        static void eraseLastCodepointUtf8(std::string& text);
    };
}

#endif /* StringUtils_h */
