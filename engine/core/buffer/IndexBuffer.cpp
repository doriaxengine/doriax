//
// (c) 2026 Eduardo Doria.
//

#include "IndexBuffer.h"
#include "Log.h"
#include "render/ObjectRender.h"

using namespace doriax;

IndexBuffer::IndexBuffer(): Buffer(){
    createIndexAttribute();

    type = BufferType::INDEX_BUFFER;

    data = nullptr;
}

IndexBuffer::~IndexBuffer(){

}

IndexBuffer::IndexBuffer(const IndexBuffer& rhs): Buffer(rhs){
    vectorBuffer = rhs.vectorBuffer;

    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];
}

IndexBuffer& IndexBuffer::operator=(const IndexBuffer& rhs){
    Buffer::operator =(rhs);

    vectorBuffer = rhs.vectorBuffer;

    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];

    return *this;
}

void IndexBuffer::createIndexAttribute(){
    Buffer::addAttribute(AttributeType::INDEX, AttributeDataType::UNSIGNED_SHORT, 1, 0);
    Buffer::setStride(sizeof(uint16_t));
}

bool IndexBuffer::increase(size_t newSize) {
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

void IndexBuffer::clearAll(){
    Buffer::clearAll();

    createIndexAttribute();
    vectorBuffer.clear();
    data = nullptr; // Explicitly set to nullptr since buffer is cleared
}

void IndexBuffer::clear(){
    Buffer::clear();

    vectorBuffer.resize(0); // Resize to 0 but keep capacity to avoid reallocation
    data = vectorBuffer.empty() ? nullptr : &vectorBuffer[0];
}
