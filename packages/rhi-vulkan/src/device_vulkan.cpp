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
// arrives, and the render-target kinds get TRANSFER_SRC because read_texture
// copies out of them.
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
        break;
    case TextureUsage::ShaderResource:
        plan.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        plan.settled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case TextureUsage::DepthShaderResource:
        plan.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

std::unique_ptr<ITexture> VulkanDevice::create_texture(
    const TextureDesc& desc, const void* data) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }

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
    const u32 mips = desc.mip_levels == 0 ? 1u : desc.mip_levels;

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

    auto texture = std::make_unique<VulkanTexture>(device_, image, memory, view, desc);
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
    return stage_and_submit(&unused, 1, [&](VkCommandBuffer cmd, VkBuffer staging) {
        (void)staging;
        barrier_image(cmd, texture, layout, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
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
        desc.uniform_buffer_count, desc.sampled_texture_count, desc.storage_texture_count);
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
    // DXC keeps the HLSL entry point name, so cs_main survives.
    info.stage.pName = "cs_main";
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
