// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "util/CommandRunner.h"

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace doriax::editor {

    struct DebugFrame {
        int level = 0;
        std::string name;
        // path from the Lua folder
        std::string source;
        int line = 0;
    };

    struct DebugVariable {
        std::string name;
        std::string type;
        std::string value;
        // of a table, 0 when there are no fields to show
        int ref = 0;
    };

    // Runs a scene as a process of its own and debugs its Lua scripts.
    // Files are paths from the Lua folder and lines start at 1.
    class ScriptDebugger {
    public:
        enum class State { IDLE, BUILDING, RUNNING, STOPPED };

        ~ScriptDebugger();

        // build runs first, on the thread that then waits for the process
        void start(const std::string& command, const fs::path& workingDir, std::function<bool()> build);
        void stop();
        // true from stop() on, for a build that has to end as well
        bool isStopRequested() const;
        // stops, and returns once the build and the process are gone
        void waitForEnd();
        // for another project: ends as above, and forgets the breakpoints
        void reset();
        State getState() const;
        bool isActive() const;

        bool hasBreakpoints() const;
        void toggleBreakpoint(const std::string& file, int line);
        void setBreakpoints(const std::string& file, const std::set<int>& lines);
        const std::set<int>& getBreakpoints(const std::string& file) const;

        void resume();
        void stepOver();
        void stepInto();
        void stepOut();
        void pause();

        // Of the last stop
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
        void onLine(const std::string& line);
        void onMessage(const std::string& text);
        void clearStop();

        CommandRunner runner;
        std::thread worker;
        std::atomic<State> state{State::IDLE};
        // of the last start, so the end of an older one changes nothing
        unsigned int session = 0;

        std::map<std::string, std::set<int>> breakpoints;

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
