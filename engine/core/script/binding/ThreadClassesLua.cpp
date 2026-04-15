//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"

#include "thread/ResourceProgress.h"
#include "thread/ThreadPoolManager.h"

using namespace doriax;

void LuaBinding::registerThreadClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ResourceType")
        .addVariable("Shader", ResourceType::Shader)
        .addVariable("Texture", ResourceType::Texture)
        .addVariable("Model", ResourceType::Model)
        .addVariable("Audio", ResourceType::Audio)
        .addVariable("Material", ResourceType::Material)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<ResourceBuildInfo>("ResourceBuildInfo")
        .addProperty("type", &ResourceBuildInfo::type)
        .addProperty("name", &ResourceBuildInfo::name)
        .addProperty("progress", &ResourceBuildInfo::progress)
        .addProperty("isActive", &ResourceBuildInfo::isActive)
        .addProperty("startTime", &ResourceBuildInfo::startTime)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<OverallBuildProgress>("OverallBuildProgress")
        .addProperty("totalProgress", &OverallBuildProgress::totalProgress)
        .addProperty("totalBuilds", &OverallBuildProgress::totalBuilds)
        .addProperty("completedBuilds", &OverallBuildProgress::completedBuilds)
        .addProperty("currentBuildName", &OverallBuildProgress::currentBuildName)
        .addProperty("currentBuildType", &OverallBuildProgress::currentBuildType)
        .addProperty("hasActiveBuilds", &OverallBuildProgress::hasActiveBuilds)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ResourceProgress>("ResourceProgress")
        .addStaticFunction("startBuild", &ResourceProgress::startBuild)
        .addStaticFunction("updateProgress", &ResourceProgress::updateProgress)
        .addStaticFunction("completeBuild", &ResourceProgress::completeBuild)
        .addStaticFunction("failBuild", &ResourceProgress::failBuild)
        .addStaticFunction("hasActiveBuilds", &ResourceProgress::hasActiveBuilds)
        .addStaticFunction("getOverallProgress", &ResourceProgress::getOverallProgress)
        .addStaticFunction("getActiveBuildCount", &ResourceProgress::getActiveBuildCount)
        .addStaticFunction("getAllActiveBuilds", &ResourceProgress::getAllActiveBuilds)
        .addStaticFunction("getCurrentBuild", &ResourceProgress::getCurrentBuild)
        .addStaticFunction("getResourceTypeName", &ResourceProgress::getResourceTypeName)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ThreadPoolManager>("ThreadPoolManager")
        .addStaticFunction("getInstance", &ThreadPoolManager::getInstance)
        .addStaticFunction("initialize", &ThreadPoolManager::initialize)
        .addStaticFunction("shutdown", &ThreadPoolManager::shutdown)
        .addFunction("getQueueSize", &ThreadPoolManager::getQueueSize)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}