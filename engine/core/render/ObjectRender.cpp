// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ObjectRender.h"

#include "Engine.h"

using namespace doriax;

ObjectRender::ObjectRender(){ }

ObjectRender::ObjectRender(const ObjectRender& rhs) : backend(rhs.backend), primitiveType(rhs.primitiveType) { }

ObjectRender& ObjectRender::operator=(const ObjectRender& rhs) { 
    backend = rhs.backend; 
    primitiveType = rhs.primitiveType;
    return *this; 
}

ObjectRender::~ObjectRender(){
    //Cannot destroy because its a handle
}

void ObjectRender::beginLoad(PrimitiveType primitiveType){
    this->primitiveType = primitiveType;
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

void ObjectRender::replaceVertexBuffer(BufferRender* original, BufferRender* replacement, size_t byteOffset){
    backend.replaceVertexBuffer(original->backend.get().id, replacement->backend.get(), byteOffset);
}

void ObjectRender::setIndexBuffer(BufferRender* buffer, size_t byteOffset){
    backend.setIndexBuffer(buffer->backend.get(), byteOffset);
}

void ObjectRender::resetIndexBuffer(){
    backend.resetIndexBuffer();
}

void ObjectRender::addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture){
    backend.addTexture(slot, stage, texture);
}

bool ObjectRender::endLoad(uint16_t pipelines, bool enableFaceCulling, bool enableDepthWrite, CullingMode cullingMode, WindingOrder windingOrder, bool prepassFaceCulling){
    return backend.endLoad(pipelines, enableFaceCulling, enableDepthWrite, cullingMode, windingOrder, prepassFaceCulling);
}

bool ObjectRender::sharesState(const ObjectRender& other, PipelineType pipType) const{
    return backend.sharesState(other.backend, pipType);
}

bool ObjectRender::sharesPipeline(const ObjectRender& other, PipelineType pipType) const{
    return backend.sharesPipeline(other.backend, pipType);
}

void ObjectRender::getStateKey(std::vector<uint32_t>& key) const{
    backend.getStateKey(key);
}

uint64_t ObjectRender::getSortKey(PipelineType pipType) const{
    return backend.getSortKey(pipType);
}

bool ObjectRender::beginDraw(PipelineType pipType){
    return backend.beginDraw(pipType);
}

void ObjectRender::applyUniformBlock(int slot, unsigned int count, void* data){
    backend.applyUniformBlock(slot, count, data);
}

void ObjectRender::draw(unsigned int baseElement, unsigned int vertexCount, unsigned int instanceCount){
    backend.draw(baseElement, vertexCount, instanceCount);

    uint64_t triangles = 0;
    if (primitiveType == PrimitiveType::TRIANGLES)
        triangles = vertexCount / 3;
    else if (primitiveType == PrimitiveType::TRIANGLE_STRIP && vertexCount > 2)
        triangles = vertexCount - 2;
    Engine::addDrawStats(instanceCount, triangles * (instanceCount > 0 ? instanceCount : 1));
}

void ObjectRender::destroy(){
    backend.destroy();

}
