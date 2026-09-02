#include "device_vulkan.hpp"

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

VulkanBuffer::~VulkanBuffer() {
    if (device_ == VK_NULL_HANDLE) {
        return;
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
    VkImageView view, const TextureDesc& desc)
    : device_(device), image_(image), memory_(memory), view_(view), desc_(desc) {}

VulkanTexture::~VulkanTexture() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
    }
}

VulkanPipeline::VulkanPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout,
    VkDescriptorSetLayout set_layout, u32 uniform_count)
    : device_(device), pipeline_(pipeline), layout_(layout), set_layout_(set_layout),
      uniform_count_(uniform_count) {}

VulkanPipeline::~VulkanPipeline() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    }
}

// ── Device ──────────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        }
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence_, nullptr);
        }
        if (cmd_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, cmd_pool_, nullptr);
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    destroy_vulkan_instance(state_);
}

bool VulkanDevice::init(const DeviceDesc& desc) {
    depth_convention_ = desc.depth_convention;
    width_ = desc.width;
    height_ = desc.height;
    present_interval_ = desc.present_interval == 0 ? 0u : 1u;
    offscreen_ = desc.window_handle == nullptr;

    if (!offscreen_) {
        // Presentation is a separate pass. Refusing here rather than ignoring
        // the handle means a caller cannot believe it has a windowed Vulkan
        // device and then wonder why nothing appears.
        log(LogLevel::Error, LogChannel::Render,
            "rhi-vulkan has no swapchain yet - create it with a null window_handle for an "
            "offscreen device. Presentation lands with the parity pass.");
        return false;
    }

    if (!create_vulkan_instance(state_)) {
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

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features13;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    if (vk_failed(vkCreateDevice(state_.gpu, &device_info, nullptr, &device_),
            "device creation")) {
        return false;
    }
    // Device-level entry points come from vkGetDeviceProcAddr, which skips the
    // loader's dispatch trampoline. Without this every call still works but
    // goes the long way round.
    volkLoadDevice(device_);
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
    cmd_info.commandBufferCount = 1;
    if (vk_failed(vkAllocateCommandBuffers(device_, &cmd_info, &cmd_buffer_),
            "command buffer allocation")) {
        return false;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vk_failed(vkCreateFence(device_, &fence_info, nullptr, &fence_), "fence creation")) {
        return false;
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
    };
    VkDescriptorPoolCreateInfo descriptor_info{};
    descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_info.maxSets = 64;
    descriptor_info.poolSizeCount = static_cast<u32>(std::size(sizes));
    descriptor_info.pPoolSizes = sizes;
    if (vk_failed(vkCreateDescriptorPool(device_, &descriptor_info, nullptr, &descriptor_pool_),
            "descriptor pool creation")) {
        return false;
    }

    // Shader model is an HLSL notion, and it is the honest one to report: the
    // SPIR-V this backend consumes is compiled by DXC from SM 6.0 HLSL.
    //
    // Feature level has no Vulkan equivalent at all, so it stays 0 rather than
    // being filled with an unrelated number. That is a real strain in
    // GpuBaseline - the struct is D3D-shaped - and it is recorded rather than
    // papered over, because finding out where the contract is D3D-shaped is
    // what a second backend is for.
    baseline_.shader_model = kGpuShaderModel_6_0;
    baseline_.feature_level = 0;

    char message[192];
    std::snprintf(message, sizeof(message),
        "Vulkan offscreen device initialized (%s, SM %u.%u, no swapchain)",
        state_.device_name.c_str(), (baseline_.shader_model >> 4) & 0xFu,
        baseline_.shader_model & 0xFu);
    log(LogLevel::Info, LogChannel::Render, message);
    return true;
}

ISwapchain& VulkanDevice::swapchain() {
    // Unreachable by construction: init refuses a non-null window handle, so
    // every device here is offscreen. Asserting rather than returning something
    // fake keeps that true when presentation lands.
    ENGINE_ASSERT_MSG(false, "offscreen device has no swapchain");
    static struct NoSwapchain final : ISwapchain {
        void present() override {}
        u32 current_back_buffer_index() const override { return 0; }
    } unreachable;
    return unreachable;
}

ITexture& VulkanDevice::swapchain_color() {
    ENGINE_ASSERT_MSG(false, "offscreen device has no swapchain colour target");
    static VulkanTexture unreachable(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
        VK_NULL_HANDLE, TextureDesc{});
    return unreachable;
}

ITexture& VulkanDevice::swapchain_depth() {
    ENGINE_ASSERT_MSG(false, "offscreen device has no swapchain depth target");
    static VulkanTexture unreachable(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
        VK_NULL_HANDLE, TextureDesc{});
    return unreachable;
}

void VulkanDevice::begin_frame() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    vkResetFences(device_, 1, &fence_);
    vkResetDescriptorPool(device_, descriptor_pool_, 0);
}

void VulkanDevice::submit() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffer_;
    if (vk_failed(vkQueueSubmit(queue_, 1, &submit, fence_), "queue submit")) {
        // A lost device never recovers, and the frame loop has to stop rather
        // than keep submitting into it - the same latch the D3D12 backend uses.
        device_lost_ = true;
    }
}

void VulkanDevice::end_frame() {
    // Nothing to present. Named rather than left to the pure virtual so it is
    // clear this is complete for an offscreen device, not missing.
}

void VulkanDevice::wait_idle() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    // Waits the fence submit() signalled, then the queue, so a caller that
    // submitted nothing this frame is not left blocked on an unsignalled fence.
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkQueueWaitIdle(queue_);
}

bool VulkanDevice::resize(u32 width, u32 height) {
    // No swapchain to resize; the extent is whatever the caller asked for.
    width_ = width;
    height_ = height;
    return true;
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

// ── Not yet implemented ─────────────────────────────────────────────────────
//
// Filled in by the parity pass. Each says so once, by name, so a partial
// backend cannot be mistaken for a working one.

std::unique_ptr<IBuffer> VulkanDevice::create_buffer(const BufferDesc& desc, const void* data) {
    (void)desc;
    (void)data;
    not_implemented("create_buffer");
    return nullptr;
}

std::unique_ptr<ITexture> VulkanDevice::create_texture(
    const TextureDesc& desc, const void* data) {
    (void)desc;
    (void)data;
    not_implemented("create_texture");
    return nullptr;
}

FrameAllocation VulkanDevice::alloc_frame_memory(usize size) {
    (void)size;
    not_implemented("alloc_frame_memory");
    return {};
}

void VulkanDevice::write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) {
    (void)buffer;
    (void)offset;
    (void)data;
    (void)size;
    not_implemented("write_buffer");
}

void VulkanDevice::read_buffer(IBuffer& buffer, usize offset, void* data, usize size) {
    (void)buffer;
    (void)offset;
    (void)data;
    (void)size;
    not_implemented("read_buffer");
}

bool VulkanDevice::read_texture(ITexture& texture, void* out, usize size) {
    (void)texture;
    (void)out;
    (void)size;
    not_implemented("read_texture");
    return false;
}

void VulkanDevice::set_debug_name(IBuffer& buffer, std::string_view name) {
    (void)buffer;
    (void)name;
    not_implemented("set_debug_name(IBuffer)");
}

void VulkanDevice::set_debug_name(ITexture& texture, std::string_view name) {
    (void)texture;
    (void)name;
    not_implemented("set_debug_name(ITexture)");
}

std::unique_ptr<IComputePipeline> VulkanDevice::create_compute_pipeline(
    const ComputePipelineDesc& desc) {
    (void)desc;
    not_implemented("create_compute_pipeline");
    return nullptr;
}

std::unique_ptr<ISampler> VulkanDevice::create_sampler(const SamplerDesc& desc) {
    (void)desc;
    not_implemented("create_sampler");
    return nullptr;
}

} // namespace engine::rhi::vulkan
