// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "util/CommandRunner.h"

#include "json.hpp"

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
        // path from the Lua folder, or the name of a C++ file
        std::string source;
        int line = 0;
    };

    struct DebugVariable {
        std::string name;
        std::string type;
        std::string value;
        // of a value with fields, 0 when there are none to show
        int ref = 0;
    };

    // Attaches gdb or lldb to the process of a scene, for its C++ scripts
    class NativeDebugger {
    public:
        // Command of the debugger for a compiler, empty when there is none
        static std::string findAdapter(const std::string& cxxCompiler);
        // The name of a file in breakpoints and frames
        static std::string sourceName(const fs::path& file);

        ~NativeDebugger();

        // ready is called once attached, or once that failed
        void attach(const std::string& adapter, int pid, const std::map<std::string, std::set<int>>& files, std::function<void()> ready);
        // lets the process go, the debugger then ends by itself
        void detach();
        void end();
        bool isAttached() const;

        // lines start at 1
        void setBreakpoints(const std::string& file, const std::set<int>& lines);

        void resume();
        void stepOver();
        void stepInto();
        void stepOut();
        void pause();

        std::function<void()> onStopped;
        std::function<void()> onContinued;

        // Of the last stop
        const std::string& getStopReason() const;
        const std::string& getStopMessage() const;
        const std::vector<DebugFrame>& getFrames() const;
        // the first one with a file to show
        int getSourceFrame() const;
        // nullptr until the debugger answers
        const std::vector<DebugVariable>* getVariables(int frame);
        const std::vector<DebugVariable>* getFields(int ref);

    private:
        enum class State { IDLE, ATTACHING, RUNNING, STOPPED };
        enum class Request { OTHER, INITIALIZE, ATTACH, CONFIGURATION, STACK, SIGNAL, SCOPES, VARIABLES, FIELDS };

        struct Pending {
            Request request;
            // the stop it asks about
            unsigned int stop;
            int key;
        };

        struct FrameVariables {
            // scopes still to answer
            int waiting = 1;
            std::vector<DebugVariable> list;
        };

        std::string pack(const std::string& command, const nlohmann::json& arguments, Request request = Request::OTHER, int key = 0);
        void send(const std::string& command, const nlohmann::json& arguments, Request request = Request::OTHER, int key = 0);
        void sendBreakpoints(const std::string& file, const std::set<int>& lines);
        void step(const char* command);
        void onOutput(const std::string& chunk, unsigned int started);
        void onMessage(const nlohmann::json& message);
        void onResponse(const nlohmann::json& message);
        void onStop(const nlohmann::json& body);
        void checkReady();
        void fail(const std::string& reason);
        void setRunning();
        void clearStop();

        CommandRunner runner;
        std::thread worker;
        std::atomic<bool> processRunning{false};
        // of the last attach, so the end of an older one changes nothing
        unsigned int session = 0;
        State state = State::IDLE;

        // only the thread that reads the debugger uses it
        std::string received;
        // what the debugger wrote that is no message
        std::string noise;

        int pid = 0;
        std::function<void()> ready;
        bool attachAnswered = false;
        bool configurationAnswered = false;
        // from when the debugger takes breakpoints
        bool configurable = false;
        std::map<std::string, std::set<int>> breakpoints;

        int sequence = 0;
        std::map<int, Pending> pending;

        int stopThread = 0;
        unsigned int stopSerial = 0;
        std::string stopReason;
        std::string stopMessage;
        std::vector<DebugFrame> frames;
        std::vector<int> frameIds;
        std::map<int, FrameVariables> frameVariables;
        std::map<int, std::vector<DebugVariable>> fields;
        std::set<int> requestedFields;
    };

}
