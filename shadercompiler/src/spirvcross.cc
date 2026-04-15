//
// (c) 2024 Eduardo Doria.
//

#include "shadercompiler.h"

#include "spirv_cross.hpp"
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_msl.hpp"
#include "spirv_parser.hpp"

#include <memory>

//
// From https://github.com/floooh/sokol-tools
//
// workaround for Compiler.comparison_ids being protected
class UnprotectedCompiler: spirv_cross::Compiler {
public:
    bool is_comparison_sampler(const spirv_cross::SPIRType &type, uint32_t id) {
        if (type.basetype == spirv_cross::SPIRType::Sampler) {
            return comparison_ids.count(id) > 0;
        }
        return 0;
    }
    bool is_used_as_depth_texture(const spirv_cross::SPIRType &type, uint32_t id) {
        if (type.basetype == spirv_cross::SPIRType::Image) {
            return comparison_ids.count(id) > 0;
        }
        return 0;
    }
};

using namespace shadercompiler;

static attribute_type_t spirtype_to_attribute_type(const spirv_cross::SPIRType& type) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        if (type.columns == 1) {
            switch (type.vecsize) {
                case 1: return attribute_type_t::FLOAT;
                case 2: return attribute_type_t::FLOAT2;
                case 3: return attribute_type_t::FLOAT3;
                case 4: return attribute_type_t::FLOAT4;
            }
        }
    } else if (type.basetype == spirv_cross::SPIRType::Int) {
        if (type.columns == 1) {
            switch (type.vecsize) {
                case 1: return attribute_type_t::INT;
                case 2: return attribute_type_t::INT2;
                case 3: return attribute_type_t::INT3;
                case 4: return attribute_type_t::INT4;
            }
        }
    }

    return attribute_type_t::INVALID;
}

static uniform_type_t spirtype_to_uniform_type(const spirv_cross::SPIRType& type) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        if (type.columns == 1) {
            switch (type.vecsize) {
                case 1: return uniform_type_t::FLOAT;
                case 2: return uniform_type_t::FLOAT2;
                case 3: return uniform_type_t::FLOAT3;
                case 4: return uniform_type_t::FLOAT4;
            }
        }
        else {
            if ((type.vecsize == 3) && (type.columns == 3)) {
                return uniform_type_t::MAT3;
            } else if ((type.vecsize == 4) && (type.columns == 4)) {
                return uniform_type_t::MAT4;
            }
        }
    } else if (type.basetype == spirv_cross::SPIRType::Int) {
        if (type.columns == 1) {
            switch (type.vecsize) {
                case 1: return uniform_type_t::INT;
                case 2: return uniform_type_t::INT2;
                case 3: return uniform_type_t::INT3;
                case 4: return uniform_type_t::INT4;
            }
        }
    }

    return uniform_type_t::INVALID;
}

static texture_type_t spirtype_to_image_type(const spirv_cross::SPIRType& type) {
    if (type.image.arrayed) {
        if (type.image.dim == spv::Dim2D) {
            return texture_type_t::TEXTURE_ARRAY;
        }
    }
    else {
        switch (type.image.dim) {
            case spv::Dim2D:    return texture_type_t::TEXTURE_2D;
            case spv::DimCube:  return texture_type_t::TEXTURE_CUBE;
            case spv::Dim3D:    return texture_type_t::TEXTURE_3D;
            default: break;
        }
    }

    return texture_type_t::INVALID;
}

static texture_samplertype_t spirtype_to_image_samplertype(const spirv_cross::SPIRType& type) {
    if (type.image.depth) {
        return texture_samplertype_t::DEPTH;
    } else {
        switch (type.basetype) {
            case spirv_cross::SPIRType::Int:
            case spirv_cross::SPIRType::Short:
            case spirv_cross::SPIRType::SByte:
                return texture_samplertype_t::SINT;
            case spirv_cross::SPIRType::UInt:
            case spirv_cross::SPIRType::UShort:
            case spirv_cross::SPIRType::UByte:
                return texture_samplertype_t::UINT;
            default:
                return texture_samplertype_t::FLOAT;
        }
    }
}

//
// From https://github.com/floooh/sokol-tools
//
static bool can_flatten_uniform_block(const spirv_cross::Compiler* compiler, const spirv_cross::Resource& ub_res) {
    const spirv_cross::SPIRType& ub_type = compiler->get_type(ub_res.base_type_id);
    spirv_cross::SPIRType::BaseType basic_type = spirv_cross::SPIRType::Unknown;
    for (int m_index = 0; m_index < (int)ub_type.member_types.size(); m_index++) {
        const spirv_cross::SPIRType& m_type = compiler->get_type(ub_type.member_types[m_index]);
        if (basic_type == spirv_cross::SPIRType::Unknown) {
            basic_type = m_type.basetype;
            if ((basic_type != spirv_cross::SPIRType::Float) && (basic_type != spirv_cross::SPIRType::Int)) {
                return false;
            }
        }
        else if (basic_type != m_type.basetype) {
            return false;
        }
    }
    return true;
}

//
// From https://github.com/floooh/sokol-tools
//
static void flatten_uniform_blocks(spirv_cross::CompilerGLSL* compiler) {
    /* this flattens each uniform block into a vec4 array, in WebGL/GLES2 this
        allows more efficient uniform updates
    */
    spirv_cross::ShaderResources res = compiler->get_shader_resources();
    for (const spirv_cross::Resource& ub_res: res.uniform_buffers) {
        if (can_flatten_uniform_block(compiler, ub_res)){
            compiler->flatten_buffer_block(ub_res.id);
        }
    }
}

//
// From https://github.com/floooh/sokol-tools
//
static void to_combined_image_samplers(spirv_cross::CompilerGLSL* compiler) {
    compiler->build_combined_image_samplers();
    // give the combined samplers new names
    uint32_t binding = 0;
    for (auto& remap: compiler->get_combined_image_samplers()) {
        const std::string img_name = compiler->get_name(remap.image_id);
        const std::string smp_name = compiler->get_name(remap.sampler_id);
        compiler->set_name(remap.combined_id, img_name + "_" + smp_name);
        compiler->set_decoration(remap.combined_id, spv::DecorationBinding, binding++);
    }
}

static bool validate_uniform_blocks_and_separate_image_samplers(const spirv_cross::Compiler* compiler, const spirv_cross::ShaderResources& res, const input_t& input) {
    for (const spirv_cross::Resource& ub_res: res.uniform_buffers) {
        const spirv_cross::SPIRType& ub_type = compiler->get_type(ub_res.base_type_id);
        for (int m_index = 0; m_index < (int)ub_type.member_types.size(); m_index++) {
            const spirv_cross::SPIRType& m_type = compiler->get_type(ub_type.member_types[m_index]);
            if ((m_type.basetype != spirv_cross::SPIRType::Float) && (m_type.basetype != spirv_cross::SPIRType::Int)) {
                fprintf(stderr, "%s: uniform block '%s': uniform blocks can only contain float or int base types\n", input.filename.c_str(), ub_res.name.c_str());
                return false;
            }
            if (m_type.array.size() > 0) {
                if (m_type.vecsize != 4) {
                    fprintf(stderr, "%s: uniform block '%s': arrays must be of type vec4[], ivec4[] or mat4[]\n", input.filename.c_str(), ub_res.name.c_str());
                    return false;
                }
                if (m_type.array.size() > 1) {
                    fprintf(stderr, "%s: uniform block '%s': arrays must be 1-dimensional\n", input.filename.c_str(), ub_res.name.c_str());
                    return false;
                }
            }
        }
    }
    if (res.sampled_images.size() > 0) {
        fprintf(stderr, "%s: combined image sampler '%s' detected, please use separate textures and samplers\n", input.filename.c_str(), res.sampled_images[0].name.c_str());
        return false;
    }
    return true;
}

//
// From https://github.com/floooh/sokol-tools
//
uint32_t base_slot(const shadercompiler::lang_type_t* lang, const stage_type_t stage_type, BindingType type){
    int res = 0;
    if (!lang)
        return res;

    switch (type) {
        case BindingType::UNIFORM_BLOCK:
            // TODO: wgsl
            break;
        case BindingType::IMAGE_SAMPLER:
            if (*lang == LANG_GLSL) {
                res = (stage_type == STAGE_VERTEX) ? 0 : MaxImageSamplers;
            }
            break;
        case BindingType::IMAGE:
            // TODO: wgsl
            break;
        case BindingType::SAMPLER:
            // TODO: wgsl
            break;
        case BindingType::STORAGE_BUFFER:
            if (*lang == LANG_MSL) {
                res = MaxUniformBlocks;
            } else if (*lang == LANG_HLSL) {
                res = MaxImages;
            } else if (*lang == LANG_GLSL) {
                if (stage_type == STAGE_FRAGMENT) {
                    res = MaxStorageBuffers;
                }
            } // TODO: wgsl
            break;
    }
    return res;
}

static void fix_bind_slots(spirv_cross::Compiler* compiler, const stage_type_t stage_type, const shadercompiler::lang_type_t* lang) {
    spirv_cross::ShaderResources shader_resources = compiler->get_shader_resources();

    // uniform buffers
    {
        uint32_t binding = base_slot(lang, stage_type, BindingType::UNIFORM_BLOCK);
        for (const spirv_cross::Resource& res: shader_resources.uniform_buffers) {
            compiler->set_decoration(res.id, spv::DecorationDescriptorSet, 0);
            compiler->set_decoration(res.id, spv::DecorationBinding, binding++);
        }
    }

    // combined image samplers
    {
        uint32_t binding = base_slot(lang, stage_type, BindingType::IMAGE_SAMPLER);
        for (const spirv_cross::Resource& res: shader_resources.sampled_images) {
            compiler->set_decoration(res.id, spv::DecorationDescriptorSet, 0);
            compiler->set_decoration(res.id, spv::DecorationBinding, binding++);
        }
    }

    // separate images
    {
        uint32_t binding = base_slot(lang, stage_type, BindingType::IMAGE);
        for (const spirv_cross::Resource& res: shader_resources.separate_images) {
            compiler->set_decoration(res.id, spv::DecorationDescriptorSet, 0);
            compiler->set_decoration(res.id, spv::DecorationBinding, binding++);
        }
    }

    // separate samplers
    {
        uint32_t binding = base_slot(lang, stage_type, BindingType::SAMPLER);
        for (const spirv_cross::Resource& res: shader_resources.separate_samplers) {
            compiler->set_decoration(res.id, spv::DecorationDescriptorSet, 0);
            compiler->set_decoration(res.id, spv::DecorationBinding, binding++);
        }
    }

    // storage buffers
    {
        uint32_t binding = base_slot(lang, stage_type, BindingType::STORAGE_BUFFER);
        for (const spirv_cross::Resource& res: shader_resources.storage_buffers) {
            compiler->set_decoration(res.id, spv::DecorationDescriptorSet, 0);
            compiler->set_decoration(res.id, spv::DecorationBinding, binding++);
        }
    }
}

static bool parse_stage_reflection(spirvcross_t& spirvcross, const spirv_cross::Compiler* compiler) {

    spirv_cross::ShaderResources shd_resources = compiler->get_shader_resources();

    // Stage
    switch (compiler->get_execution_model()) {
        case spv::ExecutionModelVertex:   spirvcross.stage_type = STAGE_VERTEX; break;
        case spv::ExecutionModelFragment: spirvcross.stage_type = STAGE_FRAGMENT; break;
        default: fprintf(stderr, "INVALID Stage\n"); return false; break;
    }

    // Entry function
    const auto entry_points = compiler->get_entry_points_and_stages();
    for (const auto& item: entry_points) {
        if (compiler->get_execution_model() == item.execution_model) {
            spirvcross.entry_point = item.name;
            break;
        }
    }

    // Stage inputs and outputs
    for (const spirv_cross::Resource& res_attr: shd_resources.stage_inputs) {
        s_attr_t attr;
        const uint32_t loc = compiler->get_decoration(res_attr.id, spv::DecorationLocation);
        const spirv_cross::SPIRType& type = compiler->get_type(res_attr.type_id);

        attr.location = loc;
        attr.name = res_attr.name;
        attr.semantic_name = k_attrib_sem_names[loc];
        attr.semantic_index = k_attrib_sem_indices[loc];
        attr.type = spirtype_to_attribute_type(type);

        spirvcross.inputs.push_back(attr);
    }
    for (const spirv_cross::Resource& res_attr: shd_resources.stage_outputs) {
        s_attr_t attr;
        const uint32_t loc = compiler->get_decoration(res_attr.id, spv::DecorationLocation);
        const spirv_cross::SPIRType& type = compiler->get_type(res_attr.type_id);
        
        attr.location = loc;
        attr.name = res_attr.name;
        attr.semantic_name = k_attrib_sem_names[loc];
        attr.semantic_index = k_attrib_sem_indices[loc];
        attr.type = spirtype_to_attribute_type(type);

        spirvcross.outputs.push_back(attr);
    }

    // uniform blocks
    for (const spirv_cross::Resource& ub_res: shd_resources.uniform_buffers) {
        s_uniform_block_t ub;

        const spirv_cross::SPIRType& ub_type = compiler->get_type(ub_res.base_type_id);

        ub.name = ub_res.name;
        ub.inst_name = compiler->get_name(ub_res.id);
        if (ub.inst_name.empty()){
            ub.inst_name = compiler->get_fallback_name(ub_res.id);
        }
        ub.set = compiler->get_decoration(ub_res.id, spv::DecorationDescriptorSet);
        ub.binding = compiler->get_decoration(ub_res.id, spv::DecorationBinding);
        ub.size_bytes = (int) compiler->get_declared_struct_size(ub_type);
        ub.flattened = can_flatten_uniform_block(compiler, ub_res);

        for (int m_index = 0; m_index < (int)ub_type.member_types.size(); m_index++) {
            s_uniform_t uniform;
            const spirv_cross::SPIRType& m_type = compiler->get_type(ub_type.member_types[m_index]);

            uniform.name = compiler->get_member_name(ub_res.base_type_id, m_index);
            uniform.offset = compiler->type_struct_member_offset(ub_type, m_index);
            uniform.type = spirtype_to_uniform_type(m_type);            
            if (m_type.array.size() > 0) {
                uniform.array_count = m_type.array[0];
            }

            ub.uniforms.push_back(uniform);
        }

        spirvcross.uniform_blocks.push_back(ub);
    }

    // storage buffers
    for (const spirv_cross::Resource& sbuf_res: shd_resources.storage_buffers) {
        s_storage_buffer_t sb;

        const spirv_cross::SPIRType& struct_type = compiler->get_type(sbuf_res.base_type_id);
        if (struct_type.basetype != spirv_cross::SPIRType::Struct) {
            fprintf(stderr, "toplevel item %s is not a struct\n", sbuf_res.name.c_str());
            return false;
        }

        sb.name = sbuf_res.name;
        sb.inst_name = compiler->get_name(sbuf_res.id);
        if (sb.inst_name.empty()) {
            sb.inst_name = compiler->get_fallback_name(sbuf_res.id);
        }
        sb.set = compiler->get_decoration(sbuf_res.id, spv::DecorationDescriptorSet);
        sb.binding = compiler->get_decoration(sbuf_res.id, spv::DecorationBinding);
        sb.readonly = compiler->get_buffer_block_flags(sbuf_res.id).get(spv::DecorationNonWritable);
        sb.size_bytes = (int) compiler->get_declared_struct_size_runtime_array(struct_type, 1);
        sb.type = storage_buffer_type_t::STRUCT;

        spirvcross.storage_buffers.push_back(sb);
    }

    // (separate) images
    for (const spirv_cross::Resource& img_res: shd_resources.separate_images) {
        s_texture_t image;

        const spirv_cross::SPIRType& img_type = compiler->get_type(img_res.type_id);

        image.name = img_res.name;
        image.set = compiler->get_decoration(img_res.id, spv::DecorationDescriptorSet);
        image.binding = compiler->get_decoration(img_res.id, spv::DecorationBinding);
        image.type = spirtype_to_image_type(img_type);
        if (((UnprotectedCompiler*)compiler)->is_used_as_depth_texture(img_type, img_res.id)) {
            image.sampler_type = texture_samplertype_t::DEPTH;
        } else {
            image.sampler_type = spirtype_to_image_samplertype(compiler->get_type(img_type.image.type));
        }

        spirvcross.textures.push_back(image);
    }

    // (separate) samplers
    for (const spirv_cross::Resource& smp_res: shd_resources.separate_samplers) {
        s_sampler_t sampler;

        const spirv_cross::SPIRType& smp_type = compiler->get_type(smp_res.type_id);

        sampler.name = smp_res.name;
        sampler.set = compiler->get_decoration(smp_res.id, spv::DecorationDescriptorSet);
        sampler.binding = compiler->get_decoration(smp_res.id, spv::DecorationBinding);
        if (((UnprotectedCompiler*)compiler)->is_comparison_sampler(smp_type, smp_res.id)) {
            sampler.type = sampler_type_t::COMPARISON;
        } else {
            sampler.type = sampler_type_t::FILTERING;
        }

        spirvcross.samplers.push_back(sampler);
    }

    // combined image samplers
    for (auto& img_smp_res: compiler->get_combined_image_samplers()) {
        s_texture_sampler_pair_t img_smp;
        img_smp.name = compiler->get_name(img_smp_res.combined_id);
        img_smp.texture_name = compiler->get_name(img_smp_res.image_id);
        img_smp.sampler_name = compiler->get_name(img_smp_res.sampler_id);
        spirvcross.texture_sampler_pairs.push_back(img_smp);
    }

    return true;
}


//
// From https://github.com/floooh/sokol-tools
//
static bool parse_reflection(const std::vector<uint32_t>& bytecode, const stage_type_t stage_type, spirvcross_t& spirvcross) {
    // NOTE: do *NOT* use CompilerReflection here, this doesn't generate
    // the right reflection info for depth textures and comparison samplers
    spirv_cross::CompilerGLSL compiler(bytecode);
    spirv_cross::CompilerGLSL::Options options;
    options.emit_line_directives = false;
    options.version = 430;
    options.es = false;
    options.vulkan_semantics = false;
    options.enable_420pack_extension = false;
    options.emit_uniform_buffer_as_plain_uniforms = true;
    compiler.set_common_options(options);
    flatten_uniform_blocks(&compiler);
    to_combined_image_samplers(&compiler);
    fix_bind_slots(&compiler, stage_type, nullptr);
    // NOTE: we need to compile here, otherwise the reflection won't be
    // able to detect depth-textures and comparison-samplers!
    compiler.compile();

    return parse_stage_reflection(spirvcross, &compiler);
}


bool validate_inputs_and_outputs(std::vector<spirvcross_t>& spirvcrossvec, const std::vector<input_t>& inputs){
    int vsIndex = -1;
    int fsIndex = -1;
    for (int s = 0; s < inputs.size(); s++){
        if (inputs[s].stage_type == stage_type_t::STAGE_VERTEX)
            vsIndex = s;
        if (inputs[s].stage_type == stage_type_t::STAGE_FRAGMENT)
            fsIndex = s;
    }

    for (int o = 0; o < spirvcrossvec[vsIndex].outputs.size(); o++){
        bool found = false;
        shadercompiler::s_attr_t output = spirvcrossvec[vsIndex].outputs[o];
        for (int i = 0; i < spirvcrossvec[fsIndex].inputs.size(); i++){
            shadercompiler::s_attr_t input = spirvcrossvec[fsIndex].inputs[i];
            if (output.name == input.name && output.type == input.type){
                found = true;
            }
        }

        if (!found){
            fprintf(stderr, "%s, %s: vertex shader output '%s' does not exist in fragment shader inputs\n", inputs[vsIndex].filename.c_str(), inputs[fsIndex].filename.c_str(), output.name.c_str());
            return false;
        }
    }

    for (int i = 0; i < spirvcrossvec[fsIndex].inputs.size(); i++){
        bool found = false;
        shadercompiler::s_attr_t input = spirvcrossvec[fsIndex].inputs[i];
        for (int o = 0; o < spirvcrossvec[vsIndex].outputs.size(); o++){
            shadercompiler::s_attr_t output = spirvcrossvec[vsIndex].outputs[o];
            if (output.name == input.name && output.type == input.type){
                found = true;
            }
        }

        if (!found){
            fprintf(stderr, "%s, %s: fragment shader input '%s' does not exist in vertex shader outputs\n", inputs[vsIndex].filename.c_str(), inputs[fsIndex].filename.c_str(), input.name.c_str());
            return false;
        }
    }

    return true;
}

bool shadercompiler::compile_to_lang(std::vector<spirvcross_t>& spirvcrossvec, const std::vector<spirv_t>& spirvvec, const std::vector<input_t>& inputs, const args_t& args){
    for (int i = 0; i < inputs.size(); i++){
        spirv_cross::Parser spirv_parser(std::move(spirvvec[i].bytecode));
	    spirv_parser.parse();

        std::unique_ptr<spirv_cross::CompilerGLSL> compiler;
        // Use spirv-cross to convert to other types of shader
        if (args.lang == LANG_GLSL) {
            compiler.reset(new spirv_cross::CompilerGLSL(std::move(spirv_parser.get_parsed_ir())));
        } else if (args.lang == LANG_MSL) {
            compiler.reset(new spirv_cross::CompilerMSL(std::move(spirv_parser.get_parsed_ir())));
        } else if (args.lang == LANG_HLSL) {
            compiler.reset(new spirv_cross::CompilerHLSL(std::move(spirv_parser.get_parsed_ir())));
        } else {
            fprintf(stderr, "Language not implemented");
            return false;
        }

        spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
        
        opts.flatten_multidimensional_arrays = true;
        opts.vertex.flip_vert_y = false;

        if (args.lang == LANG_GLSL) {

            opts.vulkan_semantics = false; //TODO: True if vulkan
            opts.emit_line_directives = false;
            opts.vertex.fixup_clipspace = false;
            opts.enable_420pack_extension = false;
            opts.emit_uniform_buffer_as_plain_uniforms = true;  //TODO: False if vulkan
            opts.es = args.es;
            opts.version = args.version;

        } else if (args.lang == LANG_HLSL) {

            opts.emit_line_directives = true;
            opts.vertex.fixup_clipspace = true;

            spirv_cross::CompilerHLSL* hlsl = (spirv_cross::CompilerHLSL*)compiler.get();
            spirv_cross::CompilerHLSL::Options hlsl_opts = hlsl->get_hlsl_options();
            hlsl_opts.shader_model = args.version;
            hlsl_opts.point_size_compat = true;
            hlsl_opts.point_coord_compat = true;

            hlsl->set_hlsl_options(hlsl_opts);

        } else if (args.lang == LANG_MSL) {

            opts.emit_line_directives = true;
            opts.vertex.fixup_clipspace = true;

            spirv_cross::CompilerMSL* msl = (spirv_cross::CompilerMSL*)compiler.get();
            spirv_cross::CompilerMSL::Options msl_opts = msl->get_msl_options();
            if (args.platform == SHADER_MACOS){
                msl_opts.platform = spirv_cross::CompilerMSL::Options::macOS;
            }else if (args.platform == SHADER_IOS){
                msl_opts.platform = spirv_cross::CompilerMSL::Options::iOS;
            }
            msl_opts.enable_decoration_binding = true;
            msl_opts.msl_version = args.version;

            msl->set_msl_options(msl_opts);

        }

        compiler->set_common_options(opts);

        // Vertex attribute remap for HLSL
        if (args.lang == LANG_HLSL) {
            spirv_cross::CompilerHLSL* hlsl_compiler = (spirv_cross::CompilerHLSL*)compiler.get();
            for (int i = 0; i < VERTEX_ATTRIB_COUNT; i++) {
                spirv_cross::HLSLVertexAttributeRemap remap = { (uint32_t)i, k_attrib_names[i] };
                hlsl_compiler->add_vertex_attribute_remap(remap);
            }
        }

        fix_bind_slots(compiler.get(), inputs[i].stage_type, &args.lang);

        spirv_cross::ShaderResources res = compiler->get_shader_resources();
        if (!validate_uniform_blocks_and_separate_image_samplers(compiler.get(), res, inputs[i]))
            return false;

        // GL/GLES try to flatten UBs if attributes are same type to use only one glUniform4fv call
        // TODO: Not for Vulkan
        if (args.lang == LANG_GLSL) {
            flatten_uniform_blocks(compiler.get());
            to_combined_image_samplers(compiler.get());
        }
        
        spirvcrossvec[i].source = compiler->compile();

        if (!parse_reflection(spirvvec[i].bytecode, inputs[i].stage_type, spirvcrossvec[i]))
            return false;
    }

    if (!validate_inputs_and_outputs(spirvcrossvec, inputs))
        return false;

    return true;
}