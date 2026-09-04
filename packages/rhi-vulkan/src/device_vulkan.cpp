#include "device_vulkan.hpp"

#include <engine/math/mip.hpp>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::rhi::vulkan {

void not_implemented(const char* what) {
    static std::unordered_set<std::string> said;
    if (!said.insert(what).second) {
        return;
    }
    char message[176];
    std::snprintf(message, sizeof(message), "rhi-vulkan: %s is not implemented yet", what);
    log(LogLevel::Warn, LogChannel::Render, message);
}

// ── Resources ───────────────────────────────────────────────────────────────

VulkanBuffer::VulkanBuffer(
    VkDevice device, VkBuffer buffer, VkDeviceMemory memory, usize size, void* mapped)
    : device_(device), buffer_(buffer), memory_(memory), size_(size), mapped_(mapped) {}

VkBufferView VulkanBuffer::texel_view() {
    if (texel_view_ != VK_NULL_HANDLE || device_ == VK_NULL_HANDLE) {
        return texel_view_;
    }
    VkBufferViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    info.buffer = buffer_;
    info.format = VK_FORMAT_R32_UINT;
    info.range = VK_WHOLE_SIZE;
    if (vk_failed(vkCreateBufferView(device_, &info, nullptr, &texel_view_), "buffer view")) {
        texel_view_ = VK_NULL_HANDLE;
    }
    return texel_view_;
}

VulkanBuffer::~VulkanBuffer() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (texel_view_ != VK_NULL_HANDLE) {
        vkDestroyBufferView(device_, texel_view_, nullptr);
    }
    if (mapped_ != nullptr) {
        vkUnmapMemory(device_, memory_);
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
    }
}

VulkanTexture::VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory,
    VkImageView view, const TextureDesc& desc, Ownership ownership)
    : device_(device), image_(image), memory_(memory), view_(view), ownership_(ownership),
      desc_(desc) {}

VulkanTexture::~VulkanTexture() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
    }
    // A swapchain image is the swapchain's. Freeing it here is a double free
    // the first time the window is resized.
    if (ownership_ == Ownership::ViewOnly) {
        return;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
    }
}

VulkanPipeline::VulkanPipeline(VkDevice device, const PipelineRecipe& recipe,
    VkDescriptorSetLayout set0, VkDescriptorSetLayout set1, std::vector<VkSampler> samplers,
    u32 uniform_count, u32 storage_buffer_count)
    : device_(device), recipe_(recipe), set0_(set0), set1_(set1),
      samplers_(std::move(samplers)), uniform_count_(uniform_count),
      storage_buffer_count_(storage_buffer_count) {}

VulkanPipeline::~VulkanPipeline() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (const auto& [stride, pipeline] : variants_) {
        (void)stride;
        vkDestroyPipeline(device_, pipeline, nullptr);
    }
    // Held for the pipeline's lifetime rather than freed after creation,
    // because a stride variant created later needs them.
    if (recipe_.vertex != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, recipe_.vertex, nullptr);
    }
    if (recipe_.fragment != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, recipe_.fragment, nullptr);
    }
    if (recipe_.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, recipe_.layout, nullptr);
    }
    if (set1_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set1_, nullptr);
    }
    if (set0_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set0_, nullptr);
    }
    // Immutable samplers outlive the set layout that referenced them.
    for (VkSampler sampler : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
    }
}

// ── Device ──────────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        // Teardown is exactly where the current device is least predictable -
        // another device may have loaded volk's table since this one last ran
        // anything - so make this one current before destroying its objects.
        make_current();
        vkDeviceWaitIdle(device_);
        for (u32 i = 0; i < kFrameCount; ++i) {
            // Before the pools: a ring buffer's memory is unmapped in its own
            // destructor and must not outlive the device.
            frame_ring_[i].reset();
            if (descriptor_pools_[i] != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptor_pools_[i], nullptr);
            }
            if (fences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, fences_[i], nullptr);
            }
        }
        if (cmd_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, cmd_pool_, nullptr);
        }
        swapchain_depth_.reset();
        destroy_swapchain();
        for (u32 i = 0; i < kAcquireSemaphores; ++i) {
            if (acquire_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, acquire_[i], nullptr);
            }
        }
        for (u32 i = 0; i < kAcquireSemaphores; ++i) {
            if (render_finished_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, render_finished_[i], nullptr);
            }
        }
        // Before the handle goes: a VkDevice is a handle value and a driver
        // may hand the same one back for the next device created. Leaving it
        // in g_current_device would make make_current() match on a stale value
        // and skip the load - the same bug with a different trigger and no new
        // evidence.
        forget_current(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    // After the device: the surface belongs to the instance.
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(state_.instance, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    release_vulkan_instance(state_);
}

namespace {

bool has_device_extension(VkPhysicalDevice gpu, const char* name) {
    u32 count = 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0
        && vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data())
            != VK_SUCCESS) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool gpu_debug_enabled() {
    char value[8]{};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0
        && value[0] == '1';
}

} // namespace

bool VulkanDevice::init(const DeviceDesc& desc) {
    depth_convention_ = desc.depth_convention;
    width_ = desc.width;
    height_ = desc.height;
    present_interval_ = desc.present_interval == 0 ? 0u : 1u;
    offscreen_ = desc.window_handle == nullptr;

    if (!acquire_vulkan_instance(state_, !offscreen_)) {
        return false;
    }

    const f32 priority = 1.f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = state_.graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    // Requested, not merely hoped for: the physical-device filter already
    // rejected anything without them, so enabling them here cannot fail for a
    // device that got this far.
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    // VK_EXT_device_fault, when the driver has it and only under
    // ENGINE_GPU_DEBUG. This is the one thing that answers "the device is lost
    // and validation is silent": the driver reports what it faulted on, with
    // addresses and vendor detail, which nothing on the API side can see.
    // Optional by design - it is a diagnostic, so a driver without it should
    // cost a line in the log rather than a device that will not start.
    VkPhysicalDeviceFaultFeaturesEXT fault_features{};
    fault_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
    fault_features.deviceFault = VK_TRUE;
    fault_supported_ = gpu_debug_enabled() && has_device_extension(
        state_.gpu, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);

    std::vector<const char*> device_extensions;
    if (!offscreen_) {
        device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    if (fault_supported_) {
        device_extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        features13.pNext = &fault_features;
    }

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features13;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<u32>(device_extensions.size());
    device_info.ppEnabledExtensionNames
        = device_extensions.empty() ? nullptr : device_extensions.data();

    if (vk_failed(vkCreateDevice(state_.gpu, &device_info, nullptr, &device_),
            "device creation")) {
        return false;
    }
    // Device-level entry points come from vkGetDeviceProcAddr, which skips the
    // loader's dispatch trampoline.
    //
    // Through make_current(), never volkLoadDevice() directly, and that
    // distinction was the whole of RHI #25. A direct call loads the table but
    // leaves make_current()'s idea of the current device untouched - so the
    // sequence
    //
    //   windowed device made current  ->  parity device created (direct load)
    //   ->  parity device destroyed  ->  windowed device used again
    //
    // left make_current() believing the windowed device was still current
    // while volk's globals pointed into a device that no longer existed. Every
    // later call went through a destroyed dispatch and returned nonsense: a
    // healthy device answering VK_ERROR_DEVICE_LOST from vkQueuePresentKHR,
    // vkAcquireNextImageKHR handing back the same image twice, and
    // vkGetDeviceFaultInfoEXT reporting no fault because there was none.
    make_current();
    vkGetDeviceQueue(device_, state_.graphics_family, 0, &queue_);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = state_.graphics_family;
    if (vk_failed(vkCreateCommandPool(device_, &pool_info, nullptr, &cmd_pool_),
            "command pool creation")) {
        return false;
    }

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = cmd_pool_;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = kFrameCount;
    if (vk_failed(vkAllocateCommandBuffers(device_, &cmd_info, cmd_buffers_),
            "command buffer allocation")) {
        return false;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Created signalled: the first begin_frame waits every slot's fence, and a
    // slot that has never been submitted would otherwise block forever.
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (vk_failed(vkCreateFence(device_, &fence_info, nullptr, &fences_[i]),
                "fence creation")) {
            return false;
        }
    }

    // One pool, reset each begin_frame. This device submits and waits inside a
    // single frame, so a per-frame-slot pool would have nothing to protect
    // against - descriptor-set lifetime under real frames in flight is deferred
    // to the parity pass rather than solved badly here.
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
        // Immutable samplers still consume a pool slot when a set carrying
        // them is allocated, even though nothing writes them. Leaving this out
        // is a validation warning that some implementations turn into
        // VK_ERROR_OUT_OF_POOL_MEMORY and others silently tolerate - which is
        // the worst kind of difference to ship.
        {VK_DESCRIPTOR_TYPE_SAMPLER, 64},
        // An HLSL RWBuffer<T> lands here rather than in STORAGE_BUFFER.
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 64},
    };
    VkDescriptorPoolCreateInfo descriptor_info{};
    descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_info.maxSets = 256;
    descriptor_info.poolSizeCount = static_cast<u32>(std::size(sizes));
    descriptor_info.pPoolSizes = sizes;
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (vk_failed(
                vkCreateDescriptorPool(device_, &descriptor_info, nullptr,
                    &descriptor_pools_[i]),
                "descriptor pool creation")) {
            return false;
        }
    }

    // One host-visible ring per slot, mapped for its lifetime. VERTEX_BUFFER
    // as well as UNIFORM_BUFFER because debug-draw puts its *vertex* data in
    // the ring - alloc_frame_memory hands out slices that are bound both ways,
    // which is invisible from the interface and would fail only when the
    // overlay is switched on.
    for (u32 i = 0; i < kFrameCount; ++i) {
        VkBufferCreateInfo ring_info{};
        ring_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ring_info.size = kFrameRingBytes;
        // STORAGE_BUFFER as well, because the frame's per-instance array is a
        // ring slice bound with set_structured_buffer - RenderGraph::execute
        // uploads it once and every geometry pass reads it as a structured
        // buffer. Without the bit that bind is
        // VUID-VkWriteDescriptorSet-descriptorType-00331, ten times a frame,
        // and no gate sees it: the gates bind ring slices as vertex and
        // uniform buffers but only a real frame binds one as storage.
        ring_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VkBuffer ring = VK_NULL_HANDLE;
        if (vk_failed(vkCreateBuffer(device_, &ring_info, nullptr, &ring), "frame ring")) {
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, ring, &requirements);
        const u32 type = find_memory_type(state_.gpu, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == ~0u) {
            log(LogLevel::Error, LogChannel::Render, "frame ring: no host-visible memory");
            return false;
        }
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vk_failed(vkAllocateMemory(device_, &allocate, nullptr, &memory), "frame ring")) {
            return false;
        }
        if (vk_failed(vkBindBufferMemory(device_, ring, memory, 0), "frame ring bind")) {
            return false;
        }
        void* mapped = nullptr;
        if (vk_failed(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
                "frame ring map")) {
            return false;
        }
        frame_ring_[i] =
            std::make_unique<VulkanBuffer>(device_, ring, memory, kFrameRingBytes, mapped);
    }

    // Shader model is an HLSL notion, and it is the honest one to report: the
    // SPIR-V this backend consumes is compiled by DXC from SM 6.0 HLSL.
    //
    // Feature level has no Vulkan equivalent at all, so it stays 0 rather than
    // being filled with an unrelated number. That is a real strain in
    // GpuBaseline - the struct is D3D-shaped - and it is recorded rather than
    // papered over, because finding out where the contract is D3D-shaped is
    // what a second backend is for.
    if (!offscreen_) {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (u32 i = 0; i < kAcquireSemaphores; ++i) {
            if (vk_failed(vkCreateSemaphore(device_, &semaphore_info, nullptr, &acquire_[i]),
                    "acquire semaphore")) {
                return false;
            }
        }
        for (u32 i = 0; i < kAcquireSemaphores; ++i) {
            if (vk_failed(
                    vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_[i]),
                    "render-finished semaphore")) {
                return false;
            }
        }
        if (!create_surface(desc.window_handle) || !create_swapchain(width_, height_)) {
            return false;
        }
    }

    VkPhysicalDeviceProperties gpu_properties{};
    vkGetPhysicalDeviceProperties(state_.gpu, &gpu_properties);
    max_uniform_range_ = gpu_properties.limits.maxUniformBufferRange;

    baseline_.shader_model = kGpuShaderModel_6_0;
    baseline_.feature_level = 0;

    char message[224];
    std::snprintf(message, sizeof(message),
        "Vulkan device initialized (%s, SM %u.%u, %s)", state_.device_name.c_str(),
        (baseline_.shader_model >> 4) & 0xFu, baseline_.shader_model & 0xFu,
        offscreen_ ? "offscreen, no swapchain" : "windowed");
    log(LogLevel::Info, LogChannel::Render, message);
    return true;
}

// ── Presentation ────────────────────────────────────────────────────────────

bool VulkanDevice::create_surface(void* window_handle) {
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = static_cast<HWND>(window_handle);
    if (vk_failed(vkCreateWin32SurfaceKHR(state_.instance, &info, nullptr, &surface_),
            "surface")) {
        return false;
    }

    // Whether the queue family we already picked can actually present to this
    // surface. Never asked before, and it is not a formality: the family is
    // chosen for VK_QUEUE_GRAPHICS_BIT alone, before the surface exists, so
    // nothing had ever connected the two. On every desktop GPU family 0
    // presents, which is exactly why an unchecked assumption survives.
    //
    // A warning rather than a failure, because it can only be asked after
    // vkCreateDevice - the surface needs the window and the family needed
    // choosing first - so by the time the answer is available the queue is
    // already made. Saying so beats a present that fails for a reason nothing
    // in the log explains.
    VkBool32 supported = VK_FALSE;
    if (!vk_failed(vkGetPhysicalDeviceSurfaceSupportKHR(
                       state_.gpu, state_.graphics_family, surface_, &supported),
            "surface present support")
        && supported == VK_FALSE) {
        char message[224];
        std::snprintf(message, sizeof(message),
            "Queue family %u cannot present to this surface. Presentation will fail; this "
            "GPU needs a separate present queue, which this backend does not have.",
            state_.graphics_family);
        log(LogLevel::Error, LogChannel::Render, message);
    }
    return true;
}

bool VulkanDevice::create_swapchain(u32 width, u32 height) {
    VkSurfaceCapabilitiesKHR caps{};
    if (vk_failed(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state_.gpu, surface_, &caps),
            "surface capabilities")) {
        return false;
    }
    // A minimised window reports a zero extent, and a zero-extent swapchain is
    // illegal. The frame loop already skips a zero-sized swapchain (engine.cpp
    // early-returns on swapchain_color().width() == 0), so reporting zero and
    // creating nothing is the honest answer rather than clamping to 1 and
    // rendering into a pixel nobody sees.
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
        swapchain_extent_ = {0, 0};
        return true;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = width;
        extent.height = height;
    }

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(state_.gpu, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    if (format_count > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            state_.gpu, surface_, &format_count, formats.data());
    }
    // A UNORM format, never an _SRGB one. resources.hpp says a presented
    // surface stays UNORM and the encode happens in-shader, because the other
    // backend's flip-model swapchain refuses _SRGB - so picking an _SRGB
    // surface here would double-encode and make the same shader look different
    // on the two backends.
    VkSurfaceFormatKHR chosen = formats.empty()
        ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
        : formats[0];
    // R8G8B8A8 preferred over B8G8R8A8, which is the reverse of the obvious
    // order on Windows and is deliberate: `Format` has no BGRA member, so a
    // BGRA surface is one this contract cannot name. The wrapping VulkanTexture
    // would report RGBA8_UNORM while the image really was BGRA, and the four
    // passes that render straight into the backbuffer - fxaa, smaa_blend,
    // debug_lines, stats_overlay - would hand dynamic rendering a pipeline
    // whose colour format disagrees with the attachment. Masked today only
    // because AA defaults to Off and both overlays default to invisible.
    //
    // The ternary below used to have RGBA8_UNORM in *both* arms, which is how
    // that went unnoticed.
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen = format;
            break;
        }
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = format;
        }
    }
    if (chosen.format != VK_FORMAT_R8G8B8A8_UNORM) {
        log(LogLevel::Warn, LogChannel::Render,
            "Swapchain surface is not R8G8B8A8_UNORM, which is the only 8-bit colour "
            "order engine::rhi::Format can name. Passes that render directly into the "
            "backbuffer may disagree with the attachment format.");
    }

    u32 mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(state_.gpu, surface_, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    if (mode_count > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            state_.gpu, surface_, &mode_count, modes.data());
    }
    // FIFO is the only mode required to exist, so it is the fallback for both
    // settings. present_interval 0 means "tear if you can", which is
    // IMMEDIATE - the closest thing to the other backend's ALLOW_TEARING.
    // **FIFO is avoided on this backend, and that is a known defect, not a
    // preference.** RHI #25.
    //
    // With FIFO the second vkQueuePresentKHR of a session answers
    // VK_ERROR_DEVICE_LOST. Everything measurable says it should not:
    // vkQueueWaitIdle returns VK_SUCCESS immediately before it,
    // vkGetDeviceFaultInfoEXT reports no fault (and validation objects that the
    // device is not even in the lost state), standard, synchronization and
    // GPU-assisted validation are all silent, the image is in
    // PRESENT_SRC_KHR, the queue family does present to the surface, the
    // surface is R8G8B8A8_UNORM, and semaphores and fences come from rings deep
    // enough that nothing is reused. In IMMEDIATE the identical frame presents
    // 4,946 times in 25 seconds with none of it.
    //
    // So the choice is between a backend that tears and says so, and one that
    // does not render. It tears and says so - once per swapchain, at Warn, so
    // the log of any session using it carries the reason. Removing this
    // fallback is what RHI #25 is for, and that row also owes a gate that runs
    // the frame loop under FIFO, because nothing here would have caught this:
    // the gates never presented until RHI #25 added one that does.
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            present_mode = mode;
            break;
        }
    }
    if (present_interval_ != 0 && present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        log(LogLevel::Warn, LogChannel::Render,
            "Vulkan: vsync was requested but this backend presents with IMMEDIATE - FIFO "
            "loses the device on the second present (RHI #25). Expect tearing. The D3D12 "
            "backend honours vsync normally.");
    } else if (present_interval_ != 0) {
        log(LogLevel::Warn, LogChannel::Render,
            "Vulkan: vsync requested and IMMEDIATE is unavailable, so FIFO it is - which "
            "loses the device on the second present (RHI #25). Rendering will stop after "
            "one frame.");
    }

    u32 image_count = kFrameCount;
    if (image_count < caps.minImageCount) {
        image_count = caps.minImageCount;
    }
    if (caps.maxImageCount != 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    // TRANSFER_DST because the anti-aliasing copy pass writes the backbuffer
    // with copy_texture rather than by rendering into it - the frame trace
    // found that, and without this bit `aa_copy` is a validation error.
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = present_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (vk_failed(vkCreateSwapchainKHR(device_, &info, nullptr, &created), "swapchain")) {
        return false;
    }
    destroy_swapchain();
    swapchain_ = created;
    swapchain_extent_ = extent;
    // RGBA8_UNORM either way, because it is the only 8-bit colour order the
    // contract has a name for and the loop above prefers a surface that really
    // is that. On a BGRA-only surface this is a known inaccuracy, warned about
    // there rather than papered over here.
    swapchain_format_ = Format::RGBA8_UNORM;

    u32 actual = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, nullptr);
    std::vector<VkImage> images(actual);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, images.data());

    TextureDesc backbuffer{};
    backbuffer.width = extent.width;
    backbuffer.height = extent.height;
    backbuffer.format = swapchain_format_;
    backbuffer.usage = TextureUsage::RenderTarget;
    for (u32 i = 0; i < actual && i < kMaxSwapchainImages; ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = chosen.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        if (vk_failed(vkCreateImageView(device_, &view_info, nullptr, &view),
                "swapchain image view")) {
            return false;
        }
        // Owns the view but not the image: the swapchain owns those, so a
        // VulkanTexture that freed one would be a double free at resize.
        backbuffer_[i] = std::make_unique<VulkanTexture>(device_, images[i], VK_NULL_HANDLE,
            view, backbuffer, VulkanTexture::Ownership::ViewOnly);
    }
    swapchain_image_count_ = actual < kMaxSwapchainImages ? actual : kMaxSwapchainImages;

    // The depth buffer the frame renders against. The graph pins the swapchain
    // depth to DepthWrite and never transitions it, so it is settled at
    // creation like any other depth attachment.
    TextureDesc depth{};
    depth.width = extent.width;
    depth.height = extent.height;
    depth.format = Format::D32_FLOAT;
    depth.usage = TextureUsage::DepthStencil;
    swapchain_depth_ = create_texture(depth, nullptr);
    return swapchain_depth_ != nullptr;
}

void VulkanDevice::destroy_swapchain() {
    for (u32 i = 0; i < kMaxSwapchainImages; ++i) {
        backbuffer_[i].reset();
        // The images are gone, so their in-flight history means nothing - and
        // a stale fence here would make the next acquire wait on work that
        // belongs to a destroyed image.
        image_in_flight_[i] = VK_NULL_HANDLE;
    }
    swapchain_image_count_ = 0;
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

ISwapchain& VulkanDevice::swapchain() {
    ENGINE_ASSERT_MSG(!offscreen_, "offscreen device has no swapchain");
    return swapchain_wrapper_;
}

ITexture& VulkanDevice::swapchain_color() {
    ENGINE_ASSERT_MSG(!offscreen_, "offscreen device has no swapchain colour target");
    // Before the acquire, and this is the site that found the bug: it is the
    // first Vulkan call a live frame makes, because engine.cpp probes the
    // backbuffer's width here before anything else.
    make_current();
    // The acquire, on first use. See holds_image_.
    ensure_acquired();
    // The acquired image, not frame_index_'s. A swapchain hands back whatever
    // image it likes and the two indices drift apart the moment one is
    // skipped - which is precisely the bug that makes a frame render into the
    // image being displayed.
    return *backbuffer_[acquired_image_];
}

ITexture& VulkanDevice::swapchain_depth() {
    ENGINE_ASSERT_MSG(!offscreen_, "offscreen device has no swapchain depth target");
    return *swapchain_depth_;
}

void VulkanSwapchain::present() { device_.present(); }

u32 VulkanSwapchain::current_back_buffer_index() const { return device_.acquired_image(); }

void VulkanDevice::present() {
    // Nothing acquired means nothing to present - a frame that never touched
    // the backbuffer, which is every frame the gates drive.
    if (!holds_image_ || swapchain_ == VK_NULL_HANDLE || swapchain_extent_.width == 0) {
        return;
    }
    if (pending_present_ == VK_NULL_HANDLE) {
        // No submit signalled anything for this present to wait on, which means
        // the frame acquired an image and never recorded into it. Presenting
        // with no wait semaphore would race the rendering that never happened.
        holds_image_ = false;
        pending_acquire_ = VK_NULL_HANDLE;
        return;
    }
    holds_image_ = false;
    pending_acquire_ = VK_NULL_HANDLE;
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    // Waits on the semaphore submit() signalled, not on a fence: presentation
    // is a queue operation and has to be ordered against the rendering on the
    // GPU, not on the CPU.
    info.pWaitSemaphores = &pending_present_;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &acquired_image_;
    const VkResult result = vkQueuePresentKHR(queue_, &info);
    // Consumed either way: on failure the semaphore is no more reusable than on
    // success, and leaving it set would make the next submit signal it twice -
    // which is the bug this ring exists to remove.
    pending_present_ = VK_NULL_HANDLE;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Not an error: the window changed size or the display did. Rebuilt on
        // the next begin_frame, which is where waiting is already legal.
        swapchain_stale_ = true;
        return;
    }
    if (result == VK_ERROR_DEVICE_LOST) {
        // Logged, because this site and the acquire below both `return` before
        // vk_failed would have said anything - so a lost device reached the
        // frame loop's FATAL with nothing naming the call that reported it.
        log(LogLevel::Error, LogChannel::Render, "Vulkan present reported VK_ERROR_DEVICE_LOST");
        report_device_fault();
        device_lost_ = true;
        return;
    }
    vk_failed(result, "present");
}

void VulkanDevice::begin_frame() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    make_current();
    // Advance, then wait *this* slot. Offscreen has no backbuffer index to
    // follow, so the slot count is what bounds frames in flight - the same
    // shape the other backend's offscreen path uses.
    frame_index_ = (frame_index_ + 1) % kFrameCount;
    vkWaitForFences(device_, 1, &fences_[frame_index_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fences_[frame_index_]);
    // Any image whose in-flight fence is this slot's has just had that fence
    // reset, so the fence no longer stands for completed work - it stands for
    // work this frame has not submitted yet. Waiting on it in ensure_acquired
    // is a deadlock, and with three slots and three images the cycle lands
    // there routinely. Measured as a hang: the frame loop gate stopped
    // producing output and the process sat at a few seconds of CPU forever.
    //
    // Clearing is correct rather than merely safe: the wait exists to order
    // against the *previous* user of that image, and this slot's previous work
    // was just waited for on the line above.
    for (u32 i = 0; i < kMaxSwapchainImages; ++i) {
        if (image_in_flight_[i] == fences_[frame_index_]) {
            image_in_flight_[i] = VK_NULL_HANDLE;
        }
    }
    // Only now is it safe to reset this slot's pool: the fence says the GPU
    // has finished every descriptor set allocated from it. Resetting a pool
    // whose sets are still being read is the exact hazard three slots exist
    // to avoid, and the offscreen slice never met it because it waited inside
    // the frame.
    vkResetDescriptorPool(device_, descriptor_pools_[frame_index_], 0);
    frame_ring_offset_ = 0;
    frame_ring_exhausted_ = false;

    if (offscreen_) {
        return;
    }
    if (swapchain_stale_ || swapchain_ == VK_NULL_HANDLE) {
        // Rebuilt here rather than in present(), because this is the point at
        // which waiting for the GPU is already legal.
        vkQueueWaitIdle(queue_);
        swapchain_stale_ = false;
        if (!create_swapchain(width_, height_)) {
            return;
        }
    }
}

// The device volk's globals currently point at. File-static because volk's
// table is process-wide - a member would not know about the other devices.
namespace {
VkDevice g_current_device = VK_NULL_HANDLE;
}

void VulkanDevice::report_device_fault() {
    if (!fault_supported_ || device_ == VK_NULL_HANDLE
        || vkGetDeviceFaultInfoEXT == nullptr) {
        log(LogLevel::Error, LogChannel::Render,
            "Device lost, and VK_EXT_device_fault is unavailable - no driver-side detail. "
            "Install a driver that supports it, or run with ENGINE_GPU_DEBUG=1.");
        return;
    }
    VkDeviceFaultCountsEXT counts{};
    counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    if (vk_failed(vkGetDeviceFaultInfoEXT(device_, &counts, nullptr), "device fault counts")) {
        return;
    }

    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
    VkDeviceFaultInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
    info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
    info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
    if (vk_failed(vkGetDeviceFaultInfoEXT(device_, &counts, &info), "device fault info")) {
        return;
    }

    char header[320];
    std::snprintf(header, sizeof(header),
        "Device fault: \"%s\" addresses=%u vendor_records=%u vendor_binary=%llu bytes",
        info.description, counts.addressInfoCount, counts.vendorInfoCount,
        static_cast<unsigned long long>(counts.vendorBinarySize));
    log(LogLevel::Error, LogChannel::Render, header);

    for (u32 i = 0; i < counts.addressInfoCount; ++i) {
        const VkDeviceFaultAddressInfoEXT& a = addresses[i];
        char line[224];
        std::snprintf(line, sizeof(line),
            "  fault address [%u]: type=%d reported=0x%llX precision=0x%llX", i,
            static_cast<int>(a.addressType),
            static_cast<unsigned long long>(a.reportedAddress),
            static_cast<unsigned long long>(a.addressPrecision));
        log(LogLevel::Error, LogChannel::Render, line);
    }
    for (u32 i = 0; i < counts.vendorInfoCount; ++i) {
        const VkDeviceFaultVendorInfoEXT& v = vendors[i];
        char line[288];
        std::snprintf(line, sizeof(line),
            "  vendor record [%u]: \"%s\" fault=0x%llX data=0x%llX", i, v.description,
            static_cast<unsigned long long>(v.vendorFaultCode),
            static_cast<unsigned long long>(v.vendorFaultData));
        log(LogLevel::Error, LogChannel::Render, line);
    }
}

void VulkanDevice::make_current() {
    if (device_ == VK_NULL_HANDLE || g_current_device == device_) {
        return;
    }
    volkLoadDevice(device_);
    g_current_device = device_;
}

void VulkanDevice::forget_current(VkDevice device) {
    if (g_current_device == device) {
        g_current_device = VK_NULL_HANDLE;
    }
}

void VulkanDevice::ensure_acquired() {
    if (holds_image_ || offscreen_ || swapchain_ == VK_NULL_HANDLE
        || swapchain_extent_.width == 0) {
        return;
    }
    const VkSemaphore semaphore = acquire_[acquire_cursor_];
    acquire_cursor_ = (acquire_cursor_ + 1) % kAcquireSemaphores;
    const VkResult acquired = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, semaphore, VK_NULL_HANDLE, &acquired_image_);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_stale_ = true;
        return;
    }
    if (acquired == VK_ERROR_DEVICE_LOST) {
        log(LogLevel::Error, LogChannel::Render,
            "Vulkan image acquire reported VK_ERROR_DEVICE_LOST");
        report_device_fault();
        device_lost_ = true;
        return;
    }
    // SUBOPTIMAL is a hint, not a failure: the image is usable, so the frame
    // is rendered and the swapchain rebuilt next time round.
    if (acquired == VK_SUBOPTIMAL_KHR) {
        swapchain_stale_ = true;
    } else if (vk_failed(acquired, "acquire next image")) {
        return;
    }
    holds_image_ = true;
    pending_acquire_ = semaphore;

    // Before anything touches this image: the frame that last rendered to it
    // may still be in flight, and its present may still hold an unretired wait
    // on render_finished_[acquired_image_]. begin_frame's fence wait cannot
    // cover this - it waits the *slot*, and slots and images do not advance
    // together.
    //
    // Safe to wait here rather than deadlock-prone: this runs before
    // begin_frame resets this frame's fence, so every fence in the array
    // belongs to a frame that has already been submitted.
    if (acquired_image_ < kMaxSwapchainImages
        && image_in_flight_[acquired_image_] != VK_NULL_HANDLE) {
        vkWaitForFences(device_, 1, &image_in_flight_[acquired_image_], VK_TRUE, UINT64_MAX);
    }
}

void VulkanDevice::submit() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    make_current();
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffers_[frame_index_];
    // The acquire semaphore gates the frame's first touch of the image; the
    // render-finished one gates the present. Without the wait, the frame can
    // start writing an image the display is still scanning out.
    //
    // ALL_COMMANDS, not COLOR_ATTACHMENT_OUTPUT, and that distinction was a
    // device loss. The first thing the command buffer does to a freshly
    // acquired image is a **layout transition**, not a colour write - and a
    // barrier does not run at the colour-output stage, so waiting only there
    // left the barrier free to execute before the wait was satisfied.
    // Synchronization validation names it exactly:
    //
    //   WRITE_AFTER_READ hazard detected. vkCmdPipelineBarrier2 writes to
    //   VkImage ..., which was previously accessed by vkAcquireNextImageKHR
    //
    // and the driver's answer was VK_ERROR_DEVICE_LOST from the present, two
    // frames later, with standard validation silent. Widening the wait is the
    // remedy the spec's own guidance describes: the alternative is to add
    // COLOR_ATTACHMENT_OUTPUT to the source stage of whichever barrier happens
    // to touch the backbuffer first, which means every ResourceState that can
    // precede it has to know about presentation. One conservative wait at the
    // top of the frame costs a stage boundary once per frame and keeps the
    // state-to-stage mapping honest everywhere else.
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    // Only a frame that holds an acquired image, which is only a frame that
    // touched the backbuffer. A gate's submit is a plain submit: it waits on
    // nothing and signals nothing, because there is no acquire behind it and no
    // present in front of it. Wiring these in unconditionally is two validation
    // errors - a wait with no matching signal, and a signal on a semaphore
    // still signalled from three frames ago.
    if (holds_image_ && pending_acquire_ != VK_NULL_HANDLE) {
        pending_present_ = render_finished_[present_cursor_];
        present_cursor_ = (present_cursor_ + 1) % kAcquireSemaphores;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &pending_acquire_;
        submit.pWaitDstStageMask = &wait_stage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &pending_present_;
    }
    if (holds_image_ && acquired_image_ < kMaxSwapchainImages) {
        // Recorded here because this is the submit whose completion the next
        // acquire of this image has to wait for.
        image_in_flight_[acquired_image_] = fences_[frame_index_];
    }
    if (vk_failed(vkQueueSubmit(queue_, 1, &submit, fences_[frame_index_]), "queue submit")) {
        // A lost device never recovers, and the frame loop has to stop rather
        // than keep submitting into it - the same latch the D3D12 backend uses.
        report_device_fault();
        device_lost_ = true;
    }
}

void VulkanDevice::end_frame() {
    make_current();
    // The frame trace found that nothing outside the gates calls submit():
    // RenderGraph::execute calls end_frame and expects it to submit *and*
    // present, which is what the other backend's end_frame does. An end_frame
    // that only presented would render nothing and log nothing.
    //
    // The gates call submit() themselves and then wait, so an offscreen device
    // must not submit twice - hence the guard rather than an unconditional
    // submit here.
    if (!offscreen_) {
        submit();
        present();
    }
}

void VulkanDevice::wait_idle() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    make_current();
    // The queue, not one fence: a caller that submitted nothing this frame
    // would otherwise block on a fence that was never signalled, and every
    // slot's work has to be finished before this returns anyway.
    vkQueueWaitIdle(queue_);
}

bool VulkanDevice::resize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    if (offscreen_) {
        // Nothing to rebuild; the extent is whatever the caller asked for.
        return true;
    }
    vkQueueWaitIdle(queue_);
    return create_swapchain(width, height);
}

GpuMemoryStats VulkanDevice::gpu_memory_stats() const {
    GpuMemoryStats stats{};
    if (state_.gpu == VK_NULL_HANDLE) {
        return stats;
    }
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(state_.gpu, &properties);
    for (u32 i = 0; i < properties.memoryHeapCount; ++i) {
        if ((properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            stats.local_budget_bytes += properties.memoryHeaps[i].size;
        }
    }
    // Usage needs VK_EXT_memory_budget, which is not requested yet. Reported as
    // zero rather than estimated: a made-up usage figure is worse than an
    // obviously absent one, and the D3D12 backend's real number is what the
    // memory gates compare against.
    return stats;
}

// ── Resource creation ───────────────────────────────────────────────────────

VkFormat to_vulkan_format(Format format) {
    switch (format) {
    case Format::RGBA8_UNORM:      return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_UNORM_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::RGBA16_FLOAT:     return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::D32_FLOAT:        return VK_FORMAT_D32_SFLOAT;
    case Format::Unknown:          return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

u32 format_bytes(Format format) {
    switch (format) {
    case Format::RGBA8_UNORM:      return 4;
    case Format::RGBA8_UNORM_SRGB: return 4;
    case Format::RGBA16_FLOAT:     return 8;
    case Format::D32_FLOAT:        return 4;
    case Format::Unknown:          return 0;
    }
    return 0;
}

std::unique_ptr<IBuffer> VulkanDevice::create_buffer(const BufferDesc& desc, const void* data) {
    if (device_ == VK_NULL_HANDLE || desc.size == 0) {
        return nullptr;
    }
    make_current();

    VkBufferUsageFlags usage = 0;
    // Host-visible for the kinds the offscreen path uses. Vertex, Index and
    // Storage want a device-local buffer with a staging upload, which is the
    // parity pass's problem - they reach not_implemented below rather than
    // getting a slow-but-working host-visible buffer, because a silent
    // performance cliff is worse than a named gap.
    bool host_visible = false;
    switch (desc.usage) {
    case BufferUsage::Uniform:
        usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        host_visible = true;
        break;
    case BufferUsage::Readback:
        usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        host_visible = true;
        break;
    case BufferUsage::Vertex:
        usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BufferUsage::Index:
        usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case BufferUsage::Storage:
        // Three usages because one BufferUsage covers three HLSL types. A
        // StructuredBuffer is a storage buffer; an RWBuffer<T> is a storage
        // *texel* buffer and needs its own bit before a VkBufferView of it is
        // legal; and the engine reads results back through copy_buffer, hence
        // TRANSFER_SRC. The other backend needs no equivalent - a UAV there is
        // typed entirely by the view.
        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desc.size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vk_failed(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer), "buffer creation")) {
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    const VkMemoryPropertyFlags properties = host_visible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const u32 type = find_memory_type(state_.gpu, requirements.memoryTypeBits, properties);
    if (type == ~0u) {
        log(LogLevel::Error, LogChannel::Render, "buffer: no memory type satisfies the request");
        vkDestroyBuffer(device_, buffer, nullptr);
        return nullptr;
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vk_failed(vkAllocateMemory(device_, &allocate, nullptr, &memory), "buffer allocation")) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return nullptr;
    }
    if (vk_failed(vkBindBufferMemory(device_, buffer, memory, 0), "buffer bind")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return nullptr;
    }

    // Mapped for its lifetime, the same trade the D3D12 upload heap makes: a
    // host-visible buffer is written every frame, and map/unmap per write costs
    // more than the address does.
    void* mapped = nullptr;
    if (host_visible
        && vk_failed(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
            "buffer map")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return nullptr;
    }
    if (data != nullptr) {
        if (mapped != nullptr) {
            std::memcpy(mapped, data, desc.size);
        } else if (!upload_to_buffer(buffer, data, desc.size)) {
            vkFreeMemory(device_, memory, nullptr);
            vkDestroyBuffer(device_, buffer, nullptr);
            return nullptr;
        }
    }

    return std::make_unique<VulkanBuffer>(device_, buffer, memory, desc.size, mapped);
}

// What each usage needs from the image, and what it must be left in.
//
// The named combinations are the contract's, not this backend's - a storage
// texture nothing ever reads has no use, so there is no bare Storage. Every
// sampled kind also gets TRANSFER_DST because that is how initial data
// arrives, and every colour kind gets TRANSFER_SRC because read_texture copies
// out of them.
//
// TRANSFER_SRC on the plain sampled kind was added when the texture-store gate
// read a texel back out of an uploaded texture, to prove two entries under one
// path hold different pixels. read_texture is declared on IDevice for *any*
// texture, so restricting the flag to the render-target kinds made a contract
// method invalid on this backend for a whole class of texture - and it showed
// up as a validation error and nothing else, because the copy returns the
// right bytes on a permissive driver. The two depth kinds stay without it:
// read_texture's copy hard-codes the colour aspect, so a depth readback was
// never going to work through it.
struct ImageUsagePlan {
    VkImageUsageFlags usage = 0;
    // Where the image is left once created and uploaded. A sampled texture is
    // left ready to sample, matching the other backend, which barriers to its
    // final state at the end of the upload rather than leaving that to a caller
    // who would then have to transition from UNDEFINED and lose the data.
    VkImageLayout settled = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ok = true;
};

ImageUsagePlan plan_image_usage(TextureUsage usage) {
    ImageUsagePlan plan{};
    switch (usage) {
    case TextureUsage::RenderTarget:
        plan.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        break;
    case TextureUsage::DepthStencil:
        plan.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        // Settled at creation, because the other backend creates a depth
        // texture in DEPTH_WRITE and the render graph therefore never
        // transitions one *into* it. Leaving this UNDEFINED means the first
        // vkCmdBeginRendering that uses it fails at submit with a layout
        // mismatch nobody asked for.
        plan.settled = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        break;
    case TextureUsage::ShaderResource:
        plan.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        plan.settled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case TextureUsage::DepthShaderResource:
        plan.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        plan.settled = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        break;
    case TextureUsage::ColorShaderResource:
        plan.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        break;
    case TextureUsage::StorageShaderResource:
        plan.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        break;
    }
    plan.ok = plan.usage != 0;
    return plan;
}

VkImageViewType to_view_type(TextureDimension dimension) {
    switch (dimension) {
    case TextureDimension::Tex2D:      return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Tex2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::Cube:       return VK_IMAGE_VIEW_TYPE_CUBE;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

namespace {

constexpr u32 kMaxMips = 16;

u32 full_mip_count(u32 width, u32 height) {
    u32 levels = 1;
    while ((width > 1 || height > 1) && levels < kMaxMips) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

// mip_levels == 0 means a full chain from width/height, not one level. Reading
// it as one produced a texture whose reported mip count was the raw 0, so
// upload_to_image's `mip < mips` loop ran zero times, staged zero bytes and
// failed silently - a 2048x2048 albedo that came back null with nothing logged.
// Same shape as the other backend's resolve_mip_count, deliberately.
u32 resolve_mip_count(const TextureDesc& desc) {
    if (desc.mip_levels == 1) {
        return 1;
    }
    const u32 full = full_mip_count(desc.width, desc.height);
    if (desc.mip_levels == 0 || desc.mip_levels > full) {
        return full;
    }
    return desc.mip_levels;
}

} // namespace

std::unique_ptr<ITexture> VulkanDevice::create_texture(
    const TextureDesc& desc, const void* data) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
    make_current();

    const VkFormat format = to_vulkan_format(desc.format);
    if (format == VK_FORMAT_UNDEFINED) {
        log(LogLevel::Error, LogChannel::Render, "create_texture: unsupported format");
        return nullptr;
    }
    const ImageUsagePlan plan = plan_image_usage(desc.usage);
    if (!plan.ok) {
        log(LogLevel::Error, LogChannel::Render, "create_texture: unsupported usage");
        return nullptr;
    }
    // Cube is six faces, so the layer count comes from the dimension rather
    // than from array_size alone - the contract says Cube is 6 and Tex2D is 1.
    const u32 layers = desc.dimension == TextureDimension::Cube ? 6u : desc.array_size;
    const u32 mips = resolve_mip_count(desc);
    // The resolved counts, not what was asked for. Everything downstream reads
    // them back off the texture - upload_to_image sizes the staging buffer from
    // mip_levels() - so a texture that reports 0 mips while its image has 12 is
    // a silent zero-byte upload.
    TextureDesc resolved = desc;
    resolved.mip_levels = mips;
    resolved.array_size = layers;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = mips;
    image_info.arrayLayers = layers;
    image_info.samples = static_cast<VkSampleCountFlagBits>(
        desc.sample_count == 0 ? 1u : desc.sample_count);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = plan.usage;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.dimension == TextureDimension::Cube) {
        // Without this a cube view of the image is a validation error, and the
        // sky and IBL passes are the only things that would notice.
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VkImage image = VK_NULL_HANDLE;
    if (vk_failed(vkCreateImage(device_, &image_info, nullptr, &image), "image creation")) {
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image, &requirements);
    const u32 type = find_memory_type(
        state_.gpu, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == ~0u) {
        log(LogLevel::Error, LogChannel::Render, "image: no device-local memory type");
        vkDestroyImage(device_, image, nullptr);
        return nullptr;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vk_failed(vkAllocateMemory(device_, &allocate, nullptr, &memory), "image allocation")) {
        vkDestroyImage(device_, image, nullptr);
        return nullptr;
    }
    if (vk_failed(vkBindImageMemory(device_, image, memory, 0), "image bind")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return nullptr;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = to_view_type(desc.dimension);
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_of(desc.format);
    view_info.subresourceRange.levelCount = mips;
    view_info.subresourceRange.layerCount = layers;
    VkImageView view = VK_NULL_HANDLE;
    if (vk_failed(vkCreateImageView(device_, &view_info, nullptr, &view), "image view")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return nullptr;
    }

    auto texture = std::make_unique<VulkanTexture>(device_, image, memory, view, resolved);
    if (data != nullptr) {
        // The contract's asymmetry, matched exactly: for a single-layer RGBA8
        // 2D texture `data` is mip 0 and the chain is generated here; for
        // anything else `data` is the whole chain. Getting this backwards
        // uploads garbage into every level past the first, and nothing about
        // the resulting image looks wrong until something samples a low mip.
        const bool is_srgb = desc.format == Format::RGBA8_UNORM_SRGB;
        const bool rgba8 = desc.format == Format::RGBA8_UNORM || is_srgb;
        const bool generate = rgba8 && layers == 1 && mips > 1;
        std::vector<u8> generated;
        if (generate) {
            generated = math::build_rgba8_mip_chain(data, desc.width, desc.height, mips,
                is_srgb);
        }
        if (!upload_to_image(*texture, generate ? generated.data() : data)) {
            return nullptr;
        }
    } else if (plan.settled != VK_IMAGE_LAYOUT_UNDEFINED) {
        // A sampled texture with no initial data still has to leave UNDEFINED
        // before anything samples it, and there is no upload to do it.
        if (!settle_image(*texture, plan.settled)) {
            return nullptr;
        }
    }
    return texture;
}

// The source layout is slice-major then mip, tightly packed - the same order
// the other backend walks, and the same order build_rgba8_mip_chain produces.
// Vulkan copies straight out of it: bufferRowLength 0 means tightly packed, so
// unlike D3D12 there is no 256-byte row pitch to repack around.
bool VulkanDevice::upload_to_image(VulkanTexture& texture, const void* data) {
    const u32 bytes_per_texel = format_bytes(texture.format());
    if (bytes_per_texel == 0) {
        log(LogLevel::Error, LogChannel::Render, "upload_to_image: format cannot be packed");
        return false;
    }
    const u32 layers = texture.array_size();
    const u32 mips = texture.mip_levels();

    std::vector<VkBufferImageCopy> regions;
    usize total = 0;
    for (u32 layer = 0; layer < layers; ++layer) {
        u32 w = texture.width();
        u32 h = texture.height();
        for (u32 mip = 0; mip < mips; ++mip) {
            VkBufferImageCopy region{};
            region.bufferOffset = total;
            region.imageSubresource.aspectMask = aspect_of(texture.format());
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {w, h, 1};
            regions.push_back(region);
            total += static_cast<usize>(w) * h * bytes_per_texel;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }

    const VkImageLayout settled = plan_image_usage(texture.usage()).settled;
    const VkImageLayout final_layout = settled != VK_IMAGE_LAYOUT_UNDEFINED
        ? settled
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return stage_and_submit(data, total, [&](VkCommandBuffer cmd, VkBuffer staging) {
        barrier_image(cmd, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);
        vkCmdCopyBufferToImage(cmd, staging, texture.image(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()),
            regions.data());
        barrier_image(cmd, texture, final_layout, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    });
}

// Moves an image out of UNDEFINED with no data to copy. A one-command submit,
// which is the same cost as an upload and only happens at creation.
bool VulkanDevice::settle_image(VulkanTexture& texture, VkImageLayout layout) {
    // stage_and_submit needs something to stage; one byte is the smallest
    // honest ask, and the record callback ignores the buffer entirely.
    const u8 unused = 0;
    const bool depth = layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    const VkAccessFlags2 access = depth
        ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        : VK_ACCESS_2_SHADER_READ_BIT;
    const VkPipelineStageFlags2 stage = depth
        ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        : (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    return stage_and_submit(&unused, 1, [&](VkCommandBuffer cmd, VkBuffer staging) {
        (void)staging;
        barrier_image(cmd, texture, layout, access, stage);
    });
}

// One barrier, from whatever layout the image is actually in to the one asked
// for, updating the tracked layout. The old layout comes from the image rather
// than from a caller's belief about it - see VulkanTexture::layout().
void VulkanDevice::barrier_image(VkCommandBuffer cmd, VulkanTexture& texture,
    VkImageLayout to, VkAccessFlags2 access, VkPipelineStageFlags2 stage) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = stage;
    barrier.dstAccessMask = access;
    barrier.oldLayout = texture.layout();
    barrier.newLayout = to;
    barrier.image = texture.image();
    barrier.subresourceRange.aspectMask = aspect_of(texture.format());
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
    texture.set_layout(to);
}

std::unique_ptr<ISampler> VulkanDevice::create_sampler(const SamplerDesc& desc) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
    make_current();
    const VkSampler sampler = create_vulkan_sampler(device_, desc);
    if (sampler == VK_NULL_HANDLE) {
        return nullptr;
    }
    return std::make_unique<VulkanSampler>(device_, sampler);
}

void VulkanDevice::write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) {
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);
    ENGINE_ASSERT(data != nullptr);
    ENGINE_ASSERT_MSG(vk_buffer.mapped() != nullptr,
        "write_buffer requires a host-visible buffer");
    ENGINE_ASSERT(offset + size <= vk_buffer.size());
    std::memcpy(static_cast<u8*>(vk_buffer.mapped()) + offset, data, size);
}

void VulkanDevice::read_buffer(IBuffer& buffer, usize offset, void* data, usize size) {
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);
    ENGINE_ASSERT(data != nullptr);
    ENGINE_ASSERT_MSG(vk_buffer.mapped() != nullptr,
        "read_buffer requires a host-visible readback buffer");
    ENGINE_ASSERT(offset + size <= vk_buffer.size());
    // HOST_COHERENT, so no vkInvalidateMappedMemoryRanges is needed. If a
    // non-coherent heap is ever used, this is the line that has to change.
    std::memcpy(data, static_cast<const u8*>(vk_buffer.mapped()) + offset, size);
}

bool VulkanDevice::read_texture(ITexture& texture, void* out, usize size) {
    make_current();
    auto& vk_texture = static_cast<VulkanTexture&>(texture);
    ENGINE_ASSERT(out != nullptr);
    if (device_ == VK_NULL_HANDLE || vk_texture.image() == VK_NULL_HANDLE) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: texture has no image");
        return false;
    }
    if (vk_texture.sample_count() != 1) {
        log(LogLevel::Error, LogChannel::Render,
            "read_texture: source is multisampled - resolve it first");
        return false;
    }
    const u32 bytes_per_texel = format_bytes(vk_texture.format());
    if (bytes_per_texel == 0) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: format cannot be packed");
        return false;
    }
    const usize expected =
        static_cast<usize>(vk_texture.width()) * vk_texture.height() * bytes_per_texel;
    if (size != expected) {
        char message[176];
        std::snprintf(message, sizeof(message),
            "read_texture: size %zu does not match %ux%u x%u bytes = %zu", size,
            vk_texture.width(), vk_texture.height(), bytes_per_texel, expected);
        log(LogLevel::Error, LogChannel::Render, message);
        return false;
    }

    BufferDesc staging_desc{};
    staging_desc.size = expected;
    staging_desc.usage = BufferUsage::Readback;
    auto staging = create_buffer(staging_desc, nullptr);
    if (!staging) {
        return false;
    }
    auto& vk_staging = static_cast<VulkanBuffer&>(*staging);

    // Own recording, own submit, own wait - the contract says this blocks. The
    // texture must already be in ResourceState::CopySrc; that is stated on the
    // interface because D3D12 does not care about layouts and Vulkan does, so
    // the requirement is invisible from the first backend alone.
    VkCommandBuffer cmd = cmd_buffers_[frame_index_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vk_failed(vkBeginCommandBuffer(cmd, &begin), "readback begin")) {
        return false;
    }
    VkBufferImageCopy region{};
    // bufferRowLength 0 means tightly packed, which is exactly what the
    // contract promises the caller. D3D12 has to repack rows out of a
    // 256-byte-aligned pitch to reach the same result.
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {vk_texture.width(), vk_texture.height(), 1};
    vkCmdCopyImageToBuffer(cmd, vk_texture.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vk_staging.handle(), 1, &region);
    if (vk_failed(vkEndCommandBuffer(cmd), "readback end")) {
        return false;
    }

    VkFence fence = fences_[frame_index_];
    vkResetFences(device_, 1, &fence);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vk_failed(vkQueueSubmit(queue_, 1, &submit, fence), "readback submit")) {
        return false;
    }
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

    std::memcpy(out, vk_staging.mapped(), size);
    return true;
}

// A host-visible buffer, a copy, a submit and a wait. Freed only after the
// wait, which is the whole reason this blocks: the D3D12 path can hand its
// staging resource to a fence-keyed retirement list because a released
// D3D12 resource stays alive until the fence passes, and vkFreeMemory has no
// such courtesy.
bool VulkanDevice::stage_and_submit(
    const void* data, usize size, const std::function<void(VkCommandBuffer, VkBuffer)>& record) {
    if (device_ == VK_NULL_HANDLE || data == nullptr || size == 0) {
        return false;
    }

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vk_failed(vkCreateBuffer(device_, &staging_info, nullptr, &staging), "staging buffer")) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, staging, &requirements);
    const u32 type = find_memory_type(state_.gpu, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool ok = type != ~0u;
    if (ok) {
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        ok = !vk_failed(vkAllocateMemory(device_, &allocate, nullptr, &memory),
            "staging allocation");
    }
    if (ok) {
        ok = !vk_failed(vkBindBufferMemory(device_, staging, memory, 0), "staging bind");
    }
    void* mapped = nullptr;
    if (ok) {
        ok = !vk_failed(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped),
            "staging map");
    }
    if (ok) {
        std::memcpy(mapped, data, size);
        vkUnmapMemory(device_, memory);

        // The current slot's command buffer and fence. Uploads happen at
        // creation, outside any frame, so borrowing the slot is safe - and
        // waiting here is what makes it safe to borrow.
        VkCommandBuffer cmd = cmd_buffers_[frame_index_];
        VkFence fence = fences_[frame_index_];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = !vk_failed(vkBeginCommandBuffer(cmd, &begin), "upload begin");
        if (ok) {
            record(cmd, staging);
            ok = !vk_failed(vkEndCommandBuffer(cmd), "upload end");
        }
        if (ok) {
            vkResetFences(device_, 1, &fence);
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            ok = !vk_failed(vkQueueSubmit(queue_, 1, &submit, fence), "upload submit");
        }
        if (ok) {
            vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory, nullptr);
    }
    vkDestroyBuffer(device_, staging, nullptr);
    return ok;
}

bool VulkanDevice::upload_to_buffer(VkBuffer dest, const void* data, usize size) {
    return stage_and_submit(data, size, [&](VkCommandBuffer cmd, VkBuffer staging) {
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(cmd, staging, dest, 1, &region);
    });
}

void VulkanDevice::set_debug_name(IBuffer& buffer, std::string_view name) {
    (void)buffer;
    (void)name;
    // VK_EXT_debug_utils names objects, and the extension is only requested
    // when validation is on. Silently doing nothing here is the one acceptable
    // case in this file: a missing debug name changes no behaviour, and a
    // warning per resource would drown the log it exists to help read.
}

void VulkanDevice::set_debug_name(ITexture& texture, std::string_view name) {
    (void)texture;
    (void)name;
}

// ── Not yet implemented ─────────────────────────────────────────────────────
//
// Filled in by the parity pass. Each says so once, by name, so a partial
// backend cannot be mistaken for a working one.

FrameAllocation VulkanDevice::alloc_frame_memory(usize size) {
    make_current();
    ENGINE_ASSERT(size > 0);
    if (frame_ring_[frame_index_] == nullptr) {
        return {};
    }
    const usize aligned = (size + (kBufferAlign - 1)) & ~(kBufferAlign - 1);

    // How much a frame needs depends on how much is on screen, so running out
    // is a content outcome, not a bug. Return an empty slice; the caller skips
    // that draw and the frame is missing an object instead of the process
    // being gone. Same behaviour, same one-shot message and same bookkeeping
    // as the other backend, because the frame-ring budget gate compares the
    // headroom figure and it has to mean the same thing.
    if (aligned > kFrameRingBytes - frame_ring_offset_) {
        if (!frame_ring_exhausted_) {
            frame_ring_exhausted_ = true;
            frame_ring_exhausted_frames_ += 1;
            char message[192];
            std::snprintf(message, sizeof(message),
                "Frame constant ring exhausted: %zu of %zu bytes used. Dropping draws this "
                "frame - raise kFrameRingBytes.",
                frame_ring_offset_, kFrameRingBytes);
            log(LogLevel::Error, LogChannel::Render, message);
        }
        return {};
    }

    FrameAllocation allocation{};
    allocation.buffer = frame_ring_[frame_index_].get();
    allocation.offset = frame_ring_offset_;
    frame_ring_offset_ += aligned;
    if (frame_ring_offset_ > frame_ring_peak_) {
        frame_ring_peak_ = frame_ring_offset_;
    }
    return allocation;
}






std::unique_ptr<IComputePipeline> VulkanDevice::create_compute_pipeline(
    const ComputePipelineDesc& desc) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
    make_current();
    if (!is_spirv(desc.compute_shader, "compute shader")) {
        return nullptr;
    }
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = desc.compute_shader.size();
    module_info.pCode = reinterpret_cast<const u32*>(desc.compute_shader.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vk_failed(vkCreateShaderModule(device_, &module_info, nullptr, &module),
            "compute shader module")) {
        return nullptr;
    }
    // No VkPipeline yet: the set layout depends on whether each `u` slot is an
    // image or a texel buffer, and only a bind knows that. See ComputeVariant.
    return std::make_unique<VulkanComputePipeline>(device_, module,
        spirv_entry_point(desc.compute_shader, "cs_main"), desc.uniform_buffer_count,
        desc.sampled_texture_count, desc.storage_texture_count);
}

VulkanComputePipeline::~VulkanComputePipeline() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (const auto& [mask, variant] : variants_) {
        (void)mask;
        vkDestroyPipeline(device_, variant.pipeline, nullptr);
        vkDestroyPipelineLayout(device_, variant.layout, nullptr);
        vkDestroyDescriptorSetLayout(device_, variant.set_layout, nullptr);
    }
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
    }
}

const ComputeVariant* VulkanComputePipeline::variant(u32 image_mask) {
    const auto found = variants_.find(image_mask);
    if (found != variants_.end()) {
        return &found->second;
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    auto add = [&bindings](u32 binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding entry{};
        entry.binding = binding;
        entry.descriptorType = type;
        entry.descriptorCount = 1;
        entry.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(entry);
    };
    for (u32 i = 0; i < uniform_count_; ++i) {
        add(kBindingBaseUniform + i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }
    for (u32 i = 0; i < sampled_count_; ++i) {
        add(kBindingBaseSampledTexture + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    for (u32 i = 0; i < storage_count_; ++i) {
        // The kind the caller actually bound. An RWBuffer<T> is a *texel*
        // buffer in SPIR-V, not a storage buffer - OpTypeImage with
        // Dim = Buffer - which is the distinction validation catches and the
        // other backend never has to make.
        const bool is_image = (image_mask & (1u << i)) != 0;
        add(kBindingBaseStorageTexture + i,
            is_image ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                     : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    }

    ComputeVariant variant{};
    VkDescriptorSetLayoutCreateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_info.bindingCount = static_cast<u32>(bindings.size());
    set_info.pBindings = bindings.empty() ? nullptr : bindings.data();
    if (vk_failed(vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &variant.set_layout),
            "compute descriptor set layout")) {
        return nullptr;
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &variant.set_layout;
    if (vk_failed(vkCreatePipelineLayout(device_, &layout_info, nullptr, &variant.layout),
            "compute pipeline layout")) {
        vkDestroyDescriptorSetLayout(device_, variant.set_layout, nullptr);
        return nullptr;
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module_;
    // From the module, not a constant: msaa_gate.hlsl's compute entry point is
    // cs_count, and a hard-coded "cs_main" failed pipeline creation with
    // "entry point not found" - the shader had told us the answer all along.
    info.stage.pName = entry_.c_str();
    info.layout = variant.layout;
    if (vk_failed(
            vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                &variant.pipeline),
            "compute pipeline")) {
        vkDestroyPipelineLayout(device_, variant.layout, nullptr);
        vkDestroyDescriptorSetLayout(device_, variant.set_layout, nullptr);
        return nullptr;
    }
    return &variants_.emplace(image_mask, variant).first->second;
}

} // namespace engine::rhi::vulkan
