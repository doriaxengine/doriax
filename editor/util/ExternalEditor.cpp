// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ExternalEditor.h"

#include "util/CommandRunner.h"
#include "util/FileUtils.h"
#include "util/ShellEnv.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <spawn.h>
    #include <sys/wait.h>
    extern char** environ;
#endif

using namespace doriax;

namespace fs = std::filesystem;

namespace {

    std::string firstExisting(const std::vector<fs::path>& candidates) {
        std::error_code ec;
        for (const fs::path& candidate : candidates) {
            if (fs::is_regular_file(candidate, ec)) {
                return candidate.string();
            }
        }
        return "";
    }

    std::string quote(const std::string& text) {
    #ifdef _WIN32
        return "\"" + text + "\"";
    #else
        std::string quoted = "'";
        for (char c : text) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        return quoted + "'";
    #endif
    }

    void replaceAll(std::string& text, const std::string& from, const std::string& to) {
        for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
            text.replace(pos, from.size(), to);
        }
    }

    std::string findCLion() {
    #ifdef _WIN32
        // the launcher of the installer, then the script of Toolbox
        std::string found = editor::ShellEnv::findExecutable("clion64.exe");
        if (found.empty()) {
            found = editor::ShellEnv::findExecutable("clion.cmd");
        }
        if (found.empty()) {
            // the newest install, when PATH has none
            const char* programFiles = std::getenv("ProgramFiles");
            const char* localAppData = std::getenv("LOCALAPPDATA");
            std::vector<fs::path> installs;
            std::error_code ec;
            for (const fs::path& root : {fs::path(programFiles ? programFiles : "") / "JetBrains", fs::path(localAppData ? localAppData : "") / "Programs"}) {
                for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
                    if (entry.path().filename().string().find("CLion") != std::string::npos) {
                        installs.push_back(entry.path() / "bin" / "clion64.exe");
                    }
                }
            }
            std::sort(installs.rbegin(), installs.rend());
            found = firstExisting(installs);
        }
        return found;
    #else
        std::string found = editor::ShellEnv::findExecutable("clion");
        if (found.empty()) {
            found = editor::ShellEnv::findExecutable("clion.sh");
        }
    #ifdef __APPLE__
        if (found.empty()) {
            found = firstExisting({"/Applications/CLion.app/Contents/MacOS/clion"});
        }
    #endif
        return found;
    #endif
    }

    std::string locate(editor::ScriptEditor scriptEditor) {
        switch (scriptEditor) {
            case editor::ScriptEditor::VSCODE: {
            #ifdef _WIN32
                std::string found = editor::ShellEnv::findExecutable("code.cmd");
                if (found.empty()) {
                    const char* localAppData = std::getenv("LOCALAPPDATA");
                    const char* programFiles = std::getenv("ProgramFiles");
                    found = firstExisting({
                        fs::path(localAppData ? localAppData : "") / "Programs" / "Microsoft VS Code" / "bin" / "code.cmd",
                        fs::path(programFiles ? programFiles : "") / "Microsoft VS Code" / "bin" / "code.cmd"
                    });
                }
            #else
                std::string found = editor::ShellEnv::findExecutable("code");
                if (found.empty()) {
                #ifdef __APPLE__
                    found = firstExisting({"/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code"});
                #else
                    found = firstExisting({"/snap/bin/code", "/usr/share/code/bin/code"});
                #endif
                }
            #endif
                return found;
            }
            case editor::ScriptEditor::VISUAL_STUDIO: {
            #ifdef _WIN32
                // the default products of vswhere leave Build Tools out
                const std::string vswhere = editor::CommandRunner::findVswherePath();
                if (!vswhere.empty()) {
                    const std::string devenv = editor::CommandRunner::runCaptureNoWindow("\"" + vswhere + "\" -latest -property productPath 2>nul");
                    return firstExisting({editor::FileUtils::pathFromUtf8(devenv)});
                }
            #endif
                return "";
            }
            case editor::ScriptEditor::CLION:
                return findCLion();
            default:
                return "";
        }
    }

#ifdef _WIN32
    std::wstring toWide(const std::string& text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring result(size > 0 ? size - 1 : 0, L'\0');
        if (size > 1) {
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
        }
        return result;
    }

    bool createProcess(const std::string& commandLine) {
        std::wstring line = toWide(commandLine);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            return false;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    bool runShell(const std::string& command) {
        return createProcess("cmd.exe /d /s /c \"" + command + "\"");
    }

    bool run(const std::vector<std::string>& args) {
        std::string commandLine;
        for (const std::string& arg : args) {
            if (!commandLine.empty()) commandLine += " ";
            commandLine += quote(arg);
        }
        // .cmd launchers only run through cmd.exe
        std::string ext = fs::path(args[0]).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".cmd" || ext == ".bat") {
            return runShell(commandLine);
        }
        return createProcess(commandLine);
    }
#else
    bool run(const std::vector<std::string>& args) {
        std::vector<char*> argv;
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        for (int fd = 0; fd <= 2; fd++) {
            posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
        }

        // own group, so Ctrl+C on the editor's terminal spares it
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);

        pid_t pid = 0;
        const int result = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&actions);
        if (result != 0) {
            return false;
        }

        std::thread([pid]() { waitpid(pid, nullptr, 0); }).detach();
        return true;
    }

    bool runShell(const std::string& command) {
        return run({"/bin/sh", "-c", command});
    }
#endif

    bool runCustom(const std::string& project, const std::string& file, int line, int column) {
        std::string command = editor::AppSettings::getScriptEditorCommand();
        if (command.empty()) {
            return false;
        }
        replaceAll(command, "{project}", quote(project));
        replaceAll(command, "{file}", quote(file));
        replaceAll(command, "{line}", std::to_string(std::max(line, 1)));
        replaceAll(command, "{column}", std::to_string(std::max(column, 1)));
        return runShell(command);
    }

}

const char* editor::ExternalEditor::getName(ScriptEditor scriptEditor) {
    switch (scriptEditor) {
        case ScriptEditor::VSCODE: return "VS Code";
        case ScriptEditor::VISUAL_STUDIO: return "Visual Studio";
        case ScriptEditor::CLION: return "CLion";
        case ScriptEditor::CUSTOM: return "Custom Command";
        default: return "Built-in Editor";
    }
}

std::vector<editor::ScriptEditor> editor::ExternalEditor::getChoices() {
    return {
        ScriptEditor::BUILTIN,
        ScriptEditor::VSCODE,
    #ifdef _WIN32
        ScriptEditor::VISUAL_STUDIO,
    #endif
        ScriptEditor::CLION,
        ScriptEditor::CUSTOM
    };
}

std::string editor::ExternalEditor::findExecutable(ScriptEditor scriptEditor) {
    // a miss is not kept, the IDE may be installed meanwhile
    static std::map<ScriptEditor, std::string> found;
    auto it = found.find(scriptEditor);
    if (it != found.end()) {
        return it->second;
    }
    // PATH is in the ANSI code page on Windows
    const std::string path = locate(scriptEditor);
    if (path.empty()) {
        return "";
    }
    found[scriptEditor] = FileUtils::pathToUtf8(fs::path(path));
    return found[scriptEditor];
}

bool editor::ExternalEditor::isEnabled() {
    return AppSettings::getScriptEditor() != ScriptEditor::BUILTIN;
}

std::string editor::ExternalEditor::getOpenLabel() {
    const ScriptEditor scriptEditor = AppSettings::getScriptEditor();
    if (scriptEditor == ScriptEditor::BUILTIN || scriptEditor == ScriptEditor::CUSTOM) {
        return "Open in External Editor";
    }
    return std::string("Open in ") + getName(scriptEditor);
}

std::string editor::ExternalEditor::getStartError() {
    const ScriptEditor scriptEditor = AppSettings::getScriptEditor();
    if (scriptEditor == ScriptEditor::CUSTOM) {
        return "Could not run the script editor command. Check it in Editor Settings > General.";
    }
    return std::string("Could not start ") + getName(scriptEditor) + ". Install it, or choose another script editor in Editor Settings > General.";
}

bool editor::ExternalEditor::openFile(const fs::path& projectPath, const fs::path& file, int line, int column) {
    const ScriptEditor scriptEditor = AppSettings::getScriptEditor();
    const std::string project = FileUtils::pathToUtf8(projectPath);
    const std::string path = FileUtils::pathToUtf8(file);
    if (scriptEditor == ScriptEditor::CUSTOM) {
        return runCustom(project, path, line, column);
    }

    const std::string exe = findExecutable(scriptEditor);
    if (exe.empty()) {
        return false;
    }

    std::vector<std::string> args = {exe};
    if (scriptEditor == ScriptEditor::VSCODE) {
        args.push_back(project);
        if (line > 0) {
            args.push_back("-g");
            args.push_back(path + ":" + std::to_string(line) + ":" + std::to_string(std::max(column, 1)));
        } else {
            args.push_back(path);
        }
    } else if (scriptEditor == ScriptEditor::VISUAL_STUDIO) {
        args.push_back("/edit");
        args.push_back(path);
        if (line > 0) {
            args.push_back("/command");
            args.push_back("Edit.GoTo " + std::to_string(line));
        }
    } else {
        args.push_back(project);
        if (line > 0) {
            args.push_back("--line");
            args.push_back(std::to_string(line));
            if (column > 0) {
                args.push_back("--column");
                args.push_back(std::to_string(column));
            }
        }
        args.push_back(path);
    }

    return run(args);
}

bool editor::ExternalEditor::openProject(const fs::path& projectPath) {
    const ScriptEditor scriptEditor = AppSettings::getScriptEditor();
    const std::string project = FileUtils::pathToUtf8(projectPath);
    if (scriptEditor == ScriptEditor::CUSTOM) {
        return runCustom(project, project, 0, 0);
    }

    const std::string exe = findExecutable(scriptEditor);
    if (exe.empty()) {
        return false;
    }
    return run({exe, project});
}
