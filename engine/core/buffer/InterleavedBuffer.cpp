//
// (c) 2026 Eduardo Doria.
//

#include "InterleavedBuffer.h"
#include "Log.h"
#include <cstdlib>

using namespace doriax;

InterleavedBuffer::InterleavedBuffer(): Buffer(){
    vectorBuffer.clear();
    vertexSize = 0;

    data = nullptr;
}

InterleavedBuffer::~InterleavedBuffer(){

}

InterleavedBuffer::InterleavedBuffer(const InterleavedBuffer& rhs): Buffer(rhs){
    vectorBuffer = rhs.vectorBuffer;
    vertexSize = rhs.vertexSize;

    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];
}

InterleavedBuffer& InterleavedBuffer::operator=(const InterleavedBuffer& rhs){
    Buffer::operator =(rhs);

    vectorBuffer = rhs.vectorBuffer;
    vertexSize = rhs.vertexSize;

    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];

    return *this;
}

bool InterleavedBuffer::increase(size_t newSize) {
    if (newSize >= vectorBuffer.size()) {
        try {
            vectorBuffer.resize(newSize);
            data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];
            return Buffer::increase(newSize);
        }
        catch (const std::bad_alloc& e) {
            Log::error("Failed to increase buffer: out of memory");
            return false;
        }
    }
    return false;
}

void InterleavedBuffer::clearAll(){
    Buffer::clearAll();

    vertexSize = 0;
    vectorBuffer.clear(); // clear only here to not lose ref in async commands
    data = nullptr; // Explicitly set to nullptr since buffer is cleared
}

void InterleavedBuffer::clear(){
    Buffer::clear(); 

    vertexSize = 0;
    vectorBuffer.resize(0); // Resize to 0 but keep capacity to avoid reallocation
    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];
}

void InterleavedBuffer::addAttribute(AttributeType attribute, int elements){
    addAttribute(attribute, elements, false);
}

void InterleavedBuffer::addAttribute(AttributeType attribute, int elements, bool perInstance){
    if (vectorBuffer.size() == 0) {
        Attribute attData;
        attData.setCount(0);
        attData.setElements(elements);
        attData.setDataType(AttributeDataType::FLOAT);
        attData.setOffset(vertexSize);
        attData.setPerInstance(perInstance);

        vertexSize += elements * sizeof(float);

        Buffer::addAttribute(attribute, attData);

        setStride(vertexSize);
    }
}

unsigned int InterleavedBuffer::getVertexSize() const { 
    return vertexSize; 
}

void InterleavedBuffer::setVertexSize(unsigned int size) { 
    vertexSize = size;
}

