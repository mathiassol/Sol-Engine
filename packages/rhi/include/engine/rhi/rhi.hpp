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

struct DeviceDesc {
    void* window_handle = nullptr;
    u32 width  = 0;
    u32 height = 0;
    GraphicsAPI preferred_api = GraphicsAPI::None;
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
