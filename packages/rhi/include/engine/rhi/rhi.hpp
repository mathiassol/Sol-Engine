#pragma once

#include <engine/core/types.hpp>

#include <memory>
#include <string_view>

namespace engine::rhi {

// A clear colour, and the only place the RHI names one.
//
// Lives here rather than in commands.hpp because both commands.hpp and
// resources.hpp need it - a render pass says what to clear to, and a texture
// says what it was created expecting - and commands.hpp includes
// resources.hpp, so the dependency only runs one way.
struct Color4 {
    f32 r = 0.f;
    f32 g = 0.f;
    f32 b = 0.f;
    f32 a = 1.f;
};

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
    // Null means an offscreen device: no surface, no swapchain, no
    // presentation, everything else identical. swapchain(), swapchain_color()
    // and swapchain_depth() are then programming errors and assert by name - a
    // null-object swapchain whose present() quietly does nothing is the failure
    // mode this engine is built to avoid.
    //
    // Beyond a second backend this is real capability: a GPU gate that needs no
    // window, on a machine or a runner that has none.
    void* window_handle = nullptr;
    u32 width  = 0;
    u32 height = 0;
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
