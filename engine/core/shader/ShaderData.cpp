// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderData.h"

#include "Log.h"

#include <cstring>
#include <set>
#include <algorithm>

using namespace doriax;

ShaderData::ShaderData(){

}

ShaderData::~ShaderData(){
    releaseSourceData();
}

ShaderData::ShaderData(const ShaderData& d){
    this->lang = d.lang;
    this->version = d.version;
    this->es = d.es;
    this->stages = d.stages;

    for (int i = 0; i < stages.size(); i++){
        if (this->stages[i].bytecode.data){
            this->stages[i].bytecode.data = new unsigned char[this->stages[i].bytecode.size];
            memcpy(this->stages[i].bytecode.data, d.stages[i].bytecode.data, this->stages[i].bytecode.size);
        }
    }
}

ShaderData& ShaderData::operator = (const ShaderData& d){
    this->lang = d.lang;
    this->version = d.version;
    this->es = d.es;
    this->stages = d.stages;

    for (int i = 0; i < stages.size(); i++){
        if (this->stages[i].bytecode.data){
            this->stages[i].bytecode.data = new unsigned char[this->stages[i].bytecode.size];
            memcpy(this->stages[i].bytecode.data, d.stages[i].bytecode.data, this->stages[i].bytecode.size);
        }
    }

    return *this;
}

int ShaderData::getAttrIndex(AttributeType type){
    std::string attrstr;
    
    if (type == AttributeType::POSITION){
        attrstr = "a_position";
    }else if (type == AttributeType::TEXCOORD1){
        attrstr = "a_texcoord1";
    }else if (type == AttributeType::TEXCOORD2){
        attrstr = "a_texcoord2";
    }else if (type == AttributeType::NORMAL){
        attrstr = "a_normal";
    }else if (type == AttributeType::TANGENT){
        attrstr = "a_tangent";
    }else if (type == AttributeType::COLOR){
        attrstr = "a_color";
    }else if (type == AttributeType::POINTSIZE){
        attrstr = "a_pointsize";
    }else if (type == AttributeType::POINTROTATION){
        attrstr = "a_pointrotation";
    }else if (type == AttributeType::TEXTURERECT){
        attrstr = "a_texturerect";
    }else if (type == AttributeType::BONEIDS){
        attrstr = "a_boneIds";
    }else if (type == AttributeType::BONEWEIGHTS){
        attrstr = "a_boneWeights";
    }else if (type == AttributeType::MORPHTARGET0){
        attrstr = "a_morphTarget0";
    }else if (type == AttributeType::MORPHTARGET1){
        attrstr = "a_morphTarget1";
    }else if (type == AttributeType::MORPHTARGET2){
        attrstr = "a_morphTarget2";
    }else if (type == AttributeType::MORPHTARGET3){
        attrstr = "a_morphTarget3";
    }else if (type == AttributeType::MORPHTARGET4){
        attrstr = "a_morphTarget4";
    }else if (type == AttributeType::MORPHTARGET5){
        attrstr = "a_morphTarget5";
    }else if (type == AttributeType::MORPHTARGET6){
        attrstr = "a_morphTarget6";
    }else if (type == AttributeType::MORPHTARGET7){
        attrstr = "a_morphTarget7";
    }else if (type == AttributeType::MORPHNORMAL0){
        attrstr = "a_morphNormal0";
    }else if (type == AttributeType::MORPHNORMAL1){
        attrstr = "a_morphNormal1";
    }else if (type == AttributeType::MORPHNORMAL2){
        attrstr = "a_morphNormal2";
    }else if (type == AttributeType::MORPHNORMAL3){
        attrstr = "a_morphNormal3";
    }else if (type == AttributeType::MORPHTANGENT0){
        attrstr = "a_morphTangent0";
    }else if (type == AttributeType::MORPHTANGENT1){
        attrstr = "a_morphTangent1";
    }else if (type == AttributeType::INSTANCEMATRIXCOL1){
        attrstr = "i_matrix_col1";
    }else if (type == AttributeType::INSTANCEMATRIXCOL2){
        attrstr = "i_matrix_col2";
    }else if (type == AttributeType::INSTANCEMATRIXCOL3){
        attrstr = "i_matrix_col3";
    }else if (type == AttributeType::INSTANCEMATRIXCOL4){
        attrstr = "i_matrix_col4";
    }else if (type == AttributeType::INSTANCECOLOR){
        attrstr = "i_color";
    }else if (type == AttributeType::INSTANCETEXTURERECT){
        attrstr = "i_textureRect";
    }else if (type == AttributeType::TERRAINNODEPOSITION){
        attrstr = "i_terrainnode_pos";
    }else if (type == AttributeType::TERRAINNODESIZE){
        attrstr = "i_terrainnode_size";
    }else if (type == AttributeType::TERRAINNODERANGE){
        attrstr = "i_terrainnode_range";
    }else if (type == AttributeType::TERRAINNODERESOLUTION){
        attrstr = "i_terrainnode_resolution";
    }

    if (attrstr.empty()){
        Log::error("Trying to get a not mapped attribute");
        return -1;
    }

    int vsIndex = -1;
    for (int s = 0; s < stages.size(); s++){
        if (stages[s].type == ShaderStageType::VERTEX)
            vsIndex = s;
    }

    int attrIndex = -1;
    if (vsIndex != -1){
        for (int a = 0; a < stages[vsIndex].attributes.size(); a++){
            if (stages[vsIndex].attributes[a].name == attrstr)
                attrIndex = stages[vsIndex].attributes[a].location;
        }
    }

    return attrIndex;
}

int ShaderData::getUniformBlockIndex(UniformBlockType type){
    std::string ustr;
    
    if (type == UniformBlockType::PBR_VS_PARAMS){
        ustr = "u_vs_pbrParams";
    }else if (type == UniformBlockType::PBR_FS_PARAMS){
        ustr = "u_fs_pbrParams";
    }else if (type == UniformBlockType::PBR_FS_TEXCOORDSETS){
        ustr = "u_fs_texCoordSets";
    }else if (type == UniformBlockType::PBR_VS_FADE){
        ustr = "u_vs_fade";
    }else if (type == UniformBlockType::FS_LIGHTING){
        ustr = "u_fs_lighting";
    }else if (type == UniformBlockType::FS_REFLECTION_PROBE){
        ustr = "u_fs_reflectionProbe";
    }else if (type == UniformBlockType::FS_FOG){
        ustr = "u_fs_fog";
    }else if (type == UniformBlockType::FS_MIRROR){
        ustr = "u_fs_mirror";
    }else if (type == UniformBlockType::VS_SHADOWS){
        ustr = "u_vs_shadows";
    }else if (type == UniformBlockType::FS_SHADOWS){
        ustr = "u_fs_shadows";
    }else if (type == UniformBlockType::FS_SHADOW_PROJECTION){
        ustr = "u_fs_shadow_projection";
    }else if (type == UniformBlockType::FS_POINT_SHADOWS){
        ustr = "u_fs_point_shadows";
    }else if (type == UniformBlockType::SKY_VS_PARAMS){
        ustr = "u_vs_skyParams";
    }else if (type == UniformBlockType::SKY_FS_PARAMS){
        ustr = "u_fs_skyParams";
    }else if (type == UniformBlockType::DEPTH_VS_PARAMS){
        ustr = "u_vs_depthParams";
    }else if (type == UniformBlockType::DEPTH_FS_MATERIAL){
        ustr = "u_fs_depthMaterial";
    }else if (type == UniformBlockType::GBUFFER_VS_PARAMS){
        ustr = "u_vs_gbufferParams";
    }else if (type == UniformBlockType::GBUFFER_FS_MATERIAL){
        ustr = "u_fs_gbufferMaterial";
    }else if (type == UniformBlockType::UI_VS_PARAMS){
        ustr = "u_vs_uiParams";
    }else if (type == UniformBlockType::UI_FS_PARAMS){
        ustr = "u_fs_uiParams";
    }else if (type == UniformBlockType::SPRITE_VS_PARAMS){
        ustr = "u_vs_spriteParams";
    }else if (type == UniformBlockType::POINTS_VS_PARAMS){
        ustr = "u_vs_pointsParams";
    }else if (type == UniformBlockType::LINES_VS_PARAMS){
        ustr = "u_vs_linesParams";
    }else if (type == UniformBlockType::VS_SKINNING){
        ustr = "u_vs_skinning";
    }else if (type == UniformBlockType::DEPTH_VS_SKINNING){
        ustr = "u_vs_skinning";
    }else if (type == UniformBlockType::VS_MORPHTARGET){
        ustr = "u_vs_morphtarget";
    }else if (type == UniformBlockType::DEPTH_VS_MORPHTARGET){
        ustr = "u_vs_morphtarget";
    }else if (type == UniformBlockType::TERRAIN_VS_PARAMS){
        ustr = "u_vs_terrainParams";
    }else if (type == UniformBlockType::DEPTH_TERRAIN_VS_PARAMS){
        ustr = "u_vs_terrainParams";
    }else if (type == UniformBlockType::FS_TERRAIN_LAYERS){
        ustr = "u_fs_terrainLayers";
    }else if (type == UniformBlockType::SSAO_FS_PARAMS){
        ustr = "u_fs_ssaoParams";
    }else if (type == UniformBlockType::SSAO_BLUR_FS_PARAMS){
        ustr = "u_fs_ssaoBlurParams";
    }else if (type == UniformBlockType::SSR_FS_PARAMS){
        ustr = "u_fs_ssrParams";
    }else if (type == UniformBlockType::SSR_BLUR_FS_PARAMS){
        ustr = "u_fs_ssrBlurParams";
    }else if (type == UniformBlockType::COMPOSITE_FS_PARAMS){
        ustr = "u_fs_compositeParams";
    }else if (type == UniformBlockType::FS_LIGHTING2D){
        ustr = "u_fs_lighting2d";
    }else if (type == UniformBlockType::SHADOW2D_VS_PARAMS){
        ustr = "u_vs_shadow2dParams";
    }else if (type == UniformBlockType::BLIT_FS_PARAMS){
        ustr = "u_fs_blitParams";
    }else if (type == UniformBlockType::WATER_VS_PARAMS){
        ustr = "u_vs_waterParams";
    }else if (type == UniformBlockType::WATER_FS_PARAMS){
        ustr = "u_fs_waterParams";
    }else if (type == UniformBlockType::UNDERWATER_FS_PARAMS){
        ustr = "u_fs_underwaterParams";
    }else if (type == UniformBlockType::PROBE_FILTER_FS_PARAMS){
        ustr = "u_fs_probeFilterParams";
    }

    if (ustr.empty()){
        Log::error("Trying to get a not mapped uniform");
        return -1;
    }

    return getUniformBlockIndexByName(ustr);
}

int ShaderData::getUniformBlockIndexByName(const std::string& name){
    for (int s = 0; s < stages.size(); s++){
        for (int u = 0; u < stages[s].uniformblocks.size(); u++){
            if (stages[s].uniformblocks[u].name == name){
                return stages[s].uniformblocks[u].slot;
            }
        }
    }

    return -1;
}

const std::vector<ShaderUniform>* ShaderData::getUniformBlockMembers(const std::string& name, unsigned int& sizeBytes){
    for (int s = 0; s < stages.size(); s++){
        for (int u = 0; u < stages[s].uniformblocks.size(); u++){
            if (stages[s].uniformblocks[u].name == name){
                sizeBytes = stages[s].uniformblocks[u].sizeBytes;
                return &stages[s].uniformblocks[u].uniforms;
            }
        }
    }

    sizeBytes = 0;
    return NULL;
}

std::string ShaderData::getUniformShortName(const std::string& name){
    size_t dot = name.rfind('.');
    return (dot == std::string::npos) ? name : name.substr(dot + 1);
}

int ShaderData::getStorageBufferIndex(StorageBufferType type){
    std::string ustr;
    
    if (type == StorageBufferType::VS_VERTEX){
        ustr = "sbo_vertex";
    }else if (type == StorageBufferType::VS_SKINNING){
        ustr = "sbo_skinning";
    }

    if (ustr.empty()){
        Log::error("Trying to get a not mapped storage buffer");
        return -1;
    }

    for (int s = 0; s < stages.size(); s++){
        for (int u = 0; u < stages[s].storagebuffers.size(); u++){
            if (stages[s].storagebuffers[u].name == ustr){
                return stages[s].storagebuffers[u].slot;
            }
        }
    }

    return -1;
}

std::pair<int, int> ShaderData::getTextureIndex(TextureShaderType type){
    std::string texstr;
    
    if (type == TextureShaderType::BASECOLOR){
        texstr = "u_baseColorTexture";
    }else if (type == TextureShaderType::EMISSIVE){
        texstr = "u_emissiveTexture";
    }else if (type == TextureShaderType::METALLICROUGHNESS){
        texstr = "u_metallicRoughnessTexture";
    }else if (type == TextureShaderType::OCCULSION){
        texstr = "u_occlusionTexture";
    }else if (type == TextureShaderType::NORMAL){
        texstr = "u_normalTexture";
    }else if (type == TextureShaderType::SHADOWATLAS){
        texstr = "u_shadowAtlas";
    }else if (type == TextureShaderType::SHADOWPOINTATLAS){
        texstr = "u_shadowPointAtlas";
    }else if (type == TextureShaderType::SKYCUBE){
        texstr = "u_skyTexture";
    }else if (type == TextureShaderType::IRRADIANCEMAP){
        texstr = "u_lambertianEnvTexture";
    }else if (type == TextureShaderType::PREFILTEREDMAP){
        texstr = "u_GGXEnvTexture";
    }else if (type == TextureShaderType::REFLECTIONPROBE){
        texstr = "u_reflectionProbeTexture";
    }else if (type == TextureShaderType::UI){
        texstr = "u_uiTexture";
    }else if (type == TextureShaderType::POINTS){
        texstr = "u_pointsTexture";
    }else if (type == TextureShaderType::BONES){
        texstr = "u_bonesTexture";
    }else if (type == TextureShaderType::HEIGHTMAP){
        texstr = "u_heightMap";
    }else if (type == TextureShaderType::BLENDMAP){
        texstr = "u_blendMap";
    }else if (type == TextureShaderType::BLENDMAP1){
        texstr = "u_blendMap1";
    }else if (type == TextureShaderType::BLENDMAP2){
        texstr = "u_blendMap2";
    }else if (type == TextureShaderType::TERRAINDETAIL){
        texstr = "u_terrainDetail";
    }else if (type == TextureShaderType::DEPTHTEXTURE){
        texstr = "u_depthTexture";
    }else if (type == TextureShaderType::SSAOTEXTURE){
        texstr = "u_ssaoTexture";
    }else if (type == TextureShaderType::NOISETEXTURE){
        texstr = "u_noiseTexture";
    }else if (type == TextureShaderType::SCENECOLORTEXTURE){
        texstr = "u_sceneColorTexture";
    }else if (type == TextureShaderType::SSRTEXTURE){
        texstr = "u_ssrTexture";
    }else if (type == TextureShaderType::GBUFFERTEXTURE){
        texstr = "u_gbufferTexture";
    }else if (type == TextureShaderType::GBUFFERALBEDOTEXTURE){
        texstr = "u_albedoTexture";
    }else if (type == TextureShaderType::SHADOW2DATLAS){
        texstr = "u_shadow2DAtlas";
    }else if (type == TextureShaderType::SPOTMASKATLAS){
        texstr = "u_spotMaskAtlas";
    }else if (type == TextureShaderType::WATERNORMAL){
        texstr = "u_waterNormalTexture";
    }else if (type == TextureShaderType::WATERREFLECTION){
        texstr = "u_reflectionTexture";
    }else if (type == TextureShaderType::WATERREFRACTION){
        texstr = "u_refractionTexture";
    }else if (type == TextureShaderType::WATEREXCLUSION){
        texstr = "u_waterExclusionMaps";
    }else if (type == TextureShaderType::PROBESOURCE){
        texstr = "u_probeSourceTexture";
    }

    if (texstr.empty()){
        Log::error("Trying to get a not mapped texture");
        return std::pair(-1, -1);
    }

    return getTextureIndexByName(texstr);
}

std::pair<int, int> ShaderData::getTextureIndexByName(const std::string& texstr){
    for (int s = 0; s < stages.size(); s++){
        // get texture index
        int texIndex = -1;
        for (int te = 0; te < stages[s].textures.size(); te++){
            if (stages[s].textures[te].name == texstr){
                texIndex = stages[s].textures[te].slot;
            }
        }

        if (texIndex != -1){
            // get sampler name by texture
            std::string samplerName;
            for (int ts = 0; ts < stages[s].textureSamplerPairs.size(); ts++){
                if (stages[s].textureSamplerPairs[ts].textureName == texstr){
                    samplerName = stages[s].textureSamplerPairs[ts].samplerName;
                }
            }

            // get sampler index
            int samIndex = -1;
            for (int sa = 0; sa < stages[s].samplers.size(); sa++){
                if (stages[s].samplers[sa].name == samplerName){
                    samIndex = stages[s].samplers[sa].slot;

                    return std::pair(texIndex, samIndex);
                }
            }
        }
    }

    return std::pair(-1, -1);
}

std::vector<std::string> ShaderData::getCustomTextureNames() const{
    std::vector<std::string> names;
    for (const ShaderStage& stage : stages){
        for (const ShaderTexture& tex : stage.textures){
            if (tex.name.rfind("u_custom_", 0) != 0)
                continue;
            if (std::find(names.begin(), names.end(), tex.name) == names.end())
                names.push_back(tex.name);
        }
        for (const ShaderTextureSamplerPair& pair : stage.textureSamplerPairs) {
            if (pair.textureName.rfind("u_custom_", 0) != 0)
                continue;
            if (std::find(names.begin(), names.end(), pair.textureName) == names.end())
                names.push_back(pair.textureName);
        }
    }
    return names;
}

void ShaderData::releaseSourceData(){
    for (int i = 0; i < stages.size(); i++){
        if (stages[i].bytecode.data)
            delete stages[i].bytecode.data;
        stages[i].bytecode.data = NULL;
        stages[i].bytecode.size = 0;
        stages[i].source.clear();
    }
}
