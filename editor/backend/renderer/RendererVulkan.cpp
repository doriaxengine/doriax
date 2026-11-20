// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Renderer.h"

#include "CameraRender.h"
#include "SystemRender.h"
#include "TextureRender.h"

#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace doriax;
using namespace doriax::editor;

// VK_EXT_descriptor_buffer is only declared by recent Vulkan headers (1.3.275+).
// Ubuntu 22.04 ships 1.3.204, which is enough for the push descriptor path but
// doesn't know the extension, so the descriptor buffer code is compiled out
// there and the renderer always picks push descriptors.
#if defined(VK_EXT_descriptor_buffer)
#define DORIAX_HAS_DESCRIPTOR_BUFFER (1)
#else
#define DORIAX_HAS_DESCRIPTOR_BUFFER (0)
#endif

namespace {

// ImGui's swapchain helper accepts at most 15 images, and allocating the whole
// ring keeps it valid across present-mode changes
constexpr uint32_t IMGUI_RENDER_BUFFER_COUNT = 15;

// ImGui takes checkVkResult as its CheckVkResultFn, so latching here covers its
// submits too. A lost device never recovers, so one flag per process is enough.
bool deviceLost = false;

void checkVkResult(VkResult result){
    if (result == VK_ERROR_DEVICE_LOST) deviceLost = true;
    if (result < 0) std::fprintf(stderr, "Vulkan error: %d\n", result);
}

} // namespace

struct Renderer::State{
    RendererPlatform platform;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window swapchain;
    std::vector<VkSemaphore> sokolFinishedSemaphores;
    struct ImGuiTexture {
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
    };
    struct RetiredImGuiTexture {
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        uint32_t retireAfterFrame = 0;
    };
    std::unordered_map<uint32_t, ImGuiTexture> imguiTextures;
    std::vector<RetiredImGuiTexture> retiredImGuiTextures;
    // Sokol owns the image views handed to ImGui and frees them from its own
    // delete queue, which only waits Sokol's fences. ImGui command buffers
    // reference those views through descriptor sets that are submitted on a
    // separate schedule, so a view can be destroyed while still in use. This
    // flag records that a view was released and the command buffers have to
    // be drained before Sokol collects its delete queue.
    bool imguiViewReleased = false;
    uint32_t completedFrameCount = 0;
    CameraRender render;
    // devices without VK_EXT_descriptor_buffer (Vulkan 1.2 class hardware such
    // as Haswell) bind through VK_KHR_push_descriptor instead, and sokol picks
    // that path from the extension being enabled on the device
    bool pushDescriptors = false;
    // a Vulkan 1.2 device only answers to the core 1.3 names while the loader
    // promotes them, and a validation layer in between stops that, so the
    // entry points are resolved by hand with the KHR spelling as fallback
    PFN_vkCmdBeginRendering cmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering cmdEndRendering = nullptr;
    bool swapchainRebuild = false;
    bool frameSynchronized = true;

    static bool hasDeviceExtension(VkPhysicalDevice device, const char* name);

    bool failMainFrame(VkResult result);
    bool supportsVulkanDevice(VkPhysicalDevice device, uint32_t& family) const;
    bool createVulkanDevice();
    VkPresentModeKHR choosePresentMode(bool synchronized) const;
    void destroySokolSemaphores();
    void rebuildImGuiPipeline();
    bool createMainSwapchain(int width, int height, bool synchronized);
    void retireImGuiTexture(VkDescriptorSet descriptor);
    void waitSecondaryViewportFences();
    void drainImGuiCommandBuffers();
    void flushRetiredImGuiTextures(bool all);
};

bool Renderer::State::failMainFrame(VkResult result){
    checkVkResult(result);
    swapchainRebuild = true;
    return false;
}

bool Renderer::State::hasDeviceExtension(
    VkPhysicalDevice candidate, const char* name){
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(
            candidate, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(
            candidate, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;
    for (const VkExtensionProperties& extension : extensions)
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    return false;
}

bool Renderer::State::supportsVulkanDevice(
    VkPhysicalDevice candidate, uint32_t& family) const{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    if (!hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        return false;

    // Two ways to bind resources. Vulkan 1.3 hardware exposes descriptor
    // buffers; older hardware (Intel Haswell is Vulkan 1.2) can only bind
    // through push descriptors, which needs no buffer device address either.
    const bool descriptorBuffers =
#if DORIAX_HAS_DESCRIPTOR_BUFFER
        properties.apiVersion >= VK_API_VERSION_1_3 &&
        hasDeviceExtension(candidate, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
#else
        false;
#endif
    const bool pushDescriptors =
        hasDeviceExtension(candidate, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) &&
        hasDeviceExtension(candidate, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
        hasDeviceExtension(candidate, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (!descriptorBuffers && !pushDescriptors)
        return false;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState{};
    extendedDynamicState.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
#if DORIAX_HAS_DESCRIPTOR_BUFFER
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer{};
    descriptorBuffer.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    extendedDynamicState.pNext = descriptorBuffers ? &descriptorBuffer : nullptr;
#endif
    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12.pNext = &extendedDynamicState;
    // Vulkan 1.3 devices answer the core feature struct, older ones only the
    // KHR structs of the same extensions
    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
    dynamicRendering.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
    synchronization2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    if (descriptorBuffers){
        vulkan13.pNext = &vulkan12;
    }else{
        dynamicRendering.pNext = &synchronization2;
    }
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = descriptorBuffers ? static_cast<void*>(&vulkan13)
                                      : static_cast<void*>(&dynamicRendering);
    vkGetPhysicalDeviceFeatures2(candidate, &features);
    if (!features.features.samplerAnisotropy || !features.features.dualSrcBlend)
        return false;
    if (descriptorBuffers){
#if DORIAX_HAS_DESCRIPTOR_BUFFER
        if (!vulkan12.bufferDeviceAddress || !vulkan13.dynamicRendering ||
            !vulkan13.synchronization2 || !descriptorBuffer.descriptorBuffer)
            return false;
#endif
    }else if (!dynamicRendering.dynamicRendering ||
              !synchronization2.synchronization2){
        return false;
    }

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
    const VkQueueFlags required =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    for (uint32_t index = 0; index < count; ++index){
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &present);
        if ((families[index].queueFlags & required) == required && present){
            family = index;
            return true;
        }
    }
    return false;
}

bool Renderer::State::createVulkanDevice(){
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Doriax Engine Editor";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "Doriax";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        platform.surfaceExtension
    };
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(
        nullptr, &extensionCount, availableExtensions.data());
    for (const VkExtensionProperties& extension : availableExtensions){
        if (std::strcmp(extension.extensionName,
                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0){
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            break;
        }
    }

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS){
        std::fprintf(stderr, "Error: Could not create Vulkan instance (%d).\n", result);
        return false;
    }

    result = static_cast<VkResult>(platform.createSurface(
        ImGui::GetMainViewport(), reinterpret_cast<ImU64>(instance), nullptr,
        reinterpret_cast<ImU64*>(&surface)));
    if (result != VK_SUCCESS){
        std::fprintf(stderr, "Error: Could not create the Vulkan surface (%d).\n", result);
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    for (VkPhysicalDevice candidate : devices){
        uint32_t family = UINT32_MAX;
        if (!supportsVulkanDevice(candidate, family)) continue;
        physicalDevice = candidate;
        queueFamily = family;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) break;
    }
    if (physicalDevice == VK_NULL_HANDLE){
        std::fprintf(stderr,
            "Error: Vulkan 1.3 with descriptor buffers, or Vulkan 1.2 with push descriptors, is required.\n");
        return false;
    }

    // Haswell and other Vulkan 1.2 devices have no descriptor buffers, so the
    // renderer and sokol switch to push descriptors for them
#if DORIAX_HAS_DESCRIPTOR_BUFFER
    pushDescriptors =
        !hasDeviceExtension(physicalDevice, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
#else
    pushDescriptors = true;
#endif

    VkPhysicalDeviceFeatures2 supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
    // the descriptor buffer properties struct is unknown to a Vulkan 1.2
    // driver, so it is only queried when the device has the extension
#if DORIAX_HAS_DESCRIPTOR_BUFFER
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorProps{};
    descriptorProps.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
#endif
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
#if DORIAX_HAS_DESCRIPTOR_BUFFER
    properties.pNext = pushDescriptors ? nullptr : &descriptorProps;
#endif
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState{};
    extendedDynamicState.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
#if DORIAX_HAS_DESCRIPTOR_BUFFER
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer{};
    descriptorBuffer.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBuffer.descriptorBuffer = VK_TRUE;
    extendedDynamicState.pNext = pushDescriptors ? nullptr : &descriptorBuffer;
#endif
    extendedDynamicState.extendedDynamicState = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12.pNext = &extendedDynamicState;
    vulkan12.bufferDeviceAddress = pushDescriptors ? VK_FALSE : VK_TRUE;
    // Vulkan 1.2 hardware only knows the KHR feature structs, 1.3 promotes them
    // to the core structs, and the core structs must not be chained on 1.2
    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
    dynamicRendering.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
    synchronization2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    if (pushDescriptors){
        dynamicRendering.dynamicRendering = VK_TRUE;
        synchronization2.synchronization2 = VK_TRUE;
        dynamicRendering.pNext = &synchronization2;
        vulkan12.pNext = &dynamicRendering;
    }else{
        vulkan13.pNext = &vulkan12;
        vulkan13.dynamicRendering = VK_TRUE;
        vulkan13.synchronization2 = VK_TRUE;
    }
    VkPhysicalDeviceFeatures2 requiredFeatures{};
    requiredFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    requiredFeatures.pNext = pushDescriptors ? static_cast<void*>(&vulkan12)
                                                    : static_cast<void*>(&vulkan13);
    requiredFeatures.features.samplerAnisotropy = VK_TRUE;
    requiredFeatures.features.dualSrcBlend = VK_TRUE;
    // out-of-range buffer reads return zeros instead of hanging the GPU (seen on Intel),
    // only where the robust descriptor sizes match the plain ones sokol writes
    requiredFeatures.features.robustBufferAccess =
#if DORIAX_HAS_DESCRIPTOR_BUFFER
        !pushDescriptors && supported.features.robustBufferAccess &&
        descriptorProps.robustUniformBufferDescriptorSize == descriptorProps.uniformBufferDescriptorSize &&
        descriptorProps.robustStorageBufferDescriptorSize == descriptorProps.storageBufferDescriptorSize;
#else
        VK_FALSE;
#endif
    requiredFeatures.features.textureCompressionBC =
        supported.features.textureCompressionBC;
    requiredFeatures.features.textureCompressionETC2 =
        supported.features.textureCompressionETC2;
    requiredFeatures.features.textureCompressionASTC_LDR =
        supported.features.textureCompressionASTC_LDR;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    if (pushDescriptors){
        deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME);
    }else{
#if DORIAX_HAS_DESCRIPTOR_BUFFER
        deviceExtensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
#else
        // unreachable: without the extension in the headers pushDescriptors is
        // always true above
        std::fprintf(stderr, "Error: descriptor buffers are not available in these Vulkan headers.\n");
        return false;
#endif
    }
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &requiredFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS){
        std::fprintf(stderr, "Error: Could not create Vulkan device (%d).\n", result);
        return false;
    }
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Vulkan 1.2 drivers answer to the core 1.3 names only through the loader's
    // promotion, which a validation layer breaks, so ask for both spellings
    cmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
        vkGetDeviceProcAddr(device, "vkCmdBeginRendering"));
    if (cmdBeginRendering == nullptr){
        cmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
            vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
    }
    cmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
        vkGetDeviceProcAddr(device, "vkCmdEndRendering"));
    if (cmdEndRendering == nullptr){
        cmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
            vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
    }
    if (cmdBeginRendering == nullptr || cmdEndRendering == nullptr){
        std::fprintf(stderr, "Error: Dynamic rendering entry points are missing.\n");
        return false;
    }
    return true;
}

VkPresentModeKHR Renderer::State::choosePresentMode(
    bool synchronized) const{
    if (synchronized){
        const VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        return ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, surface, &mode, 1);
    }
    const VkPresentModeKHR modes[] = {
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR
    };
    return ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, surface, modes, 3);
}

void Renderer::State::destroySokolSemaphores(){
    for (VkSemaphore semaphore : sokolFinishedSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);
    sokolFinishedSemaphores.clear();
}

void Renderer::State::rebuildImGuiPipeline(){
    VkFormat format = swapchain.SurfaceFormat.format;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &format;

    ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
    pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.PipelineRenderingCreateInfo = renderingInfo;
    ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
}

bool Renderer::State::createMainSwapchain(
    int width, int height, bool synchronized){
    if (width <= 0 || height <= 0) return false;
    // nothing to build on a lost device, and the wait below can stall on one
    if (deviceLost) return false;
    if (device != VK_NULL_HANDLE){
        const VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS){
            checkVkResult(result);
            return false;
        }
        flushRetiredImGuiTextures(true);
    }
    destroySokolSemaphores();

    const VkFormat previousFormat = swapchain.SurfaceFormat.format;
    swapchain.UseDynamicRendering = true;
    swapchain.Surface = surface;
    const VkFormat formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    swapchain.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice, swapchain.Surface, formats, 2, VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    swapchain.PresentMode = choosePresentMode(synchronized);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance, physicalDevice, device, &swapchain, queueFamily, nullptr,
        width, height, 2, 0);
    if (ImGui::GetIO().BackendRendererUserData != nullptr &&
        previousFormat != swapchain.SurfaceFormat.format)
        rebuildImGuiPipeline();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sokolFinishedSemaphores.resize(swapchain.SemaphoreCount);
    for (VkSemaphore& semaphore : sokolFinishedSemaphores){
        if (vkCreateSemaphore(
                device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS){
            std::fprintf(stderr, "Error: Could not create Vulkan frame semaphore.\n");
            return false;
        }
    }
    swapchainRebuild = false;
    frameSynchronized = synchronized;
    platform.requestRedraw();
    return true;
}

void Renderer::State::retireImGuiTexture(VkDescriptorSet descriptor){
    if (descriptor == VK_NULL_HANDLE) return;
    uint32_t delay = swapchain.ImageCount;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable){
        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        for (int i = 0; i < platformIo.Viewports.Size; i++){
            ImGuiViewport* viewport = platformIo.Viewports[i];
            if (viewport == mainViewport) continue;
            ImGui_ImplVulkanH_Window* window =
                ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport);
            if (window && window->ImageCount > delay)
                delay = window->ImageCount;
        }
    }
    if (delay < 2) delay = 2;
    retiredImGuiTextures.push_back({descriptor, completedFrameCount + delay});
    imguiViewReleased = true;
}

void Renderer::State::drainImGuiCommandBuffers(){
    if (device == VK_NULL_HANDLE || !imguiViewReleased) return;
    imguiViewReleased = false;
    // Only the frame ImGui is about to record is known to be idle here; the
    // other swapchain frames may still be reading the released view from
    // their descriptor sets, so wait for all of them before Sokol frees it.
    for (int frame = 0; frame < swapchain.Frames.Size; frame++){
        VkFence fence = swapchain.Frames[frame].Fence;
        if (fence == VK_NULL_HANDLE) continue;
        const VkResult result =
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) checkVkResult(result);
    }
    waitSecondaryViewportFences();
}

void Renderer::State::waitSecondaryViewportFences(){
    if (device == VK_NULL_HANDLE) return;
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    for (int i = 0; i < platformIo.Viewports.Size; i++){
        ImGuiViewport* viewport = platformIo.Viewports[i];
        if (viewport == mainViewport) continue;
        ImGui_ImplVulkanH_Window* window =
            ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport);
        if (!window) continue;
        for (int frame = 0; frame < window->Frames.Size; frame++){
            VkFence fence = window->Frames[frame].Fence;
            if (fence == VK_NULL_HANDLE) continue;
            const VkResult result =
                vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS) checkVkResult(result);
        }
    }
}

void Renderer::State::flushRetiredImGuiTextures(bool all){
    if (retiredImGuiTextures.empty()) return;
    if (all){
        for (const RetiredImGuiTexture& texture : retiredImGuiTextures){
            ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
        }
        retiredImGuiTextures.clear();
        return;
    }

    bool anyDue = false;
    for (const RetiredImGuiTexture& texture : retiredImGuiTextures){
        if (completedFrameCount >= texture.retireAfterFrame){
            anyDue = true;
            break;
        }
    }
    if (!anyDue) return;

    // Platform windows submit after the main swapchain. Their fences are not
    // the ones waited in beginFrame, so drain them before freeing descriptors.
    waitSecondaryViewportFences();

    retiredImGuiTextures.erase(
        std::remove_if(
            retiredImGuiTextures.begin(),
            retiredImGuiTextures.end(),
            [this](const RetiredImGuiTexture& texture){
                if (completedFrameCount < texture.retireAfterFrame){
                    return false;
                }
                ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
                return true;
            }),
        retiredImGuiTextures.end());
}

Renderer::Renderer() : state(std::make_unique<State>()){}

Renderer::~Renderer(){
    // a lost device has nothing left to wait for, and the wait can stall on one
    if (state->device != VK_NULL_HANDLE && !deviceLost) vkDeviceWaitIdle(state->device);
    state->destroySokolSemaphores();
    if (state->swapchain.Swapchain != VK_NULL_HANDLE)
        ImGui_ImplVulkanH_DestroyWindow(
            state->instance, state->device, &state->swapchain, nullptr);
    if (state->surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(state->instance, state->surface, nullptr);
    if (state->device != VK_NULL_HANDLE) vkDestroyDevice(state->device, nullptr);
    if (state->instance != VK_NULL_HANDLE)
        vkDestroyInstance(state->instance, nullptr);
}

bool Renderer::init(const RendererPlatform& platform, int width, int height, bool synchronized){
    state->platform = platform;
    // ImGui_ImplVulkan_Init asserts on this handler when viewports are enabled
    ImGui::GetPlatformIO().Platform_CreateVkSurface = state->platform.createSurface;
    if (!state->createVulkanDevice() ||
        !state->createMainSwapchain(width, height, synchronized))
        return false;

    VkFormat format = state->swapchain.SurfaceFormat.format;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &format;

    ImGui_ImplVulkan_InitInfo info{};
    // the image backend resolves the KHR entry points when the core ones are
    // missing, which is the case on Vulkan 1.2 hardware
    info.ApiVersion = state->pushDescriptors ? VK_API_VERSION_1_2 : VK_API_VERSION_1_3;
    info.Instance = state->instance;
    info.PhysicalDevice = state->physicalDevice;
    info.Device = state->device;
    info.QueueFamily = state->queueFamily;
    info.Queue = state->queue;
    info.DescriptorPoolSize = 8192;
    info.MinImageCount = 2;
    info.ImageCount = IMGUI_RENDER_BUFFER_COUNT;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    info.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = checkVkResult;
    if (!ImGui_ImplVulkan_Init(&info)) return false;

    return true;
}

void Renderer::shutdownImGui(){
    if (state->device != VK_NULL_HANDLE && !deviceLost) vkDeviceWaitIdle(state->device);
    state->flushRetiredImGuiTextures(true);
    ImGui_ImplVulkan_Shutdown();
    state->imguiTextures.clear();
    ImGui::GetPlatformIO().Platform_CreateVkSurface = nullptr;
}

bool Renderer::updateTarget(int width, int height, bool synchronized){
    if (width <= 0 || height <= 0) return true;
    if (!state->swapchainRebuild && width == state->swapchain.Width &&
        height == state->swapchain.Height &&
        synchronized == state->frameSynchronized)
        return true;
    return state->createMainSwapchain(width, height, synchronized);
}

bool Renderer::beginFrame(){
    ImGui_ImplVulkanH_FrameSemaphores& semaphores =
        state->swapchain.FrameSemaphores[state->swapchain.SemaphoreIndex];
    VkResult result = vkAcquireNextImageKHR(
        state->device, state->swapchain.Swapchain, UINT64_MAX,
        semaphores.ImageAcquiredSemaphore, VK_NULL_HANDLE,
        &state->swapchain.FrameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR){
        state->swapchainRebuild = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) state->swapchainRebuild = true;
    else if (result != VK_SUCCESS) return state->failMainFrame(result);

    ImGui_ImplVulkanH_Frame& frame =
        state->swapchain.Frames[state->swapchain.FrameIndex];
    result = vkWaitForFences(state->device, 1, &frame.Fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) return state->failMainFrame(result);
    state->completedFrameCount++;
    state->flushRetiredImGuiTextures(false);
    // Sokol collects its delete queue when the first pass of the frame opens,
    // and that is where a view handed to ImGui gets destroyed. The frames that
    // still reference it have to finish before the frame is drawn.
    state->drainImGuiCommandBuffers();
    return true;
}

void Renderer::newFrame(){
    ImGui_ImplVulkan_NewFrame();
}

bool Renderer::endFrame(ImDrawData* drawData, int width, int height){
    // Textures are also retired while the frame is built (a view that changed
    // or went invalid), that is after the drain in beginFrame, and the pass
    // below is where Sokol collects its delete queue.
    state->drainImGuiCommandBuffers();
    // App::show() may create preview framebuffers. Initialize those before
    // opening the swapchain pass so Sokol never sees nested passes.
    state->render.setClearColor(Vector4(0.45f, 0.55f, 0.60f, 1.00f));
    state->render.startRenderPass(width, height);
    state->render.endRenderPass();
    SystemRender::commit();

    ImGui_ImplVulkanH_Frame& frame =
        state->swapchain.Frames[state->swapchain.FrameIndex];
    const uint32_t semaphoreIndex = state->swapchain.SemaphoreIndex;
    VkSemaphore waitSemaphore = state->sokolFinishedSemaphores[semaphoreIndex];
    VkSemaphore signalSemaphore =
        state->swapchain.FrameSemaphores[semaphoreIndex].RenderCompleteSemaphore;

    VkResult result = vkResetCommandPool(state->device, frame.CommandPool, 0);
    if (result != VK_SUCCESS) return state->failMainFrame(result);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(frame.CommandBuffer, &beginInfo);
    if (result != VK_SUCCESS) return state->failMainFrame(result);

    VkImageMemoryBarrier beginBarrier{};
    beginBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beginBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    beginBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    beginBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    beginBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.image = frame.Backbuffer;
    beginBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    beginBarrier.subresourceRange.levelCount = 1;
    beginBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        frame.CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        0, nullptr, 0, nullptr, 1, &beginBarrier);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = frame.BackbufferView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent.width = state->swapchain.Width;
    renderingInfo.renderArea.extent.height = state->swapchain.Height;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;
    state->cmdBeginRendering(frame.CommandBuffer, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(drawData, frame.CommandBuffer);
    state->cmdEndRendering(frame.CommandBuffer);

    VkImageMemoryBarrier endBarrier{};
    endBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    endBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    endBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    endBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    endBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.image = frame.Backbuffer;
    endBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    endBarrier.subresourceRange.levelCount = 1;
    endBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        frame.CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &endBarrier);

    result = vkEndCommandBuffer(frame.CommandBuffer);
    if (result != VK_SUCCESS) return state->failMainFrame(result);
    result = vkResetFences(state->device, 1, &frame.Fence);
    if (result != VK_SUCCESS) return state->failMainFrame(result);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;
    result = vkQueueSubmit(state->queue, 1, &submitInfo, frame.Fence);
    if (result != VK_SUCCESS) return state->failMainFrame(result);
    return true;
}

void Renderer::present(){
    ImGui_ImplVulkanH_FrameSemaphores& semaphores =
        state->swapchain.FrameSemaphores[state->swapchain.SemaphoreIndex];
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphores.RenderCompleteSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &state->swapchain.Swapchain;
    presentInfo.pImageIndices = &state->swapchain.FrameIndex;
    const VkResult result = vkQueuePresentKHR(state->queue, &presentInfo);
    if (result != VK_SUCCESS){
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            checkVkResult(result);
        state->swapchainRebuild = true;
    }
    state->swapchain.SemaphoreIndex =
        (state->swapchain.SemaphoreIndex + 1) % state->swapchain.SemaphoreCount;
}

void Renderer::renderViewports(bool render){
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)){
        return;
    }
    ImGui::UpdatePlatformWindows();
    if (render){
        ImGui::RenderPlatformWindowsDefault();
    }
}

bool Renderer::isDeviceLost(){
    return deviceLost;
}

ImTextureID Renderer::getTexture(TextureRender* texture){
    const uint32_t viewId = texture->getViewId();
    VkImageView imageView = reinterpret_cast<VkImageView>(
        const_cast<void*>(texture->getVulkanHandler()));
    if (imageView == VK_NULL_HANDLE) return ImTextureID{};

    auto existing = state->imguiTextures.find(viewId);
    if (existing != state->imguiTextures.end()){
        if (existing->second.imageView == imageView)
            return reinterpret_cast<ImTextureID>(existing->second.descriptor);
        state->retireImGuiTexture(existing->second.descriptor);
        state->imguiTextures.erase(existing);
    }

    VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    state->imguiTextures.emplace(viewId, State::ImGuiTexture{descriptor, imageView});
    return reinterpret_cast<ImTextureID>(descriptor);
}

void Renderer::purgeTextures(){
    if (state->device == VK_NULL_HANDLE) return;

    for (auto entry = state->imguiTextures.begin();
         entry != state->imguiTextures.end();){
        if (TextureRender::isViewValid(entry->first)){
            ++entry;
            continue;
        }
        state->retireImGuiTexture(entry->second.descriptor);
        entry = state->imguiTextures.erase(entry);
    }
}

sg_environment Renderer::getSokolEnvironment() const{
    sg_environment environment{};
    environment.defaults.sample_count = 1;
    environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    environment.vulkan.instance = state->instance;
    environment.vulkan.physical_device = state->physicalDevice;
    environment.vulkan.device = state->device;
    environment.vulkan.queue = state->queue;
    environment.vulkan.queue_family_index = state->queueFamily;
    return environment;
}

sg_swapchain Renderer::getSokolSwapchain() const{
    const ImGui_ImplVulkanH_Frame& frame =
        state->swapchain.Frames[state->swapchain.FrameIndex];
    const ImGui_ImplVulkanH_FrameSemaphores& semaphores =
        state->swapchain.FrameSemaphores[state->swapchain.SemaphoreIndex];
    sg_swapchain result{};
    result.width = state->swapchain.Width;
    result.height = state->swapchain.Height;
    result.sample_count = 1;
    result.color_format =
        state->swapchain.SurfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM
            ? SG_PIXELFORMAT_RGBA8
            : SG_PIXELFORMAT_BGRA8;
    result.depth_format = SG_PIXELFORMAT_NONE;
    result.vulkan.render_image = frame.Backbuffer;
    result.vulkan.render_view = frame.BackbufferView;
    result.vulkan.present_complete_semaphore = semaphores.ImageAcquiredSemaphore;
    result.vulkan.render_finished_semaphore =
        state->sokolFinishedSemaphores[state->swapchain.SemaphoreIndex];
    return result;
}
