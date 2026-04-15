//
// (c) 2026 Eduardo Doria.
//

#include "Attribute.h"

using namespace doriax;

Attribute::Attribute(){
    setDataType(AttributeDataType::FLOAT);
    setBufferName("");
    setElements(0);
    setOffset(0);
    setCount(0);
    setNormalized(true);
    setPerInstance(false);
}

Attribute::Attribute(AttributeDataType dataType, const std::string& bufferName, unsigned int elements, size_t offset, unsigned int count, bool normalized, bool perInstance){
    setDataType(dataType);
    setBufferName(bufferName);
    setElements(elements);
    setOffset(offset);
    setCount(count);
    setNormalized(normalized);
    setPerInstance(perInstance);
}

Attribute::~Attribute(){

}

Attribute::Attribute(const Attribute& a){
    this->dataType = a.dataType;
    this->bufferName = a.bufferName;
    this->elements = a.elements;
    this->offset = a.offset;
    this->count = a.count;
    this->normalized = a.normalized;
    this->perInstance = a.perInstance;
}

Attribute& Attribute::operator = (const Attribute& a){
    this->dataType = a.dataType;
    this->bufferName = a.bufferName;
    this->elements = a.elements;
    this->offset = a.offset;
    this->count = a.count;
    this->normalized = a.normalized;
    this->perInstance = a.perInstance;

    return *this;
}

AttributeDataType Attribute::getDataType() const {
    return dataType;
}

void Attribute::setDataType(AttributeDataType dataType) {
    Attribute::dataType = dataType;
}

const std::string &Attribute::getBufferName() const{
    return bufferName;
}

void Attribute::setBufferName(const std::string &bufferName){
    this->bufferName = bufferName;
}

unsigned int Attribute::getElements() const{
    return elements;
}

void Attribute::setElements(unsigned int elements){
    this->elements = elements;
}

const size_t &Attribute::getOffset() const{
    return offset;
}

void Attribute::setOffset(const size_t &offset){
    this->offset = offset;
}

unsigned int Attribute::getCount() const {
    return count;
}

void Attribute::setCount(unsigned int count) {
    Attribute::count = count;
}

bool Attribute::getNormalized() const {
    return normalized;
}

void Attribute::setNormalized(bool normalized) {
    this->normalized = normalized;
}

bool Attribute::getPerInstance() const {
    return perInstance;
}

void Attribute::setPerInstance(bool perInstance) {
    this->perInstance = perInstance;
}
