// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "DebuggerWindow.h"

#include "CodeEditor.h"
#include "OutputWindow.h"
#include "Project.h"
#include "external/IconsFontAwesome6.h"
#include "util/FileUtils.h"

#include "imgui.h"
#include "imgui_internal.h"

using namespace doriax;

editor::DebuggerWindow::DebuggerWindow(Project* project, CodeEditor* codeEditor) {
    this->project = project;
    this->codeEditor = codeEditor;
}

// Opens the line of a frame in the code editor
void editor::DebuggerWindow::showLocation(const DebugFrame& frame) {
    const fs::path file = project->resolveLuaPath(FileUtils::pathFromUtf8(frame.source));
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
        codeEditor->openFileAt(FileUtils::pathToUtf8(file), frame.line);
    }
}

void editor::DebuggerWindow::showControls(ScriptDebugger& debugger) {
    const ScriptDebugger::State state = debugger.getState();
    const bool stopped = state == ScriptDebugger::State::STOPPED;

    ImGui::BeginDisabled(!stopped);
    if (ImGui::Button(ICON_FA_PLAY " Continue")) debugger.resume();
    ImGui::SetItemTooltip("F5");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP " Step Over")) debugger.stepOver();
    ImGui::SetItemTooltip("F10");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT_TO_BRACKET " Step Into")) debugger.stepInto();
    ImGui::SetItemTooltip("F11");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_UP_FROM_BRACKET " Step Out")) debugger.stepOut();
    ImGui::SetItemTooltip("Shift+F11");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(state != ScriptDebugger::State::RUNNING);
    if (ImGui::Button(ICON_FA_PAUSE " Pause")) debugger.pause();
    ImGui::SetItemTooltip("F6, stops at the next line of Lua that runs");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " Stop")) project->stopDebug();
    ImGui::SetItemTooltip("F7");

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (state == ScriptDebugger::State::BUILDING) {
        ImGui::TextUnformatted("Building the project...");
    } else if (!stopped) {
        ImGui::TextUnformatted("Running");
    } else if (debugger.getStopReason() == "error") {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", debugger.getStopMessage().c_str());
    } else if (debugger.getStopReason() == "breakpoint") {
        ImGui::TextUnformatted("Stopped at a breakpoint");
    } else {
        ImGui::TextUnformatted("Stopped");
    }
}

void editor::DebuggerWindow::showVariables(ScriptDebugger& debugger, const std::vector<DebugVariable>* variables) {
    if (!variables) {
        return;
    }
    for (const DebugVariable& variable : *variables) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | (variable.ref ? 0 : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        // named, so a table stays open from one stop to the next
        const bool open = ImGui::TreeNodeEx(variable.name.c_str(), flags);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(variable.value.c_str());
        if (open && variable.ref) {
            showVariables(debugger, debugger.getFields(variable.ref));
            ImGui::TreePop();
        }
    }
}

void editor::DebuggerWindow::show() {
    ScriptDebugger& debugger = project->getDebugger();
    if (!debugger.isActive()) {
        shownFrame = -1;
        return;
    }

    const std::vector<DebugFrame>& frames = debugger.getFrames();
    if (debugger.getStopCount() != shownStop) {
        shownStop = debugger.getStopCount();
        shownFrame = -1;
        focusRequested = true;
    }
    if (!frames.empty() && debugger.getSelectedFrame() != shownFrame) {
        shownFrame = debugger.getSelectedFrame();
        showLocation(frames[shownFrame]);
    }

    // beside Output, until the user docks it elsewhere
    if (const ImGuiWindow* output = ImGui::FindWindowByName(OutputWindow::WINDOW_NAME)) {
        ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_FirstUseEver);
    }
    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }
    if (ImGui::Begin(WINDOW_NAME)) {
        showControls(debugger);
        ImGui::Separator();

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("DebuggerPanes", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Call Stack", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::BeginChild("CallStack")) {
                for (int i = 0; i < static_cast<int>(frames.size()); i++) {
                    const DebugFrame& frame = frames[i];
                    const std::string label = frame.name + "  " + frame.source + ":" + std::to_string(frame.line) + "##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), i == debugger.getSelectedFrame())) {
                        debugger.selectFrame(i);
                        // a click on the frame it shows goes back to its line
                        shownFrame = -1;
                    }
                }
            }
            ImGui::EndChild();

            ImGui::TableNextColumn();
            if (ImGui::BeginTable("Variables", 2, tableFlags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                showVariables(debugger, debugger.getVariables());
                ImGui::EndTable();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
