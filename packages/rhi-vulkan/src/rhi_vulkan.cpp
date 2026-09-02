#include <engine/rhi/vulkan/rhi_vulkan.hpp>

#include "device_vulkan.hpp"

namespace engine::rhi::vulkan {

namespace {

class VulkanRHI final : public IRHI {
public:
    std::unique_ptr<IDevice> create_device(const DeviceDesc& desc) override {
        auto device = std::make_unique<VulkanDevice>();
        if (!device->init(desc)) {
            // Null rather than a half-built device. The caller decides whether
            // an absent Vulkan device is a skip or a failure, and it cannot
            // decide that about an object that exists but does not work.
            return nullptr;
        }
        return device;
    }

    std::string_view name() const override { return "Vulkan"; }
    GraphicsAPI api() const override { return GraphicsAPI::Vulkan; }
};

} // namespace

std::unique_ptr<IRHI> create_rhi() { return std::make_unique<VulkanRHI>(); }

} // namespace engine::rhi::vulkan
