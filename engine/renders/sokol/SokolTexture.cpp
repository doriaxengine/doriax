// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SokolTexture.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "SokolObject.h"
#include "render/SystemRender.h"
#include "Engine.h"

#include <cstring>
#include <cstdlib>

#include "meshoptimizer.h"

// Sokol never sets TRANSFER_SRC, needed to copy render targets out for editor
// thumbnails (GraphicUtils). Wrapping the call keeps sokol itself unpatched
// (re-check when sokol_gfx is updated)
#if defined(DORIAX_EDITOR) && defined(SOKOL_VULKAN)
#include <vulkan/vulkan.h>

namespace {
VkResult createSokolImage(VkDevice device, const VkImageCreateInfo* createInfo,
                          const VkAllocationCallbacks* allocator, VkImage* image);
}

#define vkCreateImage createSokolImage

#include "../../libs/sokol/sokol.cpp"

#undef vkCreateImage

namespace {
VkResult createSokolImage(VkDevice device, const VkImageCreateInfo* createInfo,
                          const VkAllocationCallbacks* allocator, VkImage* image){
    VkImageCreateInfo info = *createInfo;
    if (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return vkCreateImage(device, &info, allocator, image);
}
}
#endif

using namespace doriax;

const void* SokolTexture::getVulkanView(sg_view view) {
#if defined(DORIAX_EDITOR) && defined(SOKOL_VULKAN)
    const _sg_view_t* resource = _sg_lookup_view(view.id);
    return resource ? reinterpret_cast<const void*>(resource->vk.img_view) : nullptr;
#else
    (void)view;
    return nullptr;
#endif
}

const void* SokolTexture::getVulkanImage(sg_image image) {
#if defined(DORIAX_EDITOR) && defined(SOKOL_VULKAN)
    const _sg_image_t* resource = _sg_lookup_image(image.id);
    return resource ? reinterpret_cast<const void*>(resource->vk.img) : nullptr;
#else
    (void)image;
    return nullptr;
#endif
}

SokolTexture::SokolTexture(){
    image.id = SG_INVALID_ID;
    sampler.id = SG_INVALID_ID;
    view.id = SG_INVALID_ID;
}

SokolTexture::SokolTexture(const SokolTexture& rhs): image(rhs.image), sampler(rhs.sampler), view(rhs.view) {}

SokolTexture& SokolTexture::operator=(const SokolTexture& rhs){
    image = rhs.image;
    sampler = rhs.sampler;
    view = rhs.view;
    return *this;
}

sg_image_type SokolTexture::getTextureType(TextureType textureType){
    if (textureType == TextureType::TEXTURE_2D){
        return SG_IMAGETYPE_2D;
    }else if (textureType == TextureType::TEXTURE_CUBE){
        return SG_IMAGETYPE_CUBE;
    }else if (textureType == TextureType::TEXTURE_ARRAY){
        return SG_IMAGETYPE_ARRAY;
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
// mip-level data layout: all slices (cube faces) of a level in one contiguous block
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

    int num_slices = (desc.num_slices > 0)? desc.num_slices : 1;

    int total_size = 0;
    {
        int w = desc.width;
        int h = desc.height;
        for (int level = 1; level < SG_MAX_MIPMAPS; ++level) {
            if (w == 1 && h == 1)
                break;

            w /= 2;
            h /= 2;

            if (w < 1)
                w = 1;

            if (h < 1)
                h = 1;

            total_size += (w * h * num_slices * pixel_size);
        }
    }

    unsigned char *big_target = (unsigned char *)malloc(total_size);
    unsigned char *target = big_target;

    const unsigned char* source = (const unsigned char*)desc.data.mip_levels[0].ptr;
    int source_width = desc.width;
    int source_height = desc.height;

    for (int level = 1; level < SG_MAX_MIPMAPS; ++level) {
        if (!source)
            break;

        if (source_width == 1 && source_height == 1)
            break;

        int target_width = source_width / 2;
        int target_height = source_height / 2;

        if (target_width < 1)
            target_width = 1;

        if (target_height < 1)
            target_height = 1;

        unsigned level_size = target_width * target_height * num_slices * pixel_size;

        for (int slice = 0; slice < num_slices; ++slice) {
            const unsigned char* srcslice = source + (slice * source_width * source_height * pixel_size);
            unsigned char* miptarget = target + (slice * target_width * target_height * pixel_size);

            for (int x = 0; x < target_width; ++x)
            {
                for (int y = 0; y < target_height; ++y)
                {
                    for (int chanell = 0; chanell < pixel_size; ++chanell)
                    {
                        int color = 0;
                        int sx = x * 2;
                        int sy = y * 2;
                        const int sx1 = (sx + 1 < source_width) ? (sx + 1) : (source_width - 1);
                        const int sy1 = (sy + 1 < source_height) ? (sy + 1) : (source_height - 1);

                        color += srcslice[(sy * source_width + sx) * pixel_size + chanell];
                        color += srcslice[(sy * source_width + sx1) * pixel_size + chanell];
                        color += srcslice[(sy1 * source_width + sx1) * pixel_size + chanell];
                        color += srcslice[(sy1 * source_width + sx) * pixel_size + chanell];
                        color /= 4;
                        miptarget[(y * target_width + x) * pixel_size + chanell] = (uint8_t)color;
                    }
                }
            }
        }

        desc.data.mip_levels[level].ptr = target;
        desc.data.mip_levels[level].size = level_size;

        source = target;
        target += level_size;
        source_width = target_width;
        source_height = target_height;

        if (desc.num_mipmaps <= level)
            desc.num_mipmaps = level + 1;
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

// R32F where float textures filter, else R16F, which always does
static void* unorm16ToFloat(sg_image_desc* desc){
    const uint16_t* source = (const uint16_t*)desc->data.mip_levels[0].ptr;
    const size_t count = desc->data.mip_levels[0].size / sizeof(uint16_t);

    if (sg_query_pixelformat(SG_PIXELFORMAT_R32F).filter){
        float* target = (float*)malloc(count * sizeof(float));
        for (size_t i = 0; i < count; i++){
            target[i] = source[i] / 65535.0f;
        }
        desc->pixel_format = SG_PIXELFORMAT_R32F;
        desc->data.mip_levels[0] = {target, count * sizeof(float)};
        return target;
    }

    uint16_t* target = (uint16_t*)malloc(count * sizeof(uint16_t));
    for (size_t i = 0; i < count; i++){
        target[i] = meshopt_quantizeHalf(source[i] / 65535.0f);
    }
    desc->pixel_format = SG_PIXELFORMAT_R16F;
    desc->data.mip_levels[0] = {target, count * sizeof(uint16_t)};
    return target;
}

bool SokolTexture::createTexture(
            const std::string& label, int width, int height,
            ColorFormat colorFormat, TextureType type, int numFaces, void* data[], size_t size[], 
            TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){

    sg_pixel_format pixelFormat;
    if (colorFormat == ColorFormat::RGBA){
        pixelFormat = SG_PIXELFORMAT_RGBA8;
    }else if (colorFormat == ColorFormat::RED){
        pixelFormat = SG_PIXELFORMAT_R8;
    }else if (colorFormat == ColorFormat::RED16){
        pixelFormat = SG_PIXELFORMAT_R16;
    }else{
        Log::error("Renders only support 8bpp, 16bpp and 32bpp textures");
    }

    sg_image_desc image_desc = {0};
    image_desc.type = getTextureType(type);
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = pixelFormat;
    image_desc.num_slices = (type == TextureType::TEXTURE_CUBE)? 6 : ((type == TextureType::TEXTURE_ARRAY)? numFaces : 1);
    image_desc.label = label.c_str();

    sg_sampler_desc sampler_desc = {0};
    sampler_desc.min_filter = getFilter(minFilter);
    sampler_desc.mag_filter = getFilter(magFilter);
    sampler_desc.mipmap_filter = getFilterMipmap(minFilter);
    sampler_desc.wrap_u = getWrap(wrapU);
    sampler_desc.wrap_v = getWrap(wrapV);

    // all faces of a mip level must be in one contiguous memory block
    unsigned char* combined = NULL;
    if (numFaces > 1){
        size_t total_size = 0;
        for (int f = 0; f < numFaces; f++){
            total_size += size[f];
        }
        combined = (unsigned char*)malloc(total_size);
        size_t offset = 0;
        for (int f = 0; f < numFaces; f++){
            memcpy(combined + offset, data[f], size[f]);
            offset += size[f];
        }
        image_desc.data.mip_levels[0].ptr = combined;
        image_desc.data.mip_levels[0].size = total_size;
    }else{
        image_desc.data.mip_levels[0].ptr = data[0];
        image_desc.data.mip_levels[0].size = size[0];
    }

    // sokol has no unorm16 textures on GLES3 and WebGL
    void* converted = NULL;
    if (pixelFormat == SG_PIXELFORMAT_R16 && !sg_query_pixelformat(SG_PIXELFORMAT_R16).filter){
        converted = unorm16ToFloat(&image_desc);
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
    // must come after image creation: in the synchronous path scheduleCleanup
    // frees immediately, and the data is only consumed inside sg_make_image
    if (combined){
        SystemRender::scheduleCleanup(cleanupMipmapTexture, combined);
    }
    if (converted){
        SystemRender::scheduleCleanup(cleanupMipmapTexture, converted);
    }
    if (Engine::isAsyncThread()){
        sampler = SokolCmdQueue::add_command_make_sampler(sampler_desc);
    }else{
        sampler = sg_make_sampler(sampler_desc);
    }

    createTextureView(label.c_str());

    if (image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID && view.id != SG_INVALID_ID)
        return true;

    return false;
}

bool SokolTexture::createDynamicTexture(const std::string& label, int width, int height){
    sg_image_desc imageDesc = {};
    imageDesc.usage.stream_update = true;
    imageDesc.width = width;
    imageDesc.height = height;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA32F;
    imageDesc.label = label.c_str();

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_NEAREST;
    samplerDesc.mag_filter = SG_FILTER_NEAREST;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;

    if (Engine::isAsyncThread()){
        image = SokolCmdQueue::add_command_make_image(imageDesc);
        sampler = SokolCmdQueue::add_command_make_sampler(samplerDesc);
    }else{
        image = sg_make_image(imageDesc);
        sampler = sg_make_sampler(samplerDesc);
    }
    createTextureView(label.c_str());
    return image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID && view.id != SG_INVALID_ID;
}

void SokolTexture::updateTexture(const void* data, size_t size){
    if (image.id == SG_INVALID_ID || !data || size == 0)
        return;

    sg_image_data imageData = {};
    imageData.mip_levels[0] = {data, size};
    if (Engine::isAsyncThread()){
        SokolCmdQueue::add_command_update_image(image, imageData);
    }else{
        sg_update_image(image, imageData);
        SokolObject::resetAppliedState();
    }
}

bool SokolTexture::createTextureCubeWithMips(
            const std::string& label, int width,
            ColorFormat colorFormat, int numMipmaps, void* data[], size_t size[],
            TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){

    sg_pixel_format pixelFormat;
    if (colorFormat == ColorFormat::RGBA){
        pixelFormat = SG_PIXELFORMAT_RGBA8;
    }else if (colorFormat == ColorFormat::RED){
        pixelFormat = SG_PIXELFORMAT_R8;
    }else{
        Log::error("Renders only support 8bpp and 32bpp textures");
        return false;
    }

    if (numMipmaps > SG_MAX_MIPMAPS){
        Log::error("Cubemap %s has more mipmaps than supported", label.c_str());
        return false;
    }

    sg_image_desc image_desc = {0};
    image_desc.type = SG_IMAGETYPE_CUBE;
    image_desc.width = width;
    image_desc.height = width;
    image_desc.pixel_format = pixelFormat;
    image_desc.num_slices = 6;
    image_desc.num_mipmaps = numMipmaps;
    image_desc.label = label.c_str();

    for (int level = 0; level < numMipmaps; level++){
        image_desc.data.mip_levels[level].ptr = data[level];
        image_desc.data.mip_levels[level].size = size[level];
    }

    sg_sampler_desc sampler_desc = {0};
    sampler_desc.min_filter = getFilter(minFilter);
    sampler_desc.mag_filter = getFilter(magFilter);
    sampler_desc.mipmap_filter = getFilterMipmap(minFilter);
    sampler_desc.wrap_u = getWrap(wrapU);
    sampler_desc.wrap_v = getWrap(wrapV);

    if (Engine::isAsyncThread()){
        image = SokolCmdQueue::add_command_make_image(image_desc);
        sampler = SokolCmdQueue::add_command_make_sampler(sampler_desc);
    }else{
        image = sg_make_image(image_desc);
        sampler = sg_make_sampler(sampler_desc);
    }

    createTextureView(label.c_str());

    if (image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID && view.id != SG_INVALID_ID)
        return true;

    return false;
}

void SokolTexture::createTextureView(const char* label){
    if (image.id == SG_INVALID_ID){
        view.id = SG_INVALID_ID;
        return;
    }

    sg_view_desc view_desc = {0};
    view_desc.texture.image = image;
    view_desc.label = label;

    if (Engine::isAsyncThread()){
        view = SokolCmdQueue::add_command_make_view(view_desc);
    }else{
        view = sg_make_view(view_desc);
    }
}

bool SokolTexture::createFramebufferTexture(
            TextureType type, bool depth, bool shadowMap, int width, int height,
            TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV,
            ColorFormat colorFormat, int numMipmaps){
    sg_image_desc img_desc = {0};
    if (depth){
        img_desc.usage.depth_stencil_attachment = true;
    }else{
        img_desc.usage.color_attachment = true;
    }
    img_desc.type = getTextureType(type);
    img_desc.width = width;
    img_desc.height = height;
    img_desc.num_slices = (type == TextureType::TEXTURE_CUBE)? 6 : 1;
    img_desc.num_mipmaps = numMipmaps;

    sg_sampler_desc sampler_desc = {0};
    sampler_desc.min_filter = getFilter(minFilter);
    sampler_desc.mag_filter = getFilter(magFilter);
    sampler_desc.mipmap_filter = getFilterMipmap(minFilter);
    sampler_desc.wrap_u = getWrap(wrapU);
    sampler_desc.wrap_v = getWrap(wrapV);
    if (shadowMap){
        // Over sampling - https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping
        sampler_desc.border_color = SG_BORDERCOLOR_OPAQUE_WHITE;
        if (depth){
            sampler_desc.compare = SG_COMPAREFUNC_LESS_EQUAL;
        }
    }

    //if not set Sokol gets default from sg_desc.context.sample_count
    img_desc.sample_count = 1;

    if (depth){
        img_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
        img_desc.label = "framebuffer-depth-image";
    } else {
        img_desc.pixel_format = (colorFormat == ColorFormat::RED) ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
        img_desc.label = "framebuffer-color-image";
    }

    if (Engine::isAsyncThread()){
        image = SokolCmdQueue::add_command_make_image(img_desc);
        sampler = SokolCmdQueue::add_command_make_sampler(sampler_desc);
    }else{
        image = sg_make_image(img_desc);
        sampler = sg_make_sampler(sampler_desc);
    }

    createTextureView(img_desc.label);

    if (image.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID && view.id != SG_INVALID_ID)
        return true;

    return false;
}

void SokolTexture::destroyTexture(){
    if (view.id != SG_INVALID_ID && sg_isvalid()){
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_destroy_view(view);
        }else{
            sg_destroy_view(view);
        }
    }
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
    view.id = SG_INVALID_ID;
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

const void* SokolTexture::getVulkanHandler() const{
    if (view.id != SG_INVALID_ID && sg_isvalid()){
        return getVulkanView(view);
    }

    return nullptr;
}

const void* SokolTexture::getVulkanImageHandler() const{
    if (image.id != SG_INVALID_ID && sg_isvalid()){
        return getVulkanImage(image);
    }

    return nullptr;
}

uint32_t SokolTexture::getViewId() const{
    return view.id;
}

bool SokolTexture::isViewValid(uint32_t viewId){
    if (viewId == SG_INVALID_ID || !sg_isvalid()) return false;
    return sg_query_view_state({viewId}) == SG_RESOURCESTATE_VALID;
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

sg_view SokolTexture::getView(){
    return view;
}
