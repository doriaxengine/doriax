// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LuaDebugger.h"

#ifdef DORIAX_EDITOR

#include "System.h"

#include "lua.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace doriax;
using json = nlohmann::json;

namespace {

    enum class Step { NONE, INTO, OVER, OUT };

    const size_t MAX_TEXT = 200;
    const size_t MAX_FIELDS = 500;

    bool attached = false;

    // commands of the editor, read by a thread of their own
    struct Inbox {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::string> lines;
        std::atomic<bool> pending{false};
    };
    // never freed, the reading thread outlives main
    Inbox& inbox = *new Inbox;

    std::map<std::string, std::set<int>> breakpoints;
    // lines with a breakpoint in any file, checked on every line
    std::vector<bool> breakLines;

    Step step = Step::NONE;
    lua_State* stepThread = nullptr;
    int stepDepth = 0;
    bool pauseRequested = false;
    bool stopped = false;

    // values the editor can expand while stopped
    int refsTable = LUA_NOREF;
    int refCount = 0;

    void send(const json& message) {
        // ASCII only, so no code page of a console changes it
        const std::string line = LuaDebugger::MESSAGE_MARK + message.dump(-1, ' ', true, json::error_handler_t::replace) + "\n";
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    }

    // a chunk name as a path from the Lua folder
    std::string sourceName(const char* source) {
        std::string name = source;
        if (!name.empty() && (name[0] == '@' || name[0] == '=')) {
            name.erase(0, 1);
        }
        if (name.rfind("lua://", 0) == 0) {
            name.erase(0, 6);
        }
        std::replace(name.begin(), name.end(), '\\', '/');
        return name;
    }

    int stackDepth(lua_State* L) {
        lua_Debug ar;
        int depth = 0;
        while (lua_getstack(L, depth, &ar)) {
            depth++;
        }
        return depth;
    }

    int toText(lua_State* L) {
        luaL_tolstring(L, 1, nullptr);
        return 1;
    }

    // protected, a __tostring may raise an error
    std::string text(lua_State* L, int index) {
        index = lua_absindex(L, index);
        lua_pushcfunction(L, toText);
        lua_pushvalue(L, index);
        std::string result = luaL_typename(L, index);
        if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
            result = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        return result;
    }

    json describe(lua_State* L, int index, const std::string& name) {
        index = lua_absindex(L, index);
        json variable = {{"name", name}, {"type", luaL_typename(L, index)}};
        if (lua_type(L, index) == LUA_TSTRING) {
            size_t length = 0;
            const char* value = lua_tolstring(L, index, &length);
            variable["value"] = "\"" + std::string(value, std::min(length, MAX_TEXT)) + (length > MAX_TEXT ? "...\"" : "\"");
        } else if (lua_type(L, index) == LUA_TTABLE) {
            variable["value"] = "table";
            lua_rawgeti(L, LUA_REGISTRYINDEX, refsTable);
            lua_pushvalue(L, index);
            lua_rawseti(L, -2, ++refCount);
            lua_pop(L, 1);
            variable["ref"] = refCount;
        } else {
            variable["value"] = text(L, index);
        }
        return variable;
    }

    json stackFrames(lua_State* L, int firstLevel) {
        json frames = json::array();
        lua_Debug ar;
        for (int level = firstLevel; lua_getstack(L, level, &ar); level++) {
            lua_getinfo(L, "nSl", &ar);
            if (*ar.what == 'C') {
                continue;
            }
            // the engine calls a function without a name for it
            std::string name = *ar.what == 'm' ? "main chunk" : "function <" + std::to_string(ar.linedefined) + ">";
            if (ar.name) {
                name = ar.name;
            }
            frames.push_back({{"level", level}, {"name", name}, {"source", sourceName(ar.source)}, {"line", ar.currentline}});
        }
        return frames;
    }

    // locals and upvalues of a frame, the innermost of a repeated name
    json frameVariables(lua_State* L, int level) {
        json variables = json::array();
        lua_Debug ar;
        if (!lua_getstack(L, level, &ar)) {
            return variables;
        }
        const auto add = [&](const char* name) {
            json variable = describe(L, -1, name);
            for (json& other : variables) {
                if (other["name"] == name) {
                    other = variable;
                    return;
                }
            }
            variables.push_back(variable);
        };
        const char* name = nullptr;
        for (int i = 1; (name = lua_getlocal(L, &ar, i)); i++) {
            // "(temporary)" and the like are not variables
            if (*name != '(') {
                add(name);
            }
            lua_pop(L, 1);
        }
        lua_getinfo(L, "f", &ar);
        for (int i = 1; (name = lua_getupvalue(L, -1, i)); i++) {
            if (*name && std::strcmp(name, "_ENV") != 0) {
                add(name);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return variables;
    }

    json tableFields(lua_State* L, int ref) {
        json fields = json::array();
        const int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, refsTable);
        lua_rawgeti(L, -1, ref);
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (fields.size() < MAX_FIELDS && lua_next(L, -2)) {
                // not lua_tolstring on the key, it would break lua_next
                const std::string key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "[" + text(L, -2) + "]";
                fields.push_back(describe(L, -1, key));
                lua_pop(L, 1);
            }
        }
        lua_settop(L, top);
        return fields;
    }

    void setBreakpoints(const json& message) {
        std::set<int> lines;
        for (const json& line : message.value("lines", json::array())) {
            if (line.is_number_integer() && line.get<int>() > 0) {
                lines.insert(line.get<int>());
            }
        }
        const std::string file = message.value("file", "");
        if (lines.empty()) {
            breakpoints.erase(file);
        } else {
            breakpoints[file] = lines;
        }
        breakLines.clear();
        for (const auto& [name, fileLines] : breakpoints) {
            breakLines.resize(std::max(breakLines.size(), static_cast<size_t>(*fileLines.rbegin()) + 1));
            for (int line : fileLines) {
                breakLines[line] = true;
            }
        }
    }

    // true when the command lets the script go on
    bool handle(lua_State* L, const std::string& line) {
        const json message = json::parse(line, nullptr, false);
        if (!message.is_object()) {
            return false;
        }
        const std::string command = message.value("command", "");
        if (command == "breakpoints") {
            setBreakpoints(message);
        } else if (!stopped) {
            pauseRequested = pauseRequested || command == "pause";
        } else if (command == "continue") {
            return true;
        } else if (command == "stepInto" || command == "stepOver" || command == "stepOut") {
            step = command == "stepInto" ? Step::INTO : (command == "stepOver" ? Step::OVER : Step::OUT);
            stepThread = L;
            stepDepth = stackDepth(L);
            return true;
        } else if (command == "frame") {
            const int level = message.value("level", 0);
            send({{"event", "frame"}, {"level", level}, {"variables", frameVariables(L, level)}});
        } else if (command == "expand") {
            const int ref = message.value("ref", 0);
            send({{"event", "expand"}, {"ref", ref}, {"variables", tableFields(L, ref)}});
        }
        return false;
    }

    bool takeCommand(std::string& line, bool wait) {
        std::unique_lock<std::mutex> lock(inbox.mutex);
        if (wait) {
            inbox.ready.wait(lock, []() { return !inbox.lines.empty(); });
        }
        if (inbox.lines.empty()) {
            inbox.pending = false;
            return false;
        }
        line = inbox.lines.front();
        inbox.lines.pop_front();
        return true;
    }

    void waitForResume(lua_State* L) {
        stopped = true;
        std::string line;
        do {
            takeCommand(line, true);
        } while (!handle(L, line));
        stopped = false;
    }

    void pause(lua_State* L, const char* reason, int firstLevel, const char* message = nullptr) {
        if (!lua_checkstack(L, LUA_MINSTACK)) {
            return;
        }
        step = Step::NONE;
        pauseRequested = false;
        lua_newtable(L);
        refsTable = luaL_ref(L, LUA_REGISTRYINDEX);
        refCount = 0;

        json event = {{"event", "stopped"}, {"reason", reason}, {"frames", stackFrames(L, firstLevel)}};
        if (message) {
            event["message"] = message;
        }
        send(event);
        waitForResume(L);

        luaL_unref(L, LUA_REGISTRYINDEX, refsTable);
        refsTable = LUA_NOREF;
        send({{"event", "continued"}});
    }

    void hook(lua_State* L, lua_Debug* ar) {
        // a __tostring shown while stopped runs lines too
        if (stopped) {
            return;
        }
        if (inbox.pending) {
            std::string line;
            while (takeCommand(line, false)) {
                handle(L, line);
            }
        }

        const char* reason = nullptr;
        if (pauseRequested) {
            reason = "pause";
        } else if (step == Step::INTO) {
            reason = "step";
        } else if (step != Step::NONE && L == stepThread) {
            const int depth = stackDepth(L);
            if (step == Step::OVER ? depth <= stepDepth : depth < stepDepth) {
                reason = "step";
            }
        }
        const int line = ar->currentline;
        if (!reason && line > 0 && static_cast<size_t>(line) < breakLines.size() && breakLines[line]) {
            lua_getinfo(L, "S", ar);
            const auto it = breakpoints.find(sourceName(ar->source));
            if (it != breakpoints.end() && it->second.count(line)) {
                reason = "breakpoint";
            }
        }
        if (reason) {
            pause(L, reason, 0);
        }
    }

    void readCommands() {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> lock(inbox.mutex);
            inbox.lines.push_back(line);
            inbox.pending = true;
            inbox.ready.notify_one();
        }
        // the editor is gone, even if it had no time to stop this process
        std::_Exit(0);
    }

}

void LuaDebugger::attach(lua_State* L) {
    const std::vector<std::string>& args = System::instance().args;
    if (std::find(args.begin(), args.end(), ARGUMENT) == args.end()) {
        return;
    }
    // a line hook from the start, so coroutines get it too
    lua_sethook(L, hook, LUA_MASKLINE, 0);
    if (attached) {
        return;
    }
    attached = true;
    // a pipe would hold the output back otherwise
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::thread(readCommands).detach();
    // the editor sends its breakpoints before any script runs
    send({{"event", "attached"}});
    waitForResume(L);
}

void LuaDebugger::onError(lua_State* L, const char* message) {
    if (attached && !stopped) {
        // level 0 is the message handler
        pause(L, "error", 1, message);
    }
}

#endif
