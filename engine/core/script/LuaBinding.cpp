//
// (c) 2024 Eduardo Doria.
//

#include "LuaBinding.h"

#include "Log.h"
#include "System.h"
#include "Engine.h"
#include "Scene.h"
#include "SceneManager.h"
#include "component/ScriptComponent.h"
#include "ScriptProperty.h"
#include "object/EntityHandle.h"
#include "object/Mesh.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "object/Model.h"
#include "object/Points.h"
#include "object/Sprite.h"
#include "object/Terrain.h"
#include "object/Light.h"
#include "object/Camera.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <locale>
#include <vector>
#include <memory>

using namespace Supernova;



lua_State *LuaBinding::luastate = NULL;


LuaBinding::LuaBinding() {

}

LuaBinding::~LuaBinding() {

}


void LuaBinding::createLuaState(){
    LuaBinding::luastate = luaL_newstate();

    registerClasses(luastate);
    registerHelpersFunctions(luastate);
}

lua_State* LuaBinding::getLuaState(){
    return luastate;
}

void LuaBinding::luaCallback(int nargs, int nresults, int msgh){
    int status = lua_pcall(luastate, nargs, nresults, msgh);
    if (status != 0){
        Log::error("Lua Error: %s", lua_tostring(luastate, -1));
    }
}

void LuaBinding::stackDump (lua_State *L) {
    int i = lua_gettop(L);
    Log::debug(" ----------------  Stack Dump ----------------" );
    while(  i   ) {
        int t = lua_type(L, i);
        switch (t) {
            case LUA_TSTRING:
                Log::debug("%d:`%s'", i, lua_tostring(L, i));
                break;
            case LUA_TBOOLEAN:
                Log::debug("%d: %s",i,lua_toboolean(L, i) ? "true" : "false");
                break;
            case LUA_TNUMBER:
                Log::debug("%d: %g",  i, lua_tonumber(L, i));
                break;
            default: Log::debug("%d: %s", i, lua_typename(L, t)); break;
        }
        i--;
    }
    Log::debug("--------------- Stack Dump Finished ---------------" );
}

int LuaBinding::setLuaSearcher(lua_CFunction f, bool cleanSearchers) {

    lua_State *L = luastate;

    // Add the package loader to the package.loaders table.
    lua_getglobal(L, "package");
    if(lua_isnil(L, -1))
        return luaL_error(L, "package table does not exist.");

    lua_getfield(L, -1, "searchers");
    if(lua_isnil(L, -1))
        return luaL_error(L, "package.loaders table does not exist.");

    size_t numloaders = lua_rawlen(L, -1);

    if (cleanSearchers) {
        //remove preconfigured loaders
        for (int i = 0; i < numloaders; i++) {
            lua_pushnil(L);
            lua_rawseti(L, -2, i + 1);
        }
        //add new loader
        lua_pushcfunction(L, f);
        lua_rawseti(L, -2, 1);
    }else{
        lua_pushcfunction(L, f);
        lua_rawseti(L, -2, numloaders+1);
    }

    lua_pop(L, 1);

    return 0;
}

// Common implementation for event registration
int LuaBinding::luaRegisterEventImpl(lua_State* L, int eventIndex, int selfIndex, const char* methodName, const char* tag) {
    // Look up self[methodName] using __index
    lua_pushvalue(L, selfIndex);
    lua_pushstring(L, methodName);
    lua_gettable(L, -2);
    lua_remove(L, -2);

    int methodFuncIndex = lua_gettop(L);
    if (!lua_isfunction(L, methodFuncIndex)) {
        return luaL_error(L, "Event registration: method '%s' not found on script", methodName);
    }

    // Build tag if needed
    std::string tagStr;
    if (!tag) {
        lua_getfield(L, selfIndex, "__name");
        const char* typeName = lua_tostring(L, -1);
        if (!typeName) {
            lua_pop(L, 1);
            lua_getfield(L, selfIndex, "name");
            typeName = lua_tostring(L, -1);
        }
        if (!typeName)
            typeName = "Script";

        // Get pointer address of the table
        const void* ptr = lua_topointer(L, selfIndex);
        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(ptr);

        // Format: ClassName_Address_MethodName
        tagStr = std::string(typeName) + "_" + std::to_string(address) + "_" + methodName;
        tag = tagStr.c_str();

        lua_pop(L, 1); // pop typeName
    }

    // Build closure: function(...) method(self, ...) end
    lua_pushvalue(L, methodFuncIndex); // method
    lua_pushvalue(L, selfIndex);       // self
    lua_pushcclosure(L,
        [](lua_State* Linner) -> int {
            int nargs = lua_gettop(Linner);

            lua_pushvalue(Linner, lua_upvalueindex(1)); // method
            lua_pushvalue(Linner, lua_upvalueindex(2)); // self

            for (int i = 1; i <= nargs; ++i) {
                lua_pushvalue(Linner, i);
            }

            lua_call(Linner, 1 + nargs, 0);
            return 0;
        },
        2);
    int closureIndex = lua_gettop(L);

    // event:add(tag, closure)
    lua_getfield(L, eventIndex, "add");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "Event registration: event object has no 'add' method");
    }

    lua_pushvalue(L, eventIndex);
    lua_pushstring(L, tag);
    lua_pushvalue(L, closureIndex);

    lua_call(L, 3, 0);

    return 0;
}

// Generic: RegisterEvent(self, event, methodName [, tag])
int LuaBinding::luaRegisterEvent(lua_State* L) {
    int top = lua_gettop(L);
    if (top < 3)
        return luaL_error(L, "RegisterEvent(self, event, methodName [, tag]) requires at least 3 arguments");

    if (!lua_istable(L, 1))
        return luaL_error(L, "RegisterEvent: 'self' must be a table");

    if (!lua_isstring(L, 3))
        return luaL_error(L, "RegisterEvent: methodName must be a string");

    const char* methodName = lua_tostring(L, 3);
    const char* tag = (top >= 4 && !lua_isnil(L, 4)) ? lua_tostring(L, 4) : nullptr;

    return luaRegisterEventImpl(L, 2, 1, methodName, tag);
}

// Engine-specific: RegisterEngineEvent(self, methodName [, tag])
int LuaBinding::luaRegisterEngineEvent(lua_State* L) {
    int top = lua_gettop(L);
    if (top < 2)
        return luaL_error(L, "RegisterEngineEvent(self, methodName [, tag]) requires at least 2 arguments");

    if (!lua_istable(L, 1))
        return luaL_error(L, "RegisterEngineEvent: 'self' must be a table");

    if (!lua_isstring(L, 2))
        return luaL_error(L, "RegisterEngineEvent: methodName must be a string");

    const char* methodName = lua_tostring(L, 2);
    const char* tag = (top >= 3 && !lua_isnil(L, 3)) ? lua_tostring(L, 3) : nullptr;

    // Map methodName to Engine event
    lua_getglobal(L, "Engine");
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "RegisterEngineEvent: Engine table not found");
    }

    lua_getfield(L, -1, methodName);
    if (lua_isnil(L, -1)) {
        return luaL_error(L, "RegisterEngineEvent: Engine.%s not found", methodName);
    }

    int eventIndex = lua_gettop(L);

    return luaRegisterEventImpl(L, eventIndex, 1, methodName, tag);
}

// Note it can be done in the same way with Sol2: https://github.com/ThePhD/sol2/issues/692
int LuaBinding::setLuaPath(const char* path) {

    lua_State *L = luastate;

    lua_getglobal( L, "package" );
    if(lua_isnil(L, -1))
        return luaL_error(L, "package table does not exist.");

    lua_getfield( L, -1, "path" );
    if(lua_isnil(L, -1))
        return luaL_error(L, "package.path table does not exist.");

    std::string cur_path = lua_tostring( L, -1 );
    cur_path.append( ";" );
    cur_path.append( path );
    lua_pop( L, 1 );
    lua_pushstring( L, cur_path.c_str() );
    lua_setfield( L, -2, "path" );
    lua_pop( L, 1 );

    return 0;
}

int LuaBinding::moduleLoader(lua_State *L) {
    
    const char *filename = lua_tostring(L, 1);
    filename = luaL_gsub(L, filename, ".", std::to_string(System::instance().getDirSeparator()).c_str());
    
    std::string filepath;
    Data filedata;
    
    filepath = "lua://" + std::string("lua") + System::instance().getDirSeparator() + filename + ".lua";
    filedata.open(filepath.c_str());
    if (filedata.getMemPtr() != NULL) {
        
        luaL_loadbuffer(L, (const char *) filedata.getMemPtr(), filedata.length(),
                        filepath.c_str());
        
        return 1;
    }
    
    filepath = "lua://" + std::string("") + filename + ".lua";
    filedata.open(filepath.c_str());
    if (filedata.getMemPtr() != NULL) {
        
        luaL_loadbuffer(L, (const char *) filedata.getMemPtr(), filedata.length(),
                        filepath.c_str());
        
        return 1;
    }
    
    lua_pushstring(L, "\n\tno file in assets directory");
    
    return 1;
}

//The same msghandler of lua.c
int LuaBinding::handleLuaError(lua_State* L) {
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) {  /* is error object not a string? */
    if (luaL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
        lua_type(L, -1) == LUA_TSTRING)  /* that produces a string? */
        return 1;  /* that is the message */
    else
        msg = lua_pushfstring(L, "(error object is a %s value)",
                                luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);  /* append a standard traceback */
    return 1;  /* return the traceback */
}

void LuaBinding::init(){

    lua_State *L = luastate;

    std::string luadir = std::string("lua") + System::instance().getDirSeparator();

    setLuaPath(std::string("lua://" + luadir + "?.lua").c_str());
    setLuaSearcher(moduleLoader, true);

    std::string luafile = std::string("lua://") + "main.lua";
    std::string luafile_subdir = std::string("lua://") + luadir + "main.lua";

    Data filedata;

    //First try open on root assets dir
    if (filedata.open(luafile.c_str()) != FileErrors::FILEDATA_OK){
        //Second try to open on lua dir
        filedata.open(luafile_subdir.c_str());
    }

    lua_pushcfunction(L, handleLuaError);
    int msgh = lua_gettop(L);

    //int luaL_dofile (lua_State *L, const char *filename);
    if (luaL_loadbuffer(L,(const char*)filedata.getMemPtr(),filedata.length(), luafile.c_str()) == 0){
        if(lua_pcall(L, 0, LUA_MULTRET, msgh) != 0){
            Log::error("Lua Error: %s", lua_tostring(L,-1));
            lua_close(L);
        }
    }else{
        Log::error("Lua Error: %s", lua_tostring(L,-1));
        lua_close(L);
    }

}

void LuaBinding::registerClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS
    // luaL_openlibs() open all libraries: base, string, io, os, package, table, debug
    luaL_openlibs(L);

    registerCoreClasses(L);
    registerObjectClasses(L);
    registerActionClasses(L);
    registerECSClasses(L);
    registerIOClasses(L);
    registerMathClasses(L); 
    registerUtilClasses(L);
    registerThreadClasses(L);

#endif //DISABLE_LUA_BINDINGS
}

void LuaBinding::registerHelpersFunctions(lua_State *L){
    // Event helper table
    lua_newtable(L);

    lua_pushcfunction(L, luaRegisterEvent);
    lua_setglobal(L, "RegisterEvent");

    lua_pushcfunction(L, luaRegisterEngineEvent);
    lua_setglobal(L, "RegisterEngineEvent");
}


void LuaBinding::cleanup(){
    if (luastate) {
        lua_close(luastate);
        luastate = NULL;
    }
}

void LuaBinding::removeScriptSubscriptions(int luaRef){
    if (!luastate) return;

    lua_rawgeti(luastate, LUA_REGISTRYINDEX, luaRef);
    const void* ptr = lua_topointer(luastate, -1);
    if (ptr) {
        std::string addr = "_" + std::to_string(reinterpret_cast<std::uintptr_t>(ptr)) + "_";
        Engine::removeSubscriptionsByTag(addr);
    }
    lua_pop(luastate, 1);
}

void LuaBinding::releaseLuaRef(int luaRef){
    if (!luastate) return;
    luaL_unref(luastate, LUA_REGISTRYINDEX, luaRef);
}

std::string LuaBinding::normalizePtrTypeName(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && (std::isspace(static_cast<unsigned char>(value.back())) || value.back() == '*' || value.back() == '&'))
        value.pop_back();
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return std::tolower(c); });
    return value;
}

template <typename T>
bool LuaBinding::pushEntityHandleTyped(lua_State* L, Supernova::Scene* scene, Supernova::Entity entity) {
    T* handle = new T(scene, entity);
    if (!luabridge::push<T*>(L, handle)) {
        delete handle;
        lua_pushnil(L);
        return false;
    }
    return true;
}

bool LuaBinding::pushEntityHandleByType(lua_State* L, Supernova::Scene* scene, Supernova::Entity entity, const std::string& ptrTypeName) {
    const std::string key = normalizePtrTypeName(ptrTypeName);
    using namespace Supernova;
    if (key == "mesh")    return pushEntityHandleTyped<Mesh>(L, scene, entity);
    if (key == "object")  return pushEntityHandleTyped<Object>(L, scene, entity);
    if (key == "shape")   return pushEntityHandleTyped<Shape>(L, scene, entity);
    if (key == "model")   return pushEntityHandleTyped<Model>(L, scene, entity);
    if (key == "points")  return pushEntityHandleTyped<Points>(L, scene, entity);
    if (key == "sprite")  return pushEntityHandleTyped<Sprite>(L, scene, entity);
    if (key == "terrain") return pushEntityHandleTyped<Terrain>(L, scene, entity);
    if (key == "light")   return pushEntityHandleTyped<Light>(L, scene, entity);
    if (key == "camera")  return pushEntityHandleTyped<Camera>(L, scene, entity);
    return pushEntityHandleTyped<EntityHandle>(L, scene, entity);
}

void LuaBinding::initializeLuaScripts(Scene* scene) {
    if (!scene) return;

    lua_State* L = luastate;
    if (!L) {
        Log::error("Lua state not initialized");
        return;
    }

    auto scriptsArray = scene->getComponentArray<ScriptComponent>();

    // PASS 1: Create all Lua script instances (without resolving EntityRef properties)
    for (size_t i = 0; i < scriptsArray->size(); i++) {
        ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);
        Entity entity = scriptsArray->getEntity(i);

        for (auto& scriptEntry : scriptComp.scripts) {
            if (!scriptEntry.enabled) continue;
            if (scriptEntry.type != ScriptType::SCRIPT_LUA) continue;

            std::string luaFile = std::string("lua://") + scriptEntry.path;
            Data filedata;
            if (filedata.open(luaFile.c_str()) != FileErrors::FILEDATA_OK) {
                Log::error("Lua script file not found: %s", scriptEntry.path.c_str());
                continue;
            }

            int status = luaL_loadbuffer(L, (const char*)filedata.getMemPtr(), filedata.length(), scriptEntry.path.c_str());
            if (status != LUA_OK) {
                Log::error("Failed to load Lua file '%s': %s", scriptEntry.path.c_str(), lua_tostring(L, -1));
                lua_pop(L, 1);
                continue;
            }

            status = lua_pcall(L, 0, 1, 0);
            if (status != LUA_OK) {
                Log::error("Failed to execute Lua module '%s': %s", scriptEntry.className.c_str(), lua_tostring(L, -1));
                lua_pop(L, 1);
                continue;
            }

            if (!lua_istable(L, -1)) {
                Log::error("Lua module '%s' did not return a table", scriptEntry.className.c_str());
                lua_pop(L, 1);
                continue;
            }

            // Create instance table with module as prototype
            lua_newtable(L);
            lua_newtable(L);
            lua_pushvalue(L, -3);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);

            lua_pushstring(L, scriptEntry.className.c_str());
            lua_setfield(L, -2, "__name");

            if (!luabridge::push<Scene*>(L, scene)) {
                Log::error("Failed to push scene to Lua");
                lua_pop(L, 2);
                continue;
            }
            lua_setfield(L, -2, "scene");

            lua_pushinteger(L, static_cast<lua_Integer>(entity));
            lua_setfield(L, -2, "entity");

            // Set script properties (skip EntityPointer for now — resolved in PASS 2)
            for (auto& prop : scriptEntry.properties) {
                if (prop.type == ScriptPropertyType::EntityPointer) {
                    lua_pushnil(L);
                    lua_setfield(L, -2, prop.name.c_str());
                    continue;
                }
                if (std::holds_alternative<bool>(prop.value)) {
                    lua_pushboolean(L, std::get<bool>(prop.value));
                } else if (std::holds_alternative<int>(prop.value)) {
                    lua_pushinteger(L, std::get<int>(prop.value));
                } else if (std::holds_alternative<float>(prop.value)) {
                    lua_pushnumber(L, std::get<float>(prop.value));
                } else if (std::holds_alternative<std::string>(prop.value)) {
                    lua_pushstring(L, std::get<std::string>(prop.value).c_str());
                } else if (std::holds_alternative<Vector2>(prop.value)) {
                    if (!luabridge::push<Vector2>(L, std::get<Vector2>(prop.value))) lua_pushnil(L);
                } else if (std::holds_alternative<Vector3>(prop.value)) {
                    if (!luabridge::push<Vector3>(L, std::get<Vector3>(prop.value))) lua_pushnil(L);
                } else if (std::holds_alternative<Vector4>(prop.value)) {
                    if (!luabridge::push<Vector4>(L, std::get<Vector4>(prop.value))) lua_pushnil(L);
                } else {
                    lua_pushnil(L);
                }
                lua_setfield(L, -2, prop.name.c_str());
            }

            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            scriptEntry.instance = reinterpret_cast<void*>(static_cast<intptr_t>(ref));
            for (auto& prop : scriptEntry.properties) {
                prop.luaRef = ref;
            }

            lua_pop(L, 1); // pop module
        }
    }

    // PASS 2: Resolve Entity pointer properties
    for (size_t i = 0; i < scriptsArray->size(); i++) {
        ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);

        for (auto& scriptEntry : scriptComp.scripts) {
            if (scriptEntry.type != ScriptType::SCRIPT_LUA || !scriptEntry.enabled) continue;

            int ref = static_cast<int>(reinterpret_cast<intptr_t>(scriptEntry.instance));
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

            for (auto& prop : scriptEntry.properties) {
                if (prop.type != ScriptPropertyType::EntityPointer) continue;

                if (!std::holds_alternative<Entity>(prop.value)) {
                    lua_pushnil(L);
                    lua_setfield(L, -2, prop.name.c_str());
                    continue;
                }

                Entity targetEntity = std::get<Entity>(prop.value);
                bool foundScript = false;

                if (targetEntity != NULL_ENTITY) {
                    Scene* targetScene = scene;
                    if (prop.sceneId != 0) {
                        targetScene = SceneManager::getScenePtr(prop.sceneId);
                    }
                    if (targetScene) {
                        ScriptComponent* targetScriptComp = targetScene->findComponent<ScriptComponent>(targetEntity);
                        if (targetScriptComp) {
                            if (!prop.ptrTypeName.empty()) {
                                for (auto& targetScript : targetScriptComp->scripts) {
                                    if (targetScript.type == ScriptType::SCRIPT_LUA &&
                                        targetScript.className == prop.ptrTypeName &&
                                        targetScript.enabled && targetScript.instance) {
                                        int targetRef = static_cast<int>(reinterpret_cast<intptr_t>(targetScript.instance));
                                        lua_rawgeti(L, LUA_REGISTRYINDEX, targetRef);
                                        foundScript = true;
                                        break;
                                    }
                                }
                                if (!foundScript)
                                    foundScript = pushEntityHandleByType(L, targetScene, targetEntity, prop.ptrTypeName);
                            } else {
                                for (auto& targetScript : targetScriptComp->scripts) {
                                    if (targetScript.type == ScriptType::SCRIPT_LUA &&
                                        targetScript.enabled && targetScript.instance) {
                                        int targetRef = static_cast<int>(reinterpret_cast<intptr_t>(targetScript.instance));
                                        lua_rawgeti(L, LUA_REGISTRYINDEX, targetRef);
                                        foundScript = true;
                                        break;
                                    }
                                }
                                if (!foundScript)
                                    foundScript = pushEntityHandleByType(L, targetScene, targetEntity, prop.ptrTypeName);
                            }
                        } else {
                            foundScript = pushEntityHandleByType(L, targetScene, targetEntity, prop.ptrTypeName);
                        }
                    }
                }

                if (!foundScript) lua_pushnil(L);
                lua_setfield(L, -2, prop.name.c_str());
            }

            lua_pop(L, 1);
        }
    }

    // PASS 3: Call init() methods
    for (size_t i = 0; i < scriptsArray->size(); i++) {
        ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);
        for (auto& scriptEntry : scriptComp.scripts) {
            if (scriptEntry.type != ScriptType::SCRIPT_LUA || !scriptEntry.enabled) continue;

            int ref = static_cast<int>(reinterpret_cast<intptr_t>(scriptEntry.instance));
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_getfield(L, -1, "init");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                    Log::error("Lua init() failed for '%s': %s", scriptEntry.className.c_str(), lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }
}

void LuaBinding::cleanupLuaScripts(Scene* scene) {
    if (!scene) return;

    auto scriptsArray = scene->getComponentArray<ScriptComponent>();

    for (size_t i = 0; i < scriptsArray->size(); i++) {
        ScriptComponent& scriptComp = scriptsArray->getComponentFromIndex(i);
        for (auto& scriptEntry : scriptComp.scripts) {
            if (scriptEntry.type != ScriptType::SCRIPT_LUA) continue;
            if (scriptEntry.instance) {
                int ref = static_cast<int>(reinterpret_cast<intptr_t>(scriptEntry.instance));
                removeScriptSubscriptions(ref);
                releaseLuaRef(ref);
                scriptEntry.instance = nullptr;
                for (auto& prop : scriptEntry.properties) {
                    prop.luaRef = 0;
                }
            }
        }
    }
}