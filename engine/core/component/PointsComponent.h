// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef POINTS_COMPONENT_H
#define POINTS_COMPONENT_H

#include "util/SpriteFrameData.h"
#include "util/HybridArray.h"
#include "buffer/ExternalBuffer.h"
#include "render/ObjectRender.h"
#include "render/ShaderRender.h"
#include "texture/Texture.h"
#include "shader/ShaderParameters.h"
#include "shader/ShaderTextures.h"
#include "math/AABB.h"
#include "Engine.h"
#include <string>

namespace doriax{

    struct PointData{
        Vector3 position = Vector3(0.0, 0.0, 0.0);
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0);
        float size = 1; // world-space diameter (matches mesh extent at unit scale)
        float rotation = 0;
        Rect textureRect = Rect(0.0, 0.0, 1.0, 1.0);
        bool visible = true;
    };

    struct PointRenderData{
        Vector3 position;
        Vector4 color;
        float size;
        float rotation;
        Rect textureRect;
    };

    struct DORIAX_API PointsComponent{
        bool loaded = false;
        bool loadCalled = false;

        ExternalBuffer buffer;

        std::vector<PointData> points;
        std::vector<PointRenderData> renderPoints; //must be sorted

        unsigned int numFramesRect = 0;
        HybridArray<SpriteFrameData, MAX_SPRITE_FRAMES> framesRect;

        unsigned int maxPoints = 100;
        unsigned int numVisible = 0;
        AABB aabb; // local bounds of the visible points

        ObjectRender render;
        std::shared_ptr<ShaderRender> shaderRender;
        uint32_t shaderProperties = 0;
        // .glsl file with the shader hooks, the scene default when empty
        std::string shader;
        uint16_t customShaderId = 0;
        int slotVSParams = -1;
        // values of the shader's declared parameters, by name
        ShaderUniformValues shaderUniforms;
        ShaderTextureValues shaderTextures;
        bool needUpdateShaderUniforms = false;
        ShaderParameterBlock paramsVS;
        ShaderParameterBlock paramsFS;

        Texture texture;

        bool transparent = false;
        bool autoTransparency = true;
        bool pipelineTransparent = false; // transparent when the pipeline was built

        bool hasTextureRect = false;

        bool needUpdate = true;
        bool needUpdateBuffer = false;
        bool needUpdateTexture = false;
        bool needReload = false;
    };
    
}

#endif //POINTS_COMPONENT_H