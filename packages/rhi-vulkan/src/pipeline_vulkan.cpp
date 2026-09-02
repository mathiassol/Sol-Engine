#include "device_vulkan.hpp"

#include <vector>

namespace engine::rhi::vulkan {

// resources.hpp's binding contract, implemented. This file is the answer to the
// question the whole pass exists to ask: is "how many of each kind" enough for
// a backend that wants an explicit layout?
//
// The binding bases live in device_vulkan.hpp, because commands_vulkan.cpp
// writes descriptors at the same numbers this file builds the layout from.

namespace {

VkCompareOp to_vulkan_compare(DepthTest test) {
    switch (test) {
    case DepthTest::Disabled:     return VK_COMPARE_OP_ALWAYS;
    case DepthTest::Less:         return VK_COMPARE_OP_LESS;
    case DepthTest::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case DepthTest::Equal:        return VK_COMPARE_OP_EQUAL;
    // Reversed-Z needs no adjustment anywhere in this file: both APIs put clip
    // depth in [0, 1], so RHI #15 transfers to a second backend for free. The
    // only coordinate difference is Y, and that is a viewport sign.
    case DepthTest::Greater:      return VK_COMPARE_OP_GREATER;
    case DepthTest::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkCullModeFlags to_vulkan_cull(CullMode cull) {
    switch (cull) {
    case CullMode::None:  return VK_CULL_MODE_NONE;
    case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkShaderModule make_module(VkDevice device, std::span<const u8> bytecode) {
    if (bytecode.empty()) {
        return VK_NULL_HANDLE;
    }
    if (bytecode.size() % 4 != 0) {
        log(LogLevel::Error, LogChannel::Render,
            "SPIR-V length is not a multiple of 4 - this is probably DXIL");
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytecode.size();
    // pCode is u32*, and a SPIR-V blob from DXC is 4-byte aligned in the vector
    // it arrived in. Casting rather than copying is safe here for that reason,
    // and the length check above is what makes it checkable.
    info.pCode = reinterpret_cast<const u32*>(bytecode.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vk_failed(vkCreateShaderModule(device, &info, nullptr, &module), "shader module")) {
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace

std::unique_ptr<IGraphicsPipeline> VulkanDevice::create_graphics_pipeline(
    const GraphicsPipelineDesc& desc) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }
    if (desc.attribute_count > 0) {
        not_implemented("create_graphics_pipeline with vertex attributes");
        return nullptr;
    }
    if (desc.storage_buffer_count > 0) {
        // Structured buffers live in register space 1 on D3D12 and would be
        // descriptor set 1 here. Nothing offscreen uses one, so it is named
        // rather than written blind against a shader that cannot test it.
        not_implemented("create_graphics_pipeline with storage buffers");
        return nullptr;
    }

    // ── The counts, become a layout ─────────────────────────────────────────
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkSampler> immutable_samplers;

    for (u32 i = 0; i < desc.uniform_buffer_count; ++i) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = kBindingBaseUniform + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(binding);
    }
    for (u32 i = 0; i < desc.sampled_texture_count; ++i) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = kBindingBaseSampledTexture + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = 1;
        // Fragment only, deliberately. The contract calls this asymmetry out -
        // storage buffers are visible to every stage and sampled textures are
        // not - and says a backend must preserve it, because shaders depend on
        // it. Widening it here would make one backend accept a shader the other
        // rejects.
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(binding);
    }
    if (desc.sampler_count > 0) {
        // Immutable samplers, the contract's other named asymmetry: fixed at
        // pipeline creation from SamplerDesc, never bound per draw. There is
        // deliberately no set_sampler on ICommandList.
        not_implemented("create_graphics_pipeline with immutable samplers");
        return nullptr;
    }

    VkDescriptorSetLayoutCreateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_info.bindingCount = static_cast<u32>(bindings.size());
    set_info.pBindings = bindings.empty() ? nullptr : bindings.data();
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    if (vk_failed(vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout),
            "descriptor set layout")) {
        return nullptr;
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vk_failed(vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout),
            "pipeline layout")) {
        vkDestroyDescriptorSetLayout(device_, set_layout, nullptr);
        return nullptr;
    }

    // ── Stages ──────────────────────────────────────────────────────────────
    const VkShaderModule vertex = make_module(device_, desc.vertex_shader);
    const VkShaderModule fragment = make_module(device_, desc.pixel_shader);
    auto cleanup_modules = [&] {
        if (vertex != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertex, nullptr);
        if (fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragment, nullptr);
    };
    if (vertex == VK_NULL_HANDLE || fragment == VK_NULL_HANDLE) {
        cleanup_modules();
        vkDestroyPipelineLayout(device_, layout, nullptr);
        vkDestroyDescriptorSetLayout(device_, set_layout, nullptr);
        return nullptr;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    // DXC names the SPIR-V entry point after the HLSL one, so "vs_main" and
    // "ps_main" survive the translation and there is nothing to remap.
    stages[0].pName = "vs_main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "ps_main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = desc.topology == PrimitiveTopology::LineList
        ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic, set by begin_render_pass against the
    // target's own extent - which is also where the Y flip lives.
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<u32>(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = to_vulkan_cull(desc.cull);
    // Clockwise, matching D3D12's default front face. The negative viewport
    // height flips Y in framebuffer space but not the winding the rasteriser
    // uses, so leaving this at Vulkan's counter-clockwise default would cull
    // exactly the triangles D3D12 keeps.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.f;
    if (desc.slope_scaled_depth_bias != 0.f) {
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasSlopeFactor = desc.slope_scaled_depth_bias;
    }

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(
        desc.sample_count == 0 ? 1u : desc.sample_count);

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth != DepthTest::Disabled ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth != DepthTest::Disabled && desc.depth_write
        ? VK_TRUE
        : VK_FALSE;
    depth.depthCompareOp = to_vulkan_compare(desc.depth);

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blend == BlendMode::Alpha) {
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    const bool has_color = desc.color_format != Format::Unknown;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = has_color ? 1u : 0u;
    blend.pAttachments = has_color ? &blend_attachment : nullptr;

    // Dynamic rendering: the attachment formats live on the pipeline instead of
    // in a VkRenderPass object, which is what lets begin_render_pass take a
    // RenderPassInfo and need no render-pass cache behind it.
    const VkFormat color_format = to_vulkan_format(desc.color_format);
    const VkFormat depth_format = to_vulkan_format(desc.depth_format);
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = has_color ? 1u : 0u;
    rendering.pColorAttachmentFormats = has_color ? &color_format : nullptr;
    rendering.depthAttachmentFormat = depth_format;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const bool failed = vk_failed(
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
        "graphics pipeline");
    // Modules are consumed by pipeline creation and can go immediately, pass or
    // fail - unlike the layouts, which the pipeline object owns from here.
    cleanup_modules();
    if (failed) {
        vkDestroyPipelineLayout(device_, layout, nullptr);
        vkDestroyDescriptorSetLayout(device_, set_layout, nullptr);
        return nullptr;
    }

    return std::make_unique<VulkanPipeline>(
        device_, pipeline, layout, set_layout, desc.uniform_buffer_count);
}

} // namespace engine::rhi::vulkan
