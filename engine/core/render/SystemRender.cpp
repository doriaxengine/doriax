//
// (c) 2026 Eduardo Doria.
//

#include "SystemRender.h"

#include "sokol/SokolSystem.h"
#include "Engine.h"

using namespace doriax;

void SystemRender::setup(){
    SokolSystem::setup();
}

void SystemRender::commitQueue(){
    SokolSystem::commitQueue();
}

void SystemRender::executeQueue(){
    SokolSystem::executeQueue();
}

void SystemRender::commit(){
    SokolSystem::commit();
}

void SystemRender::shutdown(){
    SokolSystem::shutdown();
}

void SystemRender::scheduleCleanup(void (*cleanupFunc)(void* cleanupData), void* cleanupData, int32_t numFramesToDefer){
    if (Engine::isAsyncThread()){
        SokolSystem::scheduleCleanup(cleanupFunc, cleanupData, numFramesToDefer);
    }else{
        cleanupFunc(cleanupData);
    }
}

void SystemRender::addQueueCommand(void (*custom_cb)(void* custom_data), void* custom_data){
    if (Engine::isAsyncThread()){
        SokolSystem::addQueueCommand(custom_cb, custom_data);
    }else{
        custom_cb(custom_data);
    }
}