// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "AppSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doriax::editor {

    // Opens scripts in the IDE of Editor Settings
    class ExternalEditor {
    public:
        static const char* getName(ScriptEditor editor);
        // the choices this platform offers
        static std::vector<ScriptEditor> getChoices();
        // "" when the IDE is not installed
        static std::string findExecutable(ScriptEditor editor);

        static bool isEnabled();
        // "Open in VS Code", for menus
        static std::string getOpenLabel();
        static std::string getStartError();

        // line and column start at 1, 0 for none
        static bool openFile(const std::filesystem::path& projectPath, const std::filesystem::path& file, int line = 0, int column = 0);
        static bool openProject(const std::filesystem::path& projectPath);
    };

}
