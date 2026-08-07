#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/resources.hpp>

#include <memory>
#include <span>

namespace engine::rhi {

class IBuffer;
class IGraphicsPipeline;

class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    virtual void present() = 0;
    virtual u32 current_back_buffer_index() const = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual ISwapchain& swapchain() = 0;
    virtual ICommandList& command_list() = 0;

    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    virtual bool resize(u32 width, u32 height) = 0;
    virtual void wait_idle() = 0;

    virtual u32 width() const = 0;
    virtual u32 height() const = 0;

    virtual std::unique_ptr<IBuffer> create_buffer(const BufferDesc& desc, const void* data = nullptr) = 0;
    virtual void write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) = 0;

    virtual std::unique_ptr<IGraphicsPipeline> create_graphics_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) = 0;
    virtual std::unique_ptr<IGraphicsPipeline> create_forward_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) = 0;
    virtual std::unique_ptr<IGraphicsPipeline> create_overlay_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) = 0;
};

} // namespace engine::rhi
