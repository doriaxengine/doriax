#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <iterator>

namespace doriax::editor {

class FileUtils {
public:
    // Returns true if the file was written/updated.
    // Returns false if the file was unchanged or if an error occurred.
    static bool writeIfChanged(const std::filesystem::path& filePath, const std::string& newContent) {
        std::string currentContent;
        bool shouldWrite = true;

        std::error_code ec;
        if (std::filesystem::exists(filePath, ec) && !ec) {
            std::ifstream ifs(filePath, std::ios::in | std::ios::binary);
            if (ifs) {
                currentContent.assign(
                    (std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>()
                );
                shouldWrite = (currentContent != newContent);
            }
        }

        if (!shouldWrite) {
            return false;
        }

        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path(), ec);
            if (ec) {
                return false;
            }
        }

        std::ofstream ofs(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return false;
        }
        ofs.write(newContent.data(), static_cast<std::streamsize>(newContent.size()));
        return static_cast<bool>(ofs);
    }
};

} // namespace doriax::editor
