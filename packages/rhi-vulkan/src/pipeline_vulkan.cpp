#include "device_vulkan.hpp"

#include <vector>

namespace engine::rhi::vulkan {

// resources.hpp's binding contract, implemented. This file is the answer to the
// question RHI #12 existed to ask: is "how many of each kind" enough for a
// backend that wants an explicit layout?
//
// The binding bases live in device_vulkan.hpp, because commands_vulkan.cpp
// writes descriptors at the same numbers this file builds the layout from.
//
// ── The vertex stride, and why this file has a cache ─────────────────────────
//
// Vulkan bakes the vertex stride into the pipeline; D3D12 takes it at bind
// time. The contract follows D3D12 - `set_vertex_buffer(slot, buffer, stride)` -
// and it cannot do otherwise, because the engine's stride is per-batch runtime
// data (`render_graph.cpp` binds `batch.vertex_stride`) while the pipeline is
// created once at startup. There is nowhere on GraphicsPipelineDesc for it to
// live.
//
// Nor can it be derived from the attributes: the shadow pipeline declares one
// attribute (position) and binds the same 32-byte vertex the forward pipeline
// does, so max(offset + size) would give 12 and read every third vertex.
//
// So the difference is absorbed here, which is what a backend is for.
// `create_graphics_pipeline` builds a recipe and no VkPipeline; the first bind
// that knows a stride creates the variant for it and caches it. In this engine
// that is two or three variants for the whole run.
//
// The alternative was VK_EXT_vertex_input_dynamic_state, which makes the stride
// dynamic state and matches D3D12 exactly. It was not taken because it narrows
// the device requirement below Vulkan 1.3 core for a cache this small.

namespace {

VkFormat to_vulkan_attribute_format(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

const char* semantic_name(VertexSemantic semantic) {
    switch (semantic) {
    case VertexSemantic::Position: return "POSITION";
    case VertexSemantic::Normal:   return "NORMAL";
    case VertexSemantic::Color:    return "COLOR";
    case VertexSemantic::TexCoord: return "TEXCOORD";
    }
    return "?";
}

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

VkFilter to_vulkan_filter(FilterMode filter) {
    return filter == FilterMode::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode to_vulkan_address(AddressMode address) {
    switch (address) {
    case AddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::Clamp:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkCompareOp to_vulkan_sampler_compare(CompareOp compare) {
    switch (compare) {
    case CompareOp::Never:        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:         return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

VkShaderModule make_module(VkDevice device, std::span<const u8> bytecode) {
    if (bytecode.empty()) {
        return VK_NULL_HANDLE;
    }
    if (!is_spirv(bytecode, "graphics shader")) {
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

VkSampler create_vulkan_sampler(VkDevice device, const SamplerDesc& desc) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = to_vulkan_filter(desc.filter);
    info.minFilter = to_vulkan_filter(desc.filter);
    info.mipmapMode = desc.filter == FilterMode::Point ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                       : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = to_vulkan_address(desc.address);
    info.addressModeV = info.addressModeU;
    info.addressModeW = info.addressModeU;
    info.maxLod = VK_LOD_CLAMP_NONE;
    // Opaque white, matching the D3D12 side. A shadow map's border colour
    // decides whether everything outside the map reads lit or shadowed, so the
    // two backends disagreeing here is a lighting difference, not a detail.
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (desc.compare != CompareOp::Never) {
        info.compareEnable = VK_TRUE;
        info.compareOp = to_vulkan_sampler_compare(desc.compare);
    }
    VkSampler sampler = VK_NULL_HANDLE;
    if (vk_failed(vkCreateSampler(device, &info, nullptr, &sampler), "sampler")) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}

// The counts, become two descriptor set layouts. Set 0 holds everything a draw
// binds per pass; set 1 holds storage buffers, because a StructuredBuffer in
// HLSL register space 1 lands in descriptor set 1 - measured, and exactly what
// the binding contract predicted.
bool build_set_layouts(VkDevice device, const GraphicsPipelineDesc& desc,
    std::vector<VkSampler>& owned_samplers, VkDescriptorSetLayout& set0,
    VkDescriptorSetLayout& set1) {
    std::vector<VkDescriptorSetLayoutBinding> bindings0;
    for (u32 i = 0; i < desc.uniform_buffer_count; ++i) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = kBindingBaseUniform + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings0.push_back(binding);
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
        bindings0.push_back(binding);
    }
    // Immutable samplers, the contract's other named asymmetry: fixed at
    // pipeline creation from SamplerDesc, never bound per draw. That is exactly
    // pImmutableSamplers, so the rule the contract states is the rule Vulkan
    // already has - and it is why there is no set_sampler on ICommandList.
    for (u32 i = 0; i < desc.sampler_count; ++i) {
        const VkSampler sampler = create_vulkan_sampler(device, desc.samplers[i]);
        if (sampler == VK_NULL_HANDLE) {
            return false;
        }
        owned_samplers.push_back(sampler);
    }
    for (u32 i = 0; i < desc.sampler_count; ++i) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = kBindingBaseSampler + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = &owned_samplers[i];
        bindings0.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo info0{};
    info0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info0.bindingCount = static_cast<u32>(bindings0.size());
    info0.pBindings = bindings0.empty() ? nullptr : bindings0.data();
    if (vk_failed(vkCreateDescriptorSetLayout(device, &info0, nullptr, &set0),
            "descriptor set layout 0")) {
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings1;
    for (u32 i = 0; i < desc.storage_buffer_count; ++i) {
        VkDescriptorSetLayoutBinding binding{};
        // The t-shift, because a StructuredBuffer is a `t` register on D3D12.
        binding.binding = kBindingBaseSampledTexture + i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        // Every stage, which is the asymmetry the contract insists on.
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        bindings1.push_back(binding);
    }
    // Always created, even when empty: a pipeline layout with a gap at set 0
    // and a set at 1 is illegal, so set 1 exists either way and is simply
    // unused when nothing declares a storage buffer.
    VkDescriptorSetLayoutCreateInfo info1{};
    info1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info1.bindingCount = static_cast<u32>(bindings1.size());
    info1.pBindings = bindings1.empty() ? nullptr : bindings1.data();
    if (vk_failed(vkCreateDescriptorSetLayout(device, &info1, nullptr, &set1),
            "descriptor set layout 1")) {
        vkDestroyDescriptorSetLayout(device, set0, nullptr);
        return false;
    }
    return true;
}

std::unique_ptr<IGraphicsPipeline> VulkanDevice::create_graphics_pipeline(
    const GraphicsPipelineDesc& desc) {
    if (device_ == VK_NULL_HANDLE) {
        return nullptr;
    }

    PipelineRecipe recipe{};
    for (u32 i = 0; i < desc.attribute_count; ++i) {
        const VertexAttribute& attribute = desc.attributes[i];
        // Location is the attribute's index, not a lookup from its semantic.
        // DXC assigns SPIR-V Locations in the declaration order of the shader's
        // input struct, and every attribute array in this engine is filled in
        // that same order - measured, not assumed (see the RHI #24 spec).
        //
        // The equivalence breaks the moment a semantic index is non-zero:
        // TEXCOORD1 is its own location, so array order stops matching
        // declaration order. Nothing in the tree uses one, and this is what
        // stops the first one being silent.
        if (attribute.semantic_index != 0) {
            char message[208];
            std::snprintf(message, sizeof(message),
                "create_graphics_pipeline: attribute %u is %s%u - a non-zero semantic index "
                "breaks the array-order-is-location rule this backend relies on",
                i, semantic_name(attribute.semantic), attribute.semantic_index);
            log(LogLevel::Error, LogChannel::Render, message);
            return nullptr;
        }
        recipe.attributes[i].location = i;
        recipe.attributes[i].binding = 0;
        recipe.attributes[i].format = to_vulkan_attribute_format(attribute.format);
        recipe.attributes[i].offset = attribute.offset;
    }
    recipe.attribute_count = desc.attribute_count;
    recipe.topology = desc.topology == PrimitiveTopology::LineList
        ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    recipe.cull = to_vulkan_cull(desc.cull);
    recipe.blend_alpha = desc.blend == BlendMode::Alpha;
    recipe.depth_test = desc.depth != DepthTest::Disabled;
    recipe.depth_write = recipe.depth_test && desc.depth_write;
    recipe.depth_compare = to_vulkan_compare(desc.depth);
    recipe.depth_bias = desc.slope_scaled_depth_bias;
    recipe.sample_count = desc.sample_count == 0 ? 1u : desc.sample_count;
    recipe.color_format = to_vulkan_format(desc.color_format);
    recipe.depth_format = to_vulkan_format(desc.depth_format);

    std::vector<VkSampler> owned_samplers;
    VkDescriptorSetLayout set0 = VK_NULL_HANDLE;
    VkDescriptorSetLayout set1 = VK_NULL_HANDLE;
    if (!build_set_layouts(device_, desc, owned_samplers, set0, set1)) {
        for (VkSampler sampler : owned_samplers) {
            vkDestroySampler(device_, sampler, nullptr);
        }
        return nullptr;
    }

    const VkDescriptorSetLayout set_layouts[2] = {set0, set1};
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 2;
    layout_info.pSetLayouts = set_layouts;
    if (vk_failed(vkCreatePipelineLayout(device_, &layout_info, nullptr, &recipe.layout),
            "pipeline layout")) {
        vkDestroyDescriptorSetLayout(device_, set1, nullptr);
        vkDestroyDescriptorSetLayout(device_, set0, nullptr);
        for (VkSampler sampler : owned_samplers) {
            vkDestroySampler(device_, sampler, nullptr);
        }
        return nullptr;
    }

    // Kept alive for the pipeline's lifetime, unlike the usual advice to
    // destroy them straight after creation: a stride variant created later
    // needs them again.
    recipe.vertex = make_module(device_, desc.vertex_shader);
    recipe.fragment = make_module(device_, desc.pixel_shader);
    recipe.vertex_entry = spirv_entry_point(desc.vertex_shader, "vs_main");
    recipe.fragment_entry = spirv_entry_point(desc.pixel_shader, "ps_main");
    if (recipe.vertex == VK_NULL_HANDLE
        || (!desc.pixel_shader.empty() && recipe.fragment == VK_NULL_HANDLE)) {
        if (recipe.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, recipe.vertex, nullptr);
        }
        vkDestroyPipelineLayout(device_, recipe.layout, nullptr);
        vkDestroyDescriptorSetLayout(device_, set1, nullptr);
        vkDestroyDescriptorSetLayout(device_, set0, nullptr);
        for (VkSampler sampler : owned_samplers) {
            vkDestroySampler(device_, sampler, nullptr);
        }
        return nullptr;
    }

    auto pipeline = std::make_unique<VulkanPipeline>(device_, recipe, set0, set1,
        std::move(owned_samplers), desc.uniform_buffer_count, desc.storage_buffer_count);
    // A pipeline with no vertex attributes has one variant and needs no stride,
    // so create it now and fail loudly here rather than at the first draw.
    if (desc.attribute_count == 0 && pipeline->variant(0) == VK_NULL_HANDLE) {
        return nullptr;
    }
    return pipeline;
}

VkPipeline VulkanPipeline::variant(u32 stride) {
    const auto found = variants_.find(stride);
    if (found != variants_.end()) {
        return found->second;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    u32 stage_count = 0;
    stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[stage_count].module = recipe_.vertex;
    stages[stage_count].pName = recipe_.vertex_entry.c_str();
    ++stage_count;
    if (recipe_.fragment != VK_NULL_HANDLE) {
        stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage_count].module = recipe_.fragment;
        stages[stage_count].pName = recipe_.fragment_entry.c_str();
        ++stage_count;
    }

    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;
    binding_desc.stride = stride;
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (recipe_.attribute_count > 0) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding_desc;
        vertex_input.vertexAttributeDescriptionCount = recipe_.attribute_count;
        vertex_input.pVertexAttributeDescriptions = recipe_.attributes;
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = recipe_.topology;

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
    raster.cullMode = recipe_.cull;
    // Clockwise, matching D3D12's default front face. The negative viewport
    // height flips Y in framebuffer space but not the winding the rasteriser
    // uses, so Vulkan's counter-clockwise default would cull exactly the
    // triangles D3D12 keeps.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.f;
    if (recipe_.depth_bias != 0.f) {
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasSlopeFactor = recipe_.depth_bias;
    }

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(recipe_.sample_count);

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = recipe_.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = recipe_.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = recipe_.depth_compare;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (recipe_.blend_alpha) {
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    const bool has_color = recipe_.color_format != VK_FORMAT_UNDEFINED;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = has_color ? 1u : 0u;
    blend.pAttachments = has_color ? &blend_attachment : nullptr;

    // Dynamic rendering: the attachment formats live on the pipeline instead of
    // in a VkRenderPass object, which is what lets begin_render_pass take a
    // RenderPassInfo and need no render-pass cache behind it.
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = has_color ? 1u : 0u;
    rendering.pColorAttachmentFormats = has_color ? &recipe_.color_format : nullptr;
    rendering.depthAttachmentFormat = recipe_.depth_format;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = recipe_.layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vk_failed(
            vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
            "graphics pipeline")) {
        return VK_NULL_HANDLE;
    }
    variants_.emplace(stride, pipeline);
    return pipeline;
}

} // namespace engine::rhi::vulkan
