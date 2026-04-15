//
// (c) 2026 Eduardo Doria.
//

#ifndef SKY_COMPONENT_H
#define SKY_COMPONENT_H

#include "buffer/Buffer.h"
#include "texture/Texture.h"

namespace doriax{

    struct DORIAX_API SkyComponent{
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;

        Matrix4 skyViewProjectionMatrix;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        int slotVSParams = -1;
        int slotFSParams = -1;

        Texture texture;
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0); //sRGB

        float rotation = 0;

        bool needUpdateTexture = false;
        bool needUpdateSky = true;
        bool needReload = false;
    };
    
}

#endif //SKY_COMPONENT_H