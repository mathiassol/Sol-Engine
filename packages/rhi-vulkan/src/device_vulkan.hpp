#pragma once

#include "vulkan_common.hpp"

#include <engine/core/assert.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/rhi.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::rhi::vulkan {

// One header for the whole backend's types, because they reference each other -
// a command list needs a texture's image view, a pipeline needs the device's
// layout cache. The *implementations* are split by responsibility across
// instance_vulkan.cpp, device_vulkan.cpp, pipeline_vulkan.cpp and
// commands_vulkan.cpp: rhi-d3d12's single 3,414-line device file is the largest
// in the engine, and repeating that shape in the second backend would be
// choosing it on purpose.

class VulkanDevice;

// What one bootstrap produced. Separated from the device so instance selection
// can be read, and tested, without the device's frame machinery around it.
struct VulkanInstance {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    u32 graphics_family = ~0u;
    u32 api_version = 0;
    std::string device_name;
    bool validation_enabled = false;
};

// Creates instance, picks a physical device, finds a graphics queue family.
// False with the reason logged on any failure. instance_vulkan.cpp.
bool create_vulkan_instance(VulkanInstance& out);
void destroy_vulkan_instance(VulkanInstance& state);

// Layout, access mask and stage mask together. D3D12 has one state enum
// covering all three; Vulkan splits them, so they are returned as a unit -
// a barrier that gets two of the three right is a barrier that does nothing.
struct BarrierState {
    VkImageLayout layout;
    VkAccessFlags2 access;
    VkPipelineStageFlags2 stage;
};

// The aspect is a parameter because one state means two layouts: ShaderRead on
// a colour image is SHADER_READ_ONLY_OPTIMAL and on a depth image is
// DEPTH_READ_ONLY_OPTIMAL. Passing it in rather than deriving the aspect
// separately at the call site keeps one fact in one place - the two derivations
// could disagree, and the shadow map is the first thing that would notice.
BarrierState to_vulkan_barrier(ResourceState state, VkImageAspectFlags aspect);

// The aspect an image's format implies. Depth formats sample and attach through
// the depth aspect; everything else through colour.
VkImageAspectFlags aspect_of(Format format);

// resources.hpp's binding contract, as numbers. Built into the descriptor-set
// layout in pipeline_vulkan.cpp and written to in commands_vulkan.cpp, so they
// live here rather than in either.
//
// These must equal the register shifts shaders-dxc passes to DXC
// (-fvk-t-shift 16, -fvk-u-shift 32, -fvk-s-shift 48). The default HLSL->SPIR-V
// mapping sends register(xN, spaceM) to set M binding N and *ignores the
// register type*, so without disjoint ranges b0 and t0 would both be set 0
// binding 0. The same numbers therefore exist in two files, and a change to one
// without the other is a shader reading the wrong descriptor with nothing
// logged - so both sites carry a comment naming the other.
constexpr u32 kBindingBaseUniform = 0;
constexpr u32 kBindingBaseSampledTexture = 16;
constexpr u32 kBindingBaseStorageTexture = 32;
constexpr u32 kBindingBaseSampler = 48;

// Defined in device_vulkan.cpp, used by the pipeline factory too.
VkFormat to_vulkan_format(Format format);
u32 format_bytes(Format format);

// Heap index satisfying `required` for this allocation, or ~0u.
u32 find_memory_type(VkPhysicalDevice gpu, u32 type_bits, VkMemoryPropertyFlags required);

class VulkanBuffer final : public IBuffer {
public:
    VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, usize size,
        void* mapped);
    ~VulkanBuffer() override;

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    usize size() const override { return size_; }
    VkBuffer handle() const { return buffer_; }
    // Non-null for a host-visible buffer, which stays mapped for its lifetime -
    // the same trade the D3D12 upload heap makes.
    void* mapped() const { return mapped_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    usize size_ = 0;
    void* mapped_ = nullptr;
};

class VulkanTexture final : public ITexture {
public:
    VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView view,
        const TextureDesc& desc);
    ~VulkanTexture() override;

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    u32 width() const override { return desc_.width; }
    u32 height() const override { return desc_.height; }
    u32 mip_levels() const override { return desc_.mip_levels; }
    u32 array_size() const override { return desc_.array_size; }
    TextureDimension dimension() const override { return desc_.dimension; }
    Format format() const override { return desc_.format; }
    u32 sample_count() const override { return desc_.sample_count; }

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    TextureDesc desc_{};
};

// Everything needed to create a VkPipeline *except* the vertex stride, which
// Vulkan bakes in and the contract supplies at bind time. Kept so a variant can
// be built for whatever stride first arrives - see the note at the top of
// pipeline_vulkan.cpp.
//
// The shader modules live here for the pipeline's whole lifetime rather than
// being destroyed straight after creation, because a later variant needs them.
struct PipelineRecipe {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkVertexInputAttributeDescription attributes[GraphicsPipelineDesc::kMaxAttributes]{};
    u32 attribute_count = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    bool blend_alpha = false;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare = VK_COMPARE_OP_ALWAYS;
    f32 depth_bias = 0.f;
    u32 sample_count = 1;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

class VulkanPipeline final : public IGraphicsPipeline {
public:
    VulkanPipeline(VkDevice device, const PipelineRecipe& recipe, VkDescriptorSetLayout set0,
        VkDescriptorSetLayout set1, std::vector<VkSampler> samplers, u32 uniform_count,
        u32 storage_buffer_count);
    ~VulkanPipeline() override;

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // The pipeline for this vertex stride, created on first use and cached. In
    // this engine that is two or three for a whole run.
    VkPipeline variant(u32 stride);

    VkPipelineLayout layout() const { return recipe_.layout; }
    VkDescriptorSetLayout set_layout(u32 set) const { return set == 0 ? set0_ : set1_; }
    u32 uniform_count() const { return uniform_count_; }
    u32 storage_buffer_count() const { return storage_buffer_count_; }
    u32 attribute_count() const { return recipe_.attribute_count; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    PipelineRecipe recipe_{};
    VkDescriptorSetLayout set0_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set1_ = VK_NULL_HANDLE;
    std::vector<VkSampler> samplers_;
    std::unordered_map<u32, VkPipeline> variants_;
    u32 uniform_count_ = 0;
    u32 storage_buffer_count_ = 0;
};

// The one place a SamplerDesc becomes a VkSampler. Used both for immutable
// samplers baked into a set layout and for create_sampler's standalone objects.
VkSampler create_vulkan_sampler(VkDevice device, const SamplerDesc& desc);

// Both exist so the device's factory functions have something to return once
// they are implemented. Neither carries state yet, and creating one reaches
// not_implemented - see the note at the top of this file.
class VulkanComputePipeline final : public IComputePipeline {};
class VulkanSampler final : public ISampler {};

class VulkanCommandList final : public ICommandList {
public:
    explicit VulkanCommandList(VulkanDevice& device) : device_(device) {}

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassInfo& info) override;
    void end_render_pass() override;
    void transition(ITexture& texture, ResourceState from, ResourceState to) override;
    void transition(IBuffer& buffer, ResourceState from, ResourceState to) override;
    void copy_texture(ITexture& src, ITexture& dst) override;
    void copy_buffer(IBuffer& src, IBuffer& dst, usize size) override;

    void set_viewport(u32 width, u32 height) override;
    void set_pipeline(IGraphicsPipeline& pipeline) override;
    void set_compute_pipeline(IComputePipeline& pipeline) override;
    void set_vertex_buffer(
        u32 slot, IBuffer& buffer, u32 stride_bytes, usize offset_bytes) override;
    void set_index_buffer(IBuffer& buffer, usize offset_bytes) override;
    void set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) override;
    void set_shader_resource(u32 slot, ITexture& texture) override;
    void set_unordered_access(u32 slot, IBuffer& buffer) override;
    void set_unordered_access(u32 slot, ITexture& texture) override;
    void set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) override;
    void draw(u32 vertex_count, u32 start_vertex) override;
    void draw_indexed(u32 index_count, u32 start_index, i32 base_vertex,
        u32 instance_count) override;
    void dispatch(u32 group_count_x, u32 group_count_y, u32 group_count_z) override;

    void begin_event(std::string_view name) override;
    void end_event() override;
    void set_marker(std::string_view name) override;
    u32 debug_event_depth() const override { return event_depth_; }
    std::string_view debug_event_name() const override;
    std::string_view last_debug_marker() const override { return last_marker_; }

private:
    static constexpr u32 kMaxDebugEvents = 16;

    VulkanDevice& device_;
    VulkanPipeline* bound_pipeline_ = nullptr;
    // Which stride variant is currently bound, so set_vertex_buffer only
    // rebinds when the stride actually changes. ~0u means none yet.
    u32 bound_stride_ = ~0u;
    VulkanTexture* pass_color_ = nullptr;
    u32 event_depth_ = 0;
    std::string event_stack_[kMaxDebugEvents];
    std::string last_marker_;
};

class VulkanDevice final : public IDevice {
public:
    VulkanDevice() : commands_(*this) {}
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    bool init(const DeviceDesc& desc);

    DepthConvention depth_convention() const override { return depth_convention_; }
    bool offscreen() const override { return offscreen_; }
    ISwapchain& swapchain() override;
    ICommandList& command_list() override { return commands_; }

    void begin_frame() override;
    void submit() override;
    void end_frame() override;
    void set_present_interval(u32 interval) override { present_interval_ = interval; }
    u32 present_interval() const override { return present_interval_; }

    bool resize(u32 width, u32 height) override;
    void wait_idle() override;

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }
    bool device_lost() const override { return device_lost_; }

    std::unique_ptr<IBuffer> create_buffer(const BufferDesc& desc, const void* data) override;
    std::unique_ptr<ITexture> create_texture(const TextureDesc& desc, const void* data) override;
    FrameAllocation alloc_frame_memory(usize size) override;
    void write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) override;
    void read_buffer(IBuffer& buffer, usize offset, void* data, usize size) override;
    bool read_texture(ITexture& texture, void* out, usize size) override;
    void set_debug_name(IBuffer& buffer, std::string_view name) override;
    void set_debug_name(ITexture& texture, std::string_view name) override;
    ITexture& swapchain_color() override;
    ITexture& swapchain_depth() override;
    GpuMemoryStats gpu_memory_stats() const override;
    FrameRingStats frame_ring_stats() const override { return ring_stats_; }
    GpuBaseline gpu_baseline() const override { return baseline_; }
    f32 last_gpu_time_ms() const override { return 0.f; }

    std::unique_ptr<IGraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc) override;
    std::unique_ptr<IComputePipeline> create_compute_pipeline(
        const ComputePipelineDesc& desc) override;
    std::unique_ptr<ISampler> create_sampler(const SamplerDesc& desc) override;

    // One-shot copy on the device's own command buffer, submitted and waited.
    // The D3D12 side retires its staging buffer against a fence and returns
    // immediately; this waits instead, because a Vulkan staging buffer freed
    // while a copy is in flight is undefined rather than merely early - and an
    // upload path that blocks is honest at creation time, which is the only
    // time either backend uploads.
    bool stage_and_submit(const void* data, usize size,
        const std::function<void(VkCommandBuffer, VkBuffer)>& record);
    bool upload_to_buffer(VkBuffer dest, const void* data, usize size);
    bool upload_to_image(VkImage dest, const TextureDesc& desc, const void* data);

    // For the command list and the pipeline factory, which live in other
    // translation units.
    VkDevice handle() const { return device_; }
    VkPhysicalDevice gpu() const { return state_.gpu; }
    VkCommandBuffer cmd() const { return cmd_buffer_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }
    const std::string& device_name() const { return state_.device_name; }

private:
    VulkanInstance state_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

    VulkanCommandList commands_;
    DepthConvention depth_convention_ = DepthConvention::Standard;
    bool offscreen_ = true;
    bool device_lost_ = false;
    u32 width_ = 0;
    u32 height_ = 0;
    u32 present_interval_ = 1;
    GpuBaseline baseline_{};
    FrameRingStats ring_stats_{};
};

} // namespace engine::rhi::vulkan
