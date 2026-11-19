// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SokolFramebuffer.h"
#include "Log.h"
#include "SokolCmdQueue.h"
#include "Engine.h"

using namespace doriax;

SokolFramebuffer::SokolFramebuffer(){
    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        for (int i = 0; i < 6; i++){
            colorAttachmentViews[a][i].id = SG_INVALID_ID;
        }
    }
    depthAttachmentView.id = SG_INVALID_ID;
    numColorAttachments = 1;
    colorFormat = ColorFormat::RGBA;
}

SokolFramebuffer::SokolFramebuffer(const SokolFramebuffer& rhs){
    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        for (int i = 0; i < 6; i++){
            colorAttachmentViews[a][i] = rhs.colorAttachmentViews[a][i];
        }
        colorTexture[a] = rhs.colorTexture[a];
    }
    depthAttachmentView = rhs.depthAttachmentView;
    depthTexture = rhs.depthTexture;
    numColorAttachments = rhs.numColorAttachments;
    colorFormat = rhs.colorFormat;
}

SokolFramebuffer& SokolFramebuffer::operator=(const SokolFramebuffer& rhs){
    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        for (int i = 0; i < 6; i++){
            colorAttachmentViews[a][i] = rhs.colorAttachmentViews[a][i];
        }
        colorTexture[a] = rhs.colorTexture[a];
    }
    depthAttachmentView = rhs.depthAttachmentView;
    depthTexture = rhs.depthTexture;
    numColorAttachments = rhs.numColorAttachments;
    colorFormat = rhs.colorFormat;

    return *this;
}

bool SokolFramebuffer::createFramebuffer(TextureType textureType, int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap){
    if ((textureType != TextureType::TEXTURE_2D) && (textureType != TextureType::TEXTURE_CUBE)){
        Log::error("Framebuffer texture type must be 2D or CUBE");
        return false;
    }

    destroyFramebuffer();

    numColorAttachments = 1;
    colorFormat = ColorFormat::RGBA;

    bool colorCreated = colorTexture[0].createFramebufferTexture(textureType, false, shadowMap, width, height, minFilter, magFilter, wrapU, wrapV);
    bool depthCreated = depthTexture.createFramebufferTexture(TextureType::TEXTURE_2D, true, shadowMap, width, height, minFilter, magFilter, wrapU, wrapV);

    if (!colorCreated || !depthCreated) {
        destroyFramebuffer();
        return false;
    }

    size_t faces = (textureType == TextureType::TEXTURE_CUBE)? 6 : 1;

    for (int i = 0; i < faces; i++){
        sg_view_desc color_view_desc = {0};
        color_view_desc.color_attachment.image = colorTexture[0].backend.get();
        color_view_desc.color_attachment.slice = i;
        color_view_desc.label = "framebuffer-color-attachment-view";

        if (Engine::isAsyncThread()){
            colorAttachmentViews[0][i] = SokolCmdQueue::add_command_make_view(color_view_desc);
        }else{
            colorAttachmentViews[0][i] = sg_make_view(color_view_desc);
        }
    }

    sg_view_desc depth_view_desc = {0};
    depth_view_desc.depth_stencil_attachment.image = depthTexture.backend.get();
    depth_view_desc.label = "framebuffer-depth-attachment-view";

    if (Engine::isAsyncThread()){
        depthAttachmentView = SokolCmdQueue::add_command_make_view(depth_view_desc);
    }else{
        depthAttachmentView = sg_make_view(depth_view_desc);
    }

    bool created = isCreated();
    if (!created) {
        destroyFramebuffer();
    }else{
        initializeAttachments(faces);
    }

    return created;
}

bool SokolFramebuffer::createDepthOnlyFramebuffer(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, bool shadowMap){
    destroyFramebuffer();

    numColorAttachments = 0;

    bool depthCreated = depthTexture.createFramebufferTexture(
        TextureType::TEXTURE_2D, true, shadowMap, width, height,
        minFilter, magFilter, wrapU, wrapV);
    if (!depthCreated){
        destroyFramebuffer();
        return false;
    }

    sg_view_desc depth_view_desc = {0};
    depth_view_desc.depth_stencil_attachment.image = depthTexture.backend.get();
    depth_view_desc.label = "framebuffer-depth-only-attachment-view";

    if (Engine::isAsyncThread()){
        depthAttachmentView = SokolCmdQueue::add_command_make_view(depth_view_desc);
    }else{
        depthAttachmentView = sg_make_view(depth_view_desc);
    }

    bool created = isCreated();
    if (!created){
        destroyFramebuffer();
    }else{
        initializeAttachments(1);
    }

    return created;
}

bool SokolFramebuffer::createFramebufferMRT(int width, int height, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV, int numColor, const ColorFormat* formats){
    if (numColor < 1 || numColor > MAX_COLOR_ATTACHMENTS){
        Log::error("Framebuffer MRT color attachment count must be 1..%d", MAX_COLOR_ATTACHMENTS);
        return false;
    }

    destroyFramebuffer();

    numColorAttachments = numColor;
    colorFormat = formats ? formats[0] : ColorFormat::RGBA;

    bool depthCreated = depthTexture.createFramebufferTexture(TextureType::TEXTURE_2D, true, false, width, height, minFilter, magFilter, wrapU, wrapV);
    if (!depthCreated){
        destroyFramebuffer();
        return false;
    }

    for (int a = 0; a < numColorAttachments; a++){
        ColorFormat fmt = formats ? formats[a] : ColorFormat::RGBA;
        bool colorCreated = colorTexture[a].createFramebufferTexture(TextureType::TEXTURE_2D, false, false, width, height, minFilter, magFilter, wrapU, wrapV, fmt);
        if (!colorCreated){
            destroyFramebuffer();
            return false;
        }

        sg_view_desc color_view_desc = {0};
        color_view_desc.color_attachment.image = colorTexture[a].backend.get();
        color_view_desc.color_attachment.slice = 0;
        color_view_desc.label = "framebuffer-mrt-color-attachment-view";

        if (Engine::isAsyncThread()){
            colorAttachmentViews[a][0] = SokolCmdQueue::add_command_make_view(color_view_desc);
        }else{
            colorAttachmentViews[a][0] = sg_make_view(color_view_desc);
        }
    }

    sg_view_desc depth_view_desc = {0};
    depth_view_desc.depth_stencil_attachment.image = depthTexture.backend.get();
    depth_view_desc.label = "framebuffer-mrt-depth-attachment-view";

    if (Engine::isAsyncThread()){
        depthAttachmentView = SokolCmdQueue::add_command_make_view(depth_view_desc);
    }else{
        depthAttachmentView = sg_make_view(depth_view_desc);
    }

    bool created = isCreated();
    if (!created) {
        destroyFramebuffer();
    }else{
        initializeAttachments(1);
    }

    return created;
}

void SokolFramebuffer::initializeAttachments(size_t faces){
#if defined(SOKOL_VULKAN)
    // A camera texture can be sampled before its camera has rendered once.
    // Give new attachments defined contents and a shader-readable layout.
    sg_pass pass = {};
    for (int i = 0; i < numColorAttachments; i++){
        pass.action.colors[i].store_action = SG_STOREACTION_STORE;
    }
    pass.action.depth.store_action = SG_STOREACTION_STORE;

    for (size_t face = 0; face < faces; face++){
        pass.attachments = get(face);
        if (Engine::isAsyncThread()){
            SokolCmdQueue::add_command_begin_pass(pass);
            SokolCmdQueue::add_command_end_pass();
        }else{
            sg_begin_pass(pass);
            sg_end_pass();
        }
    }
#else
    (void)faces;
#endif
}

void SokolFramebuffer::destroyFramebuffer(){
    if (sg_isvalid()){
        for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
            for (int i = 0; i < 6; i++){
                if (colorAttachmentViews[a][i].id == SG_INVALID_ID) {
                    continue;
                }

                if (Engine::isAsyncThread()){
                    SokolCmdQueue::add_command_destroy_view(colorAttachmentViews[a][i]);
                }else{
                    sg_destroy_view(colorAttachmentViews[a][i]);
                }
            }
        }

        if (depthAttachmentView.id != SG_INVALID_ID){
            if (Engine::isAsyncThread()){
                SokolCmdQueue::add_command_destroy_view(depthAttachmentView);
            }else{
                sg_destroy_view(depthAttachmentView);
            }
        }
    }

    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        colorTexture[a].destroyTexture();
    }
    depthTexture.destroyTexture();

    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        for (int i = 0; i < 6; i++){
            colorAttachmentViews[a][i].id = SG_INVALID_ID;
        }
    }
    depthAttachmentView.id = SG_INVALID_ID;
    numColorAttachments = 1;
    colorFormat = ColorFormat::RGBA;
}

bool SokolFramebuffer::isCreated(){
    if (!sg_isvalid()) {
        return false;
    }

    bool hasAttachment = false;
    for (int a = 0; a < MAX_COLOR_ATTACHMENTS; a++){
        for (int i = 0; i < 6; i++) {
            if (colorAttachmentViews[a][i].id == SG_INVALID_ID) {
                continue;
            }

            hasAttachment = true;
            if (sg_query_view_state(colorAttachmentViews[a][i]) != SG_RESOURCESTATE_VALID) {
                return false;
            }
        }
    }

    if (depthAttachmentView.id != SG_INVALID_ID) {
        hasAttachment = true;
        if (sg_query_view_state(depthAttachmentView) != SG_RESOURCESTATE_VALID) {
            return false;
        }
    }

    return hasAttachment;
}

int SokolFramebuffer::getNumColorAttachments() const{
    return numColorAttachments;
}

ColorFormat SokolFramebuffer::getColorFormat() const{
    return colorFormat;
}

bool SokolFramebuffer::isColorFormatSupported(ColorFormat format){
    const sg_pixelformat_info info = sg_query_pixelformat(SokolTexture::getRenderTargetFormat(format));
    return info.render && info.blend && info.filter;
}

TextureRender& SokolFramebuffer::getColorTexture(){
    return colorTexture[0];
}

TextureRender& SokolFramebuffer::getColorTexture(int index){
    if (index < 0 || index >= MAX_COLOR_ATTACHMENTS){
        index = 0;
    }
    return colorTexture[index];
}

TextureRender& SokolFramebuffer::getDepthTexture(){
    return depthTexture;
}

const void* SokolFramebuffer::getD3D11HandlerColorRTV() const{
    if (colorAttachmentViews[0][0].id != SG_INVALID_ID && sg_isvalid()){
        sg_d3d11_view_info info = sg_d3d11_query_view_info(colorAttachmentViews[0][0]);

        return info.rtv;
    }

    return nullptr;
}

const void* SokolFramebuffer::getD3D11HandlerDSV() const{
    if (depthAttachmentView.id != SG_INVALID_ID && sg_isvalid()){
        sg_d3d11_view_info info = sg_d3d11_query_view_info(depthAttachmentView);

        return info.dsv;
    }

    return nullptr;
}

sg_attachments SokolFramebuffer::get(size_t face){
    sg_attachments attachments = {};

    if (numColorAttachments > 1){
        // MRT framebuffers are 2D (single face); bind every color attachment.
        for (int a = 0; a < numColorAttachments; a++){
            attachments.colors[a] = colorAttachmentViews[a][0];
        }
    }else if (numColorAttachments == 1){
        attachments.colors[0] = colorAttachmentViews[0][face];
    }
    attachments.depth_stencil = depthAttachmentView;

    return attachments;
}
