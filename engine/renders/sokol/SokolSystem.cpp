// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SokolSystem.h"

#include "System.h"
#include "sokol_gfx.h"
#include "SokolCmdQueue.h"
#include "SokolObject.h"
#include "SokolBuffer.h"
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

// release sokol passes no message, the item name still says what failed
#define _SG_LOGITEM_XMACRO(item, msg) #item,
static const char* sokolLogItems[] = { _SG_LOG_ITEMS };
#undef _SG_LOGITEM_XMACRO

void sokol_log(const char* tag,                // e.g. 'sg'
                    uint32_t log_level,             // 0=panic, 1=error, 2=warn, 3=info
                    uint32_t log_item_id,           // SG_LOGITEM_*
                    const char* message_or_null,    // a message string, may be nullptr in release mode
                    uint32_t line_nr,               // line number in sokol_gfx.h
                    const char* filename_or_null,   // source filename, may be nullptr in release mode
                    void* user_data){
    if (!message_or_null && log_item_id < sizeof(sokolLogItems) / sizeof(sokolLogItems[0])){
        message_or_null = sokolLogItems[log_item_id];
    }
    // The GL backend warns (and logs the resource name) when the driver strips an unused
    // uniform block or image-sampler. This is benign and expected for custom/simplified
    // shaders that don't use every binding their variant provides, so log it at debug only.
    if (log_item_id == SG_LOGITEM_GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER ||
        log_item_id == SG_LOGITEM_GL_IMAGE_SAMPLER_NAME_NOT_FOUND_IN_SHADER){
        Log::debug("%s\n", message_or_null);
        return;
    }
    // a draw call that failed can leave sokol skipping draws until the next sg_apply_pipeline
    if (log_level <= 1 && !Engine::isAsyncThread()){
        SokolObject::resetAppliedState();
    }
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
    desc.buffer_pool_size = 32768; //default: 128 (each sprite has its own vertex buffer)
    desc.image_pool_size = 2048; //default: 128
    desc.sampler_pool_size = 2048; //default: 64
    desc.view_pool_size = 4096; //default: 256 (texture, storage-buffer and attachment views)
    desc.pipeline_pool_size = 16384; //default: 64 (up to 12 per object, and each foliage chunk is an object)
    desc.shader_pool_size = 1024; //default: 64 (one per ShaderType+properties variant)
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
    SokolObject::clearSharedPipelines();
    SokolBuffer::clearSharedBuffers();
    sg_shutdown();
}

void SokolSystem::scheduleCleanup(void (*cleanupFunc)(void* cleanupData), void* cleanupData, int32_t numFramesToDefer){
    SokolCmdQueue::schedule_cleanup(cleanupFunc, cleanupData, numFramesToDefer);
}

void SokolSystem::addQueueCommand(void (*custom_cb)(void* custom_data), void* custom_data){
    SokolCmdQueue::add_command_custom(custom_cb, custom_data);
}
