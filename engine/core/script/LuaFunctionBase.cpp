//
// (c) 2026 Eduardo Doria.
//

#include "LuaFunction.h"

#include "util/StringUtils.h"
#include "object/physics/Body2D.h"
#include "object/physics/Contact2D.h"
#include "object/physics/Manifold2D.h"
#include "object/physics/Body3D.h"
#include "object/physics/Contact3D.h"
#include "object/physics/CollideShapeResult3D.h"
#include "LuaBinding.h"

#include "lua.hpp"
#include "LuaBridge.h"
#include <stdexcept>

using namespace doriax;

LuaFunctionBase::LuaFunctionBase(lua_State *vm, const std::string &func): m_vm(vm) {
    // get the function
    lua_getglobal(m_vm, func.c_str());

    store_function();
}

LuaFunctionBase::LuaFunctionBase(lua_State *vm): m_vm(vm) {
    store_function();
}

LuaFunctionBase::LuaFunctionBase(const LuaFunctionBase &other): m_vm(other.m_vm) {
    // copy the registry reference
    lua_rawgeti(m_vm, LUA_REGISTRYINDEX, other.m_func);
    m_func = luaL_ref(m_vm, LUA_REGISTRYINDEX);
}

LuaFunctionBase::~LuaFunctionBase(){
    // delete the reference from registry
    if (LuaBinding::getLuaState()){ //check if state is not closed
        if (lua_isfunction(m_vm, -1)) {
            luaL_unref(m_vm, LUA_REGISTRYINDEX, m_func);
        }
    }
}

LuaFunctionBase& LuaFunctionBase::operator=(const LuaFunctionBase &other){
    if (this != &other) {
        m_vm = other.m_vm;
        lua_rawgeti(m_vm, LUA_REGISTRYINDEX, other.m_func);
        m_func = luaL_ref(m_vm, LUA_REGISTRYINDEX);
    }
    return *this;
}

void LuaFunctionBase::store_function(){
    // ensure it's a function
    if (!lua_isfunction(m_vm, -1)) {
        // throw an exception; you'd use your own exception class here
        // of course, but for sake of simplicity i use runtime_error
        lua_pop(m_vm, 1);
        throw std::runtime_error("not a valid function");
    }
    // store it in registry for later use
    m_func = luaL_ref(m_vm, LUA_REGISTRYINDEX);
}

void LuaFunctionBase::push_function(lua_State *vm, int func){
    lua_rawgeti(vm, LUA_REGISTRYINDEX, func);
}

void LuaFunctionBase::call(int args, int results) {
    // call it with no return values
    int status = lua_pcall(m_vm, args, results, 0);
    if (status) {
        // call failed; throw an exception
        std::string error = lua_tostring(m_vm, -1);
        lua_pop(m_vm, 1);

        #ifdef DORIAX_CRASH_GUARD
        auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
        if (crashHandler) {
            std::string funcName = "Lua Script Error";

            // Try to get the actual function name from the call stack
            lua_Debug ar;
            if (lua_getstack(m_vm, 0, &ar) && lua_getinfo(m_vm, "n", &ar)) {
                if (ar.name && strlen(ar.name) > 0) {
                    funcName = ar.name;
                }
            }

            crashHandler(funcName, error);
            // Push nil values to satisfy the expected results count
            // This prevents stack underflow if the caller tries to retrieve return values
            for (int i = 0; i < results; i++) {
                lua_pushnil(m_vm);
            }
            return;
        }
        #endif

        // in reality you'd want to use your own exception class here
        throw std::runtime_error(error.c_str());
    }
}

void LuaFunctionBase::push_value(lua_State *vm, int n){ 
    lua_pushinteger(vm, n); 
}

void LuaFunctionBase::push_value(lua_State *vm, unsigned int n){ 
    lua_pushinteger(vm, n); 
}

void LuaFunctionBase::push_value(lua_State *vm, float n){
    lua_pushnumber(vm, n); 
}

void LuaFunctionBase::push_value(lua_State *vm, long n){
    lua_pushnumber(vm, n); 
}

void LuaFunctionBase::push_value(lua_State *vm, unsigned long n){
    lua_pushnumber(vm, n); 
}

void LuaFunctionBase::push_value(lua_State *vm, bool b){
    lua_pushboolean(vm, b);
}

void LuaFunctionBase::push_value(lua_State *vm, const std::string &s){
    lua_pushstring(vm, s.c_str());
}

void LuaFunctionBase::push_value(lua_State *vm, wchar_t s){
    lua_pushstring(vm, StringUtils::toUTF8(s).c_str());
}

void LuaFunctionBase::push_value(lua_State *vm, Vector2 o){
    if (!luabridge::push<Vector2>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Vector3 o){
    if (!luabridge::push<Vector3>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Body2D o){
    if (!luabridge::push<Body2D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Manifold2D o){
    if (!luabridge::push<Manifold2D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Contact2D o){
    if (!luabridge::push<Contact2D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Body3D o){
    if (!luabridge::push<Body3D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, Contact3D o){
    if (!luabridge::push<Contact3D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, CollideShapeResult3D o){
    if (!luabridge::push<CollideShapeResult3D>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

template <>
int LuaFunctionBase::get_value<int>(lua_State* vm){
    int val = lua_tointeger(vm, -1);
    lua_pop(vm, 1);
    return val;
}

template <>
float LuaFunctionBase::get_value<float>(lua_State* vm){
    float val = lua_tonumber(vm, -1);
    lua_pop(vm, 1);
    return val;
}

template <>
bool LuaFunctionBase::get_value<bool>(lua_State* vm){
    bool val = lua_toboolean(vm, -1);
    lua_pop(vm, 1);
    return val;
}

template <>
std::string LuaFunctionBase::get_value<std::string>(lua_State* vm){
    std::string val = lua_tostring(vm, -1);
    lua_pop(vm, 1);
    return val;
}