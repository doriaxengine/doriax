// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "NativeDebugger.h"
#include "util/CommandRunner.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace doriax::editor {

    // Runs a scene as a process of its own and debugs its scripts, lines start at 1
    class ScriptDebugger {
    public:
        enum class State { IDLE, BUILDING, RUNNING, STOPPED };

        ~ScriptDebugger();

        // build runs first, nativeAdapter is the command of gdb or lldb for C++
        void start(const std::string& command, const fs::path& workingDir, std::function<bool()> build, const std::string& nativeAdapter = "");
        void stop();
        // true from stop() on, for a build that has to end as well
        bool isStopRequested() const;
        // stops, and returns once the build and the process are gone
        void waitForEnd();
        // for another project: ends as above, and forgets the breakpoints
        void reset();
        State getState() const;
        bool isActive() const;

        // a Lua file is a path from the Lua folder, a native one (C++) a whole path
        bool hasBreakpoints() const;
        bool hasNativeBreakpoints() const;
        // forgets the native ones of files that are gone or no longer C++
        void dropStaleBreakpoints();
        void setBreakpoints(const std::string& file, const std::set<int>& lines, bool native = false);
        const std::set<int>& getBreakpoints(const std::string& file, bool native = false) const;
        // of a renamed file, or of the files in a renamed folder
        void renameBreakpoints(const std::string& from, const std::string& to, bool native = false);

        void resume();
        void stepOver();
        void stepInto();
        void stepOut();
        void pause();
        // gdb or lldb is on the process too
        bool isNativeAttached() const;
        bool isNativeAttaching() const;
        // true while a run goes on with no debugger for the C++ breakpoints
        bool isNativeOff() const;

        // Of the last stop
        bool isNativeStop() const;
        // false for a C++ frame of the engine or of the system
        bool isProjectFrame(const DebugFrame& frame) const;
        const std::string& getStopReason() const;
        const std::string& getStopMessage() const;
        // counts the stops, for a view to follow each one
        unsigned int getStopCount() const;
        const std::vector<DebugFrame>& getFrames() const;
        int getSelectedFrame() const;
        void selectFrame(int index);
        // nullptr until the process answers
        const std::vector<DebugVariable>* getVariables();
        const std::vector<DebugVariable>* getFields(int ref);

    private:
        void send(const std::string& command, const std::string& extra = "");
        void sendBreakpoints(const std::string& file);
        void release();
        void awaitPause(unsigned int started);
        void showStop();
        void onLine(const std::string& line, unsigned int started);
        void onMessage(const std::string& text);
        void clearStop();

        CommandRunner runner;
        std::thread worker;
        std::atomic<State> state{State::IDLE};
        // of the last start, so the end of an older one changes nothing
        unsigned int session = 0;

        std::map<std::string, std::set<int>> breakpoints;
        std::map<std::string, std::set<int>> nativeBreakpoints;

        // gdb or lldb on the same process, for the C++ scripts
        NativeDebugger native;
        std::string nativeAdapter;
        // a stop of it hides the one of Lua
        bool nativeStop = false;
        bool luaStop = false;
        // from when the scripts of the process run
        bool released = false;
        // the project folder as the frames of C++ files start
        std::string sourceRoot;
        bool pausePending = false;
        std::chrono::steady_clock::time_point pauseDeadline;

        std::string stopReason;
        std::string stopMessage;
        unsigned int stopCount = 0;
        std::vector<DebugFrame> frames;
        int selectedFrame = 0;
        // answers of the process, and what was asked for already
        std::map<int, std::vector<DebugVariable>> frameVariables;
        std::map<int, std::vector<DebugVariable>> tableFields;
        std::set<int> requestedFrames;
        std::set<int> requestedFields;
    };

}
