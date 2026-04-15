//
// (c) 2026 Eduardo Doria.
//

#ifndef LUA_FUNCTION_H
#define LUA_FUNCTION_H

// Based on: http://stackoverflow.com/questions/7885299/c-call-lua-function-with-variable-parameters

#include "LuaFunctionBase.h"
#include <string>

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

        Ret operator()() {
            // push the function from the registry
            push_function(m_vm, m_func);
            // call the function on top of the stack (throws exception on error)
            call(0, 1);
            // return the value
            return get_value<Ret>(m_vm);
        }

        template <typename T1>
        Ret operator()(const T1 &p1){
            push_function(m_vm, m_func);
            // push the argument and call with 1 arg
            push_value(m_vm, p1);
            call(1, 1);
            return get_value<Ret>(m_vm);
        }

        template <typename T1, typename T2>
        Ret operator()(const T1 &p1, const T2 &p2){
            push_function(m_vm, m_func);
            // push the arguments and call with 2 args
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            call(2, 1);
            return get_value<Ret>(m_vm);
        }

        template <typename T1, typename T2, typename T3>
        Ret operator()(const T1 &p1, const T2 &p2, const T3 &p3){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            call(3, 1);
            return get_value<Ret>(m_vm);
        }

        template <typename T1, typename T2, typename T3, typename T4>
        Ret operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            call(4, 1);
            return get_value<Ret>(m_vm);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5>
        Ret operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            call(5, 1);
            return get_value<Ret>(m_vm);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
        Ret operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5, const T6 &p6){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            push_value(m_vm, p6);
            call(6, 1);
            return get_value<Ret>(m_vm);
        }

        // et cetera, provide as many overloads as you need
    };

    // we need to specialize the function for void return type
    // as the other class would fail to compile with void as return type
    template <>
    class LuaFunction<void> : public LuaFunctionBase {

    public:
        LuaFunction(lua_State *vm, const std::string &func)
        : LuaFunctionBase(vm, func) { }

        LuaFunction(lua_State *vm)
        : LuaFunctionBase(vm) { }

        void operator()(){
            push_function(m_vm, m_func);
            call(0);
        }

        template <typename T1>
        void operator()(const T1 &p1){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            call(1);
        }

        template <typename T1, typename T2>
        void operator()(const T1 &p1, const T2 &p2){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            call(2);
        }

        template <typename T1, typename T2, typename T3>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            call(3);
        }

        template <typename T1, typename T2, typename T3, typename T4>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            call(4);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            call(5);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5, const T6 &p6){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            push_value(m_vm, p6);
            call(6);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5, const T6 &p6, const T7 &p7){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            push_value(m_vm, p6);
            push_value(m_vm, p7);
            call(7);
        }

        template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
        void operator()(const T1 &p1, const T2 &p2, const T3 &p3, const T4 &p4, const T5 &p5, const T6 &p6, const T7 &p7, const T8 &p8){
            push_function(m_vm, m_func);
            push_value(m_vm, p1);
            push_value(m_vm, p2);
            push_value(m_vm, p3);
            push_value(m_vm, p4);
            push_value(m_vm, p5);
            push_value(m_vm, p6);
            push_value(m_vm, p7);
            push_value(m_vm, p8);
            call(8);
        }

        // et cetera, provide as many overloads as you need
    };

}

#endif // LUA_FUNCTION_H