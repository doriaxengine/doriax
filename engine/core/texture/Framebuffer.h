//
// (c) 2026 Eduardo Doria.
//

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "render/FramebufferRender.h"

namespace doriax{

    class DORIAX_API Framebuffer{

        private:
            FramebufferRender render;
            unsigned int width;
            unsigned int height;
            TextureFilter minFilter;
            TextureFilter magFilter;
            TextureWrap wrapU;
            TextureWrap wrapV;

            unsigned long version; // increment every creation

        public:
            Framebuffer();
            virtual ~Framebuffer();

            Framebuffer(const Framebuffer& rhs);
            Framebuffer& operator=(const Framebuffer& rhs);

            void create();
            void destroy();
            bool isCreated();

            FramebufferRender& getRender();
            unsigned long getVersion();

            void setWidth(unsigned int width);
            unsigned int getWidth() const;

            void setHeight(unsigned int height);
            unsigned int getHeight() const;

            void setMinFilter(TextureFilter filter);
            TextureFilter getMinFilter() const;

            void setMagFilter(TextureFilter filter);
            TextureFilter getMagFilter() const;

            void setWrapU(TextureWrap wrapU);
            TextureWrap getWrapU() const;

            void setWrapV(TextureWrap wrapV);
            TextureWrap getWrapV() const;
    };
}

#endif //FRAMEBUFFER_H