//
// (c) 2026 Eduardo Doria.
//

#include "SokolSystem.h"

#include "System.h"
#include "sokol_gfx.h"
#include "SokolCmdQueue.h"
#include "Engine.h"
#include "Log.h"

// Render thread
// 
// execute_commands() in render loop
// wait_for_flush() on termination
//
//
// Update thread
// 
// add_command_xxx()
// commit_commands() when you're done for the frame
// flush_commands() on termination, before exiting the thread

using namespace doriax;

void sokol_log(const char* tag,                // e.g. 'sg'
                    uint32_t log_level,             // 0=panic, 1=error, 2=warn, 3=info
                    uint32_t log_item_id,           // SG_LOGITEM_*
                    const char* message_or_null,    // a message string, may be nullptr in release mode
                    uint32_t line_nr,               // line number in sokol_gfx.h
                    const char* filename_or_null,   // source filename, may be nullptr in release mode
                    void* user_data){
    if (log_level == 0){
        Log::print("(PANIC): %s\n", message_or_null);
    }else if (log_level == 1){
        Log::error("%s\n", message_or_null);
    }else if (log_level == 2){
        Log::warn("%s\n", message_or_null);
    }else if (log_level == 3){
        Log::verbose("%s\n", message_or_null);
    }
}


void SokolSystem::setup(){
    /* setup sokol_gfx */
    sg_desc desc = {0};
    desc.buffer_pool_size = 1024; //default: 128
    desc.pipeline_pool_size = 2048; //default: 64
    desc.environment = System::instance().getSokolEnvironment();
    desc.logger.func = sokol_log;

    sg_setup(&desc);

    SokolCmdQueue::start();
}

void SokolSystem::commitQueue(){
    SokolCmdQueue::commit_commands();
}

void SokolSystem::executeQueue(){
    SokolCmdQueue::execute_commands();
}

void SokolSystem::commit(){
    sg_commit();
}

void SokolSystem::shutdown(){
    SokolCmdQueue::flush_commands();
    SokolCmdQueue::wait_for_flush();
    SokolCmdQueue::finish();
    sg_shutdown();
}

void SokolSystem::scheduleCleanup(void (*cleanupFunc)(void* cleanupData), void* cleanupData, int32_t numFramesToDefer){
    SokolCmdQueue::schedule_cleanup(cleanupFunc, cleanupData, numFramesToDefer);
}

void SokolSystem::addQueueCommand(void (*custom_cb)(void* custom_data), void* custom_data){
    SokolCmdQueue::add_command_custom(custom_cb, custom_data);
}