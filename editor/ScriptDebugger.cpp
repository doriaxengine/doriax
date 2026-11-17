// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ScriptDebugger.h"

#include "EditorHost.h"
#include "Out.h"
#include "LuaDebugger.h"

#include "json.hpp"

#include <cstring>

using namespace doriax;
using json = nlohmann::json;

namespace {

    std::vector<editor::DebugVariable> readVariables(const json& message) {
        std::vector<editor::DebugVariable> variables;
        for (const json& entry : message.value("variables", json::array())) {
            editor::DebugVariable variable;
            variable.name = entry.value("name", "");
            variable.type = entry.value("type", "");
            variable.value = entry.value("value", "");
            variable.ref = entry.value("ref", 0);
            variables.push_back(variable);
        }
        return variables;
    }

    // "(ERROR): message" as the engine prints it
    void logProjectLine(const std::string& line) {
        static const std::pair<const char*, editor::LogType> prefixes[] = {
            {"(ERROR): ", editor::LogType::Error},
            {"(WARN): ", editor::LogType::Warning},
            {"(DEBUG): ", editor::LogType::Debug},
            {"(VERBOSE): ", editor::LogType::Verbose}
        };
        for (const auto& [prefix, type] : prefixes) {
            if (line.rfind(prefix, 0) == 0) {
                editor::Out::log(type, editor::LogSource::Project, line.substr(std::strlen(prefix)));
                return;
            }
        }
        editor::Out::log(editor::LogType::Info, editor::LogSource::Project, line);
    }

}

editor::ScriptDebugger::~ScriptDebugger() {
    waitForEnd();
}

void editor::ScriptDebugger::start(const std::string& command, const fs::path& workingDir, std::function<bool()> build) {
    if (isActive()) {
        return;
    }
    if (worker.joinable()) {
        worker.join();
    }
    state = State::BUILDING;
    runner.setInputPipe(true);
    runner.resetCancel();
    worker = std::thread([this, command, workingDir, build = std::move(build), started = ++session]() {
        if (build() && !isStopRequested()) {
            state = State::RUNNING;
            Out::success("Scene started with the debugger");
            runner.run(command, workingDir, [this, started](const std::string& line) { onLine(line, started); });
            Out::info("The debugged scene ended");
        }
        getEditorHost().enqueueMainThreadTask([this, started]() {
            if (started == session) {
                clearStop();
                state = State::IDLE;
            }
        });
    });
}

void editor::ScriptDebugger::stop() {
    if (isActive()) {
        runner.cancel();
    }
}

bool editor::ScriptDebugger::isStopRequested() const {
    return runner.isCancelRequested();
}

void editor::ScriptDebugger::waitForEnd() {
    stop();
    if (worker.joinable()) {
        worker.join();
    }
    // the task of the thread would only do it on a later frame
    session++;
    clearStop();
    state = State::IDLE;
}

void editor::ScriptDebugger::reset() {
    waitForEnd();
    breakpoints.clear();
}

editor::ScriptDebugger::State editor::ScriptDebugger::getState() const {
    return state;
}

bool editor::ScriptDebugger::isActive() const {
    return state != State::IDLE;
}

bool editor::ScriptDebugger::hasBreakpoints() const {
    return !breakpoints.empty();
}

void editor::ScriptDebugger::toggleBreakpoint(const std::string& file, int line) {
    std::set<int> lines = getBreakpoints(file);
    if (!lines.erase(line)) {
        lines.insert(line);
    }
    setBreakpoints(file, lines);
}

void editor::ScriptDebugger::setBreakpoints(const std::string& file, const std::set<int>& lines) {
    if (getBreakpoints(file) == lines) {
        return;
    }
    if (lines.empty()) {
        breakpoints.erase(file);
    } else {
        breakpoints[file] = lines;
    }
    sendBreakpoints(file);
}

const std::set<int>& editor::ScriptDebugger::getBreakpoints(const std::string& file) const {
    static const std::set<int> none;
    const auto it = breakpoints.find(file);
    return it != breakpoints.end() ? it->second : none;
}

void editor::ScriptDebugger::renameBreakpoints(const std::string& from, const std::string& to) {
    if (from.empty() || from == to) {
        return;
    }
    std::vector<std::string> files;
    for (const auto& [file, lines] : breakpoints) {
        if (file == from || file.rfind(from + "/", 0) == 0) {
            files.push_back(file);
        }
    }
    for (const std::string& file : files) {
        const std::set<int> lines = getBreakpoints(file);
        setBreakpoints(file, {});
        setBreakpoints(to + file.substr(from.size()), lines);
    }
}

void editor::ScriptDebugger::resume() {
    send("continue");
}

void editor::ScriptDebugger::stepOver() {
    send("stepOver");
}

void editor::ScriptDebugger::stepInto() {
    send("stepInto");
}

void editor::ScriptDebugger::stepOut() {
    send("stepOut");
}

void editor::ScriptDebugger::pause() {
    send("pause");
}

const std::string& editor::ScriptDebugger::getStopReason() const {
    return stopReason;
}

const std::string& editor::ScriptDebugger::getStopMessage() const {
    return stopMessage;
}

unsigned int editor::ScriptDebugger::getStopCount() const {
    return stopCount;
}

const std::vector<editor::DebugFrame>& editor::ScriptDebugger::getFrames() const {
    return frames;
}

int editor::ScriptDebugger::getSelectedFrame() const {
    return selectedFrame;
}

void editor::ScriptDebugger::selectFrame(int index) {
    if (index >= 0 && index < static_cast<int>(frames.size())) {
        selectedFrame = index;
    }
}

const std::vector<editor::DebugVariable>* editor::ScriptDebugger::getVariables() {
    if (state != State::STOPPED || frames.empty()) {
        return nullptr;
    }
    const int level = frames[selectedFrame].level;
    if (requestedFrames.insert(level).second) {
        send("frame", ",\"level\":" + std::to_string(level));
    }
    const auto it = frameVariables.find(level);
    return it != frameVariables.end() ? &it->second : nullptr;
}

const std::vector<editor::DebugVariable>* editor::ScriptDebugger::getFields(int ref) {
    if (state != State::STOPPED) {
        return nullptr;
    }
    if (requestedFields.insert(ref).second) {
        send("expand", ",\"ref\":" + std::to_string(ref));
    }
    const auto it = tableFields.find(ref);
    return it != tableFields.end() ? &it->second : nullptr;
}

void editor::ScriptDebugger::send(const std::string& command, const std::string& extra) {
    if (state == State::RUNNING || state == State::STOPPED) {
        runner.write("{\"command\":\"" + command + "\"" + extra + "}\n");
    }
}

void editor::ScriptDebugger::sendBreakpoints(const std::string& file) {
    // ASCII only, as the messages of the process
    send("breakpoints", ",\"file\":" + json(file).dump(-1, ' ', true) + ",\"lines\":" + json(getBreakpoints(file)).dump());
}

// On the thread that waits for the process
void editor::ScriptDebugger::onLine(const std::string& line, unsigned int started) {
    const size_t mark = line.find(LuaDebugger::MESSAGE_MARK);
    if (mark != 0) {
        logProjectLine(line.substr(0, mark));
    }
    if (mark != std::string::npos) {
        getEditorHost().enqueueMainThreadTask([this, started, text = line.substr(mark + 1)]() {
            // a message may wait in the queue past the end of its session
            if (started == session) {
                onMessage(text);
            }
        });
    }
}

void editor::ScriptDebugger::onMessage(const std::string& text) {
    const json message = json::parse(text, nullptr, false);
    if (!message.is_object()) {
        return;
    }
    const std::string event = message.value("event", "");
    if (event == "attached") {
        // it waits for the breakpoints before any script runs
        for (const auto& [file, lines] : breakpoints) {
            sendBreakpoints(file);
        }
        resume();
    } else if (event == "stopped") {
        clearStop();
        stopReason = message.value("reason", "");
        stopMessage = message.value("message", "");
        for (const json& entry : message.value("frames", json::array())) {
            DebugFrame frame;
            frame.level = entry.value("level", 0);
            frame.name = entry.value("name", "");
            frame.source = entry.value("source", "");
            frame.line = entry.value("line", 0);
            frames.push_back(frame);
        }
        stopCount++;
        state = State::STOPPED;
    } else if (event == "continued") {
        clearStop();
        state = State::RUNNING;
    } else if (event == "frame") {
        frameVariables[message.value("level", 0)] = readVariables(message);
    } else if (event == "expand") {
        tableFields[message.value("ref", 0)] = readVariables(message);
    }
}

void editor::ScriptDebugger::clearStop() {
    frames.clear();
    selectedFrame = 0;
    frameVariables.clear();
    tableFields.clear();
    requestedFrames.clear();
    requestedFields.clear();
}
