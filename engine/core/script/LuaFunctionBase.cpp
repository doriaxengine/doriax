// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LuaFunction.h"

#include "math/Vector2.h"
#include "util/FunctionSubscribe.h"
#include "util/StringUtils.h"
#ifdef DORIAX_PHYSICS_2D
#include "object/physics/Body2D.h"
#include "object/physics/Contact2D.h"
#include "object/physics/Manifold2D.h"
#endif
#ifdef DORIAX_PHYSICS_3D
#include "object/physics/Body3D.h"
#include "object/physics/Contact3D.h"
#include "object/physics/CollideShapeResult3D.h"
#endif
#include "service/AdMob.h"
#include "service/InAppPurchase.h"
#include "service/WebPortal.h"
#include "LuaBinding.h"
#include "Log.h"

#include "lua.hpp"
#include "LuaBridge.h"
#include <stdexcept>

using namespace doriax;

static void reportError(const std::string& error){
    #ifdef DORIAX_CRASH_GUARD
    auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
    if (crashHandler) {
        crashHandler("Lua Script Error", error);
        return;
    }
    #endif
    Log::error("Lua Error: %s", error.c_str());
}

LuaFunctionBase::LuaFunctionBase(lua_State *vm, const std::string &func): m_vm(LuaBinding::getLuaState()) {
    // get the function
    lua_getglobal(vm, func.c_str());

    store_function(vm);
}

LuaFunctionBase::LuaFunctionBase(lua_State *vm): m_vm(LuaBinding::getLuaState()) {
    store_function(vm);
}

LuaFunctionBase::LuaFunctionBase(const LuaFunctionBase &other) {
    copy_function(other);
}

LuaFunctionBase::~LuaFunctionBase(){
    release();
}

LuaFunctionBase& LuaFunctionBase::operator=(const LuaFunctionBase &other){
    if (this != &other) {
        release();
        copy_function(other);
    }
    return *this;
}

bool LuaFunctionBase::is_alive() const{
    return LuaBinding::getLuaState() && LuaBinding::getLuaStateGeneration() == m_generation;
}

bool LuaFunctionBase::can_call(int args){
    if (!is_alive())
        return false;

    // the arguments are pushed unchecked, LUA_MINSTACK is for the call and its errors
    if (!lua_checkstack(m_vm, args + LUA_MINSTACK)) {
        reportError("stack overflow calling a function");
        return false;
    }

    return true;
}

void LuaFunctionBase::copy_function(const LuaFunctionBase &other){
    m_vm = other.m_vm;
    m_generation = other.m_generation;
    if (is_alive()) {
        // copy the registry reference
        lua_rawgeti(m_vm, LUA_REGISTRYINDEX, other.m_func);
        m_func = luaL_ref(m_vm, LUA_REGISTRYINDEX);
    }
}

void LuaFunctionBase::release(){
    if (is_alive()) {
        luaL_unref(m_vm, LUA_REGISTRYINDEX, m_func);
    }
}

void LuaFunctionBase::store_function(lua_State *vm){
    // ensure it's a function
    if (!lua_isfunction(vm, -1)) {
        // throw an exception; you'd use your own exception class here
        // of course, but for sake of simplicity i use runtime_error
        lua_pop(vm, 1);
        throw std::runtime_error("not a valid function");
    }
    // store it in registry for later use
    m_func = luaL_ref(vm, LUA_REGISTRYINDEX);
    m_generation = LuaBinding::getLuaStateGeneration();
}

void LuaFunctionBase::push_function(lua_State *vm, int func){
    lua_rawgeti(vm, LUA_REGISTRYINDEX, func);
}

void LuaFunctionBase::call(int args, int results) {
    int status = LuaBinding::pcallWithTraceback(m_vm, args, results);
    if (status) {
        std::string error = LuaBinding::getLuaStackErrorString(m_vm, -1);
        lua_pop(m_vm, 1);

        reportError(error);

        // Push nil values to satisfy the expected results count
        for (int i = 0; i < results; i++) {
            lua_pushnil(m_vm);
        }
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

void LuaFunctionBase::push_value(lua_State *vm, long long n){
    lua_pushinteger(vm, n);
}

void LuaFunctionBase::push_value(lua_State *vm, AdMobFormat o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, AdMobPrecision o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, BillingResponse o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, ProductType o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, WebPortalAdType o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, WebPortalEnvironment o){
    lua_pushinteger(vm, static_cast<lua_Integer>(o));
}

void LuaFunctionBase::push_value(lua_State *vm, const PurchaseDetails& o){
    if (!luabridge::push<PurchaseDetails>(vm, o))
        throw luabridge::makeErrorCode(luabridge::ErrorCode::LuaStackOverflow);
}

void LuaFunctionBase::push_value(lua_State *vm, bool b){
    lua_pushboolean(vm, b);
}

void LuaFunctionBase::push_value(lua_State *vm, const std::string &s){
    lua_pushlstring(vm, s.data(), s.size());
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

#ifdef DORIAX_PHYSICS_2D
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
#endif

#ifdef DORIAX_PHYSICS_3D
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
#endif

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
    // nil after a script error, or any value without a string form
    size_t len = 0;
    const char* str = lua_tolstring(vm, -1, &len);
    std::string val(str ? str : "", len);
    lua_pop(vm, 1);
    return val;
}
