//
// (c) 2026 Eduardo Doria.
//

#ifndef sokolobject_h
#define sokolobject_h

#include "render/Render.h"
#include "buffer/Attribute.h"
#include "render/BufferRender.h"
#include "render/ShaderRender.h"
#include "render/TextureRender.h"

#include "sokol_gfx.h"

#include <map>


namespace doriax{

    struct UniformStageSlot{
        sg_shader_stage stage;
        int slot;
    };

    struct BufferInfo{
        uint32_t id;
        size_t offset;

        bool operator==(const BufferInfo &o) const {
            return id == o.id && offset == o.offset;
        }

        bool operator<(const BufferInfo &o) const {
            return id < o.id || (id == o.id && offset < o.offset);
        }
    };

    class SokolObject{

    private:

        sg_bindings bind;

        sg_pipeline pip;
        sg_pipeline depth_pip;
        sg_pipeline rtt_pip;

        sg_pipeline_desc pipeline_desc;

        size_t bindSlotIndex;

        std::map< BufferInfo, size_t > bufferToBindSlot;


        sg_vertex_format getVertexFormat(unsigned int elements, AttributeDataType dataType, bool normalized);
        sg_primitive_type getPrimitiveType(PrimitiveType primitiveType);
        sg_cull_mode getCullMode(CullingMode cullingMode);
        sg_face_winding getFaceWinding(WindingOrder windingOrder);

    public:

        SokolObject();
        SokolObject(const SokolObject& rhs);
        SokolObject& operator=(const SokolObject& rhs);

        void beginLoad(PrimitiveType primitiveType);
        void setShader(ShaderRender* shader);
        void setIndex(BufferRender* buffer, AttributeDataType dataType, size_t offset);
        void addAttribute(int slot, BufferRender* buffer, unsigned int elements, AttributeDataType dataType, unsigned int stride, size_t offset, bool normalized, bool perInstance);
        void addStorageBuffer(int slot, ShaderStageType stage, BufferRender* buffer);
        void addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture);
        bool endLoad(uint8_t pipelines, bool enableFaceCulling, CullingMode cullingMode, WindingOrder windingOrder);

        bool beginDraw(PipelineType pipType);
        void applyUniformBlock(int slot, unsigned int count, void* data);
        void draw(unsigned int vertexCount, unsigned int instanceCount);

        void destroy();

    };
}
#endif //sokolobject_h
