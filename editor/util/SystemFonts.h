// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

namespace doriax::editor {
    class SystemFonts {
    public:
        struct Font {
            void* data = nullptr;
            int size = 0;
            int faceIndex = 0;
        };

        // CJK fonts of the system, the user's language first. They stay memory-mapped,
        // so ImGui must not own the data.
        static const std::vector<Font>& cjkFonts();
    };
}
