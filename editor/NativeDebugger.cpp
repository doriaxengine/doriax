// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NativeDebugger.h"

#include "EditorHost.h"
#include "Out.h"
#include "util/FileUtils.h"
#include "util/ShellEnv.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>

using namespace doriax;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

    const int MAX_FRAMES = 64;
    const char* const HEADER = "Content-Length:";

    std::string lastLine(std::string text) {
        text.erase(text.find_last_not_of(" \t\r\n") + 1);
        return text.substr(text.find_last_of('\n') + 1);
    }

    std::string adapterCommand(const std::string& name, const std::string& path) {
        if (name == "gdb") {
            // no symbols come from the network while a scene waits
            return "\"" + path + "\" -q -iex \"set debuginfod enabled off\" --interpreter=dap";
        }
        return "\"" + path + "\"";
    }

    std::vector<editor::DebugVariable> readVariables(const json& body) {
        std::vector<editor::DebugVariable> variables;
        for (const json& entry : body.value("variables", json::array())) {
            editor::DebugVariable variable;
            variable.name = entry.value("name", "");
            variable.type = entry.value("type", "");
            variable.value = entry.value("value", "");
            if (variable.value.empty()) {
                variable.value = variable.type;
            }
            variable.ref = entry.value("variablesReference", 0);
            variables.push_back(variable);
        }
        return variables;
    }

}

std::string editor::NativeDebugger::findAdapter(const std::string& cxxCompiler) {
    std::error_code ec;
    const fs::path compiler = FileUtils::pathFromUtf8(cxxCompiler);
#if defined(_WIN32) && !defined(__MINGW32__)
    // neither reads well what the Visual Studio compilers write
    std::string tool = compiler.stem().string();
    std::transform(tool.begin(), tool.end(), tool.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (tool.empty() || tool == "cl" || tool == "clang-cl") {
        return "";
    }
#endif
#ifdef _WIN32
    const std::string extension = ".exe";
#else
    const std::string extension;
#endif
#ifdef __APPLE__
    const char* const names[] = {"lldb-dap", "gdb"};
#else
    const char* const names[] = {"gdb", "lldb-dap"};
#endif
    for (const char* name : names) {
        // the one of the toolchain first, as with MinGW
        const fs::path beside = compiler.parent_path() / (name + extension);
        if (compiler.has_parent_path() && fs::is_regular_file(beside, ec)) {
            return adapterCommand(name, beside.string());
        }
        const std::string found = ShellEnv::findExecutable(name);
        if (!found.empty()) {
            return adapterCommand(name, found);
        }
    }
#ifdef __APPLE__
    // Xcode keeps it out of the PATH
    const std::string xcode = lastLine(ShellEnv::runCapture("xcrun -f lldb-dap 2>/dev/null", 3000));
    if (!xcode.empty() && fs::is_regular_file(xcode, ec)) {
        return adapterCommand("lldb-dap", xcode);
    }
#endif
    return "";
}

std::string editor::NativeDebugger::sourceName(const fs::path& file) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    return FileUtils::pathToGenericUtf8(ec ? file : canonical);
}

editor::NativeDebugger::~NativeDebugger() {
    end();
}

void editor::NativeDebugger::attach(const std::string& adapter, int pid, const std::map<std::string, std::set<int>>& files, std::function<void()> ready) {
    end();
    this->pid = pid;
    this->ready = std::move(ready);
    breakpoints = files;
    state = State::ATTACHING;
    processRunning = true;
    runner.setInputPipe(true);
    runner.setRawOutput(true);
    runner.resetCancel();
    // the debugger waits for it to say anything
    runner.setFirstInput(pack("initialize", {{"adapterID", "doriax"}, {"linesStartAt1", true}, {"columnsStartAt1", true},
                                             {"pathFormat", "path"}, {"supportsVariableType", true}}, Request::INITIALIZE));
    worker = std::thread([this, adapter, started = session]() {
        received.clear();
        runner.run(adapter, fs::path(), [this, started](const std::string& chunk) { onOutput(chunk, started); });
        processRunning = false;
        getEditorHost().enqueueMainThreadTask([this, started, rest = received]() {
            if (started != session) {
                return;
            }
            noise += rest;
            const bool stopped = state == State::STOPPED;
            fail("");
            detach();
            if (stopped && onContinued) {
                onContinued();
            }
        });
    });
}

void editor::NativeDebugger::detach() {
    if (state != State::IDLE) {
        send("disconnect", {{"terminateDebuggee", false}});
    }
    session++;
    ready = nullptr;
    clearStop();
    state = State::IDLE;
}

void editor::NativeDebugger::end() {
    detach();
    // it ends by itself, in no time
    for (int i = 0; i < 30 && processRunning; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    runner.cancel();
    if (worker.joinable()) {
        worker.join();
    }
    pending.clear();
    noise.clear();
    attachAnswered = false;
    configurationAnswered = false;
    configurable = false;
    stopThread = 0;
}

bool editor::NativeDebugger::isAttached() const {
    return state == State::RUNNING || state == State::STOPPED;
}

void editor::NativeDebugger::setBreakpoints(const std::string& file, const std::set<int>& lines) {
    if (lines.empty()) {
        breakpoints.erase(file);
    } else {
        breakpoints[file] = lines;
    }
    if (configurable) {
        sendBreakpoints(file, lines);
    }
}

void editor::NativeDebugger::resume() {
    step("continue");
}

void editor::NativeDebugger::stepOver() {
    step("next");
}

void editor::NativeDebugger::stepInto() {
    step("stepIn");
}

void editor::NativeDebugger::stepOut() {
    step("stepOut");
}

void editor::NativeDebugger::pause() {
    if (state == State::RUNNING) {
        send("pause", {{"threadId", stopThread}});
    }
}

const std::string& editor::NativeDebugger::getStopReason() const {
    return stopReason;
}

const std::string& editor::NativeDebugger::getStopMessage() const {
    return stopMessage;
}

const std::vector<editor::DebugFrame>& editor::NativeDebugger::getFrames() const {
    return frames;
}

int editor::NativeDebugger::getSourceFrame() const {
    std::error_code ec;
    for (size_t i = 0; i < frames.size(); i++) {
        if (fs::is_regular_file(FileUtils::pathFromUtf8(frames[i].source), ec)) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

const std::vector<editor::DebugVariable>* editor::NativeDebugger::getVariables(int frame) {
    if (state != State::STOPPED || frame < 0 || frame >= static_cast<int>(frameIds.size())) {
        return nullptr;
    }
    const auto [it, added] = frameVariables.try_emplace(frame);
    if (added) {
        send("scopes", {{"frameId", frameIds[frame]}}, Request::SCOPES, frame);
    }
    return it->second.waiting == 0 ? &it->second.list : nullptr;
}

const std::vector<editor::DebugVariable>* editor::NativeDebugger::getFields(int ref) {
    if (state != State::STOPPED) {
        return nullptr;
    }
    if (requestedFields.insert(ref).second) {
        send("variables", {{"variablesReference", ref}}, Request::FIELDS, ref);
    }
    const auto it = fields.find(ref);
    return it != fields.end() ? &it->second : nullptr;
}

std::string editor::NativeDebugger::pack(const std::string& command, const json& arguments, Request request, int key) {
    const int seq = ++sequence;
    if (request != Request::OTHER) {
        pending[seq] = {request, stopSerial, key};
    }
    const std::string body = json({{"seq", seq}, {"type", "request"}, {"command", command}, {"arguments", arguments}}).dump();
    return std::string(HEADER) + " " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

void editor::NativeDebugger::send(const std::string& command, const json& arguments, Request request, int key) {
    runner.write(pack(command, arguments, request, key));
}

void editor::NativeDebugger::sendBreakpoints(const std::string& file, const std::set<int>& lines) {
    json places = json::array();
    for (int line : lines) {
        places.push_back({{"line", line}});
    }
    send("setBreakpoints", {{"source", {{"path", file}}}, {"breakpoints", places}});
}

void editor::NativeDebugger::step(const char* command) {
    if (state == State::STOPPED) {
        send(command, {{"threadId", stopThread}});
        setRunning();
    }
}

// On the thread that waits for the debugger
void editor::NativeDebugger::onOutput(const std::string& chunk, unsigned int started) {
    received += chunk;
    while (true) {
        const size_t header = received.find(HEADER);
        const size_t body = header == std::string::npos ? header : received.find("\r\n\r\n", header);
        if (body == std::string::npos) {
            return;
        }
        const size_t end = body + 4 + std::strtoul(received.c_str() + header + std::strlen(HEADER), nullptr, 10);
        if (received.size() < end) {
            return;
        }
        getEditorHost().enqueueMainThreadTask([this, started, before = received.substr(0, header), text = received.substr(body + 4, end - body - 4)]() {
            // a message may wait in the queue past the end of its session
            if (started == session) {
                noise += before;
                onMessage(json::parse(text, nullptr, false));
            }
        });
        received.erase(0, end);
    }
}

void editor::NativeDebugger::onMessage(const json& message) {
    if (!message.is_object()) {
        return;
    }
    const std::string type = message.value("type", "");
    const std::string event = message.value("event", "");
    if (type == "response") {
        onResponse(message);
    } else if (event == "initialized") {
        configurable = true;
        for (const auto& [file, lines] : breakpoints) {
            sendBreakpoints(file, lines);
        }
        send("configurationDone", json::object(), Request::CONFIGURATION);
    } else if (event == "stopped") {
        onStop(message.value("body", json::object()));
    } else if (event == "continued" && state == State::STOPPED) {
        setRunning();
    }
}

void editor::NativeDebugger::onResponse(const json& message) {
    const auto it = pending.find(message.value("request_seq", 0));
    if (it == pending.end()) {
        return;
    }
    const Pending asked = it->second;
    pending.erase(it);
    const bool success = message.value("success", false);
    const json body = message.value("body", json::object());
    // the answer of a stop that is over
    if (asked.request >= Request::STACK && asked.stop != stopSerial) {
        return;
    }

    switch (asked.request) {
        case Request::INITIALIZE:
        case Request::ATTACH:
            if (!success) {
                fail(message.value("message", ""));
            } else if (asked.request == Request::INITIALIZE) {
                send("attach", {{"pid", pid}}, Request::ATTACH);
            } else {
                attachAnswered = true;
                checkReady();
            }
            break;
        case Request::CONFIGURATION:
            configurationAnswered = true;
            checkReady();
            break;
        case Request::STACK:
            for (const json& entry : body.value("stackFrames", json::array())) {
                const json source = entry.value("source", json::object());
                const fs::path file = FileUtils::pathFromUtf8(source.value("path", ""));
                DebugFrame frame;
                frame.level = static_cast<int>(frames.size());
                frame.name = entry.value("name", "");
                frame.source = file.is_absolute() ? sourceName(file) : FileUtils::pathToGenericUtf8(file);
                frame.line = entry.value("line", 0);
                frames.push_back(frame);
                frameIds.push_back(entry.value("id", 0));
            }
            state = State::STOPPED;
            if (onStopped) {
                onStopped();
            }
            break;
        case Request::SIGNAL: {
            // "It stopped with signal SIGSEGV, Segmentation fault."
            const std::string result = body.value("result", "");
            const size_t signal = result.find("SIG");
            if (signal != std::string::npos) {
                stopMessage = result.substr(signal, result.find_first_of(".\r\n", signal) - signal);
            }
            break;
        }
        case Request::SCOPES: {
            FrameVariables& variables = frameVariables[asked.key];
            variables.waiting = 0;
            for (const json& scope : body.value("scopes", json::array())) {
                // not the globals nor the registers
                const std::string hint = scope.value("presentationHint", "");
                if (hint == "arguments" || hint == "locals") {
                    variables.waiting++;
                    send("variables", {{"variablesReference", scope.value("variablesReference", 0)}}, Request::VARIABLES, asked.key);
                }
            }
            break;
        }
        case Request::VARIABLES: {
            FrameVariables& variables = frameVariables[asked.key];
            const std::vector<DebugVariable> scope = readVariables(body);
            variables.list.insert(variables.list.end(), scope.begin(), scope.end());
            variables.waiting--;
            break;
        }
        case Request::FIELDS: {
            const std::vector<DebugVariable> variables = readVariables(body);
            // gdb puts what a pointer points to under one more level
            if (variables.size() == 1 && variables[0].name == "value" && variables[0].ref) {
                send("variables", {{"variablesReference", variables[0].ref}}, Request::FIELDS, asked.key);
            } else {
                fields[asked.key] = variables;
            }
            break;
        }
        default:
            break;
    }
}

void editor::NativeDebugger::onStop(const json& body) {
    stopThread = body.value("threadId", stopThread);
    const std::string reason = body.value("reason", "");
    // gdb stops the process it attaches to
    if (state == State::ATTACHING || reason == "attach" || reason == "entry") {
        send("continue", {{"threadId", stopThread}});
        return;
    }
    clearStop();
    stopSerial++;
    stopReason = reason;
    if (reason == "signal" || reason == "exception") {
        stopReason = "error";
        stopMessage = body.value("description", body.value("text", ""));
        if (stopMessage.empty()) {
            // gdb only names the signal when asked
            stopMessage = "stopped by a signal";
            send("evaluate", {{"expression", "info program"}, {"context", "repl"}}, Request::SIGNAL);
        }
    }
    send("stackTrace", {{"threadId", stopThread}, {"levels", MAX_FRAMES}}, Request::STACK);
}

void editor::NativeDebugger::checkReady() {
    if (state == State::ATTACHING && attachAnswered && configurationAnswered) {
        state = State::RUNNING;
        // it may attach again, which replaces ready
        const std::function<void()> call = std::move(ready);
        call();
    }
}

void editor::NativeDebugger::fail(const std::string& reason) {
    if (state != State::ATTACHING) {
        return;
    }
    const std::string detail = reason.empty() ? lastLine(noise) : reason;
    Out::warning("C++ breakpoints are off, gdb (14 or newer) or lldb-dap did not attach%s", detail.empty() ? "" : (": " + detail).c_str());
    const std::function<void()> call = std::move(ready);
    detach();
    call();
}

void editor::NativeDebugger::setRunning() {
    clearStop();
    stopSerial++;
    state = State::RUNNING;
    if (onContinued) {
        onContinued();
    }
}

void editor::NativeDebugger::clearStop() {
    stopReason.clear();
    stopMessage.clear();
    frames.clear();
    frameIds.clear();
    frameVariables.clear();
    fields.clear();
    requestedFields.clear();
}
