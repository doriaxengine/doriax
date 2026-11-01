// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef sokolobject_h
#define sokolobject_h

#include "render/Render.h"
#include "buffer/Attribute.h"
#include "render/BufferRender.h"
#include "render/ShaderRender.h"
#include "render/TextureRender.h"

#include "sokol_gfx.h"

#include <map>
#include <vector>
#include <memory>
#include <tuple>


namespace doriax{

    struct UniformStageSlot{
        sg_shader_stage stage;
        int slot;
    };

    struct BufferInfo{
        uint32_t id;
        size_t offset;
        unsigned int stride;
        bool perInstance;

        bool operator==(const BufferInfo &o) const {
            return id == o.id && offset == o.offset && stride == o.stride && perInstance == o.perInstance;
        }

        bool operator<(const BufferInfo &o) const {
            return std::tie(id, offset, stride, perInstance) < std::tie(o.id, o.offset, o.stride, o.perInstance);
        }
    };

    class SokolObject{

    private:

        sg_bindings bind;
        sg_buffer loadIndexBuffer;
        size_t loadIndexOffset;

        sg_pipeline pip;
        sg_pipeline depth_pip;
        sg_pipeline shadow_depth_pip;
        sg_pipeline rtt_pip;
        sg_pipeline rtt_invert_pip;
        sg_pipeline gbuffer_pip;
        sg_pipeline nodepth_pip;
        sg_pipeline rtt_nodepth_pip;
        sg_pipeline rtt_noblend_pip;
        sg_pipeline rtt_coloronly_pip;
        sg_pipeline zprepass_pip;
        sg_pipeline zprepass_rtt_pip;

        std::unique_ptr<sg_pipeline_desc> loadPipelineDesc; // only while loading

        size_t bindSlotIndex;

        std::map< BufferInfo, size_t > bufferToBindSlot;


        sg_vertex_format getVertexFormat(unsigned int elements, AttributeDataType dataType, bool normalized);
        sg_primitive_type getPrimitiveType(PrimitiveType primitiveType);
        sg_cull_mode getCullMode(CullingMode cullingMode);
        sg_face_winding getFaceWinding(WindingOrder windingOrder);
        sg_pipeline getPipeline(PipelineType pipType) const;
        sg_pipeline_desc& loadingDesc();
        bool makePipelines(const sg_pipeline_desc& pipeline_desc, uint16_t pipelines, bool enableFaceCulling, bool enableDepthWrite, CullingMode cullingMode, WindingOrder windingOrder, bool prepassFaceCulling);

    public:

        SokolObject();
        SokolObject(const SokolObject& rhs);
        SokolObject& operator=(const SokolObject& rhs);

        void beginLoad(PrimitiveType primitiveType);
        void setShader(ShaderRender* shader);
        void setIndex(BufferRender* buffer, AttributeDataType dataType, size_t offset);
        void addAttribute(int slot, BufferRender* buffer, unsigned int elements, AttributeDataType dataType, unsigned int stride, size_t offset, bool normalized, bool perInstance);
        // rebind every vertex bind-slot that was filled from fromBufferId to toBuffer,
        // keeping the same per-slot layout/offset (used to swap a per-view instance
        // buffer in just before draw). Affects only the bindings, not the pipeline.
        void replaceVertexBuffer(uint32_t fromBufferId, sg_buffer toBuffer, size_t byteOffset);
        void setIndexBuffer(sg_buffer buffer, size_t byteOffset);
        void resetIndexBuffer();
        void addStorageBuffer(int slot, ShaderStageType stage, BufferRender* buffer);
        void addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture);
        // prepassFaceCulling applies to the PIP_ZPREPASS pipelines, which must cull like the color pass
        bool endLoad(uint16_t pipelines, bool enableFaceCulling, bool enableDepthWrite, CullingMode cullingMode, WindingOrder windingOrder, bool prepassFaceCulling = true);

        // same pipeline and bindings apart from the buffers: one draw can render both
        bool sharesState(const SokolObject& other, PipelineType pipType) const;
        bool sharesPipeline(const SokolObject& other, PipelineType pipType) const;
        void getStateKey(std::vector<uint32_t>& key) const;
        uint64_t getSortKey(PipelineType pipType) const;
        bool beginDraw(PipelineType pipType);
        void applyUniformBlock(int slot, unsigned int count, void* data);
        void draw(unsigned int baseElement, unsigned int vertexCount, unsigned int instanceCount);

        void destroy();

        // the shared pipelines belong to the current sokol context
        static void clearSharedPipelines();
        // the next draw applies pipeline and bindings again: called when a pass ends, when a buffer
        // or image is updated (sokol may move it to another backend resource) and on sokol errors
        static void resetAppliedState();

    };
}
#endif //sokolobject_h
