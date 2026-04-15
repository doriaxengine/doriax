//
// (c) 2026 Eduardo Doria.
//

#ifndef LUA_H
#define LUA_H

#include "Object.h"

namespace doriax {
    class LuaScript {
    public:
        static void setObject(const std::string& global, Object* object);
        static Object* getObject(const std::string& global);
    };

}


#endif //ANDROIDSTUDIO_LUA_H
