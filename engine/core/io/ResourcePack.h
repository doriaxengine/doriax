// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#ifndef RESOURCEPACK_H
#define RESOURCEPACK_H

#include "Export.h"

#include <string>
#include <vector>

namespace doriax {

    class DORIAX_API ResourcePack {
    public:
        static bool read(const std::string& path, std::vector<unsigned char>& outData);
        static void reset();
    };

}

#endif
