// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AiPathUtils.h"

#include <cctype>

namespace fs = std::filesystem;

namespace doriax::editor::ai {

bool PathUtils::isSafeRelativePath(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path.lexically_normal()) {
        const std::string value = part.string();
        if (value.empty() || value == "." || value == "..") {
            return false;
        }
        for (unsigned char c : value) {
            if (c < 32) {
                return false;
            }
        }
    }
    return true;
}

bool PathUtils::existsExactCase(const fs::path& directory, const fs::path& relative) {
    fs::path current = directory;
    for (const auto& part : relative.lexically_normal()) {
        if (part.empty()) {
            continue;
        }
        bool found = false;
        std::error_code ec;
        for (fs::directory_iterator it(current, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().filename() == part) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
        current /= part;
    }
    return true;
}

std::string PathUtils::slugify(const std::string& value, const std::string& fallback) {
    std::string out;
    out.reserve(value.size());
    bool lastDash = false;
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            lastDash = false;
        } else if (!lastDash && !out.empty()) {
            out.push_back('-');
            lastDash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    if (out.empty()) {
        out = fallback;
    }
    return out;
}

fs::path PathUtils::uniqueChildPath(const fs::path& directory,
                                    const std::string& stem,
                                    const std::string& extension) {
    fs::path candidate = directory / (stem + extension);
    if (!fs::exists(candidate)) {
        return candidate;
    }

    for (int i = 2; i < 10000; ++i) {
        candidate = directory / (stem + "-" + std::to_string(i) + extension);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
    return directory / (stem + "-copy" + extension);
}

} // namespace doriax::editor::ai
