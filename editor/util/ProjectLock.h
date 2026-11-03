// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>

namespace doriax::editor {

    namespace fs = std::filesystem;

    // OS lock on .doriax/editor.lock, freed by the OS even if the editor crashes
    class ProjectLock {
    public:
        enum class Status {
            Acquired,
            HeldElsewhere,
            Unavailable
        };

        ProjectLock() = default;
        ~ProjectLock();

        ProjectLock(const ProjectLock&) = delete;
        ProjectLock& operator=(const ProjectLock&) = delete;

        ProjectLock(ProjectLock&& other) noexcept;
        ProjectLock& operator=(ProjectLock&& other) noexcept;

        Status acquire(const fs::path& projectPath);
        void release();

        bool isHeldFor(const fs::path& projectPath) const;

        static fs::path getLockFile(const fs::path& projectPath);

    private:
        fs::path projectPath;
    #ifdef _WIN32
        void* handle = nullptr;
    #else
        int fd = -1;
    #endif
    };

}
