//
// (c) 2026 Eduardo Doria.
//

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

        ObjectRender();
        ObjectRender(const ObjectRender& rhs);
        ObjectRender& operator=(const ObjectRender& rhs);
        virtual ~ObjectRender();

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


#endif /* ObjectRender_h */
