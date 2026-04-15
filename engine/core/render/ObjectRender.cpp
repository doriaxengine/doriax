//
// (c) 2026 Eduardo Doria.
//

#include "ObjectRender.h"

using namespace doriax;

ObjectRender::ObjectRender(){ }

ObjectRender::ObjectRender(const ObjectRender& rhs) : backend(rhs.backend) { }

ObjectRender& ObjectRender::operator=(const ObjectRender& rhs) { 
    backend = rhs.backend; 
    return *this; 
}

ObjectRender::~ObjectRender(){
    //Cannot destroy because its a handle
}

void ObjectRender::beginLoad(PrimitiveType primitiveType){
    backend.beginLoad(primitiveType);
}

void ObjectRender::setShader(ShaderRender* shader){
    backend.setShader(shader);
}

void ObjectRender::setIndex(BufferRender* buffer, AttributeDataType dataType, size_t offset){
    backend.setIndex(buffer, dataType, offset);
}

void ObjectRender::addAttribute(int slot, BufferRender* buffer, unsigned int elements, AttributeDataType dataType, unsigned int stride, size_t offset, bool normalized, bool perInstance){
    backend.addAttribute(slot, buffer, elements, dataType, stride, offset, normalized, perInstance);
}

void ObjectRender::addStorageBuffer(int slot, ShaderStageType stage, BufferRender* buffer){
    backend.addStorageBuffer(slot, stage, buffer);
}

void ObjectRender::addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture){
    backend.addTexture(slot, stage, texture);
}

bool ObjectRender::endLoad(uint8_t pipelines, bool enableFaceCulling, CullingMode cullingMode, WindingOrder windingOrder){
    return backend.endLoad(pipelines, enableFaceCulling, cullingMode, windingOrder);
}

bool ObjectRender::beginDraw(PipelineType pipType){
    return backend.beginDraw(pipType);
}

void ObjectRender::applyUniformBlock(int slot, unsigned int count, void* data){
    backend.applyUniformBlock(slot, count, data);
}

void ObjectRender::draw(unsigned int vertexCount, unsigned int instanceCount){
    backend.draw(vertexCount, instanceCount);
}

void ObjectRender::destroy(){
    backend.destroy();

}