// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NativeDebugger.h"

#include "EditorHost.h"
#include "Out.h"
#include "util/FileUtils.h"
#include "util/ShellEnv.h"
#include "util/Util.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

using namespace doriax;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

    const int MAX_FRAMES = 64;
    const char* const HEADER = "Content-Length:";

    // A field of another type, null included, reads as if it was not there
    std::string text(const json& from, const char* key) {
        const auto it = from.find(key);
        return it != from.end() && it->is_string() ? it->get<std::string>() : "";
    }

    int number(const json& from, const char* key) {
        const auto it = from.find(key);
        return it != from.end() && it->is_number_integer() ? it->get<int>() : 0;
    }

    json object(const json& from, const char* key) {
        const auto it = from.find(key);
        return it != from.end() && it->is_object() ? *it : json::object();
    }

    json list(const json& from, const char* key) {
        const auto it = from.find(key);
        return it != from.end() && it->is_array() ? *it : json::array();
    }

    std::string lastLine(std::string output) {
        output.erase(output.find_last_not_of(" \t\r\n") + 1);
        return output.substr(output.find_last_of('\n') + 1);
    }

    std::string adapterCommand(const std::string& name, const std::string& path) {
        if (name == "gdb") {
            // no file of the user, no symbols from the network, no stop at a closed socket
            return "\"" + path + "\" -q -nh -iex \"set debuginfod enabled off\" -iex \"handle SIGPIPE nostop noprint pass\" --interpreter=dap";
        }
        return "\"" + path + "\"";
    }

}

std::string editor::NativeDebugger::findAdapter(const std::string& cxxCompiler) {
#if defined(_WIN32) && !defined(__MINGW32__)
    // neither reads well what the kits of this editor write, all of the MSVC ABI
    (void)cxxCompiler;
    return "";
#else
    std::error_code ec;
    const fs::path compiler = FileUtils::pathFromUtf8(cxxCompiler);
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
    // the one of the toolchain first, as with MinGW
    for (const char* name : names) {
        const fs::path beside = compiler.parent_path() / (name + extension);
        if (compiler.has_parent_path() && fs::is_regular_file(beside, ec)) {
            return adapterCommand(name, beside.string());
        }
    }
    for (const char* name : names) {
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
#endif
}

std::string editor::NativeDebugger::sourceName(const fs::path& file) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    return FileUtils::pathToGenericUtf8(ec ? file : canonical);
}

bool editor::NativeDebugger::isSource(const fs::path& file) {
    const std::string extension = file.extension().string();
    return extension == ".c" || Util::isSourceFile(extension) || Util::isHeaderFile(extension);
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
    threadsAnswered = false;
    configurationAnswered = false;
    configurable = false;
    mainThread = 0;
    stopThread = 0;
}

bool editor::NativeDebugger::isAttaching() const {
    return state == State::ATTACHING;
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
        send("pause", {{"threadId", mainThread}});
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
        const size_t length = std::strtoul(received.c_str() + header + std::strlen(HEADER), nullptr, 10);
        if (received.size() - (body + 4) < length) {
            return;
        }
        const size_t end = body + 4 + length;
        getEditorHost().enqueueMainThreadTask([this, started, before = received.substr(0, header), content = received.substr(body + 4, length)]() {
            // a message may wait in the queue past the end of its session
            if (started == session) {
                noise += before;
                onMessage(json::parse(content, nullptr, false));
            }
        });
        received.erase(0, end);
    }
}

void editor::NativeDebugger::onMessage(const json& message) {
    if (!message.is_object()) {
        return;
    }
    const std::string event = text(message, "event");
    if (text(message, "type") == "response") {
        onResponse(message);
    } else if (event == "initialized") {
        configurable = true;
        for (const auto& [file, lines] : breakpoints) {
            sendBreakpoints(file, lines);
        }
        send("configurationDone", json::object(), Request::CONFIGURATION);
    } else if (event == "stopped") {
        onStop(object(message, "body"));
    } else if (event == "continued" && state == State::STOPPED) {
        setRunning();
    }
}

void editor::NativeDebugger::onResponse(const json& message) {
    const auto it = pending.find(number(message, "request_seq"));
    if (it == pending.end()) {
        return;
    }
    const Pending asked = it->second;
    pending.erase(it);
    const json body = object(message, "body");
    // the answer of a stop that is over
    if (asked.request >= Request::STACK && asked.stop != stopSerial) {
        return;
    }
    const auto success = message.find("success");
    if (asked.request <= Request::CONFIGURATION && (success == message.end() || *success != true)) {
        fail(text(message, "message"));
        return;
    }

    switch (asked.request) {
        case Request::INITIALIZE:
            send("attach", {{"pid", pid}}, Request::ATTACH);
            break;
        case Request::ATTACH:
            send("threads", json::object(), Request::THREADS);
            break;
        case Request::THREADS: {
            // gdb says nothing when the system refuses the attach
            const json threads = list(body, "threads");
            if (threads.empty()) {
                fail("the system refused it (ptrace)");
                break;
            }
            mainThread = number(threads[0], "id");
            threadsAnswered = true;
            checkReady();
            break;
        }
        case Request::CONFIGURATION:
            configurationAnswered = true;
            checkReady();
            break;
        case Request::STACK:
            for (const json& entry : list(body, "stackFrames")) {
                const fs::path file = FileUtils::pathFromUtf8(text(object(entry, "source"), "path"));
                DebugFrame frame;
                frame.level = static_cast<int>(frames.size());
                frame.name = text(entry, "name");
                frame.source = file.is_absolute() ? sourceName(file) : FileUtils::pathToGenericUtf8(file);
                frame.line = number(entry, "line");
                frames.push_back(frame);
                frameIds.push_back(number(entry, "id"));
            }
            state = State::STOPPED;
            if (onStopped) {
                onStopped();
            }
            break;
        case Request::SIGNAL: {
            // "It stopped with signal SIGSEGV, Segmentation fault."
            const std::string result = text(body, "result");
            const size_t signal = result.find("SIG");
            if (signal != std::string::npos) {
                stopMessage = result.substr(signal, result.find_first_of(".\r\n", signal) - signal);
            }
            break;
        }
        case Request::SCOPES: {
            FrameVariables& variables = frameVariables[asked.key];
            variables.waiting = 0;
            for (const json& scope : list(body, "scopes")) {
                // not the globals nor the registers, by name where no hint comes
                const std::string hint = text(scope, "presentationHint");
                const std::string name = hint.empty() ? text(scope, "name") : "";
                const int ref = number(scope, "variablesReference");
                if (ref > 0 && (hint == "arguments" || hint == "locals" || name == "Arguments" || name == "Locals")) {
                    variables.waiting++;
                    send("variables", {{"variablesReference", ref}}, Request::VARIABLES, asked.key);
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
            // gdb puts what a pointer points to under one more level, a "value" with none
            const json entries = list(body, "variables");
            const bool wrapped = entries.size() == 1 && text(entries[0], "name") == "value" && text(entries[0], "value").empty();
            const int pointee = wrapped && pointers.count(asked.key) ? number(entries[0], "variablesReference") : 0;
            if (pointee > 0) {
                send("variables", {{"variablesReference", pointee}}, Request::POINTEE, asked.key);
            } else {
                fields[asked.key] = readVariables(body);
            }
            break;
        }
        case Request::POINTEE:
            fields[asked.key] = readVariables(body);
            break;
        default:
            break;
    }
}

void editor::NativeDebugger::onStop(const json& body) {
    stopThread = body.contains("threadId") ? number(body, "threadId") : mainThread;
    const std::string reason = text(body, "reason");
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
        stopMessage = text(body, "description").empty() ? text(body, "text") : text(body, "description");
        if (stopMessage.empty()) {
            // gdb only names the signal when asked
            stopMessage = "stopped by a signal";
            send("evaluate", {{"expression", "info program"}, {"context", "repl"}}, Request::SIGNAL);
        }
    }
    send("stackTrace", {{"threadId", stopThread}, {"levels", MAX_FRAMES}}, Request::STACK);
}

std::vector<editor::DebugVariable> editor::NativeDebugger::readVariables(const json& body) {
    std::vector<DebugVariable> variables;
    for (const json& entry : list(body, "variables")) {
        DebugVariable variable;
        variable.name = text(entry, "name");
        variable.type = text(entry, "type");
        variable.value = text(entry, "value");
        if (variable.value.empty()) {
            variable.value = variable.type;
        }
        variable.ref = number(entry, "variablesReference");
        if (variable.ref && variable.type.find('*') != std::string::npos) {
            pointers.insert(variable.ref);
        }
        variables.push_back(variable);
    }
    return variables;
}

void editor::NativeDebugger::checkReady() {
    if (state == State::ATTACHING && threadsAnswered && configurationAnswered) {
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
    pointers.clear();
}
