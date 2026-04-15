//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "io/Data.h"
#include "io/File.h"
#include "io/FileData.h"
#include "io/UserSettings.h"

using namespace doriax;

void LuaBinding::registerIOClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("FileErrors")
        .addVariable("FILEDATA_OK", FileErrors::FILEDATA_OK)
        .addVariable("INVALID_PARAMETER", FileErrors::INVALID_PARAMETER)
        .addVariable("FILE_NOT_FOUND", FileErrors::FILE_NOT_FOUND)
        .addVariable("OUT_OF_MEMORY", FileErrors::OUT_OF_MEMORY)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<FileData>("FileData")
        .addStaticFunction("newFile", 
            luabridge::overload<bool>(&FileData::newFile),
            luabridge::overload<const char*, bool>(&FileData::newFile))
        .addStaticFunction("getBaseDir", &FileData::getBaseDir)
        .addStaticFunction("getFilePathExtension", &FileData::getFilePathExtension)
        .addStaticFunction("getSystemPath", &FileData::getSystemPath)
        .addFunction("read8", &FileData::read8)
        .addFunction("read16", &FileData::read16)
        .addFunction("read32", &FileData::read32)
        .addFunction("eof", &FileData::eof)
        //.addFunction("read", &FileData::read) //TODO: read and write data in Lua
        //.addFunction("write", &FileData::write)
        .addFunction("length", &FileData::length)
        .addFunction("seek", &FileData::seek)
        .addFunction("pos", &FileData::pos)
        .addFunction("readString", 
            luabridge::overload<>(&FileData::readString),
            luabridge::overload<unsigned int>(&FileData::readString))
        .addFunction("writeString", &FileData::writeString)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<File, FileData>("File")
        .addConstructor<void(), void(const char*, bool)> ()
        .addFunction("open", &File::open)
        .addFunction("flush", &File::flush)
        .addFunction("close", &File::close)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Data, FileData>("Data")
        .addConstructor<void(), void(const char*)> ()
        .addFunction("open", (unsigned int(Data::*)(const char *))&Data::open)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<UserSettings>("UserSettings")
        .addStaticFunction("getBoolForKey", 
            luabridge::overload<const char*>(&UserSettings::getBoolForKey),
            luabridge::overload<const char*, bool>(&UserSettings::getBoolForKey))
        .addStaticFunction("getIntegerForKey", 
            luabridge::overload<const char*>(&UserSettings::getIntegerForKey),
            luabridge::overload<const char *, int>(&UserSettings::getIntegerForKey))
        .addStaticFunction("getLongForKey", 
            luabridge::overload<const char*>(&UserSettings::getLongForKey),
            luabridge::overload<const char *, long>(&UserSettings::getLongForKey))
        .addStaticFunction("getFloatForKey", 
            luabridge::overload<const char*>(&UserSettings::getFloatForKey),
            luabridge::overload<const char *, float>(&UserSettings::getFloatForKey))
        .addStaticFunction("getDoubleForKey", 
            luabridge::overload<const char*>(&UserSettings::getDoubleForKey),
            luabridge::overload<const char *, double>(&UserSettings::getDoubleForKey))
        .addStaticFunction("getStringForKey", 
            luabridge::overload<const char*>(&UserSettings::getStringForKey),
            luabridge::overload<const char *, const std::string&>(&UserSettings::getStringForKey))
        .addStaticFunction("getDataForKey", 
            luabridge::overload<const char*>(&UserSettings::getDataForKey),
            luabridge::overload<const char *, const Data&>(&UserSettings::getDataForKey))
        .addStaticFunction("setBoolForKey", &UserSettings::setBoolForKey)
        .addStaticFunction("setIntegerForKey", &UserSettings::setIntegerForKey)
        .addStaticFunction("setLongForKey", &UserSettings::setLongForKey)
        .addStaticFunction("setFloatForKey", &UserSettings::setFloatForKey)
        .addStaticFunction("setDoubleForKey", &UserSettings::setDoubleForKey)
        .addStaticFunction("setStringForKey", &UserSettings::setStringForKey)
        .addStaticFunction("setDataForKey", &UserSettings::setDataForKey)
        .addStaticFunction("removeKey", &UserSettings::removeKey)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}