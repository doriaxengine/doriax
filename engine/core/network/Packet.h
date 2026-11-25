// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef PACKET_H
#define PACKET_H

#include "Export.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Quaternion.h"
#include <cstdint>
#include <string>
#include <vector>

namespace doriax {

    // Tags of the values an RPC carries, part of the wire format
    enum class PacketValue : uint8_t {
        NIL,
        BOOL_FALSE,
        BOOL_TRUE,
        INT,
        FLOAT,
        DOUBLE,
        STRING,
        VECTOR2,
        VECTOR3,
        VECTOR4,
        QUATERNION,
        NONE = 255 // no value left to read
    };

    // Bytes of a network message; a read past the end returns zero and invalidates it
    class DORIAX_API Packet {
    private:
        std::vector<uint8_t> data;
        size_t position = 0;
        bool valid = true;

        bool take(void* out, size_t size);

    public:
        Packet();
        Packet(const uint8_t* data, size_t size);

        const uint8_t* getData() const;
        size_t getSize() const;
        size_t getRemaining() const;
        bool isValid() const;
        void clear();
        // moves the reading position
        void skip(size_t size);

        void writeBool(bool value);
        void writeUInt8(uint8_t value);
        void writeUInt16(uint16_t value);
        void writeUInt32(uint32_t value);
        // variable length, small values take one byte
        void writeInt(int64_t value);
        void writeFloat(float value);
        void writeDouble(double value);
        void writeString(const std::string& value);
        void writeVector2(const Vector2& value);
        void writeVector3(const Vector3& value);
        void writeVector4(const Vector4& value);
        void writeQuaternion(const Quaternion& value);
        void writeBytes(const uint8_t* bytes, size_t size);

        bool readBool();
        uint8_t readUInt8();
        uint16_t readUInt16();
        uint32_t readUInt32();
        int64_t readInt();
        float readFloat();
        double readDouble();
        std::string readString();
        Vector2 readVector2();
        Vector3 readVector3();
        Vector4 readVector4();
        Quaternion readQuaternion();

        // tagged values, checked by the reader
        void writeNil();
        void writeValue(bool value);
        void writeValue(int value);
        void writeValue(unsigned int value);
        void writeValue(long value);
        void writeValue(unsigned long value);
        void writeValue(long long value);
        void writeValue(unsigned long long value);
        void writeValue(float value);
        void writeValue(double value);
        void writeValue(const char* value);
        void writeValue(const std::string& value);
        void writeValue(const Vector2& value);
        void writeValue(const Vector3& value);
        void writeValue(const Vector4& value);
        void writeValue(const Quaternion& value);

        // NONE at the end of the packet
        PacketValue peekValue() const;

        // false on another type, numbers convert to each other
        bool readNil();
        bool readValue(bool& value);
        bool readValue(int& value);
        bool readValue(unsigned int& value);
        bool readValue(long& value);
        bool readValue(unsigned long& value);
        bool readValue(long long& value);
        bool readValue(unsigned long long& value);
        bool readValue(float& value);
        bool readValue(double& value);
        bool readValue(std::string& value);
        bool readValue(Vector2& value);
        bool readValue(Vector3& value);
        bool readValue(Vector4& value);
        bool readValue(Quaternion& value);
    };

}

#endif //PACKET_H
