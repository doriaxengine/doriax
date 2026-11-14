// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SKY_COMPONENT_H
#define SKY_COMPONENT_H

#include "buffer/Buffer.h"
#include "texture/Texture.h"
#include "shader/ShaderParameters.h"
#include "shader/ShaderTextures.h"
#include <string>

namespace doriax{

    struct DORIAX_API SkyComponent{
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;

        Matrix4 skyViewProjectionMatrix;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shaderRender;
        // .glsl file with the shader hooks, the scene default when empty
        std::string shader;
        uint16_t customShaderId = 0;
        int slotVSParams = -1;
        int slotFSParams = -1;
        // values of the shader's declared parameters, by name
        ShaderUniformValues shaderUniforms;
        ShaderTextureValues shaderTextures;
        bool needUpdateShaderUniforms = false;
        ShaderParameterBlock paramsVS;
        ShaderParameterBlock paramsFS;

        Texture texture;
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0); //sRGB

        float rotation = 0;

        bool visible = true; // when false the sky is not drawn but still feeds IBL

        // IBL environment maps generated from texture (shared via TexturePool,
        // generation is expensive so maps are cached by sky texture id)
        std::shared_ptr<TextureRender> irradianceMap;
        std::shared_ptr<TextureRender> prefilteredMap;
        bool envMapsLoaded = false;
        bool needUpdateEnvironment = true;

        bool needUpdateTexture = false;
        bool needUpdateSky = true;
        bool needReload = false;
    };
    
}

#endif //SKY_COMPONENT_H