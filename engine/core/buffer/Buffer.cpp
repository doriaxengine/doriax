//
// (c) 2026 Eduardo Doria.
//

#include "Buffer.h"

#include "Log.h"
#include <string.h>

using namespace doriax;

Buffer::Buffer(){
    attributes.clear();
    count = 0;
    type = BufferType::VERTEX_BUFFER;
    usage = BufferUsage::IMMUTABLE;

    data = NULL;
    size = 0;
    stride = 0;

    renderAttributes = true;
    instanceBuffer = false;
}

Buffer::~Buffer(){
}

Buffer::Buffer(const Buffer& rhs){
    attributes = rhs.attributes;
    count = rhs.count;
    type = rhs.type;
    usage = rhs.usage;

    data = rhs.data;
    size = rhs.size;
    stride = rhs.stride;

    renderAttributes = rhs.renderAttributes;
    instanceBuffer = rhs.instanceBuffer;

    render = rhs.render;
}

Buffer& Buffer::operator=(const Buffer& rhs){
    attributes = rhs.attributes;
    count = rhs.count;
    type = rhs.type;
    usage = rhs.usage;

    data = rhs.data;
    size = rhs.size;
    stride = rhs.stride;

    renderAttributes = rhs.renderAttributes;
    instanceBuffer = rhs.instanceBuffer;

    render = rhs.render;

    return *this;
}

bool Buffer::increase(size_t newSize){
    if (newSize >= size) {
        size = newSize;

        return true;
    }

    return false;
}

void Buffer::clearAll(){
    stride = 0;
    attributes.clear();
    data = nullptr; // Reset data pointer

    clear();
}

void Buffer::clear(){
    size = 0;
    for (auto &x : attributes) {
        x.second.setCount(0);
    }
    count = 0;
    // Note: data pointer and attributes are preserved for reuse
}

void Buffer::importData(void* data, size_t size){
    increase(size);
    memcpy(this->data, data, size);
}

BufferRender* Buffer::getRender(){
    return &render;
}

void Buffer::addAttribute(AttributeType attribute, AttributeDataType dataType, unsigned int elements, size_t offset){
    Attribute attData;
    attData.setCount(0);
    attData.setDataType(dataType);
    attData.setElements(elements);
    attData.setOffset(offset);

    addAttribute(attribute, attData);
}

void Buffer::addAttribute(AttributeType attribute, unsigned int elements, size_t offset, bool perInstance){
    Attribute attData;
    attData.setCount(0);
    attData.setDataType(AttributeDataType::FLOAT);
    attData.setElements(elements);
    attData.setOffset(offset);
    attData.setPerInstance(perInstance);

    addAttribute(attribute, attData);
}

void Buffer::addAttribute(AttributeType attribute, unsigned int elements, size_t offset){
    Attribute attData;
    attData.setCount(0);
    attData.setDataType(AttributeDataType::FLOAT);
    attData.setElements(elements);
    attData.setOffset(offset);

    addAttribute(attribute, attData);
}

void Buffer::addAttribute(AttributeType attribute, Attribute attributeData){
    //if (size == 0) {
    attributes[attribute] = attributeData;
    //}else{
    //    Log::error("Cannot add attribute with not cleared buffer");
    //}
}

Attribute* Buffer::getAttribute(AttributeType attribute){
    if (attributes.count(attribute) > 0){
        return &attributes[attribute];
    }

    return NULL;
}

std::map<AttributeType, Attribute> Buffer::getAttributes() const{
    return attributes;
}

void Buffer::addUInt16(AttributeType attribute, uint16_t value){
    addUInt16(getAttribute(attribute), value);
}

void Buffer::addUInt32(AttributeType attribute, uint32_t value){
    addUInt32(getAttribute(attribute), value);
}

void Buffer::addFloat(AttributeType attribute, float value){
    addFloat(getAttribute(attribute), value);
}

void Buffer::addVector2(AttributeType attribute, Vector2 vector){
    addVector2(getAttribute(attribute), vector);
}

void Buffer::addVector3(AttributeType attribute, Vector3 vector){
    addVector3(getAttribute(attribute), vector);
}

void Buffer::addVector4(AttributeType attribute, Vector4 vector){
    addVector4(getAttribute(attribute), vector);
}

void Buffer::addUInt16(Attribute* attribute, uint16_t value){
    if (attribute){
        setUInt16(attribute->count++, attribute, value);
    }
}

void Buffer::addUInt32(Attribute* attribute, uint32_t value){
    if (attribute){
        setUInt32(attribute->count++, attribute, value);
    }
}

void Buffer::addFloat(Attribute* attribute, float value){
    if (attribute){
        setFloat(attribute->count++, attribute, value);
    }
}

void Buffer::addVector2(Attribute* attribute, Vector2 vector){
    if (attribute){
        setVector2(attribute->count++, attribute, vector);
    }
}

void Buffer::addVector3(Attribute* attribute, Vector3 vector){
    if (attribute){
        setVector3(attribute->count++, attribute, vector);
    }
}

void Buffer::addVector4(Attribute* attribute, Vector4 vector){
    if (attribute){
        setVector4(attribute->count++, attribute, vector);
    }
}

void Buffer::setUInt16(unsigned int index, Attribute* attribute, uint16_t value){
    setValues(index, attribute, 1, (char*)&value, sizeof(uint16_t));
}

void Buffer::setUInt32(unsigned int index, Attribute* attribute, uint32_t value){
    setValues(index, attribute, 1, (char*)&value, sizeof(uint32_t));
}

void Buffer::setFloat(unsigned int index, Attribute* attribute, float value){
    setValues(index, attribute, 1, (char*)&value, sizeof(float));
}

void Buffer::setVector2(unsigned int index, Attribute* attribute, Vector2 vector){
    setValues(index, attribute, 2, (char*)&vector[0], sizeof(float));
}

void Buffer::setVector3(unsigned int index, Attribute* attribute, Vector3 vector){
    setValues(index, attribute, 3, (char*)&vector[0], sizeof(float));
}

void Buffer::setVector4(unsigned int index, Attribute* attribute, Vector4 vector){
    setValues(index, attribute, 4, (char*)&vector[0], sizeof(float));
}

void Buffer::setValues(unsigned int index, Attribute* attribute, unsigned int numValues, char* vector, size_t typesize){
    if (attribute){
        unsigned int newCount = index + (numValues / attribute->elements);
        if (newCount > attribute->count)
            attribute->count = newCount;

        unsigned pos = (index * stride) + attribute->offset;

        increase(pos + (numValues * typesize));

        for (int i = 0; i < numValues; i++) {
            memcpy(&data[pos], &vector[i*typesize], typesize);
            pos += typesize;
        }

        if (attribute->count > count)
            count = attribute->count;
    }else{
        Log::error("Error add value, attribute not exist");
    }
}

uint16_t Buffer::getUInt16(AttributeType attribute, unsigned int index){
    return getUInt16(getAttribute(attribute), index, 0);
}

uint32_t Buffer::getUInt32(AttributeType attribute, unsigned int index){
    return getUInt32(getAttribute(attribute), index, 0);
}

float Buffer::getFloat(AttributeType attribute, unsigned int index){
    return getFloat(getAttribute(attribute), index, 0);
}

Vector2 Buffer::getVector2(AttributeType attribute, unsigned int index){
    return getVector2(getAttribute(attribute), index);
}

Vector3 Buffer::getVector3(AttributeType attribute, unsigned int index){
    return getVector3(getAttribute(attribute), index);
}

Vector4 Buffer::getVector4(AttributeType attribute, unsigned int index){
    return getVector4(getAttribute(attribute), index);
}

uint16_t Buffer::getUInt16(Attribute* attribute, unsigned int index, int elementIndex){
    uint16_t ret;

    if (elementIndex >= 0 && elementIndex < attribute->elements) {
        unsigned pos = (index * stride) + attribute->offset + (elementIndex * sizeof(uint16_t));
        if ((pos+sizeof(uint16_t)) <= size){
            memcpy(&ret, &data[pos], sizeof(uint16_t));
        }else{
            Log::error("Attribute index is bigger than buffer");
        }
    }else{
        Log::error("Element index is not correct");
    }

    return ret;
}

uint32_t Buffer::getUInt32(Attribute* attribute, unsigned int index, int elementIndex){
    uint32_t ret;

    if (elementIndex >= 0 && elementIndex < attribute->elements) {
        unsigned pos = (index * stride) + attribute->offset + (elementIndex * sizeof(uint32_t));
        if ((pos+sizeof(uint32_t)) <= size){
            memcpy(&ret, &data[pos], sizeof(uint32_t));
        }else{
            Log::error("Attribute index is bigger than buffer");
        }
    }else{
        Log::error("Element index is not correct");
    }

    return ret;
}

float Buffer::getFloat(Attribute* attribute, unsigned int index, int elementIndex){
    float ret;

    if (elementIndex >= 0 && elementIndex < attribute->elements) {
        unsigned pos = (index * stride) + attribute->offset + (elementIndex * sizeof(float));
        if ((pos+sizeof(float)) <= size){
            memcpy(&ret, &data[pos], sizeof(float));
        }else{
            Log::error("Attribute index is bigger than buffer");
        }
    }else{
        Log::error("Element index is not correct");
    }

    return ret;
}

Vector2 Buffer::getVector2(Attribute* attribute, unsigned int index){
    Vector2 ret;

    unsigned pos = (index * stride) + attribute->offset;
    if ((pos + 2*sizeof(float)) <= size){
        float tmp[2];
        memcpy(tmp, &data[pos], sizeof(float) * 2);
        ret.x = tmp[0];
        ret.y = tmp[1];
    }else{
        Log::error("Attribute index is bigger than buffer");
    }

    return ret;
}

Vector3 Buffer::getVector3(Attribute* attribute, unsigned int index){
    Vector3 ret;

    unsigned pos = (index * stride) + attribute->offset;
    if ((pos + 3*sizeof(float)) <= size){
        float tmp[3];
        memcpy(tmp, &data[pos], sizeof(float) * 3);
        ret.x = tmp[0];
        ret.y = tmp[1];
        ret.z = tmp[2];
    }else{
        Log::error("Attribute index is bigger than buffer");
    }

    return ret;
}

Vector4 Buffer::getVector4(Attribute* attribute, unsigned int index){
    Vector4 ret;

    unsigned pos = (index * stride) + attribute->offset;
    if ((pos + 4*sizeof(float)) <= size){
        float tmp[4];
        memcpy(tmp, &data[pos], sizeof(float) * 4);
        ret.x = tmp[0];
        ret.y = tmp[1];
        ret.z = tmp[2];
        ret.w = tmp[3];
    }else{
        Log::error("Attribute index is bigger than buffer");
    }

    return ret;
}

unsigned char* Buffer::getData() const{
    return data ? &data[0] : nullptr;
}

void Buffer::setSize(size_t size){
    this->size = size;
}

size_t Buffer::getSize() const{
    return size;
}

void Buffer::setStride(unsigned int stride){
    this->stride = stride;
}

unsigned int Buffer::getStride() const{
    return stride;
}

void Buffer::setCount(unsigned int count){
    this->count = count;
}

unsigned int Buffer::getCount() const{
    return count;
}

void Buffer::setType(BufferType type){
    this->type = type;
}

BufferType Buffer::getType() const{
    return type;
}

void Buffer::setUsage(BufferUsage usage){
    this->usage = usage;
}

BufferUsage Buffer::getUsage() const{
    return usage;
}

bool Buffer::isRenderAttributes() const {
    return renderAttributes;
}

void Buffer::setRenderAttributes(bool renderAttributes) {
    Buffer::renderAttributes = renderAttributes;
}

bool Buffer::isInstanceBuffer() const{
    return instanceBuffer;
}

void Buffer::setInstanceBuffer(bool instanceBuffer){
    Buffer::instanceBuffer = instanceBuffer;
}
