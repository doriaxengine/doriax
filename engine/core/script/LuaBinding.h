//
// (c) 2026 Eduardo Doria.
//

#ifndef luabinding_h
#define luabinding_h

#include "Export.h"
#include "ecs/Entity.h"
#include <string>

typedef struct lua_State lua_State;
typedef int (*lua_CFunction) (lua_State *L);

namespace doriax {

    class Scene;

    class DORIAX_API LuaBinding {
        
        friend class Engine;
        
    private:
        static lua_State *luastate;
        
        static void createLuaState();
        static int setLuaPath(const char* path);
        static int setLuaSearcher(lua_CFunction f, bool cleanSearchers = false);

        static int luaRegisterEvent(lua_State* L);
        static int luaRegisterEngineEvent(lua_State* L);
        static int luaRegisterEventImpl(lua_State* L, int eventIndex, int selfIndex, const char* methodName, const char* tag);
        
        static int moduleLoader(lua_State *L);
        static int handleLuaError(lua_State* L);
        static void registerClasses(lua_State *L);
        static void registerHelpersFunctions(lua_State *L);

        // in binding directory
        static void registerActionClasses(lua_State *L);
        static void registerCoreClasses(lua_State *L);
        static void registerECSClasses(lua_State *L);
        static void registerIOClasses(lua_State *L);
        static void registerMathClasses(lua_State *L);
        static void registerObjectClasses(lua_State *L);
        static void registerUtilClasses(lua_State *L);
        static void registerThreadClasses(lua_State *L);

        static void init();

        // For editor scripts use
        static std::string normalizePtrTypeName(std::string value);
        template <typename T>
        static bool pushEntityHandleTyped(lua_State* L, Scene* scene, Entity entity);
        static bool pushEntityHandleByType(lua_State* L, Scene* scene, Entity entity, const std::string& ptrTypeName);

    public:
        LuaBinding();
        virtual ~LuaBinding();
        
        static lua_State* getLuaState();
        
        // old lua function call
        static void luaCallback(int nargs, int nresults, int msgh);

        static void stackDump (lua_State *L);

        static void cleanup();

        static void removeScriptSubscriptions(int luaRef);
        static void releaseLuaRef(int luaRef);

        // For editor scripts use
        static void initializeLuaScripts(Scene* scene);
        static void cleanupLuaScripts(Scene* scene);
        
    };
    
}

#endif /* luabinding_h */
