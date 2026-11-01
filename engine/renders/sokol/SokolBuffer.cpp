// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SokolBuffer.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "SokolObject.h"
#include "Engine.h"
#include "SokolResourceCache.h"
#include "pool/MeshLodPool.h"
#include <tuple>

using namespace doriax;

namespace {
    // type, size and a hash of the bytes, the way the LOD cache identifies geometry
    using BufferKey = std::tuple<int, size_t, uint64_t>;

    SokolResourceCache<BufferKey, sg_buffer>& getSharedBuffers(){
        // never destroyed: buffers can be released at exit
        static auto* shared = new SokolResourceCache<BufferKey, sg_buffer>();
        return *shared;
    }
}

SokolBuffer::SokolBuffer(){
    buffer.id = SG_INVALID_ID;
    view.id = SG_INVALID_ID;
}

SokolBuffer::SokolBuffer(const SokolBuffer& rhs): buffer(rhs.buffer), view(rhs.view), createdUsage(rhs.createdUsage) {}

SokolBuffer& SokolBuffer::operator=(const SokolBuffer& rhs){
    buffer = rhs.buffer;
    view = rhs.view;
    createdUsage = rhs.createdUsage;
    return *this;
}

bool SokolBuffer::createBuffer(unsigned int size, void* data, BufferType type, BufferUsage usage){
    sg_buffer_desc vbuf_desc = {0};

    createdUsage = usage;

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
    }else if (usage == BufferUsage::IMMUTABLE && data && size > 0 &&
            (type == BufferType::VERTEX_BUFFER || type == BufferType::INDEX_BUFFER)){
        // the layout belongs to each pipeline, so equal bytes can share one buffer
        BufferKey key((int)type, size, MeshLodPool::hash(0, data, size));
        buffer = getSharedBuffers().get(key, [&](){ return sg_make_buffer(vbuf_desc); },
            [](sg_buffer created){ return sg_query_buffer_state(created) == SG_RESOURCESTATE_VALID; });
    }else{
        buffer = sg_make_buffer(vbuf_desc);
    }

    if (type == BufferType::STORAGE_BUFFER && buffer.id != SG_INVALID_ID){
        sg_view_desc view_desc = {0};
        view_desc.storage_buffer.buffer = buffer;

        if (Engine::isAsyncThread()){
            view = SokolCmdQueue::add_command_make_view(view_desc);
        }else{
            view = sg_make_view(view_desc);
        }
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
            SokolObject::resetAppliedState();
        }
    }
}

int SokolBuffer::appendBuffer(unsigned int size, const void* data){
    if (buffer.id == SG_INVALID_ID || Engine::isAsyncThread() || sg_query_buffer_will_overflow(buffer, size))
        return -1;
    SokolObject::resetAppliedState();
    return sg_append_buffer(buffer, {data, (size_t)size});
}

void SokolBuffer::destroyBuffer(){
    if (view.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_view(view);
        }else{
            sg_destroy_view(view);
        }
    }
    if (buffer.id != SG_INVALID_ID && sg_isvalid() && getSharedBuffers().release(buffer)){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_buffer(buffer);
        }else{
            sg_destroy_buffer(buffer);
        }
    }

    buffer.id = SG_INVALID_ID;
    view.id = SG_INVALID_ID;
}

bool SokolBuffer::isCreated(){
    if (buffer.id != SG_INVALID_ID && sg_isvalid()) {
        return sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID;
    }

    return false;
}

BufferUsage SokolBuffer::getCreatedUsage() const{
    return createdUsage;
}

void SokolBuffer::clearSharedBuffers(){
    getSharedBuffers().clear();
}

sg_buffer SokolBuffer::get(){
    return buffer;
}

sg_view SokolBuffer::getView(){
    return view;
}