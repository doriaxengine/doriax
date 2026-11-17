// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "ScriptDebugger.h"

namespace doriax::editor {

    class CodeEditor;
    class Project;

    // Controls, call stack and variables of the scene that runs with the debugger
    class DebuggerWindow {
    private:
        Project* project;
        CodeEditor* codeEditor;

        // the stop and the frame the code editor shows
        unsigned int shownStop = 0;
        int shownFrame = -1;
        bool focusRequested = false;

        void showLocation(const DebugFrame& frame);
        void showControls(ScriptDebugger& debugger);
        void showVariables(ScriptDebugger& debugger, const std::vector<DebugVariable>* variables);

    public:
        static constexpr const char* WINDOW_NAME = "Debugger";

        DebuggerWindow(Project* project, CodeEditor* codeEditor);

        void show();
    };

}
