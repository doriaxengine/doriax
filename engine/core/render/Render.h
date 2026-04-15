//
// (c) 2026 Eduardo Doria.
//

#ifndef Render_h
#define Render_h

#include "Export.h"


namespace doriax{

    enum class PrimitiveType{
        TRIANGLES,
        TRIANGLE_STRIP,
        POINTS,
        LINES
    };

    enum class CullingMode{
        BACK,
        FRONT
    };

    enum class WindingOrder{
        CCW,
        CW
    };

    enum class BufferType{
        VERTEX_BUFFER,
        INDEX_BUFFER,
        STORAGE_BUFFER
    };

    enum class BufferUsage{ //see sokol_gfx.h for details
        IMMUTABLE,
        DYNAMIC,
        STREAM
    };

    enum class ShaderType{
        POINTS,
        LINES, // Not used yet
        MESH,
        SKYBOX,
        DEPTH,
        UI
    };

    enum class AttributeType{
        INDEX,
        POSITION,
        TEXCOORD1,
        NORMAL,
        TANGENT,
        COLOR,
        POINTSIZE,
        POINTROTATION,
        TEXTURERECT,
        BONEWEIGHTS,
        BONEIDS,
        MORPHTARGET0,
        MORPHTARGET1,
        MORPHTARGET2,
        MORPHTARGET3,
        MORPHTARGET4,
        MORPHTARGET5,
        MORPHTARGET6,
        MORPHTARGET7,
        MORPHNORMAL0,
        MORPHNORMAL1,
        MORPHNORMAL2,
        MORPHNORMAL3,
        MORPHTANGENT0,
        MORPHTANGENT1,
        INSTANCEMATRIXCOL1,
        INSTANCEMATRIXCOL2,
        INSTANCEMATRIXCOL3,
        INSTANCEMATRIXCOL4,
        INSTANCECOLOR,
        INSTANCETEXTURERECT,
        TERRAINNODEPOSITION,
        TERRAINNODESIZE,
        TERRAINNODERANGE,
        TERRAINNODERESOLUTION
    };

    enum class AttributeDataType{
        BYTE, //int8_t
        UNSIGNED_BYTE, //uint8_t
        SHORT, //int16_t
        UNSIGNED_SHORT, //uint16_t
        INT, //int32_t
        UNSIGNED_INT, //uint32_t
        FLOAT
    };

    enum class UniformBlockType{
        PBR_VS_PARAMS,
        MATERIAL,
        PBR_FS_PARAMS,
        FS_LIGHTING,
        FS_FOG,
        VS_SHADOWS,
        FS_SHADOWS,
        SKY_VS_PARAMS,
        SKY_FS_PARAMS,
        DEPTH_VS_PARAMS,
        SPRITE_VS_PARAMS,
        UI_VS_PARAMS,
        UI_FS_PARAMS,
        POINTS_VS_PARAMS,
        LINES_VS_PARAMS,
        VS_SKINNING,
        DEPTH_VS_SKINNING,
        VS_MORPHTARGET,
        DEPTH_VS_MORPHTARGET,
        TERRAIN_VS_PARAMS,
        DEPTH_TERRAIN_VS_PARAMS
    };

    enum class StorageBufferType{
        VS_VERTEX // for future use only
    };

    enum class TextureShaderType{
        BASECOLOR,
        EMISSIVE,
        METALLICROUGHNESS,
        OCCULSION,
        NORMAL,
        SHADOWMAP1,
        SHADOWMAP2,
        SHADOWMAP3,
        SHADOWMAP4,
        SHADOWMAP5,
        SHADOWMAP6,
        SHADOWMAP7,
        SHADOWMAP8,
        SHADOWCUBEMAP1,
        SKYCUBE,
        UI,
        POINTS,
        HEIGHTMAP,
        BLENDMAP,
        TERRAINDETAIL_RED,
        TERRAINDETAIL_GREEN,
        TERRAINDETAIL_BLUE,
        DEPTHTEXTURE
    };

    enum class TextureType {
        TEXTURE_2D,
        TEXTURE_3D,
        TEXTURE_CUBE,
        TEXTURE_ARRAY
    };

    enum class TextureSamplerType {
        SINT,
        UINT,
        FLOAT,
        DEPTH
    };

    enum class SamplerType {
        COMPARISON,
        FILTERING
    };

    enum class ColorFormat{
        RED,
        RGBA
    };

    enum class TextureFilter{
        NEAREST,
        LINEAR,
        NEAREST_MIPMAP_NEAREST,
        NEAREST_MIPMAP_LINEAR,
        LINEAR_MIPMAP_NEAREST,
        LINEAR_MIPMAP_LINEAR
    };

    enum class TextureWrap{
        REPEAT,
        MIRRORED_REPEAT,
        CLAMP_TO_EDGE,
        CLAMP_TO_BORDER
    };

    enum PipelineType {
        PIP_DEFAULT = 1 << 0,
        PIP_RTT     = 1 << 1,
        PIP_DEPTH   = 1 << 2
    };

    //-------Start shader definition--------
    enum class ShaderLang{
        GLSL,
        MSL,
        HLSL
    };

    enum class ShaderVertexType{
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        INT,
        INT2,
        INT3,
        INT4
    };

    enum class ShaderStorageBufferType{
        STRUCT
    };

    enum class ShaderUniformType{
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        INT,
        INT2,
        INT3,
        INT4,
        MAT3,
        MAT4
    };

    enum class ShaderStageType{
        VERTEX,
        FRAGMENT
    };
    //-------End shader definition--------
}

#endif //Render_h