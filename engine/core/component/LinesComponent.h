// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LINES_COMPONENT_H
#define LINES_COMPONENT_H

#include "buffer/Buffer.h"
#include "shader/ShaderUniforms.h"
#include "texture/Texture.h"
#include "render/TextureRender.h"
#include "Engine.h"
#include <string>
#include <utility>
#include <vector>

namespace doriax{

    struct LineData{
        Vector3 pointA = Vector3(0,0,0);
        Vector4 colorA = Vector4(1,1,1,1);

        Vector3 pointB = Vector3(0,0,0);
        Vector4 colorB = Vector4(1,1,1,1);

        bool operator==(const LineData& other) const {
            return pointA == other.pointA &&
                colorA == other.colorA &&
                pointB == other.pointB &&
                colorB == other.colorB;
        }

        bool operator!=(const LineData& other) const {
            return !(*this == other);
        }
    };

    struct DORIAX_API LinesComponent{
        bool loaded = false;
        bool loadCalled = false;

        ExternalBuffer buffer;

        std::vector<LineData> lines;

        unsigned int maxLines = 100;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        uint32_t shaderProperties = 0;
        // optional user-forked shader: project-relative base path (no extension),
        // empty = built-in. customShaderId is the resolved id for ShaderPool keys.
        std::string customShader;
        uint16_t customShaderId = 0;
        int slotVSParams = -1;
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

        bool needUpdateBuffer = false;
        bool needReload = false;
    };
    
}

#endif //LINES_COMPONENT_H