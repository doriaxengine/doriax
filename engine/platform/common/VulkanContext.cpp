// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "VulkanContext.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace doriax;

// VK_EXT_descriptor_buffer is only declared by recent Vulkan headers (1.3.275+).
// Older headers (Ubuntu 22.04 ships 1.3.204) are enough for the push descriptor
// path, so the descriptor buffer code is compiled out there and the runtime
// always picks push descriptors.
#if defined(VK_EXT_descriptor_buffer)
#define DORIAX_HAS_DESCRIPTOR_BUFFER (1)
#else
#define DORIAX_HAS_DESCRIPTOR_BUFFER (0)
#endif

namespace {

    // The validation layer reports the present-complete semaphore as reused when
    // the swapchain runs with fewer than three images
    constexpr uint32_t MIN_SWAPCHAIN_IMAGES = 3;

    // What sokol maps SG_PIXELFORMAT_DEPTH_STENCIL to
    constexpr VkFormat DEPTH_STENCIL_FORMAT = VK_FORMAT_D32_SFLOAT_S8_UINT;

    // sokol_gfx renders with dynamic rendering and binds resources either
    // through descriptor buffers (Vulkan 1.3 hardware) or through push
    // descriptors (Vulkan 1.2 hardware such as Haswell, which never gets
    // VK_EXT_descriptor_buffer). The swapchain extension is always required.
    const char* const DEVICE_EXTENSIONS[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // The extensions needed by the push descriptor path, all of them promoted
    // to core in Vulkan 1.3 but only available as extensions on 1.2 devices.
    const char* const PUSH_DESCRIPTOR_EXTENSIONS[] = {
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME
    };

    // Set when the chosen device has no VK_EXT_descriptor_buffer, and therefore
    // needs push descriptors and no buffer device address.
    bool gUsePushDescriptors = false;

    // A portability driver only creates a device when this is enabled too. Spelled
    // out because the constant lives behind VK_ENABLE_BETA_EXTENSIONS.
    constexpr char PORTABILITY_SUBSET_EXTENSION[] = "VK_KHR_portability_subset";
    // Also spelled out: the headers before Vulkan 1.3.207 don't declare the
    // instance extension that exposes a portability driver.
    constexpr char PORTABILITY_ENUMERATION_EXTENSION[] = "VK_KHR_portability_enumeration";

    struct FrameSync {
        VkSemaphore presentComplete = VK_NULL_HANDLE;  // signaled by the acquire
        VkSemaphore renderFinished = VK_NULL_HANDLE;   // signaled by sokol's submit
    };

    VulkanContextConfig gConfig;

    VkInstance gInstance = VK_NULL_HANDLE;
    VkSurfaceKHR gSurface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR gSurfaceFormat{};
    VkPhysicalDevice gPhysicalDevice = VK_NULL_HANDLE;
    uint32_t gQueueFamily = UINT32_MAX;
    VkDevice gDevice = VK_NULL_HANDLE;
    VkQueue gQueue = VK_NULL_HANDLE;

    VkSwapchainKHR gSwapchain = VK_NULL_HANDLE;
    bool gSwapchainValid = false;
    bool gImageAcquired = false;
    // Whether sokol recorded a pass into the acquired image this frame. A frame
    // that draws nothing (an empty scene stack while a scene is being replaced)
    // never begins the swapchain pass, so sokol submits no signal for
    // renderFinished and presenting on it would block forever.
    bool gSwapchainRendered = false;
    uint32_t gImageCount = 0;
    uint32_t gImageIndex = 0;
    uint32_t gSyncSlot = 0;
    int gWidth = 0;
    int gHeight = 0;

    std::vector<VkImage> gImages;
    std::vector<VkImageView> gImageViews;
    std::vector<FrameSync> gSync;

    VkImage gDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory gDepthMemory = VK_NULL_HANDLE;
    VkImageView gDepthView = VK_NULL_HANDLE;

    bool hasInstanceExtension(const char* name) {
        uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateInstanceExtensionProperties(
                nullptr, &count, extensions.data()) != VK_SUCCESS)
            return false;
        for (const VkExtensionProperties& extension : extensions)
            if (std::strcmp(extension.extensionName, name) == 0) return true;
        return false;
    }

    std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice device) {
        uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
            return {};
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(
                device, nullptr, &count, extensions.data()) != VK_SUCCESS)
            return {};
        return extensions;
    }

    bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
        for (const VkExtensionProperties& extension : extensions)
            if (std::strcmp(extension.extensionName, name) == 0) return true;
        return false;
    }

    bool hasDeviceExtensions(VkPhysicalDevice device) {
        const std::vector<VkExtensionProperties> extensions = deviceExtensions(device);
        for (const char* required : DEVICE_EXTENSIONS)
            if (!hasExtension(extensions, required)) return false;
        return true;
    }

    // True when the device can bind through push descriptors instead of
    // descriptor buffers. Vulkan 1.2 class hardware reports the extensions
    // under their KHR names, so they are checked by name rather than by the
    // promoted core feature structs.
    bool supportsPushDescriptors(VkPhysicalDevice device) {
        const std::vector<VkExtensionProperties> extensions = deviceExtensions(device);
        for (const char* required : PUSH_DESCRIPTOR_EXTENSIONS)
            if (!hasExtension(extensions, required)) return false;
        return true;
    }

    bool createInstance() {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "Doriax";
        application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application.pEngineName = "Doriax";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_3;

        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            gConfig.surfaceExtension
        };
        // A debug build of sokol_gfx names every object it creates through
        // vkSetDebugUtilsObjectNameEXT, which the loader only resolves with this
        // extension enabled, and asserts when it is missing.
        if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        // A portability driver is hidden from vkEnumeratePhysicalDevices unless the
        // instance asks for it, and on macOS MoltenVK is the only driver there is.
        VkInstanceCreateFlags flags = 0;
        if (hasInstanceExtension(PORTABILITY_ENUMERATION_EXTENSION)) {
            extensions.push_back(PORTABILITY_ENUMERATION_EXTENSION);
            // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
            flags |= 0x00000001;
        }

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.flags = flags;
        instanceInfo.pApplicationInfo = &application;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.ppEnabledExtensionNames = extensions.data();

        const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &gInstance);
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not create the Vulkan instance (%d).\n", result);
            return false;
        }
        return true;
    }

    // One family doing graphics, compute, transfer and presentation, which is
    // what every desktop driver offers. UINT32_MAX when the device has none.
    uint32_t findQueueFamily(VkPhysicalDevice device) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        const VkQueueFlags required =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        for (uint32_t family = 0; family < count; family++) {
            if ((families[family].queueFlags & required) != required) continue;
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, family, gSurface, &canPresent);
            if (canPresent) return family;
        }
        return UINT32_MAX;
    }

    bool pickPhysicalDevice() {
        uint32_t count = 0;
        VkResult result = vkEnumeratePhysicalDevices(gInstance, &count, nullptr);
        if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
            std::fprintf(stderr, "Error: No Vulkan device found.\n");
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        result = vkEnumeratePhysicalDevices(gInstance, &count, devices.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            std::fprintf(stderr, "Error: Could not enumerate the Vulkan devices (%d).\n", result);
            return false;
        }

        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (!hasDeviceExtensions(candidate)) continue;

            // Descriptor buffers on Vulkan 1.3 hardware, push descriptors on
            // everything older that has the extensions they need
            const bool descriptorBuffers =
#if DORIAX_HAS_DESCRIPTOR_BUFFER
                properties.apiVersion >= VK_API_VERSION_1_3 &&
                hasExtension(deviceExtensions(candidate), VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
#else
                false;
#endif
            const bool pushDescriptors =
                properties.apiVersion >= VK_API_VERSION_1_2 &&
                supportsPushDescriptors(candidate);
            if (!descriptorBuffers && !pushDescriptors) continue;

            const uint32_t family = findQueueFamily(candidate);
            if (family == UINT32_MAX) continue;

            gPhysicalDevice = candidate;
            gQueueFamily = family;
            gUsePushDescriptors = !descriptorBuffers;
            // A software device only stands in until real hardware turns up
            if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) break;
        }

        if (gPhysicalDevice == VK_NULL_HANDLE) {
            std::fprintf(stderr,
                "Error: Vulkan 1.2 with swapchain and descriptor-buffer or push-descriptor support is required.\n");
#if defined(__APPLE__)
            std::fprintf(stderr,
                "       MoltenVK has no VK_EXT_descriptor_buffer, so macOS needs the Metal backend.\n");
#endif
            return false;
        }
        return true;
    }

    bool createDevice() {
        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vkGetPhysicalDeviceFeatures2(gPhysicalDevice, &supported);

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState{};
        extendedDynamicState.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
#if DORIAX_HAS_DESCRIPTOR_BUFFER
        VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer{};
        descriptorBuffer.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
        descriptorBuffer.descriptorBuffer = VK_TRUE;
        extendedDynamicState.pNext = gUsePushDescriptors ? nullptr : &descriptorBuffer;
#endif
        extendedDynamicState.extendedDynamicState = VK_TRUE;

        VkPhysicalDeviceVulkan12Features vulkan12{};
        vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan12.pNext = &extendedDynamicState;
        vulkan12.bufferDeviceAddress = gUsePushDescriptors ? VK_FALSE : VK_TRUE;

        // Vulkan 1.2 devices answer the KHR feature structs, and chaining the
        // promoted core structs there is invalid
        VkPhysicalDeviceVulkan13Features vulkan13{};
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
        dynamicRendering.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
        synchronization2.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
        if (gUsePushDescriptors) {
            dynamicRendering.dynamicRendering = VK_TRUE;
            synchronization2.synchronization2 = VK_TRUE;
            dynamicRendering.pNext = &synchronization2;
            vulkan12.pNext = &dynamicRendering;
        } else {
            vulkan13.pNext = &vulkan12;
            vulkan13.dynamicRendering = VK_TRUE;
            vulkan13.synchronization2 = VK_TRUE;
        }

        VkPhysicalDeviceFeatures2 required{};
        required.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        required.pNext = gUsePushDescriptors ? static_cast<void*>(&vulkan12)
                                             : static_cast<void*>(&vulkan13);
        required.features.samplerAnisotropy = VK_TRUE;
        required.features.dualSrcBlend = VK_TRUE;
        // Compressed formats are per-device: ask only for the ones this one has
        required.features.textureCompressionBC = supported.features.textureCompressionBC;
        required.features.textureCompressionETC2 = supported.features.textureCompressionETC2;
        required.features.textureCompressionASTC_LDR = supported.features.textureCompressionASTC_LDR;

        std::vector<const char*> extensions;
        for (const char* required : DEVICE_EXTENSIONS) extensions.push_back(required);
        if (gUsePushDescriptors) {
            for (const char* required : PUSH_DESCRIPTOR_EXTENSIONS)
                extensions.push_back(required);
        } else {
#if DORIAX_HAS_DESCRIPTOR_BUFFER
            extensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
#else
            // unreachable: without the extension in the headers gUsePushDescriptors
            // is always true above
            std::fprintf(stderr, "Error: descriptor buffers are not available in these Vulkan headers.\n");
            return false;
#endif
        }
        if (hasExtension(deviceExtensions(gPhysicalDevice), PORTABILITY_SUBSET_EXTENSION))
            extensions.push_back(PORTABILITY_SUBSET_EXTENSION);

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = gQueueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &required;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();

        const VkResult result = vkCreateDevice(gPhysicalDevice, &deviceInfo, nullptr, &gDevice);
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not create the Vulkan device (%d).\n", result);
            return false;
        }
        vkGetDeviceQueue(gDevice, gQueueFamily, 0, &gQueue);
        return true;
    }

    VkSurfaceFormatKHR pickSurfaceFormat() {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gPhysicalDevice, gSurface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gPhysicalDevice, gSurface, &count, formats.data());

        // The engine presents an already converted image, so a linear format is
        // the one that leaves it alone
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                format.format == VK_FORMAT_R8G8B8A8_UNORM)
                return format;
        }

        VkSurfaceFormatKHR fallback{};
        fallback.format = VK_FORMAT_B8G8R8A8_UNORM;
        fallback.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        return count > 0 ? formats[0] : fallback;
    }

    VkPresentModeKHR pickPresentMode() {
        if (gConfig.vsync) return VK_PRESENT_MODE_FIFO_KHR;

        uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                gPhysicalDevice, gSurface, &count, nullptr) != VK_SUCCESS || count == 0)
            return VK_PRESENT_MODE_FIFO_KHR;
        std::vector<VkPresentModeKHR> modes(count);
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                gPhysicalDevice, gSurface, &count, modes.data()) != VK_SUCCESS)
            return VK_PRESENT_MODE_FIFO_KHR;

        // IMMEDIATE is vsync actually off; MAILBOX still never blocks the loop
        // and is the best fallback where IMMEDIATE is missing
        VkPresentModeKHR picked = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) return VK_PRESENT_MODE_IMMEDIATE_KHR;
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) picked = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        return picked;
    }

    void destroyDepthBuffer() {
        if (gDepthView != VK_NULL_HANDLE) {
            vkDestroyImageView(gDevice, gDepthView, nullptr);
            gDepthView = VK_NULL_HANDLE;
        }
        if (gDepthImage != VK_NULL_HANDLE) {
            vkDestroyImage(gDevice, gDepthImage, nullptr);
            gDepthImage = VK_NULL_HANDLE;
        }
        if (gDepthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(gDevice, gDepthMemory, nullptr);
            gDepthMemory = VK_NULL_HANDLE;
        }
    }

    bool createDepthBuffer(uint32_t width, uint32_t height) {
        destroyDepthBuffer();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = DEPTH_STENCIL_FORMAT;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(gDevice, &imageInfo, nullptr, &gDepthImage) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not create the depth-stencil image.\n");
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(gDevice, gDepthImage, &requirements);

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(gPhysicalDevice, &memoryProperties);
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
            const bool allowed = (requirements.memoryTypeBits & (1u << i)) != 0;
            const bool deviceLocal = (memoryProperties.memoryTypes[i].propertyFlags &
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            if (allowed && deviceLocal) {
                memoryType = i;
                break;
            }
        }
        if (memoryType == UINT32_MAX) {
            std::fprintf(stderr, "Error: No device-local memory for the depth-stencil image.\n");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(gDevice, &allocateInfo, nullptr, &gDepthMemory) != VK_SUCCESS ||
            vkBindImageMemory(gDevice, gDepthImage, gDepthMemory, 0) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not allocate the depth-stencil image memory.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = gDepthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = DEPTH_STENCIL_FORMAT;
        viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(gDevice, &viewInfo, nullptr, &gDepthView) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not create the depth-stencil image view.\n");
            return false;
        }
        return true;
    }

    void destroySyncObjects() {
        for (FrameSync& sync : gSync) {
            if (sync.presentComplete != VK_NULL_HANDLE)
                vkDestroySemaphore(gDevice, sync.presentComplete, nullptr);
            if (sync.renderFinished != VK_NULL_HANDLE)
                vkDestroySemaphore(gDevice, sync.renderFinished, nullptr);
        }
        gSync.clear();
        gSyncSlot = 0;
    }

    bool createSyncObjects() {
        destroySyncObjects();

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        gSync.resize(gImageCount);
        for (FrameSync& sync : gSync) {
            if (vkCreateSemaphore(gDevice, &semaphoreInfo, nullptr, &sync.presentComplete) != VK_SUCCESS ||
                vkCreateSemaphore(gDevice, &semaphoreInfo, nullptr, &sync.renderFinished) != VK_SUCCESS) {
                std::fprintf(stderr, "Error: Could not create the Vulkan frame semaphores.\n");
                return false;
            }
        }
        return true;
    }

    void destroySwapchain() {
        destroyDepthBuffer();
        for (VkImageView view : gImageViews)
            if (view != VK_NULL_HANDLE) vkDestroyImageView(gDevice, view, nullptr);
        gImageViews.clear();
        destroySyncObjects();
        gImages.clear();
        if (gSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(gDevice, gSwapchain, nullptr);
            gSwapchain = VK_NULL_HANDLE;
        }
        gImageCount = 0;
        gSwapchainValid = false;
    }

    bool createSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                gPhysicalDevice, gSurface, &capabilities) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not read the Vulkan surface capabilities.\n");
            return false;
        }

        const uint32_t width = capabilities.currentExtent.width;
        const uint32_t height = capabilities.currentExtent.height;

        gSurfaceFormat = pickSurfaceFormat();

        uint32_t minImageCount = capabilities.minImageCount;
        if (minImageCount < MIN_SWAPCHAIN_IMAGES) minImageCount = MIN_SWAPCHAIN_IMAGES;
        if (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount)
            minImageCount = capabilities.maxImageCount;

        VkSwapchainKHR oldSwapchain = gSwapchain;
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = gSurface;
        swapchainInfo.minImageCount = minImageCount;
        swapchainInfo.imageFormat = gSurfaceFormat.format;
        swapchainInfo.imageColorSpace = gSurfaceFormat.colorSpace;
        swapchainInfo.imageExtent = capabilities.currentExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = pickPresentMode();
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = oldSwapchain;

        VkSwapchainKHR created = VK_NULL_HANDLE;
        const VkResult result = vkCreateSwapchainKHR(gDevice, &swapchainInfo, nullptr, &created);
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not create the Vulkan swapchain (%d).\n", result);
            return false;
        }

        // The old swapchain goes only after the new one has taken it as its ancestor
        destroySwapchain();
        gSwapchain = created;

        if (vkGetSwapchainImagesKHR(gDevice, gSwapchain, &gImageCount, nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not read the Vulkan swapchain images.\n");
            return false;
        }
        gImages.resize(gImageCount);
        if (vkGetSwapchainImagesKHR(gDevice, gSwapchain, &gImageCount, gImages.data()) != VK_SUCCESS) {
            std::fprintf(stderr, "Error: Could not read the Vulkan swapchain images.\n");
            return false;
        }

        gImageViews.assign(gImageCount, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < gImageCount; i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = gImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = gSurfaceFormat.format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(gDevice, &viewInfo, nullptr, &gImageViews[i]) != VK_SUCCESS) {
                std::fprintf(stderr, "Error: Could not create a swapchain image view.\n");
                return false;
            }
        }

        if (!createDepthBuffer(width, height)) return false;
        if (!createSyncObjects()) return false;

        gWidth = static_cast<int>(width);
        gHeight = static_cast<int>(height);
        gSwapchainValid = true;
        return true;
    }

    bool recreateSwapchain() {
        vkDeviceWaitIdle(gDevice);
        return createSwapchain();
    }

}

bool VulkanContext::create(const VulkanContextConfig& config) {
    gConfig = config;
    if (!gConfig.surfaceExtension || !gConfig.createSurface) {
        std::fprintf(stderr, "Error: The Vulkan backend needs a window surface.\n");
        return false;
    }

    if (!createInstance()) return false;

    const VkResult result = gConfig.createSurface(gInstance, &gSurface);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Error: Could not create the Vulkan surface (%d).\n", result);
        return false;
    }

    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    return createSwapchain();
}

void VulkanContext::destroy() {
    if (gDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(gDevice);
        destroySwapchain();
        vkDestroyDevice(gDevice, nullptr);
        gDevice = VK_NULL_HANDLE;
        gQueue = VK_NULL_HANDLE;
    }
    if (gSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(gInstance, gSurface, nullptr);
        gSurface = VK_NULL_HANDLE;
    }
    if (gInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(gInstance, nullptr);
        gInstance = VK_NULL_HANDLE;
    }
    gPhysicalDevice = VK_NULL_HANDLE;
    gQueueFamily = UINT32_MAX;
    gImageAcquired = false;
    gWidth = 0;
    gHeight = 0;
}

void VulkanContext::beginFrame() {
    if (gDevice == VK_NULL_HANDLE) return;

    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            gPhysicalDevice, gSurface, &capabilities) != VK_SUCCESS)
        return;

    // Minimized: the surface has no extent, and the swapchain it had is still
    // good for when the window comes back
    if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
        return;

    // A resize reaches us as a changed extent. Drivers report OUT_OF_DATE too,
    // but only once the stale swapchain has been presented to.
    if (capabilities.currentExtent.width != static_cast<uint32_t>(gWidth) ||
        capabilities.currentExtent.height != static_cast<uint32_t>(gHeight))
        gSwapchainValid = false;

    if (!gSwapchainValid && !recreateSwapchain()) return;

    const VkResult result = vkAcquireNextImageKHR(
        gDevice, gSwapchain, UINT64_MAX, gSync[gSyncSlot].presentComplete,
        VK_NULL_HANDLE, &gImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        gSwapchainValid = false;
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "Error: Could not acquire a swapchain image (%d).\n", result);
        return;
    }

    gImageAcquired = true;
    gSwapchainRendered = false;
}

void VulkanContext::endFrame() {
    if (!gImageAcquired) return;
    gImageAcquired = false;

    // Waits on the acquired image's semaphore, not the slot's: a swapchain is
    // free to hand back images out of order
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    // A frame that drew nothing (an empty scene stack while a scene is being
    // replaced) never begins the swapchain pass, so sokol submits no signal for
    // renderFinished and waiting on it would block the queue forever. The image
    // was acquired, though, so it still has to be handed back or the swapchain
    // runs out of images to acquire.
    if (gSwapchainRendered) {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &gSync[gImageIndex].renderFinished;
    }
    gSwapchainRendered = false;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &gSwapchain;
    presentInfo.pImageIndices = &gImageIndex;

    const VkResult result = vkQueuePresentKHR(gQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        gSwapchainValid = false;
    } else if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Error: Could not present a swapchain image (%d).\n", result);
        gSwapchainValid = false;
    }

    gSyncSlot = (gSyncSlot + 1) % gImageCount;
}

sg_environment VulkanContext::environment() {
    sg_environment environment{};
    environment.defaults.sample_count = 1;
    // Pipelines are built from these defaults, so the color format has to be the
    // swapchain's or none of them would match the swapchain pass
    environment.defaults.color_format = gSurfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM
        ? SG_PIXELFORMAT_RGBA8
        : SG_PIXELFORMAT_BGRA8;
    environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    environment.vulkan.instance = gInstance;
    environment.vulkan.physical_device = gPhysicalDevice;
    environment.vulkan.device = gDevice;
    environment.vulkan.queue = gQueue;
    environment.vulkan.queue_family_index = gQueueFamily;
    return environment;
}

sg_swapchain VulkanContext::swapchain() {
    sg_swapchain swapchain{};
    swapchain.invalid = !gSwapchainValid || !gImageAcquired;
    swapchain.width = gWidth;
    swapchain.height = gHeight;
    swapchain.sample_count = 1;
    swapchain.color_format = gSurfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM
        ? SG_PIXELFORMAT_RGBA8
        : SG_PIXELFORMAT_BGRA8;
    swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    if (swapchain.invalid) return swapchain;

    swapchain.vulkan.render_image = gImages[gImageIndex];
    swapchain.vulkan.render_view = gImageViews[gImageIndex];
    swapchain.vulkan.depth_stencil_image = gDepthImage;
    swapchain.vulkan.depth_stencil_view = gDepthView;
    swapchain.vulkan.render_finished_semaphore = gSync[gImageIndex].renderFinished;
    swapchain.vulkan.present_complete_semaphore = gSync[gSyncSlot].presentComplete;
    // Handing these semaphores out is what makes sokol wait on the acquire and
    // signal renderFinished, so from here on there is something to present.
    gSwapchainRendered = true;
    return swapchain;
}
