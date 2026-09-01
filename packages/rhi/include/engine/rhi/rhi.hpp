#pragma once

#include <engine/core/types.hpp>

#include <memory>
#include <string_view>

namespace engine::rhi {

enum class GraphicsAPI : u8 {
    None,
    D3D12,
    Vulkan,
    Metal,
};

// Which end of the depth range is "near".
//
// Reversed maps near to 1 and far to 0, which is where a float depth buffer has
// its precision. It is one value and not several because it reaches six places -
// the projection, the depth clear, the depth compare, the shadow comparison
// sampler, the sign of the slope-scaled depth bias, and the shader that samples
// the shadow map - and five of six applied is z-fighting or a black screen with
// no error anywhere. Everything derives from this; nothing decides separately.
enum class DepthConvention : u8 { Standard, Reversed };

struct DeviceDesc {
    void* window_handle = nullptr;
    u32 width  = 0;
    u32 height = 0;
    GraphicsAPI preferred_api = GraphicsAPI::None;
    // 0 = immediate (tear if the swapchain allows it). 1 = wait for vblank.
    u32 present_interval = 1;
    DepthConvention depth_convention = DepthConvention::Standard;
};

class IDevice;
class ISwapchain;
class ICommandList;

// Factory — each rhi-* backend implements this.
class IRHI {
public:
    virtual ~IRHI() = default;

    virtual std::unique_ptr<IDevice> create_device(const DeviceDesc& desc) = 0;
    virtual std::string_view name() const = 0;
    virtual GraphicsAPI api() const = 0;
};

} // namespace engine::rhi
