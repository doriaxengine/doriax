// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LUA_FUNCTION_H
#define LUA_FUNCTION_H

// Based on: http://stackoverflow.com/questions/7885299/c-call-lua-function-with-variable-parameters

#include "LuaFunctionBase.h"
#include <string>
#include <type_traits>

typedef struct lua_State lua_State;

namespace doriax{

    // the function wrapper class
    template <typename Ret>
    class LuaFunction : public LuaFunctionBase {

    public:
        LuaFunction(lua_State *vm, const std::string &func)
        : LuaFunctionBase(vm, func){ }

        LuaFunction(lua_State *vm)
        : LuaFunctionBase(vm){ }

        template <typename... Args>
        Ret operator()(const Args&... args){
            if (!can_call(sizeof...(args)))
                return Ret();

            // push the function from the registry and its arguments
            push_function(m_vm, m_func);
            (push_value(m_vm, args), ...);

            if constexpr (std::is_void_v<Ret>) {
                call(sizeof...(args));
            } else {
                call(sizeof...(args), 1);
                return get_value<Ret>(m_vm);
            }
        }
    };

}

#endif // LUA_FUNCTION_H
