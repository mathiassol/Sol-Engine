#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/resources.hpp>

#include <memory>
#include <string_view>

namespace engine::rhi {

class IBuffer;
class IGraphicsPipeline;
class IComputePipeline;
class ISampler;

struct GpuMemoryStats {
    u64 local_usage_bytes  = 0;
    u64 local_budget_bytes = 0;
};

// Packed like the D3D12 enums (no graphics API in this header).
// Feature Level 11_0 = 0xB000. Shader Model 6.0 = 0x60.
inline constexpr u32 kGpuFeatureLevel_11_0 = 0xB000;
inline constexpr u32 kGpuShaderModel_6_0 = 0x60;

struct GpuBaseline {
    u32 feature_level = 0;
    u32 shader_model = 0;
};

// Slice of the per-frame upload ring. Valid until the next begin_frame of this slot.
struct FrameAllocation {
    IBuffer* buffer = nullptr;
    usize offset = 0;
};

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

    // Frame bookkeeping only: wait the in-flight slot, reset the allocator,
    // submit, and present. Does not transition the swapchain. The render graph
    // (or one equivalent presenter) owns PRESENT ↔ RenderTarget / Copy.
    virtual void begin_frame() = 0;
    virtual void submit() = 0;
    virtual void end_frame() = 0;
    virtual void set_present_interval(u32 interval) = 0;
    virtual u32 present_interval() const = 0;

    virtual bool resize(u32 width, u32 height) = 0;
    virtual void wait_idle() = 0;

    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
    // D3D12 backbuffer / allocator ring index. Distinct from FrameContext::cpu_frame_slot.
    virtual u32 frame_slot() const = 0;

    virtual std::unique_ptr<IBuffer> create_buffer(const BufferDesc& desc, const void* data = nullptr) = 0;
    virtual std::unique_ptr<ITexture> create_texture(const TextureDesc& desc, const void* data = nullptr) = 0;
    // Bump-allocates 256-byte-aligned upload memory for this frame_slot(). GPU-safe
    // because begin_frame waits that slot before resetting the bump pointer.
    virtual FrameAllocation alloc_frame_memory(usize size) = 0;
    virtual void write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) = 0;
    virtual void read_buffer(IBuffer& buffer, usize offset, void* data, usize size) = 0;
    virtual void set_debug_name(IBuffer& buffer, std::string_view name) = 0;
    virtual void set_debug_name(ITexture& texture, std::string_view name) = 0;
    virtual ITexture& swapchain_color() = 0;
    virtual ITexture& swapchain_depth() = 0;
    virtual GpuMemoryStats gpu_memory_stats() const = 0;
    virtual GpuBaseline gpu_baseline() const = 0;
    virtual f32 last_gpu_time_ms() const = 0;

    virtual std::unique_ptr<IGraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<IComputePipeline> create_compute_pipeline(
        const ComputePipelineDesc& desc) = 0;
    virtual std::unique_ptr<ISampler> create_sampler(const SamplerDesc& desc) = 0;
};

} // namespace engine::rhi
