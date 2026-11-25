// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Packet.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace doriax;

// the tagged read of every number type, takes nothing when it fails
template<typename T>
bool Packet::readNumber(T& value){
    PacketValue type = peekValue();
    if (type != PacketValue::INT && type != PacketValue::FLOAT && type != PacketValue::DOUBLE){
        return false;
    }

    size_t start = position;
    position++;

    bool fits;
    if (type == PacketValue::INT){
        int64_t number = readInt();
        value = (T)number;
        fits = std::is_floating_point<T>::value || ((int64_t)value == number && (number >= 0 || std::is_signed<T>::value));
    }else{
        double number = (type == PacketValue::FLOAT) ? readFloat() : readDouble();
        double lowest = (double)std::numeric_limits<T>::lowest();
        double highest = (double)std::numeric_limits<T>::max();
        if (std::is_integral<T>::value){
            fits = number >= lowest && number < highest + 1.0;
        }else{
            fits = !std::isfinite(number) || (number >= lowest && number <= highest);
        }
        if (fits){
            value = (T)number;
        }
    }

    if (!fits){
        position = start;
    }

    return valid && fits;
}

Packet::Packet(){
}

Packet::Packet(const uint8_t* data, size_t size){
    this->data.assign(data, data + size);
}

bool Packet::take(void* out, size_t size){
    if (!valid || size > data.size() - position){
        valid = false;
        memset(out, 0, size);
        return false;
    }

    memcpy(out, data.data() + position, size);
    position += size;

    return true;
}

const uint8_t* Packet::getData() const{
    return data.data();
}

size_t Packet::getSize() const{
    return data.size();
}

size_t Packet::getRemaining() const{
    return data.size() - position;
}

bool Packet::isValid() const{
    return valid;
}

void Packet::skip(size_t size){
    if (!valid || size > getRemaining()){
        valid = false;
        return;
    }
    position += size;
}

void Packet::writeUInt8(uint8_t value){
    data.push_back(value);
}

void Packet::writeUInt16(uint16_t value){
    data.push_back((uint8_t)value);
    data.push_back((uint8_t)(value >> 8));
}

void Packet::writeUInt32(uint32_t value){
    for (int i = 0; i < 4; i++){
        data.push_back((uint8_t)(value >> (8 * i)));
    }
}

void Packet::writeInt(int64_t value){
    // zigzag, so small negative numbers stay small
    uint64_t encoded = ((uint64_t)value << 1) ^ (uint64_t)(value >> 63);
    while (encoded >= 0x80){
        data.push_back((uint8_t)(encoded | 0x80));
        encoded >>= 7;
    }
    data.push_back((uint8_t)encoded);
}

void Packet::writeFloat(float value){
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    writeUInt32(bits);
}

void Packet::writeDouble(double value){
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; i++){
        data.push_back((uint8_t)(bits >> (8 * i)));
    }
}

void Packet::writeString(const std::string& value){
    writeInt((int64_t)value.size());
    data.insert(data.end(), value.begin(), value.end());
}

void Packet::writeVector2(const Vector2& value){
    writeFloat(value.x);
    writeFloat(value.y);
}

void Packet::writeVector3(const Vector3& value){
    writeFloat(value.x);
    writeFloat(value.y);
    writeFloat(value.z);
}

void Packet::writeVector4(const Vector4& value){
    writeFloat(value.x);
    writeFloat(value.y);
    writeFloat(value.z);
    writeFloat(value.w);
}

void Packet::writeQuaternion(const Quaternion& value){
    writeFloat(value.w);
    writeFloat(value.x);
    writeFloat(value.y);
    writeFloat(value.z);
}

void Packet::writeBytes(const uint8_t* bytes, size_t size){
    data.insert(data.end(), bytes, bytes + size);
}

uint8_t Packet::readUInt8(){
    uint8_t value;
    take(&value, 1);

    return value;
}

uint16_t Packet::readUInt16(){
    uint8_t bytes[2];
    take(bytes, 2);

    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

uint32_t Packet::readUInt32(){
    uint8_t bytes[4];
    take(bytes, 4);

    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int64_t Packet::readInt(){
    uint64_t encoded = 0;
    for (int shift = 0; shift < 64; shift += 7){
        uint8_t byte = readUInt8();
        encoded |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)){
            return (int64_t)(encoded >> 1) ^ -(int64_t)(encoded & 1);
        }
    }

    valid = false;

    return 0;
}

float Packet::readFloat(){
    uint32_t bits = readUInt32();
    float value;
    memcpy(&value, &bits, sizeof(value));

    return value;
}

double Packet::readDouble(){
    uint8_t bytes[8];
    take(bytes, 8);

    uint64_t bits = 0;
    for (int i = 0; i < 8; i++){
        bits |= (uint64_t)bytes[i] << (8 * i);
    }
    double value;
    memcpy(&value, &bits, sizeof(value));

    return value;
}

std::string Packet::readString(){
    int64_t size = readInt();
    if (!valid || size < 0 || (uint64_t)size > getRemaining()){
        valid = false;
        return "";
    }

    std::string value((const char*)data.data() + position, (size_t)size);
    position += (size_t)size;

    return value;
}

Vector2 Packet::readVector2(){
    float x = readFloat();
    float y = readFloat();

    return Vector2(x, y);
}

Vector3 Packet::readVector3(){
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();

    return Vector3(x, y, z);
}

Vector4 Packet::readVector4(){
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    float w = readFloat();

    return Vector4(x, y, z, w);
}

Quaternion Packet::readQuaternion(){
    float w = readFloat();
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();

    return Quaternion(w, x, y, z);
}

void Packet::writeNil(){
    writeUInt8((uint8_t)PacketValue::NIL);
}

void Packet::writeValue(bool value){
    writeUInt8((uint8_t)(value ? PacketValue::BOOL_TRUE : PacketValue::BOOL_FALSE));
}

void Packet::writeValue(int value){
    writeValue((long long)value);
}

void Packet::writeValue(unsigned int value){
    writeValue((long long)value);
}

void Packet::writeValue(long value){
    writeValue((long long)value);
}

void Packet::writeValue(unsigned long value){
    writeValue((long long)value);
}

void Packet::writeValue(long long value){
    writeUInt8((uint8_t)PacketValue::INT);
    writeInt(value);
}

void Packet::writeValue(unsigned long long value){
    writeValue((long long)value);
}

void Packet::writeValue(float value){
    writeUInt8((uint8_t)PacketValue::FLOAT);
    writeFloat(value);
}

void Packet::writeValue(double value){
    writeUInt8((uint8_t)PacketValue::DOUBLE);
    writeDouble(value);
}

void Packet::writeValue(const char* value){
    writeValue(std::string(value ? value : ""));
}

void Packet::writeValue(const std::string& value){
    writeUInt8((uint8_t)PacketValue::STRING);
    writeString(value);
}

void Packet::writeValue(const Vector2& value){
    writeUInt8((uint8_t)PacketValue::VECTOR2);
    writeVector2(value);
}

void Packet::writeValue(const Vector3& value){
    writeUInt8((uint8_t)PacketValue::VECTOR3);
    writeVector3(value);
}

void Packet::writeValue(const Vector4& value){
    writeUInt8((uint8_t)PacketValue::VECTOR4);
    writeVector4(value);
}

void Packet::writeValue(const Quaternion& value){
    writeUInt8((uint8_t)PacketValue::QUATERNION);
    writeQuaternion(value);
}

PacketValue Packet::peekValue() const{
    if (!valid || position >= data.size()){
        return PacketValue::NONE;
    }

    return (PacketValue)data[position];
}

bool Packet::readNil(){
    if (peekValue() != PacketValue::NIL){
        return false;
    }
    position++;

    return true;
}

bool Packet::readValue(bool& value){
    PacketValue type = peekValue();
    if (type != PacketValue::BOOL_TRUE && type != PacketValue::BOOL_FALSE){
        return false;
    }
    position++;
    value = (type == PacketValue::BOOL_TRUE);

    return true;
}

bool Packet::readValue(int& value){
    return readNumber(value);
}

bool Packet::readValue(unsigned int& value){
    return readNumber(value);
}

bool Packet::readValue(long& value){
    return readNumber(value);
}

bool Packet::readValue(unsigned long& value){
    return readNumber(value);
}

bool Packet::readValue(long long& value){
    return readNumber(value);
}

bool Packet::readValue(unsigned long long& value){
    return readNumber(value);
}

bool Packet::readValue(float& value){
    return readNumber(value);
}

bool Packet::readValue(double& value){
    return readNumber(value);
}

bool Packet::readValue(std::string& value){
    if (peekValue() != PacketValue::STRING){
        return false;
    }
    position++;
    value = readString();

    return valid;
}

bool Packet::readValue(Vector2& value){
    if (peekValue() != PacketValue::VECTOR2){
        return false;
    }
    position++;
    value = readVector2();

    return valid;
}

bool Packet::readValue(Vector3& value){
    if (peekValue() != PacketValue::VECTOR3){
        return false;
    }
    position++;
    value = readVector3();

    return valid;
}

bool Packet::readValue(Vector4& value){
    if (peekValue() != PacketValue::VECTOR4){
        return false;
    }
    position++;
    value = readVector4();

    return valid;
}

bool Packet::readValue(Quaternion& value){
    if (peekValue() != PacketValue::QUATERNION){
        return false;
    }
    position++;
    value = readQuaternion();

    return valid;
}
