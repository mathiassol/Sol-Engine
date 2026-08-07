#pragma once

#include <engine/rhi/device.hpp>
#include <engine/rhi/rhi.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace engine::rhi::d3d12 {

class D3D12Device;

class D3D12Buffer final : public IBuffer {
public:
    D3D12Buffer(ID3D12Resource* resource, usize size);
    ~D3D12Buffer() override;

    usize size() const override;
    ID3D12Resource* resource() const;

private:
    ID3D12Resource* resource_ = nullptr;
    usize size_ = 0;
};

class D3D12Pipeline final : public IGraphicsPipeline {
public:
    D3D12Pipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig);
    ~D3D12Pipeline() override;

    ID3D12PipelineState* pso() const;
    ID3D12RootSignature* root_signature() const;

private:
    ID3D12PipelineState* pso_ = nullptr;
    ID3D12RootSignature* root_sig_ = nullptr;
};

class D3D12CommandList final : public ICommandList {
public:
    explicit D3D12CommandList(D3D12Device& device);

    void begin() override;
    void end() override;
    void begin_render_pass(const RenderPassInfo& info) override;
    void end_render_pass() override;
    void set_viewport(u32 width, u32 height) override;
    void set_pipeline(IGraphicsPipeline& pipeline) override;
    void set_vertex_buffer(u32 slot, IBuffer& buffer, u32 stride_bytes, usize offset_bytes) override;
    void set_index_buffer(IBuffer& buffer, usize offset_bytes) override;
    void set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) override;
    void draw(u32 vertex_count, u32 start_vertex) override;
    void draw_indexed(u32 index_count, u32 start_index, i32 base_vertex) override;

private:
    D3D12Device& device_;
    bool recording_ = false;
    bool in_pass_   = false;
};

class D3D12Swapchain final : public ISwapchain {
public:
    explicit D3D12Swapchain(D3D12Device& device);

    void present() override;
    u32 current_back_buffer_index() const override;

private:
    D3D12Device& device_;
};

class D3D12Device final : public IDevice {
public:
    D3D12Device();
    ~D3D12Device() override;

    bool init(const DeviceDesc& desc);

    ISwapchain& swapchain() override;
    ICommandList& command_list() override;

    void begin_frame() override;
    void end_frame() override;
    bool resize(u32 width, u32 height) override;
    void wait_idle() override;

    u32 width() const override;
    u32 height() const override;

    std::unique_ptr<IBuffer> create_buffer(const BufferDesc& desc, const void* data) override;
    void write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) override;

    std::unique_ptr<IGraphicsPipeline> create_graphics_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) override;
    std::unique_ptr<IGraphicsPipeline> create_forward_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) override;
    std::unique_ptr<IGraphicsPipeline> create_overlay_pipeline(
        std::span<const u8> vertex_shader,
        std::span<const u8> pixel_shader) override;

    ID3D12GraphicsCommandList* d3d12_cmd_list() { return cmd_list_; }
    D3D12_CPU_DESCRIPTOR_HANDLE current_rtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE current_dsv() const;
    u32 frame_index() const;
    void present();

private:
    bool create_swapchain();
    bool create_render_targets();
    bool create_depth_buffer();
    bool create_frame_resources();
    void cleanup_swapchain_resources();
    void cleanup_depth_buffer();
    void wait_for_frame(u32 index);
    void wait_for_gpu();

    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    IDXGISwapChain3* swapchain_ = nullptr;

    ID3D12DescriptorHeap* rtv_heap_ = nullptr;
    UINT rtv_descriptor_size_ = 0;
    ID3D12Resource* render_targets_[3]{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles_[3]{};

    ID3D12DescriptorHeap* dsv_heap_ = nullptr;
    ID3D12Resource* depth_buffer_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle_{};

    ID3D12CommandAllocator* frame_allocators_[3]{};
    ID3D12GraphicsCommandList* cmd_list_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_values_[3]{};

    u32 frame_index_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
    HWND hwnd_ = nullptr;

    D3D12CommandList cmd_wrapper_;
    D3D12Swapchain swapchain_wrapper_;
};

} // namespace engine::rhi::d3d12
