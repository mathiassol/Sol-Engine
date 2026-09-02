#include "device_vulkan.hpp"

namespace engine::rhi::vulkan {

// resources.hpp's binding contract, implemented.
//
// These bases must equal the register shifts shaders-dxc passes to DXC
// (-fvk-t-shift 16, -fvk-u-shift 32, -fvk-s-shift 48). The default HLSL->SPIR-V
// mapping sends register(xN, spaceM) to set M binding N and ignores the
// register type, so without disjoint ranges b0 and t0 would both be set 0
// binding 0.
//
// The same numbers therefore live in two places. A change here without a change
// there is a shader reading the wrong descriptor, with nothing logged - so both
// sites carry a comment naming the other.
constexpr u32 kBindingBaseUniform = 0;
constexpr u32 kBindingBaseSampledTexture = 16;
constexpr u32 kBindingBaseStorageTexture = 32;
constexpr u32 kBindingBaseSampler = 48;

std::unique_ptr<IGraphicsPipeline> VulkanDevice::create_graphics_pipeline(
    const GraphicsPipelineDesc& desc) {
    (void)desc;
    (void)kBindingBaseUniform;
    (void)kBindingBaseSampledTexture;
    (void)kBindingBaseStorageTexture;
    (void)kBindingBaseSampler;
    not_implemented("create_graphics_pipeline");
    return nullptr;
}

} // namespace engine::rhi::vulkan
