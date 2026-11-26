// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ScriptDebugger.h"

#include "EditorHost.h"
#include "Out.h"
#include "LuaDebugger.h"
#include "util/FileUtils.h"

#include "json.hpp"

#include <algorithm>
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

void editor::ScriptDebugger::start(const std::string& command, const fs::path& workingDir, std::function<bool()> build, const std::string& nativeAdapter) {
    if (isActive()) {
        return;
    }
    if (worker.joinable()) {
        worker.join();
    }
    this->nativeAdapter = nativeAdapter;
    sourceRoot = NativeDebugger::sourceName(workingDir);
    if (sourceRoot.empty() || sourceRoot.back() != '/') {
        sourceRoot += "/";
    }
    released = false;
    native.onStopped = [this]() {
        nativeStop = true;
        showStop();
    };
    native.onContinued = [this]() {
        nativeStop = false;
        showStop();
    };
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
                native.end();
                nativeStop = false;
                released = false;
                clearStop();
                state = State::IDLE;
            }
        });
    });
}

void editor::ScriptDebugger::stop() {
    if (isActive()) {
        // a process it holds stopped would not take the signal
        native.detach();
        nativeStop = false;
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
    native.end();
    nativeStop = false;
    released = false;
    clearStop();
    state = State::IDLE;
}

void editor::ScriptDebugger::reset() {
    waitForEnd();
    breakpoints.clear();
    nativeBreakpoints.clear();
}

editor::ScriptDebugger::State editor::ScriptDebugger::getState() const {
    return state;
}

bool editor::ScriptDebugger::isActive() const {
    return state != State::IDLE;
}

bool editor::ScriptDebugger::hasBreakpoints() const {
    return !breakpoints.empty() || !nativeBreakpoints.empty();
}

bool editor::ScriptDebugger::hasNativeBreakpoints() const {
    return !nativeBreakpoints.empty();
}

void editor::ScriptDebugger::dropStaleBreakpoints() {
    std::error_code ec;
    for (auto it = nativeBreakpoints.begin(); it != nativeBreakpoints.end();) {
        const fs::path file = FileUtils::pathFromUtf8(it->first);
        it = NativeDebugger::isSource(file) && fs::is_regular_file(file, ec) ? std::next(it) : nativeBreakpoints.erase(it);
    }
}

void editor::ScriptDebugger::setBreakpoints(const std::string& file, const std::set<int>& lines, bool native) {
    if (getBreakpoints(file, native) == lines) {
        return;
    }
    std::map<std::string, std::set<int>>& files = native ? nativeBreakpoints : breakpoints;
    if (lines.empty()) {
        files.erase(file);
    } else {
        files[file] = lines;
    }
    if (native) {
        this->native.setBreakpoints(file, lines);
    } else {
        sendBreakpoints(file);
    }
}

const std::set<int>& editor::ScriptDebugger::getBreakpoints(const std::string& file, bool native) const {
    static const std::set<int> none;
    const std::map<std::string, std::set<int>>& files = native ? nativeBreakpoints : breakpoints;
    const auto it = files.find(file);
    return it != files.end() ? it->second : none;
}

void editor::ScriptDebugger::renameBreakpoints(const std::string& from, const std::string& to, bool native) {
    if (from.empty() || from == to) {
        return;
    }
    std::vector<std::string> files;
    for (const auto& [file, lines] : native ? nativeBreakpoints : breakpoints) {
        if (file == from || file.rfind(from + "/", 0) == 0) {
            files.push_back(file);
        }
    }
    for (const std::string& file : files) {
        const std::set<int> lines = getBreakpoints(file, native);
        setBreakpoints(file, {}, native);
        setBreakpoints(to + file.substr(from.size()), lines, native);
    }
}

void editor::ScriptDebugger::resume() {
    nativeStop ? native.resume() : send("continue");
}

void editor::ScriptDebugger::stepOver() {
    nativeStop ? native.stepOver() : send("stepOver");
}

void editor::ScriptDebugger::stepInto() {
    nativeStop ? native.stepInto() : send("stepInto");
}

void editor::ScriptDebugger::stepOut() {
    nativeStop ? native.stepOut() : send("stepOut");
}

void editor::ScriptDebugger::pause() {
    if (state != State::RUNNING) {
        return;
    }
    send("pause");
    if (native.isAttached() && pauseSession != session) {
        // with no Lua to stop at, the whole process stops
        pauseSession = session;
        pauseDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        awaitPause(session);
    }
}

bool editor::ScriptDebugger::isNativeAttached() const {
    return native.isAttached();
}

bool editor::ScriptDebugger::isNativeAttaching() const {
    return native.isAttaching();
}

bool editor::ScriptDebugger::isNativeOff() const {
    return released && !native.isAttached();
}

bool editor::ScriptDebugger::isNativeStop() const {
    return nativeStop;
}

bool editor::ScriptDebugger::isProjectFrame(const DebugFrame& frame) const {
    // the build has its copy of engine files in a hidden folder of the project
    return !nativeStop || (frame.source.rfind(sourceRoot, 0) == 0 && frame.source.compare(sourceRoot.size(), 1, ".") != 0);
}

const std::string& editor::ScriptDebugger::getStopReason() const {
    return nativeStop ? native.getStopReason() : stopReason;
}

const std::string& editor::ScriptDebugger::getStopMessage() const {
    return nativeStop ? native.getStopMessage() : stopMessage;
}

unsigned int editor::ScriptDebugger::getStopCount() const {
    return stopCount;
}

const std::vector<editor::DebugFrame>& editor::ScriptDebugger::getFrames() const {
    return nativeStop ? native.getFrames() : frames;
}

int editor::ScriptDebugger::getSelectedFrame() const {
    return selectedFrame;
}

void editor::ScriptDebugger::selectFrame(int index) {
    if (index >= 0 && index < static_cast<int>(getFrames().size())) {
        selectedFrame = index;
    }
}

const std::vector<editor::DebugVariable>* editor::ScriptDebugger::getVariables() {
    if (nativeStop) {
        return native.getVariables(selectedFrame);
    }
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
    if (nativeStop) {
        return native.getFields(ref);
    }
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
        const int pid = message.value("pid", 0);
        if (nativeAdapter.empty() || pid <= 0) {
            release();
            return;
        }
        // it waits meanwhile, so no C++ breakpoint is missed either
        native.attach(nativeAdapter, pid, nativeBreakpoints, [this]() { release(); });
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
        luaStop = true;
        showStop();
    } else if (event == "continued") {
        clearStop();
        showStop();
    } else if (event == "frame") {
        frameVariables[message.value("level", 0)] = readVariables(message);
    } else if (event == "expand") {
        tableFields[message.value("ref", 0)] = readVariables(message);
    }
}

// The process waits for the breakpoints before any script runs
void editor::ScriptDebugger::release() {
    released = true;
    for (const auto& [file, lines] : breakpoints) {
        sendBreakpoints(file);
    }
    send("continue");
}

// Checked frame by frame, until Lua stops or its time is over
void editor::ScriptDebugger::awaitPause(unsigned int started) {
    getEditorHost().enqueueMainThreadTask([this, started]() {
        const bool waiting = started == session && state == State::RUNNING;
        if (waiting && std::chrono::steady_clock::now() < pauseDeadline) {
            awaitPause(started);
            return;
        }
        if (started == pauseSession) {
            pauseSession = 0;
        }
        if (waiting) {
            // takes the pause of Lua back
            send("continue");
            native.pause();
        }
    });
}

// After a stop began or ended, of Lua or of the native debugger
void editor::ScriptDebugger::showStop() {
    if (!nativeStop && !luaStop) {
        state = State::RUNNING;
        return;
    }
    // the first frame of the project, under those of the engine or the system
    const std::vector<DebugFrame>& stack = getFrames();
    const auto own = std::find_if(stack.begin(), stack.end(), [this](const DebugFrame& frame) { return isProjectFrame(frame); });
    selectedFrame = own != stack.end() ? static_cast<int>(own - stack.begin()) : 0;
    stopCount++;
    state = State::STOPPED;
}

void editor::ScriptDebugger::clearStop() {
    luaStop = false;
    frames.clear();
    selectedFrame = 0;
    frameVariables.clear();
    tableFields.clear();
    requestedFrames.clear();
    requestedFields.clear();
}
