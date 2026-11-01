// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SokolCamera.h"

#include "System.h"
#include "SokolCmdQueue.h"
#include "SokolObject.h"

#include "sokol_gfx.h"
#include <cmath>

using namespace doriax;

SokolCamera::SokolCamera(){
    pass = {0};
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
    textureView.id = SG_INVALID_ID;
}

SokolCamera::SokolCamera(const SokolCamera& rhs) : pass(rhs.pass) {
    textureView.id = SG_INVALID_ID;
}

SokolCamera& SokolCamera::operator=(const SokolCamera& rhs) { 
    pass = rhs.pass; 
    return *this;
}

SokolCamera::~SokolCamera(){

}

void SokolCamera::setClearColor(Vector4 clearColor){
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
}

void SokolCamera::setClearDepth(float clearDepth){
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    // Depth defaults to DONTCARE in Sokol; the atlas is sampled later, so store it.
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.action.depth.clear_value = clearDepth;
}

void SokolCamera::setLoadActionLoad(){
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
}

void SokolCamera::setDepthLoadActionLoad(){
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
}

void SokolCamera::setStoreDepth(bool storeDepth){
    pass.action.depth.store_action = storeDepth ? SG_STOREACTION_STORE : SG_STOREACTION_DONTCARE;
}

void SokolCamera::startRenderPass(FramebufferRender* framebuffer, size_t face){
    pass.swapchain = {};
    pass.attachments = framebuffer->backend.get(face);
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::startRenderPass(TextureRender* texture, size_t face, int mip){
    sg_view_desc view_desc = {0};
    view_desc.color_attachment.image = texture->backend.get();
    view_desc.color_attachment.mip_level = mip;
    view_desc.color_attachment.slice = (int)face;
    view_desc.label = "texture-pass-color-view";
    textureView = sg_make_view(view_desc);

    pass.swapchain = {};
    // with no view the empty attachments would begin a swapchain pass
    pass.swapchain.invalid = (textureView.id == SG_INVALID_ID);
    pass.attachments = {};
    pass.attachments.colors[0] = textureView;
    sg_begin_pass(pass);
}

void SokolCamera::startRenderPass(int width, int height){
    pass.attachments = {};
    pass.swapchain = System::instance().getSokolSwapchain();
    pass.swapchain.width = width;
    pass.swapchain.height = height;
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::startRenderPass(){
    pass.attachments = {};
    pass.swapchain = System::instance().getSokolSwapchain();
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::applyViewport(Rect rect){
    float x = rect.getX();
    float y = rect.getY();
    float flooredX = std::floor(x);
    float flooredY = std::floor(y);

    // Add truncated fractional parts to width and height
    float adjustedWidth = rect.getWidth() + (x - flooredX);
    float adjustedHeight = rect.getHeight() + (y - flooredY);

    //SokolCmdQueue::add_command_apply_viewport(flooredX, flooredY, std::ceil(adjustedWidth), std::ceil(adjustedHeight), false);
    sg_apply_viewport(flooredX, flooredY, std::ceil(adjustedWidth), std::ceil(adjustedHeight), false);
}

void SokolCamera::applyScissor(Rect rect){
    float x = rect.getX();
    float y = rect.getY();
    float flooredX = std::floor(x);
    float flooredY = std::floor(y);

    // Add truncated fractional parts to width and height
    float adjustedWidth = rect.getWidth() + (x - flooredX);
    float adjustedHeight = rect.getHeight() + (y - flooredY);

    //SokolCmdQueue::add_command_apply_scissor_rect(flooredX, flooredY, std::ceil(adjustedWidth), std::ceil(adjustedHeight), false);
    sg_apply_scissor_rect(flooredX, flooredY, std::ceil(adjustedWidth), std::ceil(adjustedHeight), false);
}

void SokolCamera::endRenderPass(){
    //SokolCmdQueue::add_command_end_pass();
    sg_end_pass();
    SokolObject::resetAppliedState();

    if (textureView.id != SG_INVALID_ID){
        sg_destroy_view(textureView);
        textureView.id = SG_INVALID_ID;
    }
}
