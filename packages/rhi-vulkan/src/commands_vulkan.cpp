#include "device_vulkan.hpp"

namespace engine::rhi::vulkan {

// D3D12 has one state enum covering layout, visibility and pipeline stage;
// Vulkan splits them into three. So this is not a lookup table, it is the
// translation - and the three results come back together because a barrier that
// gets two of them right is a barrier that does nothing.
BarrierState to_vulkan_barrier(ResourceState state) {
    switch (state) {
    case ResourceState::Common:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    case ResourceState::RenderTarget:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    case ResourceState::DepthWrite:
        return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT};
    case ResourceState::CopySrc:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT};
    case ResourceState::CopyDst:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT};
    case ResourceState::ShaderRead:
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    case ResourceState::Storage:
        return {VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    case ResourceState::Present:
        // Unreachable while every device here is offscreen, rather than wrong.
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
    }
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
}

// ── Recording ───────────────────────────────────────────────────────────────

void VulkanCommandList::begin() {
    event_depth_ = 0;
    last_marker_.clear();
    bound_pipeline_ = nullptr;
    pass_color_ = nullptr;
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    vkResetCommandBuffer(device_.cmd(), 0);
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_failed(vkBeginCommandBuffer(device_.cmd(), &begin_info), "command buffer begin");
}

void VulkanCommandList::end() {
    ENGINE_ASSERT(event_depth_ == 0);
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    vk_failed(vkEndCommandBuffer(device_.cmd()), "command buffer end");
}

void VulkanCommandList::transition(ITexture& texture, ResourceState from, ResourceState to) {
    if (from == to || device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_texture = static_cast<VulkanTexture&>(texture);
    const BarrierState before = to_vulkan_barrier(from);
    const BarrierState after = to_vulkan_barrier(to);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = before.layout;
    barrier.newLayout = after.layout;
    barrier.image = vk_texture.image();
    barrier.subresourceRange.aspectMask = vk_texture.format() == Format::D32_FLOAT
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(device_.cmd(), &dependency);
}

void VulkanCommandList::transition(IBuffer& buffer, ResourceState from, ResourceState to) {
    if (from == to || device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);
    const BarrierState before = to_vulkan_barrier(from);
    const BarrierState after = to_vulkan_barrier(to);

    // A buffer has no layout, so only the access and stage halves of the
    // translation apply. Using the image path for a buffer is a validation
    // error, not a no-op, which is why these are separate overloads.
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.buffer = vk_buffer.handle();
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(device_.cmd(), &dependency);
}

// ── Debug markers ───────────────────────────────────────────────────────────
//
// Bookkeeping only for now, which is enough for the contract's accessors and
// for the Pix gate's assertions. vkCmdBeginDebugUtilsLabelEXT joins this when
// the validation layer is a required rather than optional install.

void VulkanCommandList::begin_event(std::string_view name) {
    if (event_depth_ < kMaxDebugEvents) {
        event_stack_[event_depth_].assign(name);
    }
    ++event_depth_;
}

void VulkanCommandList::end_event() {
    if (event_depth_ > 0) {
        --event_depth_;
    }
}

void VulkanCommandList::set_marker(std::string_view name) { last_marker_.assign(name); }

std::string_view VulkanCommandList::debug_event_name() const {
    if (event_depth_ == 0 || event_depth_ > kMaxDebugEvents) {
        return {};
    }
    return event_stack_[event_depth_ - 1];
}

// ── Not yet implemented ─────────────────────────────────────────────────────

void VulkanCommandList::begin_render_pass(const RenderPassInfo& info) {
    (void)info;
    not_implemented("begin_render_pass");
}

void VulkanCommandList::end_render_pass() { not_implemented("end_render_pass"); }

void VulkanCommandList::copy_texture(ITexture& src, ITexture& dst) {
    (void)src;
    (void)dst;
    not_implemented("copy_texture");
}

void VulkanCommandList::copy_buffer(IBuffer& src, IBuffer& dst, usize size) {
    (void)src;
    (void)dst;
    (void)size;
    not_implemented("copy_buffer");
}

void VulkanCommandList::set_viewport(u32 width, u32 height) {
    (void)width;
    (void)height;
    not_implemented("set_viewport");
}

void VulkanCommandList::set_pipeline(IGraphicsPipeline& pipeline) {
    (void)pipeline;
    not_implemented("set_pipeline");
}

void VulkanCommandList::set_compute_pipeline(IComputePipeline& pipeline) {
    (void)pipeline;
    not_implemented("set_compute_pipeline");
}

void VulkanCommandList::set_vertex_buffer(
    u32 slot, IBuffer& buffer, u32 stride_bytes, usize offset_bytes) {
    (void)slot;
    (void)buffer;
    (void)stride_bytes;
    (void)offset_bytes;
    not_implemented("set_vertex_buffer");
}

void VulkanCommandList::set_index_buffer(IBuffer& buffer, usize offset_bytes) {
    (void)buffer;
    (void)offset_bytes;
    not_implemented("set_index_buffer");
}

void VulkanCommandList::set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    (void)slot;
    (void)buffer;
    (void)offset_bytes;
    not_implemented("set_constant_buffer");
}

void VulkanCommandList::set_shader_resource(u32 slot, ITexture& texture) {
    (void)slot;
    (void)texture;
    not_implemented("set_shader_resource");
}

void VulkanCommandList::set_unordered_access(u32 slot, IBuffer& buffer) {
    (void)slot;
    (void)buffer;
    not_implemented("set_unordered_access(IBuffer)");
}

void VulkanCommandList::set_unordered_access(u32 slot, ITexture& texture) {
    (void)slot;
    (void)texture;
    not_implemented("set_unordered_access(ITexture)");
}

void VulkanCommandList::set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    (void)slot;
    (void)buffer;
    (void)offset_bytes;
    not_implemented("set_structured_buffer");
}

void VulkanCommandList::draw(u32 vertex_count, u32 start_vertex) {
    (void)vertex_count;
    (void)start_vertex;
    not_implemented("draw");
}

void VulkanCommandList::draw_indexed(
    u32 index_count, u32 start_index, i32 base_vertex, u32 instance_count) {
    (void)index_count;
    (void)start_index;
    (void)base_vertex;
    (void)instance_count;
    not_implemented("draw_indexed");
}

void VulkanCommandList::dispatch(u32 group_count_x, u32 group_count_y, u32 group_count_z) {
    (void)group_count_x;
    (void)group_count_y;
    (void)group_count_z;
    not_implemented("dispatch");
}

} // namespace engine::rhi::vulkan
