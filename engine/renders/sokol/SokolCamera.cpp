//
// (c) 2026 Eduardo Doria.
//

#include "SokolCamera.h"

#include "System.h"
#include "SokolCmdQueue.h"

#include "sokol_gfx.h"
#include <cmath>

using namespace doriax;

SokolCamera::SokolCamera(){
    pass = {0};
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
}

SokolCamera::SokolCamera(const SokolCamera& rhs) : pass(rhs.pass) {}

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

void SokolCamera::setLoadActionLoad(){
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
}

void SokolCamera::startRenderPass(FramebufferRender* framebuffer, size_t face){
    pass.attachments = framebuffer->backend.get(face);
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::startRenderPass(int width, int height){
    pass.swapchain = System::instance().getSokolSwapchain();
    pass.swapchain.width = width;
    pass.swapchain.height = height;
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::startRenderPass(){
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
}
