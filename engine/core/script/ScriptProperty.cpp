#include "ScriptProperty.h"
#include "LuaBinding.h"
#include "lua.hpp"
#include "LuaBridge.h"

namespace Supernova {

    void ScriptProperty::syncToMember() {
        if (memberPtr) {
            switch (type) {
                case ScriptPropertyType::Bool:
                    if (std::holds_alternative<bool>(value)) {
                        *static_cast<bool*>(memberPtr) = std::get<bool>(value);
                    }
                    break;
                case ScriptPropertyType::Int:
                    if (std::holds_alternative<int>(value)) {
                        *static_cast<int*>(memberPtr) = std::get<int>(value);
                    }
                    break;
                case ScriptPropertyType::Float:
                    if (std::holds_alternative<float>(value)) {
                        *static_cast<float*>(memberPtr) = std::get<float>(value);
                    }
                    break;
                case ScriptPropertyType::String:
                    if (std::holds_alternative<std::string>(value)) {
                        *static_cast<std::string*>(memberPtr) = std::get<std::string>(value);
                    }
                    break;
                case ScriptPropertyType::Vector2:
                    if (std::holds_alternative<Vector2>(value)) {
                        *static_cast<Vector2*>(memberPtr) = std::get<Vector2>(value);
                    }
                    break;
                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3:
                    if (std::holds_alternative<Vector3>(value)) {
                        *static_cast<Vector3*>(memberPtr) = std::get<Vector3>(value);
                    }
                    break;
                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4:
                    if (std::holds_alternative<Vector4>(value)) {
                        *static_cast<Vector4*>(memberPtr) = std::get<Vector4>(value);
                    }
                    break;
                case ScriptPropertyType::EntityReference:
                    // Intentionally no-op for runtime member sync
                    break;
            }
        } else if (luaRef != 0 && !name.empty()) {
            lua_State* L = LuaBinding::getLuaState();
            if (!L) return;

            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef); // Push instance table
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                return;
            }

            auto pushOrNil = [&](auto&& v) {
                const int top = lua_gettop(L);
                auto r = luabridge::push(L, std::forward<decltype(v)>(v));
                if (!r) {
                    lua_settop(L, top); // rollback anything partially pushed
                    lua_pushnil(L);
                }
            };

            // Push the value based on type
            switch (type) {
                case ScriptPropertyType::Bool:
                    if (std::holds_alternative<bool>(value)) {
                        lua_pushboolean(L, std::get<bool>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::Int:
                    if (std::holds_alternative<int>(value)) {
                        lua_pushinteger(L, std::get<int>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::Float:
                    if (std::holds_alternative<float>(value)) {
                        lua_pushnumber(L, std::get<float>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::String:
                    if (std::holds_alternative<std::string>(value)) {
                        lua_pushstring(L, std::get<std::string>(value).c_str());
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::Vector2:
                    if (std::holds_alternative<Vector2>(value)) {
                        pushOrNil(std::get<Vector2>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3:
                    if (std::holds_alternative<Vector3>(value)) {
                        pushOrNil(std::get<Vector3>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4:
                    if (std::holds_alternative<Vector4>(value)) {
                        pushOrNil(std::get<Vector4>(value));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
                case ScriptPropertyType::EntityReference:
                    if (std::holds_alternative<EntityReference>(value)) {
                        lua_pushinteger(L, static_cast<lua_Integer>(std::get<EntityReference>(value).entity));
                    } else {
                        lua_pushnil(L);
                    }
                    break;
            }

            lua_setfield(L, -2, name.c_str()); // instance[name] = value
            lua_pop(L, 1); // Pop instance table
        }
    }

    void ScriptProperty::syncFromMember() {
        if (memberPtr) {
            switch (type) {
                case ScriptPropertyType::Bool:
                    value = *static_cast<bool*>(memberPtr);
                    break;
                case ScriptPropertyType::Int:
                    value = *static_cast<int*>(memberPtr);
                    break;
                case ScriptPropertyType::Float:
                    value = *static_cast<float*>(memberPtr);
                    break;
                case ScriptPropertyType::String:
                    value = *static_cast<std::string*>(memberPtr);
                    break;
                case ScriptPropertyType::Vector2:
                    value = *static_cast<Vector2*>(memberPtr);
                    break;
                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3:
                    value = *static_cast<Vector3*>(memberPtr);
                    break;
                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4:
                    value = *static_cast<Vector4*>(memberPtr);
                    break;
                case ScriptPropertyType::EntityReference:
                    // Intentionally no-op for runtime member sync
                    break;
            }
        } else if (luaRef != 0 && !name.empty()) {
            lua_State* L = LuaBinding::getLuaState();
            if (!L) return;

            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef); // Push instance table
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                return;
            }

            lua_getfield(L, -1, name.c_str()); // Push instance[name]

            switch (type) {
                case ScriptPropertyType::Bool:
                    if (lua_isboolean(L, -1)) {
                        value = static_cast<bool>(lua_toboolean(L, -1));
                    }
                    break;
                case ScriptPropertyType::Int:
                    if (lua_isinteger(L, -1)) {
                        value = static_cast<int>(lua_tointeger(L, -1));
                    } else if (lua_isnumber(L, -1)) {
                        value = static_cast<int>(lua_tonumber(L, -1));
                    }
                    break;
                case ScriptPropertyType::Float:
                    if (lua_isnumber(L, -1)) {
                        value = static_cast<float>(lua_tonumber(L, -1));
                    }
                    break;
                case ScriptPropertyType::String:
                    if (lua_isstring(L, -1)) {
                        value = std::string(lua_tostring(L, -1));
                    }
                    break;
                case ScriptPropertyType::Vector2:
                    {
                        auto result = luabridge::Stack<Vector2>::get(L, -1);
                        if (result) {
                            value = result.value();
                        }
                    }
                    break;
                case ScriptPropertyType::Vector3:
                case ScriptPropertyType::Color3:
                    {
                        auto result = luabridge::Stack<Vector3>::get(L, -1);
                        if (result) {
                            value = result.value();
                        }
                    }
                    break;
                case ScriptPropertyType::Vector4:
                case ScriptPropertyType::Color4:
                    {
                        auto result = luabridge::Stack<Vector4>::get(L, -1);
                        if (result) {
                            value = result.value();
                        }
                    }
                    break;
                case ScriptPropertyType::EntityReference: {
                    uint32_t sid = std::holds_alternative<EntityReference>(value) ? std::get<EntityReference>(value).sceneId : 0;
                    if (lua_isinteger(L, -1)) {
                        value = EntityReference{static_cast<Entity>(lua_tointeger(L, -1)), sid};
                    } else if (lua_isnumber(L, -1)) {
                        value = EntityReference{static_cast<Entity>(lua_tonumber(L, -1)), sid};
                    }
                    break;
                }
            }

            lua_pop(L, 2); // Pop value and instance table
        }
    }

}