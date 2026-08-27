// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "ResourcePack.h"

#include "System.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#ifndef _WIN32
#include <sys/types.h>
#endif

using namespace doriax;

namespace {

    constexpr char PACK_MAGIC[] = {'D', 'X', 'P', 'K', '1'};

    struct PackEntry {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint8_t key = 0;
        uint32_t shift = 0;
    };

    struct PackState {
        bool triedLoad = false;
        bool loaded = false;
        std::string filename;
        std::map<std::string, PackEntry> entries;
    };

    PackState& state() {
        static PackState packState;
        return packState;
    }

    std::string normalizeSeparators(std::string path) {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    std::string simplifyPath(std::string path) {
        path = normalizeSeparators(path);
        while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        }
        return path;
    }

    bool startsWith(const std::string& value, const std::string& prefix) {
        return value.rfind(prefix, 0) == 0;
    }

    std::string logicalPackPath(std::string path) {
        path = normalizeSeparators(path);

        if (startsWith(path, "asset://")) {
            return "assets/" + simplifyPath(path.substr(8));
        }
        if (startsWith(path, "lua://")) {
            return "lua/" + simplifyPath(path.substr(6));
        }
        if (startsWith(path, "shader://")) {
            return "assets/shaders/" + simplifyPath(path.substr(9));
        }
        if (startsWith(path, "data://")) {
            return "";
        }
        if (startsWith(path, "/") || (path.size() > 1 && path[1] == ':')) {
            return "";
        }

        return "assets/" + simplifyPath(path);
    }

    bool readExact(FILE* file, void* dst, size_t size) {
        return std::fread(dst, 1, size, file) == size;
    }

    bool readU8(FILE* file, uint8_t& value) {
        return readExact(file, &value, 1);
    }

    bool readU16(FILE* file, uint16_t& value) {
        unsigned char bytes[2];
        if (!readExact(file, bytes, sizeof(bytes))) return false;
        value = static_cast<uint16_t>(bytes[0])
              | (static_cast<uint16_t>(bytes[1]) << 8);
        return true;
    }

    bool readU32(FILE* file, uint32_t& value) {
        unsigned char bytes[4];
        if (!readExact(file, bytes, sizeof(bytes))) return false;
        value = static_cast<uint32_t>(bytes[0])
              | (static_cast<uint32_t>(bytes[1]) << 8)
              | (static_cast<uint32_t>(bytes[2]) << 16)
              | (static_cast<uint32_t>(bytes[3]) << 24);
        return true;
    }

    bool readU64(FILE* file, uint64_t& value) {
        unsigned char bytes[8];
        if (!readExact(file, bytes, sizeof(bytes))) return false;
        value = 0;
        for (int i = 0; i < 8; i++) {
            value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
        }
        return true;
    }

    bool seekFile(FILE* file, uint64_t offset) {
#ifdef _WIN32
        return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
        return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
    }

    bool tryLoadPack(const std::string& filename) {
        FILE* file = System::instance().platformFopen(filename.c_str(), "rb");
        if (!file) return false;

        char magic[sizeof(PACK_MAGIC)];
        uint32_t fileCount = 0;
        bool ok = readExact(file, magic, sizeof(magic))
            && std::equal(std::begin(PACK_MAGIC), std::end(PACK_MAGIC), magic)
            && readU32(file, fileCount);

        if (!ok) {
            std::fclose(file);
            return false;
        }

        auto& pack = state();
        pack.entries.clear();
        pack.filename = filename;

        for (uint32_t i = 0; i < fileCount; i++) {
            uint16_t pathLength = 0;
            if (!readU16(file, pathLength) || pathLength == 0) {
                ok = false;
                break;
            }

            std::string path(pathLength, '\0');
            if (!readExact(file, path.data(), pathLength)) {
                ok = false;
                break;
            }

            PackEntry entry;
            if (!readU64(file, entry.offset)
                || !readU64(file, entry.size)
                || !readU8(file, entry.key)
                || !readU32(file, entry.shift)) {
                ok = false;
                break;
            }

            pack.entries[simplifyPath(path)] = entry;
        }

        std::fclose(file);

        if (!ok) {
            pack.entries.clear();
            pack.filename.clear();
            return false;
        }

        pack.loaded = true;
        return true;
    }

    void ensureLoaded() {
        auto& pack = state();
        if (pack.triedLoad) return;

        pack.triedLoad = true;
        pack.loaded = false;

        std::vector<std::string> candidates = {
            "game.pak",
            "assets/game.pak"
        };

        std::string assetPath = simplifyPath(System::instance().getAssetPath());
        if (!assetPath.empty()) {
            candidates.push_back(assetPath + "/game.pak");
        }

        for (const std::string& candidate : candidates) {
            if (tryLoadPack(candidate)) return;
        }
    }

    void undoXorAndShift(std::vector<unsigned char>& data, uint8_t key, uint32_t shift) {
        if (data.empty()) return;

        for (unsigned char& byte : data) {
            byte ^= key;
        }

        uint32_t normalizedShift = shift % static_cast<uint32_t>(data.size());
        if (normalizedShift == 0) return;

        std::rotate(data.begin(), data.begin() + normalizedShift, data.end());
    }

}

bool ResourcePack::read(const std::string& path, std::vector<unsigned char>& outData) {
    ensureLoaded();

    auto& pack = state();
    if (!pack.loaded) return false;

    const std::string normalizedPath = logicalPackPath(path);
    if (normalizedPath.empty()) return false;

    auto it = pack.entries.find(normalizedPath);
    if (it == pack.entries.end()) return false;

    FILE* file = System::instance().platformFopen(pack.filename.c_str(), "rb");
    if (!file) return false;

    const PackEntry& entry = it->second;
    if (!seekFile(file, entry.offset)) {
        std::fclose(file);
        return false;
    }

    outData.resize(static_cast<size_t>(entry.size));
    const bool ok = entry.size == 0 || readExact(file, outData.data(), static_cast<size_t>(entry.size));
    std::fclose(file);

    if (!ok) {
        outData.clear();
        return false;
    }

    undoXorAndShift(outData, entry.key, entry.shift);
    return true;
}

void ResourcePack::reset() {
    auto& pack = state();
    pack = PackState();
}
