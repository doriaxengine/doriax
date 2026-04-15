//
// (c) 2026 Eduardo Doria.
//

#include "SokolObject.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "Engine.h"

using namespace doriax;


SokolObject::SokolObject(){
    pip.id = SG_INVALID_ID;
    depth_pip.id = SG_INVALID_ID;
    rtt_pip.id = SG_INVALID_ID;
    bind = {}; 
    pipeline_desc = {};
    bindSlotIndex = 0;
}

SokolObject::SokolObject(const SokolObject& rhs) {
    bind = rhs.bind; 
    pip = rhs.pip;
    depth_pip = rhs.depth_pip;
    rtt_pip = rhs.rtt_pip;
    pipeline_desc = rhs.pipeline_desc;
    bindSlotIndex = rhs.bindSlotIndex;
    bufferToBindSlot = rhs.bufferToBindSlot;
}

SokolObject& SokolObject::operator=(const SokolObject& rhs) { 
    bind = rhs.bind; 
    pip = rhs.pip;
    depth_pip = rhs.depth_pip;
    rtt_pip = rhs.rtt_pip;
    pipeline_desc = rhs.pipeline_desc;
    bindSlotIndex = rhs.bindSlotIndex;
    bufferToBindSlot = rhs.bufferToBindSlot;
    
    return *this;
}

sg_vertex_format SokolObject::getVertexFormat(unsigned int elements, AttributeDataType dataType, bool normalized){
    if (dataType == AttributeDataType::BYTE){
        if (elements == 4)
            return (normalized)?SG_VERTEXFORMAT_BYTE4N:SG_VERTEXFORMAT_BYTE4;
    }
    if (dataType == AttributeDataType::UNSIGNED_BYTE){
        if (elements == 4)
            return (normalized)?SG_VERTEXFORMAT_UBYTE4N:SG_VERTEXFORMAT_UBYTE4;
    }
    if (dataType == AttributeDataType::SHORT){
        if (elements == 2)
            return (normalized)?SG_VERTEXFORMAT_SHORT2N:SG_VERTEXFORMAT_SHORT2;
        if (elements == 4)
            return (normalized)?SG_VERTEXFORMAT_SHORT4N:SG_VERTEXFORMAT_SHORT4;
    }
    if (dataType == AttributeDataType::UNSIGNED_SHORT){
        if (elements == 2)
            return SG_VERTEXFORMAT_USHORT2N;
        if (elements == 4)
            return SG_VERTEXFORMAT_USHORT4N;
    }
    if (dataType == AttributeDataType::FLOAT){
        if (elements == 1)
            return SG_VERTEXFORMAT_FLOAT;
        if (elements == 2)
            return SG_VERTEXFORMAT_FLOAT2;
        if (elements == 3)
            return SG_VERTEXFORMAT_FLOAT3;
        if (elements == 4)
            return SG_VERTEXFORMAT_FLOAT4;
    }
    return SG_VERTEXFORMAT_INVALID;
}

sg_primitive_type SokolObject::getPrimitiveType(PrimitiveType primitiveType){
    if (primitiveType == PrimitiveType::TRIANGLES)
        return SG_PRIMITIVETYPE_TRIANGLES;
    else if (primitiveType == PrimitiveType::TRIANGLE_STRIP)
        return SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    else if (primitiveType == PrimitiveType::LINES)
        return SG_PRIMITIVETYPE_LINES;
    else if (primitiveType == PrimitiveType::POINTS)
        return SG_PRIMITIVETYPE_POINTS;

    return _SG_PRIMITIVETYPE_DEFAULT;
}

sg_cull_mode SokolObject::getCullMode(CullingMode cullingMode){
    if (cullingMode == CullingMode::BACK){
        return SG_CULLMODE_BACK;
    }else if (cullingMode == CullingMode::FRONT){
        return SG_CULLMODE_FRONT;
    }

    return _SG_CULLMODE_DEFAULT;
}

sg_face_winding SokolObject::getFaceWinding(WindingOrder windingOrder){
    if (windingOrder == WindingOrder::CCW){
        return SG_FACEWINDING_CCW;
    }else if (windingOrder == WindingOrder::CW){
        return SG_FACEWINDING_CW;
    }

    return _SG_FACEWINDING_DEFAULT;
}

void SokolObject::beginLoad(PrimitiveType primitiveType){
    bind = {0};
    pip = {0};
    depth_pip = {0};
    rtt_pip = {0};
    pipeline_desc = {0};

    pipeline_desc.primitive_type = getPrimitiveType(primitiveType);

    bindSlotIndex = 0;
    bufferToBindSlot.clear();
}

void SokolObject::setShader(ShaderRender* shader){
    pipeline_desc.shader = shader->backend.get();
}

void SokolObject::setIndex(BufferRender* buffer, AttributeDataType dataType, size_t offset){
    sg_buffer ibuf = buffer->backend.get();
    bind.index_buffer = ibuf;
    bind.index_buffer_offset = offset;

    if (dataType == AttributeDataType::UNSIGNED_SHORT){
        pipeline_desc.index_type = SG_INDEXTYPE_UINT16;
    }else if (dataType == AttributeDataType::UNSIGNED_INT){
        pipeline_desc.index_type = SG_INDEXTYPE_UINT32;
    }else{
        pipeline_desc.index_type = SG_INDEXTYPE_NONE;
    }
}

void SokolObject::addAttribute(int slot, BufferRender* buffer, unsigned int elements, AttributeDataType dataType, unsigned int stride, size_t offset, bool normalized, bool perInstance){
    if (slot != -1){
        sg_buffer vbuf = buffer->backend.get();

        // D3D11 cannot have offset (AlignedByteOffset) bigger than 2048
        // https://github.com/floooh/sokol/issues/818
        // Metal also cannot use large offsets
        size_t bufferOffset = 0;
        size_t attrOffset = 0;

        if (Engine::isOpenGL()){
            bufferOffset = 0;
            attrOffset = offset;
        }else{
            bufferOffset = offset;
            attrOffset = 0;
        }
        
        if (bufferToBindSlot.count({vbuf.id, bufferOffset}) == 0){
            bind.vertex_buffers[bindSlotIndex] = vbuf;
            bind.vertex_buffer_offsets[bindSlotIndex] = bufferOffset;
            bufferToBindSlot[{vbuf.id, bufferOffset}] = bindSlotIndex;

            pipeline_desc.layout.buffers[bindSlotIndex].stride = stride;

            if (perInstance){
                pipeline_desc.layout.buffers[bindSlotIndex].step_func = SG_VERTEXSTEP_PER_INSTANCE;
            }

            bindSlotIndex++;
        }

        size_t indexBuf = bufferToBindSlot[{vbuf.id, bufferOffset}];

        pipeline_desc.layout.attrs[slot].buffer_index = indexBuf;
        pipeline_desc.layout.attrs[slot].offset = attrOffset;
        pipeline_desc.layout.attrs[slot].format = getVertexFormat(elements, dataType, normalized);
    }
}

void SokolObject::addStorageBuffer(int slot, ShaderStageType stage, BufferRender* buffer){
    if (slot != -1){
        sg_buffer sbuf = buffer->backend.get();

        if (stage == ShaderStageType::VERTEX){
            bind.storage_buffers[slot] = sbuf;
        }else if (stage == ShaderStageType::FRAGMENT){
            bind.storage_buffers[slot] = sbuf;
        }
    }
}

void SokolObject::addTexture(std::pair<int, int> slot, ShaderStageType stage, TextureRender* texture){
    if (slot.first != -1){
        sg_image image = texture->backend.get();
        sg_sampler sampler = texture->backend.getSampler();
        if (stage == ShaderStageType::VERTEX){
            bind.images[slot.first] = image;
            bind.samplers[slot.second] = sampler;
        }else if (stage == ShaderStageType::FRAGMENT){
            bind.images[slot.first] = image;
            bind.samplers[slot.second] = sampler;
        }
    }
}

bool SokolObject::endLoad(uint8_t pipelines, bool enableFaceCulling, CullingMode cullingMode, WindingOrder windingOrder){

    if (pipelines & (int)PipelineType::PIP_DEPTH) {
        sg_pipeline_desc pip_depth_desc = pipeline_desc;

        if (enableFaceCulling){
            pip_depth_desc.cull_mode = getCullMode(cullingMode);
            pip_depth_desc.face_winding = getFaceWinding(windingOrder);
        }

        pip_depth_desc.sample_count = 1;
        pip_depth_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
        pip_depth_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_depth_desc.depth.write_enabled = true;
        pip_depth_desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;

        if (Engine::isAsyncThread()){
            depth_pip = SokolCmdQueue::add_command_make_pipeline(pip_depth_desc);
        }else{
            depth_pip = sg_make_pipeline(pip_depth_desc);
        }

        if (depth_pip.id == SG_INVALID_ID){
            return false;
        }
    }

    if (pipelines & (int)PipelineType::PIP_DEFAULT) {
        sg_pipeline_desc pip_default_desc = pipeline_desc;

        if (enableFaceCulling){
            pip_default_desc.cull_mode = getCullMode(cullingMode);
            pip_default_desc.face_winding = getFaceWinding(windingOrder);
        }

        pip_default_desc.depth.write_enabled = true;
        pip_default_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;

        pip_default_desc.colors[0].write_mask = SG_COLORMASK_RGB;
        pip_default_desc.colors[0].blend.enabled = true;
        pip_default_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_default_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        if (Engine::isAsyncThread()){
            pip = SokolCmdQueue::add_command_make_pipeline(pip_default_desc);
        }else{
            pip = sg_make_pipeline(pip_default_desc);
        }

        if (pip.id == SG_INVALID_ID){
            return false;
        }
    }

    if (pipelines & (int)PipelineType::PIP_RTT){
        sg_pipeline_desc pip_rtt_desc = pipeline_desc;

        if (enableFaceCulling){
            pip_rtt_desc.cull_mode = getCullMode(cullingMode);
            pip_rtt_desc.face_winding = getFaceWinding(windingOrder);
        }

        pip_rtt_desc.sample_count = 1;

        pip_rtt_desc.depth.write_enabled = true;
        pip_rtt_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;

        pip_rtt_desc.colors[0].write_mask = SG_COLORMASK_RGBA;
        pip_rtt_desc.colors[0].blend.enabled = true;
        pip_rtt_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_rtt_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_rtt_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_rtt_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        pip_rtt_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
        pip_rtt_desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;

        if (Engine::isAsyncThread()){
            rtt_pip = SokolCmdQueue::add_command_make_pipeline(pip_rtt_desc);
        }else{
            rtt_pip = sg_make_pipeline(pip_rtt_desc);
        }

        if (rtt_pip.id == SG_INVALID_ID){
            return false;
        }
    }

    return true;
}

bool SokolObject::beginDraw(PipelineType pipType){
    if (pipType == PipelineType::PIP_DEPTH){
        if (depth_pip.id == SG_INVALID_ID){
            return false;
        }
        //SokolCmdQueue::add_command_apply_pipeline(depth_pip);
        sg_apply_pipeline(depth_pip);
    }else if (pipType == PipelineType::PIP_RTT){
        if (rtt_pip.id == SG_INVALID_ID){
            return false;
        }
        //SokolCmdQueue::add_command_apply_pipeline(rtt_pip);
        sg_apply_pipeline(rtt_pip);
    }else{
        if (pip.id == SG_INVALID_ID){
            return false;
        }
        //SokolCmdQueue::add_command_apply_pipeline(pip);
        sg_apply_pipeline(pip);
    }

    return true;
}

void SokolObject::applyUniformBlock(int slot, unsigned int count, void* data){
    if (slot != -1){
        sg_apply_uniforms(slot, {data, count});
    }
}

void SokolObject::draw(unsigned int vertexCount, unsigned int instanceCount){
    //SokolCmdQueue::add_command_apply_bindings(bind);
    sg_apply_bindings(bind);
    //SokolCmdQueue::add_command_draw(0, vertexCount, 1);
    sg_draw(0, vertexCount, instanceCount);
}

void SokolObject::destroy(){
    if (sg_isvalid()){
        if (pip.id != SG_INVALID_ID){
            if (Engine::isAsyncThread()){
                SokolCmdQueue::add_command_destroy_pipeline(pip);
            }else{
                sg_destroy_pipeline(pip);
            }
        }
        if (depth_pip.id != SG_INVALID_ID){
            if (Engine::isAsyncThread()){
                SokolCmdQueue::add_command_destroy_pipeline(depth_pip);
            }else{
                sg_destroy_pipeline(depth_pip);
            }
        }
        if (rtt_pip.id != SG_INVALID_ID){
            if (Engine::isAsyncThread()){
                SokolCmdQueue::add_command_destroy_pipeline(rtt_pip);
            }else{
                sg_destroy_pipeline(rtt_pip);
            }
        }
    }

    pip.id = SG_INVALID_ID;
    depth_pip.id = SG_INVALID_ID;
    rtt_pip.id = SG_INVALID_ID;
    bind = {}; 
    pipeline_desc = {};
    bindSlotIndex = 0;
}
