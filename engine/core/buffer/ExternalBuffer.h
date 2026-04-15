//
// (c) 2026 Eduardo Doria.
//

#ifndef EXTERNALBUFFER_H
#define EXTERNALBUFFER_H

#include "buffer/Buffer.h"
#include <string>

namespace doriax {

    class DORIAX_API ExternalBuffer : public Buffer {

    protected:
        std::string name;

    public:
        ExternalBuffer();
        virtual ~ExternalBuffer();

        ExternalBuffer(const ExternalBuffer& rhs);
        ExternalBuffer& operator=(const ExternalBuffer& rhs);

        void setData(unsigned char* data, size_t size);

        void setName(const std::string& name);
        std::string getName() const;
    };

}


#endif //EXTERNALBUFFER_H
