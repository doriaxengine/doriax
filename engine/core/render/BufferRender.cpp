//
// (c) 2026 Eduardo Doria.
//

#include "BufferRender.h"
#include "Engine.h"

using namespace doriax;

BufferRender::BufferRender(){ }

BufferRender::BufferRender(const BufferRender& rhs) : backend(rhs.backend) { }

BufferRender& BufferRender::operator=(const BufferRender& rhs) { 
    backend = rhs.backend; 
    return *this; 
}

BufferRender::~BufferRender(){
    //Cannot destroy because its a handle
}

bool BufferRender::createBuffer(unsigned int size, void* data, BufferType type, BufferUsage usage){
    if (Engine::isViewLoaded() && !isCreated())
        return backend.createBuffer(size, data, type, usage);
    else
        return false;
}

void BufferRender::updateBuffer(unsigned int size, void* data){
    backend.updateBuffer(size, data);
}

void BufferRender::destroyBuffer(){
    backend.destroyBuffer();
}

bool BufferRender::isCreated(){
    return backend.isCreated();
}