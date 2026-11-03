// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ProjectLock.h"

#include <utility>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <unistd.h>
#endif

using namespace doriax::editor;

ProjectLock::~ProjectLock() {
    release();
}

ProjectLock::ProjectLock(ProjectLock&& other) noexcept {
    *this = std::move(other);
}

ProjectLock& ProjectLock::operator=(ProjectLock&& other) noexcept {
    if (this != &other) {
        release();
        projectPath = std::exchange(other.projectPath, {});
    #ifdef _WIN32
        handle = std::exchange(other.handle, nullptr);
    #else
        fd = std::exchange(other.fd, -1);
    #endif
    }
    return *this;
}

fs::path ProjectLock::getLockFile(const fs::path& projectPath) {
    return projectPath / ".doriax" / "editor.lock";
}

ProjectLock::Status ProjectLock::acquire(const fs::path& path) {
    release();

    const fs::path lockFile = getLockFile(path);
    std::error_code ec;
    fs::create_directories(lockFile.parent_path(), ec);

#ifdef _WIN32
    HANDLE file = CreateFileW(lockFile.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Status::Unavailable;
    }
    // Past the end of the file, so the folder can still be copied
    OVERLAPPED overlapped = {};
    overlapped.Offset = 0x40000000;
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        return error == ERROR_LOCK_VIOLATION ? Status::HeldElsewhere : Status::Unavailable;
    }
    handle = file;
#else
    // Not inherited by builds or games started from the editor
    const int file = open(lockFile.c_str(), O_RDONLY | O_CREAT | O_CLOEXEC, 0644);
    if (file < 0) {
        return Status::Unavailable;
    }
    if (flock(file, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        close(file);
        return error == EWOULDBLOCK ? Status::HeldElsewhere : Status::Unavailable;
    }
    fd = file;
#endif

    projectPath = path;
    return Status::Acquired;
}

void ProjectLock::release() {
#ifdef _WIN32
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
        handle = nullptr;
    }
#else
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
#endif
    projectPath.clear();
}

bool ProjectLock::isHeldFor(const fs::path& path) const {
    std::error_code ec;
    return !projectPath.empty() && fs::equivalent(projectPath, path, ec);
}
