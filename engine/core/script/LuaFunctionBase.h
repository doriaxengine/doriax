//
// (c) 2026 Eduardo Doria.
//

#ifndef LUA_FUNCTIONBASE_H
#define LUA_FUNCTIONBASE_H

#include <string>
#include "math/Vector3.h"

typedef struct lua_State lua_State;

namespace doriax{

    class Body2D;
    class Manifold2D;
    class Contact2D;
    class Body3D;
    class Contact3D;
    class CollideShapeResult3D;

    // the base function wrapper class
    class LuaFunctionBase{

    private:

        void store_function();

    protected:
        // the virtual machine and the registry reference to the function
        lua_State *m_vm;
        int m_func;

        // push the function from the registry
        void push_function(lua_State *vm, int func);

        // call the function, throws an exception on error
        void call(int args, int results = 0);

        // we overload push_value instead of specializing
        // because this way we can also push values that
        // are implicitly convertible to one of the types

        void push_value(lua_State *vm, int n);
        void push_value(lua_State *vm, unsigned int n);
        void push_value(lua_State *vm, float n);
        void push_value(lua_State *vm, long n);
        void push_value(lua_State *vm, unsigned long n);
        void push_value(lua_State *vm, bool b);
        void push_value(lua_State *vm, const std::string &s);
        void push_value(lua_State *vm, wchar_t s);
        void push_value(lua_State *vm, Vector2 o);
        void push_value(lua_State *vm, Vector3 o);
        void push_value(lua_State *vm, Body2D o);
        void push_value(lua_State *vm, Contact2D o);
        void push_value(lua_State *vm, Manifold2D o);
        void push_value(lua_State *vm, Body3D o);
        void push_value(lua_State *vm, Contact3D o);
        void push_value(lua_State *vm, CollideShapeResult3D o);

        // other overloads, for stuff like userdata or C functions

        // for extracting return values, we specialize a simple struct
        // as overloading on return type does not work, and we only need
        // to support a specific set of return types, as the return type
        // of a function is always specified explicitly

        template <typename T>
        static T get_value(lua_State *vm);

    public:
        LuaFunctionBase(lua_State *vm, const std::string &func);
        LuaFunctionBase(lua_State *vm);

        LuaFunctionBase(const LuaFunctionBase &other);

        ~LuaFunctionBase();

        LuaFunctionBase &operator=(const LuaFunctionBase &other);
    };

}

#endif // LUA_FUNCTIONBASE_H