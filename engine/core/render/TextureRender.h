//
// (c) 2026 Eduardo Doria.
//

#ifndef TextureRender_h
#define TextureRender_h

#include "Render.h"
#include "sokol/SokolTexture.h"

namespace doriax {

    class DORIAX_API TextureRender{

    public:
        //***Backend***
        SokolTexture backend;
        //***

        TextureRender();
        TextureRender(const TextureRender& rhs);
        TextureRender& operator=(const TextureRender& rhs);

        virtual ~TextureRender();

        bool createTexture(
                const std::string& label, int width, int height,
                ColorFormat colorFormat, TextureType type, int numFaces, void* data[6], size_t size[6],
                TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);

        bool createFramebufferTexture(
                TextureType type, bool depth, bool shadowMap, int width, int height, 
                TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV);

        void destroyTexture();

        uint32_t getGLHandler() const;
        const void* getMetalHandler() const;
        const void* getD3D11Handler() const;

        bool isCreated();
    };
}


#endif /* TextureRender_h */