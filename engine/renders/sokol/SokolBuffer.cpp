//
// (c) 2026 Eduardo Doria.
//

#include "SokolBuffer.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "Engine.h"

using namespace doriax;

SokolBuffer::SokolBuffer(){
    buffer.id = SG_INVALID_ID;
}

SokolBuffer::SokolBuffer(const SokolBuffer& rhs): buffer(rhs.buffer) {}

SokolBuffer& SokolBuffer::operator=(const SokolBuffer& rhs){
    buffer = rhs.buffer;
    return *this;
}

bool SokolBuffer::createBuffer(unsigned int size, void* data, BufferType type, BufferUsage usage){
    sg_buffer_desc vbuf_desc = {0};

    if (usage == BufferUsage::IMMUTABLE){
        vbuf_desc.size = (size_t)size;
        vbuf_desc.data = {data, (size_t)size};
    }else{
        vbuf_desc.size = (size_t)size;
    }

    // TODO: Multi-purpose buffer objects
    // https://floooh.github.io/2025/05/19/sokol-gfx-compute-ms2.html
    if (type == BufferType::VERTEX_BUFFER){
        vbuf_desc.usage.vertex_buffer = true;
    } else if (type == BufferType::INDEX_BUFFER){
        vbuf_desc.usage.index_buffer = true;
    } else if (type == BufferType::STORAGE_BUFFER){
        vbuf_desc.usage.storage_buffer = true;
    }

    if (usage == BufferUsage::IMMUTABLE){
        vbuf_desc.usage.immutable = true;
    }else if (usage == BufferUsage::DYNAMIC){
        vbuf_desc.usage.dynamic_update = true;
    }else if (usage == BufferUsage::STREAM){
        vbuf_desc.usage.stream_update = true;
    }

    if (Engine::isAsyncThread()){
        buffer = SokolCmdQueue::add_command_make_buffer(vbuf_desc);
    }else{
        buffer = sg_make_buffer(vbuf_desc);
    }

    // cannot two updates in same draw loop
    //if (usage != BufferUsage::IMMUTABLE){
    //    updateBuffer(size, data);
    //}

    if (buffer.id != SG_INVALID_ID)
        return true;

    return false;
}

// called by draw
void SokolBuffer::updateBuffer(unsigned int size, void* data){
    if (buffer.id != SG_INVALID_ID && data && size > 0){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_update_buffer(buffer, {data, (size_t)size});
        }else{
            sg_update_buffer(buffer, {data, (size_t)size});
        }
    }
}

void SokolBuffer::destroyBuffer(){
    if (buffer.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_buffer(buffer);
        }else{
            sg_destroy_buffer(buffer);
        }
    }

    buffer.id = SG_INVALID_ID;
}

bool SokolBuffer::isCreated(){
    if (buffer.id != SG_INVALID_ID && sg_isvalid()) {
        return sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID;
    }

    return false;
}

sg_buffer SokolBuffer::get(){
    return buffer;
}