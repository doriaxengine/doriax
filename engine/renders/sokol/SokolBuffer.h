//
// (c) 2026 Eduardo Doria.
//

#ifndef SokolBuffer_h
#define SokolBuffer_h

#include "render/Render.h"
#include "sokol_gfx.h"

namespace doriax{
    class SokolBuffer{

    private:
        sg_buffer buffer;

    public:
        SokolBuffer();
        SokolBuffer(const SokolBuffer& rhs);
        SokolBuffer& operator=(const SokolBuffer& rhs);

        bool createBuffer(unsigned int size, void* data, BufferType type, BufferUsage usage);
        void updateBuffer(unsigned int size, void* data);
        void destroyBuffer();
        bool isCreated();

        sg_buffer get();
    };
}

#endif /* SokolBuffer_h */
