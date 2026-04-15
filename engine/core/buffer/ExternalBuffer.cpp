//
// (c) 2026 Eduardo Doria.
//

#include "ExternalBuffer.h"

using namespace doriax;

ExternalBuffer::ExternalBuffer(): Buffer(){
    name = "";
}

ExternalBuffer::~ExternalBuffer(){

}

ExternalBuffer::ExternalBuffer(const ExternalBuffer& rhs): Buffer(rhs){
    name = rhs.name;
}

ExternalBuffer& ExternalBuffer::operator=(const ExternalBuffer& rhs){
    Buffer::operator =(rhs);

    name = rhs.name;

    return *this;
}

void ExternalBuffer::setData(unsigned char* data, size_t size){
    this->data = data;
    this->size = size;
}

void ExternalBuffer::setName(const std::string& name){
    this->name = name;
}

std::string ExternalBuffer::getName() const{
    return this->name;
}