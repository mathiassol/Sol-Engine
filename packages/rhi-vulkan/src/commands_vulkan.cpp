#include "device_vulkan.hpp"

namespace engine::rhi::vulkan {

// D3D12 has one state enum covering layout, visibility and pipeline stage;
// Vulkan splits them into three. So this is not a lookup table, it is the
// translation - and the three results come back together because a barrier that
// gets two of them right is a barrier that does nothing.
VkImageAspectFlags aspect_of(Format format) {
    return format == Format::D32_FLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

BarrierState to_vulkan_barrier(ResourceState state, VkImageAspectFlags aspect) {
    const bool depth = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
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
        // The one state that means two layouts. A sampled shadow map reaches
        // here with the depth aspect, and SHADER_READ_ONLY_OPTIMAL on a depth
        // image is a validation error rather than a wrong picture.
        return {depth ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
                      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_2_SHADER_READ_BIT,
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
    const VkImageAspectFlags aspect = aspect_of(vk_texture.format());
    const BarrierState before = to_vulkan_barrier(from, aspect);
    const BarrierState after = to_vulkan_barrier(to, aspect);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = before.layout;
    barrier.newLayout = after.layout;
    barrier.image = vk_texture.image();
    barrier.subresourceRange.aspectMask = aspect;
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
    // A buffer has no aspect and no layout. Colour is passed because the
    // parameter is not optional, and only the access and stage halves of the
    // result are read below.
    const BarrierState before = to_vulkan_barrier(from, VK_IMAGE_ASPECT_COLOR_BIT);
    const BarrierState after = to_vulkan_barrier(to, VK_IMAGE_ASPECT_COLOR_BIT);

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

void VulkanCommandList::begin_render_pass(const RenderPassInfo& info) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    pass_color_ = info.color ? static_cast<VulkanTexture*>(info.color) : nullptr;
    auto* depth = info.depth ? static_cast<VulkanTexture*>(info.depth) : nullptr;
    if (info.resolve != nullptr) {
        // RenderPassInfo::resolve maps to VkRenderingAttachmentInfo's
        // resolveImageView, which is the divergence RHI #18 was designed
        // around - but nothing offscreen is multisampled yet, so it is named
        // rather than written against a case no gate covers.
        not_implemented("begin_render_pass with a resolve target");
    }

    const u32 w =
        pass_color_ ? pass_color_->width() : (depth ? depth->width() : device_.width());
    const u32 h =
        pass_color_ ? pass_color_->height() : (depth ? depth->height() : device_.height());

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (pass_color_ != nullptr) {
        color_attachment.imageView = pass_color_->view();
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = info.clear_color_target ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                          : VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue.color = {{info.clear_color.r, info.clear_color.g,
            info.clear_color.b, info.clear_color.a}};
    }

    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depth != nullptr) {
        depth_attachment.imageView = depth->view();
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = info.clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                   : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        // Follows the device's convention, exactly as the D3D12 backend does.
        // Reversed-Z clears to 0; clearing to 1 while the compare says Greater
        // rejects every fragment, silently.
        depth_attachment.clearValue.depthStencil.depth =
            device_.depth_convention() == DepthConvention::Reversed ? 0.f : 1.f;
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {w, h};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = pass_color_ != nullptr ? 1u : 0u;
    rendering.pColorAttachments = pass_color_ != nullptr ? &color_attachment : nullptr;
    rendering.pDepthAttachment = depth != nullptr ? &depth_attachment : nullptr;
    vkCmdBeginRendering(device_.cmd(), &rendering);

    // The Y flip, and the only place it happens. Vulkan's NDC Y points the
    // opposite way from D3D's; a negative viewport height inverts the
    // clip-to-framebuffer transform, so one HLSL source serves both backends.
    // Core since Vulkan 1.1 (VK_KHR_maintenance1). Doing it with -fvk-invert-y
    // instead would put the difference in the shaders, which is the thing
    // worth protecting.
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<f32>(h);
    viewport.width = static_cast<f32>(w);
    viewport.height = -static_cast<f32>(h);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(device_.cmd(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {w, h};
    vkCmdSetScissor(device_.cmd(), 0, 1, &scissor);
}

void VulkanCommandList::end_render_pass() {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndRendering(device_.cmd());
    pass_color_ = nullptr;
}

void VulkanCommandList::set_pipeline(IGraphicsPipeline& pipeline) {
    auto& vk_pipeline = static_cast<VulkanPipeline&>(pipeline);
    bound_pipeline_ = &vk_pipeline;
    bound_stride_ = ~0u;
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    // A pipeline with vertex attributes cannot be bound yet: which VkPipeline
    // it is depends on the stride, and the stride arrives with
    // set_vertex_buffer. One with no attributes has a single variant, so it
    // binds now and a caller that never binds a vertex buffer still works.
    if (vk_pipeline.attribute_count() == 0) {
        const VkPipeline variant = vk_pipeline.variant(0);
        if (variant != VK_NULL_HANDLE) {
            vkCmdBindPipeline(device_.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, variant);
            bound_stride_ = 0;
        }
    }
}

void VulkanCommandList::set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    ENGINE_ASSERT_MSG(bound_pipeline_ != nullptr, "set_constant_buffer with no pipeline bound");
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);

    // A set per bind, out of the pool begin_frame resets. Enough while this
    // device submits and waits inside one frame; under real frames in flight
    // the pool has to be per-slot, which is the parity pass's problem and is
    // deliberately not pretended to be solved here.
    VkDescriptorSetLayout layout = bound_pipeline_->set_layout(0);
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = device_.descriptor_pool();
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vk_failed(vkAllocateDescriptorSets(device_.handle(), &allocate, &set),
            "descriptor set allocation")) {
        return;
    }

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = vk_buffer.handle();
    buffer_info.offset = offset_bytes;
    buffer_info.range = vk_buffer.size() - offset_bytes;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    // The base is the register shift, not zero. See pipeline_vulkan.cpp, which
    // built the layout these bindings have to match.
    write.dstBinding = kBindingBaseUniform + slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);

    vkCmdBindDescriptorSets(device_.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS,
        bound_pipeline_->layout(), 0, 1, &set, 0, nullptr);
}

void VulkanCommandList::draw(u32 vertex_count, u32 start_vertex) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDraw(device_.cmd(), vertex_count, 1, start_vertex, 0);
}

void VulkanCommandList::copy_buffer(IBuffer& src, IBuffer& dst, usize size) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_src = static_cast<VulkanBuffer&>(src);
    auto& vk_dst = static_cast<VulkanBuffer&>(dst);
    ENGINE_ASSERT(size <= vk_src.size() && size <= vk_dst.size());
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(device_.cmd(), vk_src.handle(), vk_dst.handle(), 1, &region);
}

void VulkanCommandList::set_viewport(u32 width, u32 height) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    // Negative height, for the same reason begin_render_pass uses one.
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<f32>(height);
    viewport.width = static_cast<f32>(width);
    viewport.height = -static_cast<f32>(height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(device_.cmd(), 0, 1, &viewport);
}

// ── Not yet implemented ─────────────────────────────────────────────────────



void VulkanCommandList::copy_texture(ITexture& src, ITexture& dst) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_src = static_cast<VulkanTexture&>(src);
    auto& vk_dst = static_cast<VulkanTexture&>(dst);
    // The same asserts the D3D12 side makes. A mismatched copy there is a
    // debug-layer error; here it is a validation error - either way the caller
    // asked for something that cannot mean anything.
    ENGINE_ASSERT(vk_src.width() == vk_dst.width());
    ENGINE_ASSERT(vk_src.height() == vk_dst.height());
    ENGINE_ASSERT(vk_src.format() == vk_dst.format());
    ENGINE_ASSERT(vk_src.array_size() == vk_dst.array_size());
    ENGINE_ASSERT(vk_src.dimension() == vk_dst.dimension());

    const VkImageAspectFlags aspect = aspect_of(vk_src.format());
    VkImageCopy region{};
    region.srcSubresource.aspectMask = aspect;
    region.srcSubresource.layerCount = vk_src.array_size();
    region.dstSubresource.aspectMask = aspect;
    region.dstSubresource.layerCount = vk_dst.array_size();
    region.extent = {vk_src.width(), vk_src.height(), 1};
    // The caller owns the states, as it does on the other backend: CopySrc and
    // CopyDst are what ResourceState says, and the layouts follow from them.
    vkCmdCopyImage(device_.cmd(), vk_src.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vk_dst.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}




void VulkanCommandList::set_compute_pipeline(IComputePipeline& pipeline) {
    (void)pipeline;
    not_implemented("set_compute_pipeline");
}

void VulkanCommandList::set_vertex_buffer(
    u32 slot, IBuffer& buffer, u32 stride_bytes, usize offset_bytes) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);
    // Where the stride difference is absorbed. Vulkan bakes the stride into the
    // pipeline and the contract supplies it here, so this is the first moment
    // the right VkPipeline is knowable - see the note at the top of
    // pipeline_vulkan.cpp for why it cannot live on the desc.
    if (bound_pipeline_ != nullptr && stride_bytes != bound_stride_) {
        const VkPipeline variant = bound_pipeline_->variant(stride_bytes);
        if (variant == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(device_.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, variant);
        bound_stride_ = stride_bytes;
    }
    const VkBuffer handle = vk_buffer.handle();
    const VkDeviceSize offset = offset_bytes;
    vkCmdBindVertexBuffers(device_.cmd(), slot, 1, &handle, &offset);
}

void VulkanCommandList::set_index_buffer(IBuffer& buffer, usize offset_bytes) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    auto& vk_buffer = static_cast<VulkanBuffer&>(buffer);
    // UINT32 always. The contract says indices are 32-bit unsigned and has no
    // format parameter; UINT16 here would draw nothing rather than fail, which
    // is what the comment on set_index_buffer exists to prevent.
    vkCmdBindIndexBuffer(device_.cmd(), vk_buffer.handle(), offset_bytes,
        VK_INDEX_TYPE_UINT32);
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


void VulkanCommandList::draw_indexed(
    u32 index_count, u32 start_index, i32 base_vertex, u32 instance_count) {
    if (device_.cmd() == VK_NULL_HANDLE) {
        return;
    }
    // firstInstance is 0, deliberately and permanently. The contract has no
    // start_instance because D3D excludes it from SV_InstanceID while Vulkan
    // includes it in gl_InstanceIndex - so the same shader would read different
    // data on each backend. A batch base offset goes in a constant buffer.
    vkCmdDrawIndexed(device_.cmd(), index_count, instance_count, start_index, base_vertex, 0);
}

void VulkanCommandList::dispatch(u32 group_count_x, u32 group_count_y, u32 group_count_z) {
    (void)group_count_x;
    (void)group_count_y;
    (void)group_count_z;
    not_implemented("dispatch");
}

} // namespace engine::rhi::vulkan
