// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SokolBuffer_h
#define SokolBuffer_h

#include "render/Render.h"
#include "sokol_gfx.h"

namespace doriax{
    class SokolBuffer{

    private:
        sg_buffer buffer;
        sg_view view; // only created for storage buffers
        // usage the live buffer was created with: a sokol buffer keeps it for life,
        // so a usage change means the buffer has to be recreated
        BufferUsage createdUsage = BufferUsage::IMMUTABLE;

    public:
        SokolBuffer();
        SokolBuffer(const SokolBuffer& rhs);
        SokolBuffer& operator=(const SokolBuffer& rhs);

        bool createBuffer(unsigned int size, void* data, BufferType type, BufferUsage usage);
        void updateBuffer(unsigned int size, void* data);
        // byte offset of the appended data, or -1 when it does not fit this frame
        int appendBuffer(unsigned int size, const void* data);
        void destroyBuffer();
        bool isCreated();
        BufferUsage getCreatedUsage() const;

        // the shared buffers belong to the current sokol context
        static void clearSharedBuffers();

        sg_buffer get();
        sg_view getView();
    };
}

#endif /* SokolBuffer_h */
