// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace doriax::editor {

    enum class ScriptEventSource {
        Engine,
        Input, // engine events too, in their own menu
        UI,
        Button,
        Scrollbar,
        Panel,
        TextEdit,
        Action,
        Sound,
        Physics2D,
        Physics3D,
        AdMob,
        InAppPurchase,
        WebPortal
    };

    struct ScriptEventParam {
        const char* type; // engine types without the doriax namespace
        const char* name;
    };

    struct ScriptEvent {
        ScriptEventSource source;
        const char* name;
        const char* group;
        const char* description;
        std::vector<ScriptEventParam> params;
        bool returnsBool = false;
        const char* luaName = nullptr; // when Lua names it differently
    };

    struct ScriptEventSourceInfo {
        ScriptEventSource source;
        const char* label;
        const char* icon;
        const char* include;
        const char* component; // empty for engine, physics and class events
        const char* registerMacro;
        const char* luaObject;
        const char* eventClass = ""; // class holding static events, like AdMob::onAdLoaded
    };

    struct ScriptEventScan {
        std::vector<bool> registered; // by event index
        std::string error;
    };

    struct ScriptEventChange {
        std::vector<std::string> texts;
        size_t cursorDocument = 0;
        size_t cursorOffset = 0;
        std::string error;
    };

    class ScriptEvents {
    public:
        static const std::vector<ScriptEvent>& getEvents();
        static const std::vector<ScriptEventSourceInfo>& getSources();

        // "(x, y)", empty without parameters
        static std::string getParameterNames(const ScriptEvent& event);
        // the Lua name of an event, and the init line that registers it
        static std::string getLuaName(const ScriptEvent& event);
        static std::string getLuaRegistration(const ScriptEvent& event, const std::string& method);

        static ScriptEventScan scanLua(const std::string& text);
        static ScriptEventChange addLua(const std::string& text, size_t event);

        // The edited file first, then its header or source when there is one
        static ScriptEventScan scanCpp(const std::vector<std::string>& documents, const std::string& fileStem);
        static ScriptEventChange addCpp(const std::vector<std::string>& documents, const std::string& fileStem, size_t event);
    };

}
