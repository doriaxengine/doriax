// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef luadebugger_h
#define luadebugger_h

#ifdef DORIAX_EDITOR

typedef struct lua_State lua_State;

namespace doriax {

    // Breakpoints and stepping for a project the editor runs. Messages are
    // JSON lines on the standard streams, the ones to the editor after a mark.
    class LuaDebugger {
    public:
        // the argument the editor runs the project with
        static constexpr const char* ARGUMENT = "--lua-debug";
        // starts a message in the output, apart from what scripts print
        static constexpr char MESSAGE_MARK = '\x01';

        // does nothing unless the editor asked for it
        static void attach(lua_State* L);
        // stops where the error is raised, before the stack unwinds
        static void onError(lua_State* L, const char* message);
    };

}

#endif

#endif
