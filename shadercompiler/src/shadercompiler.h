//
// (c) 2024 Eduardo Doria.
//

#ifndef shadercompiler_h
#define shadercompiler_h

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace shadercompiler{

    //
    // From https://github.com/floooh/sokol-tools
    //
    // keep these in sync with:
    //  - SG_MAX_UNIFORMBLOCK_BINDSLOTS
    //  - SG_MAX_IMAGE_BINDSLOTS
    //  - SG_MAX_SAMPLER_BINDSLOTS
    //  - SG_MAX_STORAGEBUFFER_BINDSLOTS
    //  - SG_MAX_IMAGE_SAMPLERS_PAIRS
    //
    inline static const int MaxUniformBlocks = 10;
    inline static const int MaxImages = 16;
    inline static const int MaxSamplers = 16;
    inline static const int MaxStorageBuffers = 8;
    inline static const int MaxImageSamplers = 16;

    enum class BindingType {
        UNIFORM_BLOCK,
        IMAGE,
        SAMPLER,
        STORAGE_BUFFER,
        IMAGE_SAMPLER,
    };

    struct define_t{
        std::string def;
        std::string value;
    };

    enum lang_type_t{
        LANG_GLSL,
        LANG_HLSL,
        LANG_MSL
    };

    enum platform_t{
        SHADER_DEFAULT,
        SHADER_MACOS,
        SHADER_IOS
    };

    enum output_type_t{
        OUTPUT_JSON,
        OUTPUT_BINARY
    };

    struct args_t{
        bool isValid;

        bool useBuffers;
        std::unordered_map<std::string, std::string> fileBuffers;
        
        std::string vert_file;
        std::string frag_file;

        lang_type_t lang;
        int version;
        bool es;
        platform_t platform;

        std::string output_basename;
        std::string output_dir;
        output_type_t output_type;

        std::string include_dir;
        std::vector<define_t> defines;
        bool list_includes;

        bool optimization;
    };

    enum stage_type_t{
        STAGE_VERTEX,
        STAGE_FRAGMENT
    };

    struct input_t{
        stage_type_t stage_type;
        std::string filename;
        std::string source;
    };

    struct spirv_t{
        std::vector<uint32_t> bytecode;
    };

    enum vertex_attribs {
        VERTEX_POSITION = 0,
        VERTEX_NORMAL,
        VERTEX_TEXCOORD0,
        VERTEX_TEXCOORD1,
        VERTEX_TEXCOORD2,
        VERTEX_TEXCOORD3,
        VERTEX_TEXCOORD4,
        VERTEX_TEXCOORD5,
        VERTEX_TEXCOORD6,
        VERTEX_TEXCOORD7,
        VERTEX_COLOR0,
        VERTEX_COLOR1,
        VERTEX_COLOR2,
        VERTEX_COLOR3,
        VERTEX_TANGENT,
        VERTEX_BITANGENT,
        VERTEX_INDICES,
        VERTEX_WEIGHTS,
        VERTEX_ATTRIB_COUNT
    };

    static const char* k_attrib_names[VERTEX_ATTRIB_COUNT] = {
        "POSITION",
        "NORMAL",
        "TEXCOORD0",
        "TEXCOORD1",
        "TEXCOORD2",
        "TEXCOORD3",
        "TEXCOORD4",
        "TEXCOORD5",
        "TEXCOORD6",
        "TEXCOORD7",
        "COLOR0",
        "COLOR1",
        "COLOR2",
        "COLOR3",
        "TANGENT",
        "BINORMAL",
        "BLENDINDICES",
        "BLENDWEIGHT"
    };

    static const char* k_attrib_sem_names[VERTEX_ATTRIB_COUNT] = {
        "POSITION",
        "NORMAL",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "TEXCOORD",
        "COLOR",
        "COLOR",
        "COLOR",
        "COLOR",
        "TANGENT",
        "BINORMAL",
        "BLENDINDICES",
        "BLENDWEIGHT"
    };

    static int k_attrib_sem_indices[VERTEX_ATTRIB_COUNT] = {
        0,
        0,
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        0,
        1,
        2,
        3,
        0,
        0,
        0,
        0
    };

    enum attribute_type_t{
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        INT,
        INT2,
        INT3,
        INT4,
        INVALID
    };

    enum class uniform_type_t{
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        INT,
        INT2,
        INT3,
        INT4,
        MAT3,
        MAT4,
        INVALID
    };

    enum class storage_buffer_type_t{
        STRUCT,
        INVALID
    };

    enum class texture_type_t {
        TEXTURE_2D,
        TEXTURE_CUBE,
        TEXTURE_3D,
        TEXTURE_ARRAY,
        INVALID
    };

    enum class texture_samplertype_t {
        FLOAT,
        SINT,
        UINT,
        DEPTH,
        //UNFILTERABLE_FLOAT,
        INVALID
    };

    enum class sampler_type_t {
        FILTERING,
        COMPARISON,
        //NONFILTERING,
        INVALID
    };

    struct s_attr_t{
        std::string name;
        std::string semantic_name;
        uint32_t semantic_index;
        uint32_t location;
        attribute_type_t type = attribute_type_t::INVALID;
    };

    struct s_uniform_t {
        std::string name;
        uniform_type_t type = uniform_type_t::INVALID;
        uint32_t array_count = 1;
        uint32_t offset = 0;
    };

    struct s_uniform_block_t {
        std::string name;
        std::string inst_name;
        uint32_t set;
        uint32_t binding;
        unsigned int size_bytes;
        bool flattened = false;
        std::vector<s_uniform_t> uniforms;
    };

    struct s_storage_buffer_t {
        std::string name;
        std::string inst_name;
        uint32_t set;
        uint32_t binding;
        unsigned int size_bytes;
        bool readonly = true;
        storage_buffer_type_t type = storage_buffer_type_t::INVALID;
    };

    struct s_texture_t {
        std::string name;
        uint32_t set;
        uint32_t binding;
        texture_type_t type = texture_type_t::INVALID;
        texture_samplertype_t sampler_type = texture_samplertype_t::INVALID;
    };

    struct s_sampler_t {
        std::string name;
        uint32_t set;
        uint32_t binding;
        sampler_type_t type = sampler_type_t::INVALID;
    };

    struct s_texture_sampler_pair_t {
        std::string name;
        std::string texture_name;
        std::string sampler_name;
    };

    struct spirvcross_t{
        stage_type_t stage_type;
        std::string entry_point;

        std::string source;

        std::vector<s_attr_t> inputs;
        std::vector<s_attr_t> outputs;
        std::vector<s_uniform_block_t> uniform_blocks;
        std::vector<s_storage_buffer_t> storage_buffers;
        std::vector<s_texture_t> textures;
        std::vector<s_sampler_t> samplers;
        std::vector<s_texture_sampler_pair_t> texture_sampler_pairs;
    };


    args_t initialize_args();

    bool load_input(std::vector<input_t>& inputs, const args_t& args);

    bool compile_to_spirv(std::vector<spirv_t>& spirvvec, const std::vector<input_t>& inputs, const args_t& args);

    bool compile_to_lang(std::vector<spirvcross_t>& spirvcrossvec, const std::vector<spirv_t>& spirvvec, const std::vector<input_t>& inputs, const args_t& args);
}

#endif //shadercompiler_h