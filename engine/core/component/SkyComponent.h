// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SKY_COMPONENT_H
#define SKY_COMPONENT_H

#include "buffer/Buffer.h"
#include "texture/Texture.h"
#include "shader/ShaderUniforms.h"
#include <string>

namespace doriax{

    struct DORIAX_API SkyComponent{
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;

        Matrix4 skyViewProjectionMatrix;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        // optional user-forked shader: project-relative base path (no extension),
        // empty = built-in. customShaderId is the resolved id for ShaderPool keys.
        std::string customShader;
        uint16_t customShaderId = 0;
        int slotVSParams = -1;
        int slotFSParams = -1;
        // values of the fork's custom uniform blocks, by member name
        ShaderUniformValues shaderUniforms;
        bool needUpdateShaderUniforms = false;
        CustomUniformBlock customVSParams;
        CustomUniformBlock customFSParams;

        struct CustomTextureBinding {
            std::string name;
            std::pair<int, int> slot = {-1, -1};
            TextureRender* render = nullptr;
        };
        std::vector<CustomTextureBinding> customTextureBindings;

        std::vector<std::pair<std::string, Texture>> customTextures;
        bool needUpdateCustomTextures = false;

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