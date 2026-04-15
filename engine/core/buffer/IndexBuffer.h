//
// (c) 2026 Eduardo Doria.
//

#ifndef INDEXBUFFER_H
#define INDEXBUFFER_H

#include "buffer/Buffer.h"
#include <vector>

namespace doriax {

    class DORIAX_API IndexBuffer: public Buffer {

    private:
        std::vector<unsigned char> vectorBuffer;

    public:
        IndexBuffer();
        virtual ~IndexBuffer();

        IndexBuffer(const IndexBuffer& rhs);
        IndexBuffer& operator=(const IndexBuffer& rhs);

        void createIndexAttribute();

        virtual bool increase(size_t newSize);
        virtual void clearAll();
        virtual void clear();

    };

}


#endif //INDEXBUFFER_H
