// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef ObjectRender_h
#define ObjectRender_h

#include "texture/Texture.h"
#include "Render.h"

#include "sokol/SokolObject.h"

namespace doriax {

    class DORIAX_API ObjectRender{

    public:

        //***Backend***
        SokolObject backend;
        //***

        PrimitiveType primitiveType = PrimitiveType::TRIANGLES;

        ObjectRender();
        ObjectRender(const ObjectRender& rhs);
        ObjectRender& operator=(const ObjectRender& rhs);
        virtual ~ObjectRender();

        void beginLoad(PrimitiveType primitiveType);
        void setShader(ShaderRender* shader);
        void setIndex(BufferRender* buffer, AttributeDataType dataType, size_t offset);
        void addAttribute(int slot, BufferRender* buffer, unsigned int elements, AttributeDataType dataType, unsigned int stride, size_t offset, bool normalized, bool perInstance);
        void addStorageBuffer(int slot, ShaderStageType stage, BufferRender* buffer);
        // swap the vertex buffer bound at load (original) for one of identical layout
        // (replacement), just before draw — used to bind a per-view instance buffer
        void replaceVertexBuffer(BufferRender* original, BufferRender* replacement, size_t byteOffset = 0);
        // draw from another index buffer of the same element type (a LOD range); resetIndexBuffer
        // returns to the one bound at load
        void setIndexBuffer(BufferRender* buffer, size_t byteOffset = 0);
        void resetIndexBuffer();
        void addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture);
        bool endLoad(uint16_t pipelines, bool enableFaceCulling, bool enableDepthWrite, CullingMode cullingMode, WindingOrder windingOrder, bool prepassFaceCulling = true);

        // same pipeline and bindings apart from the buffers: one draw can render both
        bool sharesState(const ObjectRender& other, PipelineType pipType) const;
        bool sharesPipeline(const ObjectRender& other, PipelineType pipType) const;
        // what sharesState compares, for every pipeline type
        void getStateKey(std::vector<uint32_t>& key) const;
        // draws sorted by it change pipeline and bindings less often
        uint64_t getSortKey(PipelineType pipType) const;
        bool beginDraw(PipelineType pipType);
        void applyUniformBlock(int slot, unsigned int count, void* data);
        // baseElement skips that many elements of the bound index (or vertex) range,
        // used to draw a single sub-range of a submesh, like one tilemap chunk
        void draw(unsigned int baseElement, unsigned int vertexCount, unsigned int instanceCount);

        void destroy();
    };
}


#endif /* ObjectRender_h */
