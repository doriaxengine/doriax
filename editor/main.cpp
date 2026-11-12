// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Backend.h"
#include "cli/CommandLine.h"
#include "util/ShellEnv.h"

#include <cstring>

using namespace doriax;

int main(int argc, char* argv[]) {
    // A desktop launcher starts the editor without the user's shell environment,
    // so the build tools go back on PATH before anything can spawn a command.
    editor::ShellEnv::bootstrapPath();

    // CLI subcommands run before the backend starts, so they never create a window.
    if (argc >= 2 && std::strcmp(argv[1], "export") == 0) {
        return editor::CommandLine::runExportCommand(argc - 1, argv + 1, argv[0]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "shaders") == 0) {
        return editor::CommandLine::runShadersCommand(argc - 1, argv + 1, argv[0]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "shader-nodes") == 0) {
        return editor::CommandLine::runShaderNodesCommand(argc - 1, argv + 1, argv[0]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "logic-nodes") == 0) {
        return editor::CommandLine::runLogicNodesCommand(argc - 1, argv + 1, argv[0]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "logic") == 0) {
        return editor::CommandLine::runLogicCommand(argc - 1, argv + 1, argv[0]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "benchmark") == 0) {
        if (argc >= 3 && (std::strcmp(argv[2], "-h") == 0 || std::strcmp(argv[2], "--help") == 0 || std::strcmp(argv[2], "help") == 0)) {
            return editor::CommandLine::runBenchmarkHelp(argc - 1, argv + 1, argv[0]);
        }
        return editor::Backend::init(argc, argv);
    }
    if (argc >= 2) {
        return editor::CommandLine::runHelpCommand(argc - 1, argv + 1, argv[0]);
    }

    return editor::Backend::init(argc, argv);
}
