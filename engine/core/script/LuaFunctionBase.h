// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
    enum class AdMobFormat;
    enum class AdMobPrecision;
    enum class BillingResponse;
    enum class ProductType;
    enum class WebPortalAdType;
    enum class WebPortalEnvironment;
    struct PurchaseDetails;

    // the base function wrapper class
    class LuaFunctionBase{

    private:

        void store_function(lua_State *vm);
        void release();

    protected:
        // the main state (a coroutine can be collected) and the function registry reference
        lua_State *m_vm;
        int m_func;
        // of the state holding the reference, it can be closed and created again
        unsigned int m_generation;

        // push the function from the registry
        void push_function(lua_State *vm, int func);

        // call the function, logs a traceback on error
        void call(int args, int results = 0);

        // we overload push_value instead of specializing
        // because this way we can also push values that
        // are implicitly convertible to one of the types

        void push_value(lua_State *vm, int n);
        void push_value(lua_State *vm, unsigned int n);
        void push_value(lua_State *vm, float n);
        void push_value(lua_State *vm, long n);
        void push_value(lua_State *vm, unsigned long n);
        void push_value(lua_State *vm, long long n);
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
        void push_value(lua_State *vm, AdMobFormat o);
        void push_value(lua_State *vm, AdMobPrecision o);
        void push_value(lua_State *vm, BillingResponse o);
        void push_value(lua_State *vm, ProductType o);
        void push_value(lua_State *vm, WebPortalAdType o);
        void push_value(lua_State *vm, WebPortalEnvironment o);
        void push_value(lua_State *vm, const PurchaseDetails& o);

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