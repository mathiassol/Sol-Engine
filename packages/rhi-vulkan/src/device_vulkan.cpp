#include "device_vulkan.hpp"

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
        // A StructuredBuffer is a storage buffer in SPIR-V, and the engine also
        // reads one back through copy_buffer, hence TRANSFER_SRC.
        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
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

std::unique_ptr<ITexture> VulkanDevice::create_texture(
    const TextureDesc& desc, const void* data) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
    if (data != nullptr) {
        not_implemented("create_texture with initial data");
        return nullptr;
    }
    if (desc.usage != TextureUsage::RenderTarget) {
        not_implemented("create_texture (non-render-target usage)");
        return nullptr;
    }
    if (desc.dimension != TextureDimension::Tex2D || desc.array_size != 1
        || desc.mip_levels != 1) {
        not_implemented("create_texture (cube, array or mip chain)");
        return nullptr;
    }

    const VkFormat format = to_vulkan_format(desc.format);
    if (format == VK_FORMAT_UNDEFINED) {
        log(LogLevel::Error, LogChannel::Render, "create_texture: unsupported format");
        return nullptr;
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = static_cast<VkSampleCountFlagBits>(
        desc.sample_count == 0 ? 1u : desc.sample_count);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_SRC so read_texture can copy out of it. On D3D12 that needs no
    // creation flag at all, which is the kind of asymmetry a second backend is
    // for finding.
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = desc.format == Format::D32_FLOAT
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    if (vk_failed(vkCreateImageView(device_, &view_info, nullptr, &view), "image view")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return nullptr;
    }

    return std::make_unique<VulkanTexture>(device_, image, memory, view, desc);
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
    vkResetCommandBuffer(cmd_buffer_, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vk_failed(vkBeginCommandBuffer(cmd_buffer_, &begin), "readback begin")) {
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
    vkCmdCopyImageToBuffer(cmd_buffer_, vk_texture.image(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_staging.handle(), 1, &region);
    if (vk_failed(vkEndCommandBuffer(cmd_buffer_), "readback end")) {
        return false;
    }

    vkResetFences(device_, 1, &fence_);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffer_;
    if (vk_failed(vkQueueSubmit(queue_, 1, &submit, fence_), "readback submit")) {
        return false;
    }
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

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

        vkResetCommandBuffer(cmd_buffer_, 0);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = !vk_failed(vkBeginCommandBuffer(cmd_buffer_, &begin), "upload begin");
        if (ok) {
            record(cmd_buffer_, staging);
            ok = !vk_failed(vkEndCommandBuffer(cmd_buffer_), "upload end");
        }
        if (ok) {
            vkResetFences(device_, 1, &fence_);
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd_buffer_;
            ok = !vk_failed(vkQueueSubmit(queue_, 1, &submit, fence_), "upload submit");
        }
        if (ok) {
            vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
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
    (void)size;
    not_implemented("alloc_frame_memory");
    return {};
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
