// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

// CommandRunner.cpp
#include "CommandRunner.h"
#include "TextEncoding.h"
#include "editor/Out.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <thread>

#ifdef _WIN32
    #include <io.h>
#else
    #include <signal.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <spawn.h>

    extern char **environ;
#endif

using namespace doriax;

namespace {
    constexpr std::chrono::milliseconds kReadSleepMs{10};
    constexpr std::chrono::milliseconds kKillGracePeriod{100};

#ifdef _WIN32
    // Without /S, a command line holding more than two quotes and starting with
    // one loses its first and its last quote, so a quoted program path followed
    // by quoted arguments ("C:\Program Files\CMake\bin\cmake.exe" -G "Ninja")
    // decays into "C:\Program". /S "<command>" strips only the outer pair.
    std::string cmdQuoting(const std::string& command) {
        return "/s /c \"" + command + "\"";
    }
#endif
}

#ifdef _WIN32
// Run the command through cmd.exe (see cmdQuoting) and capture its output
// WITHOUT flashing a console window. The editor is a GUI app with no console
// of its own, so _popen() and system() each briefly pop up a cmd.exe window;
// CreateProcess with CREATE_NO_WINDOW avoids that flicker. Returns the captured
// output (stdout, plus stderr unless the command redirects it) with trailing
// whitespace trimmed.
std::string editor::CommandRunner::runCaptureNoWindow(const std::string& command) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string cmdLine = "cmd.exe " + cmdQuoting(command);
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }
    CloseHandle(hWrite);

    std::string result;
    char buffer[4096];
    DWORD bytesRead = 0;
    // Read until the child closes its end of the pipe (i.e. exits); reading
    // concurrently avoids a deadlock if output exceeds the pipe buffer.
    while (ReadFile(hRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        result.append(buffer, bytesRead);
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        result.pop_back();
    return TextEncoding::consoleToUtf8(result);
}

std::string editor::CommandRunner::findVswherePath() {
    auto tryEnv = [](const char* envVar) -> std::string {
        const char* pf = std::getenv(envVar);
        if (!pf) return "";
        fs::path p = fs::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (fs::exists(p)) return p.string();
        return "";
    };
    std::string result = tryEnv("ProgramFiles(x86)");
    if (result.empty()) result = tryEnv("ProgramFiles");
    return result;
}

std::string editor::CommandRunner::vsInstallationPath() {
    std::string vswhere = findVswherePath();
    if (vswhere.empty()) return "";
    return runCaptureNoWindow("\"" + vswhere + "\" -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>nul");
}

bool editor::CommandRunner::hasVSWithCppTools() {
    return !vsInstallationPath().empty();
}

// Path to vcvarsall.bat for the latest VS install with C++ tools, or "".
std::string editor::CommandRunner::findVcvarsall() {
    std::string installPath = vsInstallationPath();
    if (installPath.empty()) return "";
    fs::path vcvars = fs::path(installPath) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat";
    std::error_code ec;
    if (fs::exists(vcvars, ec)) return vcvars.string();
    return "";
}

// Path to ninja.exe on PATH, else to the one of the Visual Studio C++ CMake tools, or "".
std::string editor::CommandRunner::findNinja() {
    std::string onPath = runCaptureNoWindow("where ninja.exe 2>nul");
    onPath = onPath.substr(0, onPath.find_first_of("\r\n"));
    if (!onPath.empty()) return onPath;

    std::string installPath = vsInstallationPath();
    if (installPath.empty()) return "";
    fs::path ninja = fs::path(installPath) / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "Ninja" / "ninja.exe";
    std::error_code ec;
    if (fs::exists(ninja, ec)) return ninja.string();
    return "";
}
#endif

void editor::CommandRunner::collectBuildError(const std::string& line, std::vector<std::string>& errors) {
    // MSVC, MSBuild, GCC and Clang
    static const char* const markers[] = {": error C", ": fatal error C", "error LNK", ": error MSB", ": error:", "fatal error:"};
    const bool isError = std::any_of(std::begin(markers), std::end(markers), [&line](const char* marker) {
        return line.find(marker) != std::string::npos;
    });
    if (!isError || errors.size() >= 20) return;

    // MSBuild repeats each error at the end with " [project.vcxproj]"
    std::string error = line;
    const size_t project = error.rfind(" [");
    if (project != std::string::npos && error.back() == ']' && error.find(".vcxproj", project) != std::string::npos) {
        error.erase(project);
    }
    error.erase(0, error.find_first_not_of(" \t"));

    if (std::find(errors.begin(), errors.end(), error) == errors.end()) {
        errors.push_back(error);
    }
}

std::string editor::CommandRunner::msvcEnvPrefix(const std::string& generator) {
#ifdef _WIN32
    // Ninja (unlike the Visual Studio generator) runs the compiler directly and
    // does NOT set up the MSVC toolchain environment. A standalone Clang or MSVC
    // toolchain then cannot find the Windows SDK and CRT import libraries, so
    // linking fails with errors like "could not open 'kernel32.lib'". Loading
    // vcvars populates INCLUDE/LIB/PATH so the SDK is found. The Visual Studio
    // generator configures this itself, and MinGW must not use MSVC's env.
    if (generator == "Ninja") {
        std::string vcvars = findVcvarsall();
        if (!vcvars.empty()) {
            // Do NOT suppress vcvars output: when it cannot find the Windows SDK
            // it prints "[ERROR:...]" diagnostics that are essential for
            // troubleshooting missing '*.lib' link failures. The banner is a
            // few harmless lines on success.
            return "call \"" + vcvars + "\" x64 && ";
        }
        Out::warning("Could not locate the Visual Studio environment (vcvarsall.bat). If linking fails with missing '*.lib' files, run the editor from a Developer Command Prompt or install the Windows SDK with Visual Studio.");
    }
#endif
    (void)generator;
    return "";
}

void editor::CommandRunner::cancel() {
    cancelRequested.store(true, std::memory_order_relaxed);
    terminateCurrentProcess();
}

bool editor::CommandRunner::isCancelRequested() const {
    return cancelRequested.load(std::memory_order_relaxed);
}

void editor::CommandRunner::resetCancel() {
    cancelRequested.store(false, std::memory_order_relaxed);
}

void editor::CommandRunner::terminateCurrentProcess() {
    #ifdef _WIN32
        HANDLE handleSnapshot = nullptr;
        HANDLE jobSnapshot = nullptr;
        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            handleSnapshot = currentProcessHandle;
            jobSnapshot = currentJobHandle;
        }
        if (jobSnapshot) {
            // Kills every process in the job: cmd.exe plus the cmake/compiler
            // tree it spawned.
            TerminateJobObject(jobSnapshot, 1);
        } else if (handleSnapshot) {
            TerminateProcess(handleSnapshot, 1);
        }
    #else
        pid_t pidSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            pidSnapshot = currentProcessPid;
        }
        if (pidSnapshot > 0) {
            // The child was spawned into its own process group (pgid == pid),
            // so signal the group to tear down the shell AND its descendants
            // (cmake, ninja/make, compilers).
            if (kill(-pidSnapshot, SIGTERM) != 0 && errno != ESRCH) {
                Out::warning("Failed to send SIGTERM to process group %d: %s", pidSnapshot, strerror(errno));
            }

            std::this_thread::sleep_for(kKillGracePeriod);
            if (kill(-pidSnapshot, 0) == 0) {
                kill(-pidSnapshot, SIGKILL);
            }
        }
    #endif
}

bool editor::CommandRunner::run(const std::string& command, const fs::path& workingDir, const std::function<void(const std::string&)>& onLine) {
    cancelRequested.store(false, std::memory_order_relaxed);

    auto emit = [&onLine](const std::string& rawLine) {
        const std::string line = TextEncoding::consoleToUtf8(rawLine);
        if (onLine) {
            onLine(line);
        } else {
            Out::build("%s", line.c_str());
        }
    };

    #ifdef _WIN32
        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE hReadPipe = nullptr;
        HANDLE hWritePipe = nullptr;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            Out::error("Failed to create pipe (Win32). Error code: %lu", GetLastError());
            return false;
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(STARTUPINFOA);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError  = hWritePipe;
        si.hStdInput  = nullptr;

        PROCESS_INFORMATION pi{};
        std::string cmdLine = "cmd.exe " + cmdQuoting(command);

        // Track the whole child tree in a job object so cancellation can kill
        // cmake and the compilers it spawns, not only cmd.exe. KILL_ON_JOB_CLOSE
        // also reaps the tree if the editor itself exits.
        HANDLE hJob = CreateJobObjectA(nullptr, nullptr);
        if (hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
            jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jobLimits, sizeof(jobLimits));
        }

        if (!CreateProcessA(
                nullptr,
                cmdLine.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED,
                nullptr,
                workingDir.empty() ? nullptr : workingDir.string().c_str(),
                &si,
                &pi))
        {
            DWORD err = GetLastError();
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            if (hJob) CloseHandle(hJob);
            Out::error("Failed to create process. Error code: %lu", err);
            return false;
        }

        // Assign before the first instruction runs so no child can escape the
        // job. On failure (e.g. nested-job restrictions) fall back to plain
        // process termination.
        if (hJob && !AssignProcessToJobObject(hJob, pi.hProcess)) {
            CloseHandle(hJob);
            hJob = nullptr;
        }
        ResumeThread(pi.hThread);

        CloseHandle(hWritePipe);

        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            currentProcessHandle = pi.hProcess;
            currentJobHandle = hJob;
        }

        constexpr size_t BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        std::string accumulator;

        auto drainPipe = [&]() {
            DWORD bytesAvailable = 0;
            while (PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr) && bytesAvailable > 0) {
                DWORD bytesRead = 0;
                if (!ReadFile(hReadPipe, buffer, static_cast<DWORD>(BUFFER_SIZE - 1), &bytesRead, nullptr) || bytesRead == 0) {
                    break;
                }

                accumulator.append(buffer, bytesRead);

                size_t pos = 0;
                size_t nextPos = 0;
                while ((nextPos = accumulator.find('\n', pos)) != std::string::npos) {
                    std::string line = accumulator.substr(pos, nextPos - pos);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        emit(line);
                    }
                    pos = nextPos + 1;
                }
                accumulator.erase(0, pos);
            }
        };

        DWORD exitCode = 1;
        bool finished = false;

        while (!finished) {
            if (cancelRequested.load(std::memory_order_relaxed)) {
                terminateCurrentProcess();
                CloseHandle(pi.hThread);
                {
                    std::lock_guard<std::mutex> lock(processHandleMutex);
                    currentProcessHandle = NULL;
                    currentJobHandle = NULL;
                }
                CloseHandle(pi.hProcess);
                if (hJob) CloseHandle(hJob);
                CloseHandle(hReadPipe);  // Close pipe at the end
                return false;
            }

            drainPipe();

            DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
            if (waitResult == WAIT_OBJECT_0) {
                finished = true;
                GetExitCodeProcess(pi.hProcess, &exitCode);
            } else if (waitResult == WAIT_FAILED) {
                finished = true;
            } else {
                std::this_thread::sleep_for(kReadSleepMs);
            }
        }

        // The process can exit with output still buffered in the pipe — for a
        // command that fails immediately that is ALL of its output. Drain it
        // so errors are not silently dropped.
        drainPipe();

        if (!accumulator.empty()) {
            emit(accumulator);
        }

        if (exitCode != 0) {
            Out::build("Process exited with code %lu", exitCode);
        }

        CloseHandle(hReadPipe);
        CloseHandle(pi.hThread);

        {
            std::lock_guard<std::mutex> lock(processHandleMutex);
            currentProcessHandle = NULL;
            currentJobHandle = NULL;
        }
        CloseHandle(pi.hProcess);
        if (hJob) CloseHandle(hJob);

        return exitCode == 0;
    #else
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            Out::error("Failed to create pipe (POSIX): %s", strerror(errno));
            return false;
        }

        // Make read end non-blocking
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);

        pid_t pid = 0;
        std::string workDirStr = workingDir.string();
        std::string cdPrefix;
        if (!workDirStr.empty()) {
            cdPrefix = "cd \"" + workDirStr + "\" && ";
        }
        std::string shellCommand = cdPrefix + command;

        char *argv[] = { const_cast<char*>("/bin/sh"),
                        const_cast<char*>("-c"),
                        const_cast<char*>(shellCommand.c_str()),
                        nullptr };

        // Place the shell in its own process group so cancellation can signal
        // the whole tree: killing only the shell would orphan cmake and the
        // compilers it spawned, leaving them running (and holding build locks).
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);

        int spawnResult = posix_spawn(&pid, "/bin/sh", &actions, &attr, argv, environ);
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[1]);

        if (spawnResult != 0) {
            close(pipefd[0]);
            Out::error("Failed to spawn process (POSIX): %s", strerror(spawnResult));
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            currentProcessPid = pid;
        }

        constexpr size_t BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        bool processExited = false;
        bool reaped = false;
        int reapedStatus = 0;

        // Accumulate reads and emit per line (a read() chunk can hold several
        // lines or a partial one), matching the Win32 path.
        std::string accumulator;
        auto emitLines = [&](const char* data, size_t length) {
            accumulator.append(data, length);
            size_t pos = 0;
            size_t nextPos = 0;
            while ((nextPos = accumulator.find('\n', pos)) != std::string::npos) {
                std::string line = accumulator.substr(pos, nextPos - pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    emit(line);
                }
                pos = nextPos + 1;
            }
            accumulator.erase(0, pos);
        };

        while (!processExited) {
            // Check for cancellation
            if (cancelRequested.load(std::memory_order_relaxed)) {
                terminateCurrentProcess();
                processExited = true;
                break;
            }

            // Try to read with a short timeout
            ssize_t bytesRead = read(pipefd[0], buffer, BUFFER_SIZE);

            if (bytesRead > 0) {
                emitLines(buffer, (size_t)bytesRead);
            } else if (bytesRead == 0) {
                // EOF - pipe closed
                processExited = true;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, check process status
                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid) {
                    // Reaped here: keep the status, the final waitpid below
                    // would get ECHILD and lose the real exit code.
                    reaped = true;
                    reapedStatus = status;
                    processExited = true;
                } else if (result == -1) {
                    processExited = true;
                }
                // Sleep briefly to avoid busy-waiting
                std::this_thread::sleep_for(kReadSleepMs);
            } else {
                // Read error
                Out::error("Error reading from pipe: %s", strerror(errno));
                processExited = true;
            }
        }

        // The process can exit with output still buffered in the pipe — for a
        // command that fails immediately that is ALL of its output. Drain it
        // (non-blocking read) so errors are not silently dropped.
        while (true) {
            ssize_t bytesRead = read(pipefd[0], buffer, BUFFER_SIZE);
            if (bytesRead <= 0) break;
            emitLines(buffer, (size_t)bytesRead);
        }
        if (!accumulator.empty()) {
            emit(accumulator);
        }

        // Always close the pipe after the loop
        close(pipefd[0]);

        int status = 0;
        if (reaped) {
            status = reapedStatus;
        } else {
            while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
                // retry on EINTR
            }
        }

        {
            std::lock_guard<std::mutex> lock(processPidMutex);
            currentProcessPid = 0;
        }

        if (cancelRequested.load(std::memory_order_relaxed)) {
            return false;
        }

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code != 0) {
                Out::build("Process exited with code %d", code);
            }
            return code == 0;
        }
        if (WIFSIGNALED(status)) {
            Out::warning("Process terminated by signal %d", WTERMSIG(status));
        }
        return false;
    #endif
}
