// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
#endif

namespace doriax::editor {

class FileUtils {
public:
    static std::filesystem::path getExecutableDir() {
        namespace fs = std::filesystem;

        fs::path executablePath;

#ifdef _WIN32
        std::vector<wchar_t> buffer(MAX_PATH);
        while (true) {
            const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (size == 0) {
                return {};
            }
            if (size < buffer.size()) {
                executablePath = std::wstring(buffer.data(), size);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
#elif defined(__APPLE__)
        std::vector<char> buffer(1024);
        while (true) {
            uint32_t size = static_cast<uint32_t>(buffer.size());
            if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
                executablePath = std::string(buffer.data());
                break;
            }
            buffer.resize(size);
        }
#elif defined(__FreeBSD__)
        const int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
        size_t size = 0;
        if (::sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size == 0) {
            return {};
        }

        std::vector<char> buffer(size);
        if (::sysctl(mib, 4, buffer.data(), &size, nullptr, 0) != 0 || size == 0) {
            return {};
        }

        const size_t pathLength = buffer[size - 1] == '\0' ? size - 1 : size;
        if (pathLength == 0) {
            return {};
        }
        executablePath = std::string(buffer.data(), pathLength);
#elif defined(__linux__)
        std::error_code readLinkError;
        executablePath = fs::read_symlink("/proc/self/exe", readLinkError);
        if (readLinkError) {
            return {};
        }
#else
        return {};
#endif

        std::error_code ec;
        fs::path normalizedPath = fs::weakly_canonical(executablePath, ec);
        if (ec) {
            normalizedPath = executablePath;
        }

        return normalizedPath.parent_path();
    }

    // Inside Doriax.app the SDK sits in Contents/Resources: codesign treats
    // everything in Contents/MacOS as code and rejects plain files there.
    static std::filesystem::path getEngineDir() {
        namespace fs = std::filesystem;

        const fs::path exeDir = getExecutableDir();
#if defined(__APPLE__)
        std::error_code ec;
        const fs::path bundled = exeDir.parent_path() / "Resources" / "engine";
        if (fs::exists(bundled, ec)) {
            return bundled;
        }
#endif
        return exeDir / "engine";
    }

    // An AppImage mounts at a fresh path every launch, so anything written into
    // a file the project keeps must omit it or that file churns on every start.
    static bool isEngineDirEphemeral() {
        if (std::getenv("APPIMAGE") != nullptr || std::getenv("APPDIR") != nullptr) {
            return true;
        }

        // Fallback when the environment is cleared: both runtimes use $TMPDIR.
        const std::string engineDir = getEngineDir().generic_string();
        return engineDir.find("/.mount_") != std::string::npos ||
               engineDir.find("/appimage_extracted_") != std::string::npos;
    }

    enum class WriteResult { Unchanged, Written, Failed };

    // Writes newContent only when the file does not already hold it.
    static WriteResult writeFile(const std::filesystem::path& filePath, const std::string& newContent) {
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
            return WriteResult::Unchanged;
        }

        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path(), ec);
            if (ec) {
                return WriteResult::Failed;
            }
        }

        std::ofstream ofs(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return WriteResult::Failed;
        }
        ofs.write(newContent.data(), static_cast<std::streamsize>(newContent.size()));
        return ofs ? WriteResult::Written : WriteResult::Failed;
    }

    // Writes a temporary file and renames it over the target, so a failed save keeps the old file
    static bool writeFileReplacing(const std::filesystem::path& filePath, const std::string& content) {
        std::filesystem::path tempName = ".";
        tempName += filePath.filename();
        tempName += ".tmp";
        const std::filesystem::path tempPath = filePath.parent_path() / tempName;
        std::ofstream ofs(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.close();

        std::error_code ec;
        if (ofs) {
            std::filesystem::rename(tempPath, filePath, ec);
            if (!ec) {
                return true;
            }
        }
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    // True when the file holds newContent afterwards, written or already up to date.
    // Use writeFile() when a no-op has to be told apart from a failure.
    static bool writeIfChanged(const std::filesystem::path& filePath, const std::string& newContent) {
        return writeFile(filePath, newContent) != WriteResult::Failed;
    }

    // path's UTF-8 conversions changed shape in C++20: u8string() returns std::u8string
    // and u8path() is deprecated. These keep the call sites on std::string either way.
    static std::filesystem::path pathFromUtf8(const std::string& text) {
#ifdef __cpp_lib_char8_t
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
#else
        return std::filesystem::u8path(text);
#endif
    }

    static std::string pathToUtf8(const std::filesystem::path& path) {
        const auto utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    // With '/' separators on every platform
    static std::string pathToGenericUtf8(const std::filesystem::path& path) {
        const auto utf8 = path.generic_u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    // False when string() would throw, as before Windows 10 1903 for names outside the ANSI code page
    static bool isNarrowPath(const std::filesystem::path& path) {
        try {
            (void)path.string();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
};

} // namespace doriax::editor
