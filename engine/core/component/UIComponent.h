// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef UI_COMPONENT_H
#define UI_COMPONENT_H

#include "texture/Texture.h"
#include "render/ObjectRender.h"
#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "shader/ShaderUniforms.h"
#include "math/AABB.h"
#include <string>

namespace doriax{

    struct DORIAX_API UIComponent{
        // Render part
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;
        IndexBuffer indices;

        unsigned int minBufferCount = 0;
        unsigned int minIndicesCount = 0;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        uint32_t shaderProperties = 0;
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

        struct CustomTextureBinding
        {
            std::string name;
            std::pair<int, int> slot = {-1, -1};
            TextureRender* render = nullptr;
        };
        std::vector<CustomTextureBinding> customTextureBindings;

        std::vector<std::pair<std::string, Texture>> customTextures;
        bool needUpdateCustomTextures = false;

        PrimitiveType primitiveType = PrimitiveType::TRIANGLES;
        unsigned int vertexCount = 0;

        AABB aabb = AABB::ZERO;
        AABB worldAABB; // initially NULL

        Texture texture;
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0); //linear color

        FunctionSubscribe<void()> onGetFocus;
        FunctionSubscribe<void()> onLostFocus;
        FunctionSubscribe<void(float, float)> onPointerEnter;
        FunctionSubscribe<void(float, float)> onPointerLeave;
        FunctionSubscribe<void(float, float)> onPointerMove;
        FunctionSubscribe<void(float, float)> onPointerDown;
        FunctionSubscribe<void(float, float)> onPointerUp;
        FunctionSubscribe<void(float, float)> onClick;
        FunctionSubscribe<void(float, float)> onDoubleClick;
        FunctionSubscribe<void(float, float)> onDragStart;
        FunctionSubscribe<void(float, float)> onDrag;
        FunctionSubscribe<void(float, float)> onDragEnd;

        bool automaticFlipY = true;
        bool flipY = false;

        bool pointerMoved = false;
        bool focused = false;

        bool needReload = false;

        bool needUpdateAABB = false;
        bool needUpdateBuffer = false;
        bool needUpdateTexture = false;
    };

}

#endif //UI_COMPONENT_H