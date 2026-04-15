//
// (c) 2026 Eduardo Doria.
//

#include "SokolShader.h"

#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "render/ShaderRender.h"
#include "Engine.h"

using namespace doriax;

SokolShader::SokolShader(){
    shader.id = SG_INVALID_ID;
}

SokolShader::SokolShader(const SokolShader& rhs) : shader(rhs.shader) {}

SokolShader& SokolShader::operator=(const SokolShader& rhs){ 
    shader = rhs.shader; 
    return *this;
}

int SokolShader::roundup(int val, int round_to) {
    return (val + (round_to - 1)) & ~(round_to - 1);
}

sg_uniform_type SokolShader::uniformToSokolType(ShaderUniformType type){
    if (type == ShaderUniformType::FLOAT){
        return SG_UNIFORMTYPE_FLOAT;
    }else if (type == ShaderUniformType::FLOAT2){
        return SG_UNIFORMTYPE_FLOAT2;
    }else if (type == ShaderUniformType::FLOAT3){
        return SG_UNIFORMTYPE_FLOAT3;
    }else if (type == ShaderUniformType::FLOAT4){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::INT){
        return SG_UNIFORMTYPE_INT;
    }else if (type == ShaderUniformType::INT2){
        return SG_UNIFORMTYPE_INT2;
    }else if (type == ShaderUniformType::INT3){
        return SG_UNIFORMTYPE_INT3;
    }else if (type == ShaderUniformType::INT4){
        return SG_UNIFORMTYPE_INT4;
    }else if (type == ShaderUniformType::MAT3){
        Log::error("Sokol cannot support MAT3 uniform type");
    }else if (type == ShaderUniformType::MAT4){
        return SG_UNIFORMTYPE_MAT4;
    }

    return SG_UNIFORMTYPE_INVALID;
}

sg_uniform_type SokolShader::flattenedUniformToSokolType(ShaderUniformType type){
    if (type == ShaderUniformType::FLOAT){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::FLOAT2){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::FLOAT3){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::FLOAT4){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::INT){
        return SG_UNIFORMTYPE_INT4;
    }else if (type == ShaderUniformType::INT2){
        return SG_UNIFORMTYPE_INT4;
    }else if (type == ShaderUniformType::INT3){
        return SG_UNIFORMTYPE_INT4;
    }else if (type == ShaderUniformType::INT4){
        return SG_UNIFORMTYPE_INT4;
    }else if (type == ShaderUniformType::MAT3){
        return SG_UNIFORMTYPE_FLOAT4;
    }else if (type == ShaderUniformType::MAT4){
        return SG_UNIFORMTYPE_FLOAT4;
    }

    return SG_UNIFORMTYPE_INVALID;
}

sg_image_sample_type SokolShader::textureSamplerToSokolType(TextureSamplerType type){
    if (type == TextureSamplerType::FLOAT){
        return SG_IMAGESAMPLETYPE_FLOAT;
    }else if (type == TextureSamplerType::UINT){
        return SG_IMAGESAMPLETYPE_UINT;
    }else if (type == TextureSamplerType::SINT){
        return SG_IMAGESAMPLETYPE_SINT;
    }

    return _SG_IMAGESAMPLETYPE_DEFAULT;
}

sg_image_type SokolShader::textureToSokolType(TextureType type){
    if (type == TextureType::TEXTURE_2D){
        return SG_IMAGETYPE_2D;
    }else if (type == TextureType::TEXTURE_3D){
        return SG_IMAGETYPE_3D;
    }else if (type == TextureType::TEXTURE_CUBE){
        return SG_IMAGETYPE_CUBE;
    }else if (type == TextureType::TEXTURE_ARRAY){
        return SG_IMAGETYPE_ARRAY;
    }

    return _SG_IMAGETYPE_DEFAULT;
}

sg_sampler_type SokolShader::samplerToSokolType(SamplerType type){
    if (type == SamplerType::COMPARISON){
        return SG_SAMPLERTYPE_COMPARISON;
    }else if (type == SamplerType::FILTERING){
        return SG_SAMPLERTYPE_FILTERING;
    }

    return _SG_SAMPLERTYPE_DEFAULT;
}

bool SokolShader::createShader(ShaderData& shaderData){
    sg_shader_desc shader_desc = {0};
    sg_shader_stage shader_stage = SG_SHADERSTAGE_NONE;

    for (int i = 0; i < shaderData.stages.size(); i++){
        const ShaderStage* stage =  &shaderData.stages[i];
        sg_shader_function* shader_function = NULL;

        if (stage->type == ShaderStageType::VERTEX){
            shader_stage = SG_SHADERSTAGE_VERTEX;
            shader_function = &shader_desc.vertex_func;
            shader_function->d3d11_target = "vs_5_0";
        }else if (stage->type == ShaderStageType::FRAGMENT){
            shader_stage = SG_SHADERSTAGE_FRAGMENT;
            shader_function = &shader_desc.fragment_func;
            shader_function->d3d11_target = "ps_5_0";
        }

        if (shaderData.lang == ShaderLang::MSL){
            shader_function->entry = "main0";
        }

        if (stage->bytecode.data) {
            shader_function->bytecode = {(const uint8_t*)stage->bytecode.data, stage->bytecode.size};
        } else if (!stage->source.empty()) {
            shader_function->source = stage->source.c_str();
        }

        if (stage->type == ShaderStageType::VERTEX){
            if (SG_MAX_VERTEX_ATTRIBUTES < stage->attributes.size()){
                Log::error("Number of attributes of shader is bigger than SG_MAX_VERTEX_ATTRIBUTES");
            }else{
                // attributes
                for (int a = 0; a < stage->attributes.size(); a++) {
                    int location = stage->attributes[a].location;
                    if (location >= 0){
                        shader_desc.attrs[location].glsl_name = stage->attributes[a].name.c_str();
                        shader_desc.attrs[location].hlsl_sem_name = stage->attributes[a].semanticName.c_str();
                        shader_desc.attrs[location].hlsl_sem_index = stage->attributes[a].semanticIndex;
                    }
                }
            }
        }

        // uniform blocks
        for (int ub = 0; ub < stage->uniformblocks.size(); ub++) {
            sg_shader_uniform_block* ubdesc = &shader_desc.uniform_blocks[stage->uniformblocks[ub].slot];
            ubdesc->stage = shader_stage;
            ubdesc->size = roundup(stage->uniformblocks[ub].sizeBytes, 16);
            ubdesc->layout = SG_UNIFORMLAYOUT_STD140;
            ubdesc->hlsl_register_b_n = stage->uniformblocks[ub].binding;
            ubdesc->msl_buffer_n = stage->uniformblocks[ub].binding;
            ubdesc->wgsl_group0_binding_n = stage->uniformblocks[ub].binding;
            // GL/GLES always flatten UBs if same type inside to not declare individual uniforms and use only one glUniform4fv call
            if ((shaderData.lang == ShaderLang::GLSL) && (stage->uniformblocks[ub].uniforms.size() > 0)) {
                if (stage->uniformblocks[ub].flattened){
                    ubdesc->glsl_uniforms[0].glsl_name = stage->uniformblocks[ub].name.c_str();
                    ubdesc->glsl_uniforms[0].type = flattenedUniformToSokolType(stage->uniformblocks[ub].uniforms[0].type);
                    ubdesc->glsl_uniforms[0].array_count = ubdesc->size / 16;
                }else{
                    for (int u = 0; u < (int)stage->uniformblocks[ub].uniforms.size(); u++) {
                        ubdesc->glsl_uniforms[u].glsl_name = stage->uniformblocks[ub].uniforms[u].name.c_str();
                        ubdesc->glsl_uniforms[u].type = uniformToSokolType(stage->uniformblocks[ub].uniforms[u].type);
                        ubdesc->glsl_uniforms[u].array_count = stage->uniformblocks[ub].uniforms[u].arrayCount;
                    }
                }
            }
        }

        // storage buffers
        for (int sb = 0; sb < stage->storagebuffers.size(); sb++) {
            sg_shader_storage_buffer* sbdesc = &shader_desc.storage_buffers[stage->storagebuffers[sb].slot];

            sbdesc->stage = shader_stage;
            sbdesc->readonly = stage->storagebuffers[sb].readonly;
            sbdesc->hlsl_register_t_n = stage->storagebuffers[sb].binding;
            sbdesc->msl_buffer_n = stage->storagebuffers[sb].binding;
            sbdesc->wgsl_group1_binding_n = stage->storagebuffers[sb].binding;
            sbdesc->glsl_binding_n = stage->storagebuffers[sb].binding;
        }

        std::unordered_set<std::string> usedTextures;
        std::unordered_set<std::string> usedSamplers;

        // Collect texture and sampler names used in pairs
        for (int ts = 0; ts < stage->textureSamplerPairs.size(); ts++) {
            usedTextures.insert(stage->textureSamplerPairs[ts].textureName);
            usedSamplers.insert(stage->textureSamplerPairs[ts].samplerName);
        }

        // textures
        for (int t = 0; t < stage->textures.size(); t++) {
            if (usedTextures.find(stage->textures[t].name) != usedTextures.end()) {
                sg_shader_image* img = &shader_desc.images[stage->textures[t].slot];

                img->stage = shader_stage;
                img->image_type = textureToSokolType(stage->textures[t].type);
                img->sample_type = textureSamplerToSokolType(stage->textures[t].samplerType);
                img->multisampled = false; // TODO
                img->hlsl_register_t_n = stage->textures[t].binding;
                img->msl_texture_n = stage->textures[t].binding;
                img->wgsl_group1_binding_n = stage->textures[t].binding;
            }else{
                Log::warn("Texture '%s' is not used", stage->textures[t].name.c_str());
            }
        }

        // samplers
        for (int s = 0; s < stage->samplers.size(); s++) {
            if (usedSamplers.find(stage->samplers[s].name) != usedSamplers.end()) {
                sg_shader_sampler* sampler = &shader_desc.samplers[stage->samplers[s].slot];

                sampler->stage = shader_stage;
                sampler->sampler_type = samplerToSokolType(stage->samplers[s].type);
                sampler->hlsl_register_s_n = stage->samplers[s].binding;
                sampler->msl_sampler_n = stage->samplers[s].binding;
                sampler->wgsl_group1_binding_n = stage->samplers[s].binding;
            }else{
                Log::warn("Sampler '%s' is not used", stage->samplers[s].name.c_str());
            }
        }

        // texture sampler pair
        for (int ts = 0; ts < stage->textureSamplerPairs.size(); ts++) {
            sg_shader_image_sampler_pair* imgsamplerpair = &shader_desc.image_sampler_pairs[stage->textureSamplerPairs[ts].slot];

            // get texture index
            int texIndex = -1;
            for (int t = 0; t < stage->textures.size(); t++){
                if (stage->textures[t].name == stage->textureSamplerPairs[ts].textureName)
                    texIndex = stage->textures[t].slot;
            }

            // get sampler index
            int samIndex = -1;
            for (int s = 0; s < stage->samplers.size(); s++){
                if (stage->samplers[s].name == stage->textureSamplerPairs[ts].samplerName)
                    samIndex = stage->samplers[s].slot;
            }

            imgsamplerpair->stage = shader_stage;
            imgsamplerpair->image_slot = texIndex;
            imgsamplerpair->sampler_slot = samIndex;
            imgsamplerpair->glsl_name = stage->textureSamplerPairs[ts].name.c_str();
        }
    }

    if (Engine::isAsyncThread()){
        shader = SokolCmdQueue::add_command_make_shader(shader_desc);
    }else{
        shader = sg_make_shader(shader_desc);
    }

    return isCreated();
}

void SokolShader::destroyShader(){
    if (shader.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_shader(shader);
        }else{
            sg_destroy_shader(shader);
        }
    }
    
    shader.id = SG_INVALID_ID;
}

bool SokolShader::isCreated(){
    if (shader.id != SG_INVALID_ID && sg_isvalid()) {
        return sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID;
    }

    return false;
}

sg_shader SokolShader::get(){
    return shader;
}