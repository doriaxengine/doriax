//
// (c) 2026 Eduardo Doria.
//

#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#include <array>
#include <string>
#include "render/Render.h"
#include "Export.h"

namespace doriax {

    class DORIAX_API Attribute {

        friend class Buffer;

    private:
        AttributeDataType dataType;
        std::string bufferName;
        unsigned int elements;
        size_t offset;
        unsigned int count;
        bool normalized;
        bool perInstance;

    public:

        Attribute();
        Attribute(AttributeDataType dataType, const std::string& bufferName, unsigned int elements, size_t offset, unsigned int count, bool normalized, bool perInstance);
        Attribute(const Attribute& a);
        virtual ~Attribute();

        Attribute& operator = (const Attribute& s);

        AttributeDataType getDataType() const;
        void setDataType(AttributeDataType dataType);

        const std::string &getBufferName() const;
        void setBufferName(const std::string &bufferName);

        unsigned int getElements() const;
        void setElements(unsigned int elements);

        const size_t &getOffset() const;
        void setOffset(const size_t &offset);

        unsigned int getCount() const;
        void setCount(unsigned int count);

        bool getNormalized() const;
        void setNormalized(bool normalized);

        bool getPerInstance() const;
        void setPerInstance(bool perInstance);

    };

}


#endif //ATTRIBUTE_H
