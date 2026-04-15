//
// (c) 2026 Eduardo Doria.
//

#include "SokolTexture.h"

#include "Log.h"
#include "SokolCmdQueue.h"
#include "render/SystemRender.h"
#include "Engine.h"

using namespace doriax;

SokolTexture::SokolTexture(){
    image.id = SG_INVALID_ID;
    sampler.id = SG_INVALID_ID;
}

SokolTexture::SokolTexture(const SokolTexture& rhs): image(rhs.image), sampler(rhs.sampler) {}

SokolTexture& SokolTexture::operator=(const SokolTexture& rhs){ 
    image = rhs.image;
    sampler = rhs.sampler;
    return *this;
}

sg_image_type SokolTexture::getTextureType(TextureType textureType){
    if (textureType == TextureType::TEXTURE_2D){
        return SG_IMAGETYPE_2D;
    }else if (textureType == TextureType::TEXTURE_CUBE){
        return SG_IMAGETYPE_CUBE;
    }

    return _SG_IMAGETYPE_DEFAULT;
}

sg_filter SokolTexture::getFilter(TextureFilter textureFilter){
    if (textureFilter == TextureFilter::LINEAR){
        return SG_FILTER_LINEAR;
    }else if (textureFilter == TextureFilter::NEAREST){
        return SG_FILTER_NEAREST;
    }else if (textureFilter == TextureFilter::LINEAR_MIPMAP_LINEAR){
        return SG_FILTER_LINEAR;
    }else if (textureFilter == TextureFilter::LINEAR_MIPMAP_NEAREST){
        return SG_FILTER_LINEAR;
    }else if (textureFilter == TextureFilter::NEAREST_MIPMAP_NEAREST){
        return SG_FILTER_NEAREST;
    }else if (textureFilter == TextureFilter::NEAREST_MIPMAP_LINEAR){
        return SG_FILTER_NEAREST;
    }

    return _SG_FILTER_DEFAULT;
}

sg_filter SokolTexture::getFilterMipmap(TextureFilter textureFilter){
    if (textureFilter == TextureFilter::LINEAR_MIPMAP_LINEAR){
        return SG_FILTER_LINEAR;
    }else if (textureFilter == TextureFilter::LINEAR_MIPMAP_NEAREST){
        return SG_FILTER_NEAREST;
    }else if (textureFilter == TextureFilter::NEAREST_MIPMAP_NEAREST){
        return SG_FILTER_NEAREST;
    }else if (textureFilter == TextureFilter::NEAREST_MIPMAP_LINEAR){
        return SG_FILTER_LINEAR;
    }

    return _SG_FILTER_DEFAULT;
}

sg_wrap SokolTexture::getWrap(TextureWrap textureWrap){
    if (textureWrap == TextureWrap::REPEAT){
        return SG_WRAP_REPEAT;
    }else if (textureWrap == TextureWrap::MIRRORED_REPEAT){
        return SG_WRAP_MIRRORED_REPEAT;
    }else if (textureWrap == TextureWrap::CLAMP_TO_EDGE){
        return SG_WRAP_CLAMP_TO_EDGE;
    }else if (textureWrap == TextureWrap::CLAMP_TO_BORDER){
        return SG_WRAP_CLAMP_TO_BORDER;
    }

    return _SG_WRAP_DEFAULT;
}

void SokolTexture::cleanupMipmapTexture(void* data){
    free(data);
}

// https://github.com/floooh/sokol/issues/102
sg_image SokolTexture::generateMipmaps(const sg_image_desc* desc_){
    sg_image_desc desc = *desc_;

    unsigned pixel_size = 0;

    if (desc.pixel_format == SG_PIXELFORMAT_RGBA8){
        pixel_size = 4;
    }else if (desc.pixel_format == SG_PIXELFORMAT_BGRA8){
        pixel_size = 4;
    }else if (desc.pixel_format == SG_PIXELFORMAT_R8){
        pixel_size = 1;
    }else{
        Log::error("Undefined pixel format to generate mipmaps of %s", desc.label);
        if (Engine::isAsyncThread()){
            return SokolCmdQueue::add_command_make_image(*desc_);
        }else{
            return sg_make_image(*desc_);
        }
    }


    int w = desc.width;
    int h = desc.height * desc.num_slices;
    int total_size = 0;
    for (int level = 1; level < SG_MAX_MIPMAPS; ++level) {
        w /=2;
        h /=2;

        if (w < 1 && h < 1)
            break;

        total_size += (w * h * pixel_size);
    }

    int cube_faces = 0;
    for (; cube_faces < SG_CUBEFACE_NUM; ++cube_faces) {
        if (!desc.data.subimage[cube_faces][0].ptr)
            break;
    }

    total_size *= (cube_faces+1);
    unsigned char *big_target = (unsigned char *)malloc(total_size);
    unsigned char *target = big_target;

    for (int cube_face = 0; cube_face < cube_faces; ++cube_face)
    {
        int target_width = desc.width;
        int target_height = desc.height;
        int dst_height = target_height * desc.num_slices;

        for (int level = 1; level < SG_MAX_MIPMAPS; ++level) {
            unsigned char* source = (unsigned char*)desc.data.subimage[cube_face][level - 1].ptr;
            if (!source)
                break;

            int source_width = target_width;
            int source_height = target_height;
            target_width /= 2;
            target_height /= 2;
            if (target_width < 1 && target_height < 1)
                break;

            if (target_width < 1)
                target_width = 1;

            if (target_height < 1)
                target_height = 1;

            dst_height /= 2;
            unsigned img_size = target_width * dst_height * pixel_size;
            unsigned char *miptarget = target;

            for (int slice = 0; slice < desc.num_slices; ++slice) {
                for (int x = 0; x < target_width; ++x)
                {
                    for (int y = 0; y < target_height; ++y)
                    {
                        //uint16_t colors[8] = { 0 };
                        for (int chanell = 0; chanell < pixel_size; ++chanell)
                        {
                            int color = 0;
                            int sx = x * 2;
                            int sy = y * 2;
                            color += source[source_width * pixel_size * sx + sy * pixel_size + chanell];
                            color += source[source_width * pixel_size * (sx + 1) + sy * pixel_size + chanell];
                            color += source[source_width * pixel_size * (sx + 1) + (sy + 1) * pixel_size + chanell];
                            color += source[source_width * pixel_size * sx + (sy + 1) * pixel_size + chanell];
                            color /= 4;
                            miptarget[target_width * pixel_size * (x) + (y) * pixel_size + chanell] = (uint8_t)color;
                        }
                    }
                }

                source += (source_width * source_height * pixel_size);
                miptarget += (target_width * target_height * pixel_size);
            }
            desc.data.subimage[cube_face][level].ptr = target;
            desc.data.subimage[cube_face][level].size = img_size;
            target += img_size;
            if (desc.num_mipmaps <= level)
                desc.num_mipmaps = level + 1;
        }
    }

    sg_image img;

    if (Engine::isAsyncThread()){
        img = SokolCmdQueue::add_command_make_image(desc);
    }else{
        img = sg_make_image(desc);
    }
    SystemRender::scheduleCleanup(cleanupMipmapTexture, big_target);
    
    return img;
}

bool SokolTexture::createTexture(
            const std::string& label, int width, int height,
            ColorFormat colorFormat, TextureType type, int numFaces, void* data[6], size_t size[6], 
            TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){

    sg_pixel_format pixelFormat;
    if (colorFormat == ColorFormat::RGBA){
        pixelFormat = SG_PIXELFORMAT_RGBA8;
    }else if (colorFormat == ColorFormat::RED){
        pixelFormat = SG_PIXELFORMAT_R8;
    }else{
        Log::error("Renders only support 8bpp and 32bpp textures");
    }

    sg_image_desc image_desc = {0};
    image_desc.type = getTextureType(type);
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = pixelFormat;
    image_desc.num_slices = 1;
    image_desc.label = label.c_str();

    sg_sampler_desc sampler_desc = {0};
    sampler_desc.min_filter = getFilter(minFilter);
    sampler_desc.mag_filter = getFilter(magFilter);
    sampler_desc.mipmap_filter = getFilterMipmap(minFilter);
    sampler_desc.wrap_u = getWrap(wrapU);
    sampler_desc.wrap_v = getWrap(wrapV);

    for (int f = 0; f < numFaces; f++){
        image_desc.data.subimage[f][0].ptr = data[f];
        image_desc.data.subimage[f][0].size = size[f];
    }

    if (sampler_desc.mipmap_filter == SG_FILTER_LINEAR || sampler_desc.mipmap_filter == SG_FILTER_NEAREST){
        image = generateMipmaps(&image_desc);
    }else{
        if (Engine::isAsyncThread()){
            image = SokolCmdQueue::add_command_make_image(image_desc);
        }else{
            image = sg_make_image(image_desc);
        }
    }
    if (Engine::isAsyncThread()){
        sampler = SokolCmdQueue::add_command_make_sampler(sampler_desc);
    }else{
        sampler = sg_make_sampler(sampler_desc);
    }

    if (image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID)
        return true;

    return false;
}

bool SokolTexture::createFramebufferTexture(
            TextureType type, bool depth, bool shadowMap, int width, int height, 
            TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
    sg_image_desc img_desc = {0};
    img_desc.usage.render_attachment = true;
    img_desc.type = getTextureType(type);
    img_desc.width = width;
    img_desc.height = height;

    sg_sampler_desc sampler_desc = {0};
    sampler_desc.min_filter = getFilter(minFilter);
    sampler_desc.mag_filter = getFilter(magFilter);
    sampler_desc.mipmap_filter = getFilterMipmap(minFilter);
    sampler_desc.wrap_u = getWrap(wrapU);
    sampler_desc.wrap_v = getWrap(wrapV);
    if (shadowMap){
        // Over sampling - https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping
        sampler_desc.border_color = SG_BORDERCOLOR_OPAQUE_WHITE;
    }

    //if not set Sokol gets default from sg_desc.context.sample_count
    img_desc.sample_count = 1;

    if (depth){
        img_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
        img_desc.label = "framebuffer-depth-image";
    } else {
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        img_desc.label = "framebuffer-color-image";
    }

    if (Engine::isAsyncThread()){
        image = SokolCmdQueue::add_command_make_image(img_desc);
        sampler = SokolCmdQueue::add_command_make_sampler(sampler_desc);
    }else{
        image = sg_make_image(img_desc);
        sampler = sg_make_sampler(sampler_desc);
    }

    if (image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID)
        return true;

    return false;
}

void SokolTexture::destroyTexture(){
    if (image.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_image(image);
        }else{
            sg_destroy_image(image);
        }
    }
    if (sampler.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_sampler(sampler);
        }else{
            sg_destroy_sampler(sampler);
        }
    }

    image.id = SG_INVALID_ID;
    sampler.id = SG_INVALID_ID;
}

uint32_t SokolTexture::getGLHandler() const{
    if (image.id != SG_INVALID_ID && sg_isvalid()){
        sg_gl_image_info info = sg_gl_query_image_info(image);

        return info.tex[info.active_slot];
    }

    return 0;
}

const void* SokolTexture::getMetalHandler() const{
    if (image.id != SG_INVALID_ID && sg_isvalid()){
        sg_mtl_image_info info = sg_mtl_query_image_info(image);

        return info.tex[info.active_slot];
    }

    return nullptr;
}

const void* SokolTexture::getD3D11Handler() const{
    if (image.id != SG_INVALID_ID && sg_isvalid()){
        sg_d3d11_image_info info = sg_d3d11_query_image_info(image);

        return info.res;
    }

    return nullptr;
}

bool SokolTexture::isCreated(){
    if (image.id != SG_INVALID_ID && sg_isvalid()) {
        return sg_query_image_state(image) == SG_RESOURCESTATE_VALID;
    }

    return false;
}

sg_image SokolTexture::get(){
    return image;
}

sg_sampler SokolTexture::getSampler(){
    return sampler;
}
