#pragma once

#include <engine/rhi/device.hpp>
#include <engine/rhi/rhi.hpp>

#include "com_ptr.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <memory>
#include <string_view>
#include <vector>

namespace engine::rhi::d3d12 {

class D3D12Device;

class D3D12Buffer final : public IBuffer {
public:
    D3D12Buffer(D3D12Device* device, ID3D12Resource* resource, usize size, bool cpu_visible,
        bool persist_map = true);
    ~D3D12Buffer() override;

    usize size() const override;
    ID3D12Resource* resource() const;
    bool cpu_visible() const;
    void* mapped() const;
    void set_debug_name(std::string_view name);

private:
    D3D12Device* device_ = nullptr;
    ID3D12Resource* resource_ = nullptr;
    void* mapped_ = nullptr;
    usize size_ = 0;
    bool cpu_visible_ = false;
};

class D3D12Texture final : public ITexture {
public:
    D3D12Texture() = default;
    D3D12Texture(D3D12Device* device, ID3D12Resource* resource, u32 width, u32 height,
        Format format, ID3D12DescriptorHeap* view_heap, D3D12_CPU_DESCRIPTOR_HANDLE view,
        bool is_depth, bool owns_resource, D3D12_GPU_DESCRIPTOR_HANDLE gpu_view = {},
        D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu = {}, u32 mip_levels = 1, u32 array_size = 1,
        TextureDimension dimension = TextureDimension::Tex2D);
    ~D3D12Texture() override;

    D3D12Texture(const D3D12Texture&) = delete;
    D3D12Texture& operator=(const D3D12Texture&) = delete;

    void bind_external(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE view,
        u32 width, u32 height, Format format, bool is_depth);
    void detach();

    u32 width() const override;
    u32 height() const override;
    u32 mip_levels() const override;
    u32 array_size() const override;
    TextureDimension dimension() const override;
    Format format() const override;

    ID3D12Resource* resource() const;
    D3D12_CPU_DESCRIPTOR_HANDLE view() const;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_view() const;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu() const;
    void set_srv(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpu);
    ID3D12DescriptorHeap* descriptor_heap() const;
    bool is_depth() const;
    void set_debug_name(std::string_view name);

private:
    D3D12Device* device_ = nullptr;
    ID3D12Resource* resource_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> view_heap_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE view_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_view_{};
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_{};
    u32 width_ = 0;
    u32 height_ = 0;
    u32 mip_levels_ = 1;
    u32 array_size_ = 1;
    TextureDimension dimension_ = TextureDimension::Tex2D;
    Format format_ = Format::Unknown;
    bool is_depth_ = false;
    bool owns_resource_ = false;
};

class D3D12Sampler final : public ISampler {
public:
    D3D12Sampler(
        ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpu, const SamplerDesc& desc);
    ~D3D12Sampler() override = default;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle() const { return cpu_; }
    const SamplerDesc& desc() const { return desc_; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_{};
    SamplerDesc desc_{};
};

class D3D12Pipeline final : public IGraphicsPipeline {
public:
    D3D12Pipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig, u32 srv_table_root,
        u32 structured_root, D3D12_PRIMITIVE_TOPOLOGY topology);
    ~D3D12Pipeline() override;

    ID3D12PipelineState* pso() const;
    ID3D12RootSignature* root_signature() const;
    u32 srv_table_root() const;
    u32 structured_root() const;
    D3D12_PRIMITIVE_TOPOLOGY topology() const;

private:
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12RootSignature> root_sig_;
    u32 srv_table_root_ = ~0u;
    u32 structured_root_ = ~0u;
    D3D12_PRIMITIVE_TOPOLOGY topology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

class D3D12ComputePipeline final : public IComputePipeline {
public:
    D3D12ComputePipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig,
        u32 uav_table_root, u32 srv_table_root);
    ~D3D12ComputePipeline() override;

    ID3D12PipelineState* pso() const;
    ID3D12RootSignature* root_signature() const;
    u32 uav_table_root() const;
    u32 srv_table_root() const;

private:
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12RootSignature> root_sig_;
    u32 uav_table_root_ = ~0u;
    u32 srv_table_root_ = ~0u;
};

class D3D12CommandList final : public ICommandList {
public:
    explicit D3D12CommandList(D3D12Device& device);

    void begin() override;
    void end() override;
    void begin_render_pass(const RenderPassInfo& info) override;
    void end_render_pass() override;
    void transition(ITexture& texture, ResourceState from, ResourceState to) override;
    void transition(IBuffer& buffer, ResourceState from, ResourceState to) override;
    void copy_texture(ITexture& src, ITexture& dst) override;
    void copy_buffer(IBuffer& src, IBuffer& dst, usize size) override;
    void set_viewport(u32 width, u32 height) override;
    void set_pipeline(IGraphicsPipeline& pipeline) override;
    void set_compute_pipeline(IComputePipeline& pipeline) override;
    void set_vertex_buffer(
        u32 slot, IBuffer& buffer, u32 stride_bytes, usize offset_bytes) override;
    void set_index_buffer(IBuffer& buffer, usize offset_bytes) override;
    void set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) override;
    void set_shader_resource(u32 slot, ITexture& texture) override;
    void set_unordered_access(u32 slot, IBuffer& buffer) override;
    void set_unordered_access(u32 slot, ITexture& texture) override;
    void set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) override;
    void draw(u32 vertex_count, u32 start_vertex) override;
    void draw_indexed(u32 index_count, u32 start_index, i32 base_vertex,
        u32 instance_count) override;
    void dispatch(u32 group_count_x, u32 group_count_y, u32 group_count_z) override;
    void begin_event(std::string_view name) override;
    void end_event() override;
    void set_marker(std::string_view name) override;
    u32 debug_event_depth() const override;
    std::string_view debug_event_name() const override;
    std::string_view last_debug_marker() const override;

private:
    static constexpr u32 kMaxDebugEvents = 16;
    static constexpr u32 kDebugNameBytes = 64;

    D3D12Device& device_;
    D3D12Pipeline* bound_pipeline_ = nullptr;
    D3D12ComputePipeline* bound_compute_ = nullptr;
    bool recording_ = false;
    bool in_pass_   = false;
    u32 event_depth_ = 0;
    char event_stack_[kMaxDebugEvents][kDebugNameBytes]{};
    char last_marker_[kDebugNameBytes]{};
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

    // Fixed at device creation. Every depth decision in the frame reads this.
    DepthConvention depth_convention() const override { return depth_convention_; }

    ISwapchain& swapchain() override;
    ICommandList& command_list() override;

    void begin_frame() override;
    void submit() override;
    void end_frame() override;
    void set_present_interval(u32 interval) override;
    u32 present_interval() const override;
    bool resize(u32 width, u32 height) override;
    void wait_idle() override;

    u32 width() const override;
    u32 height() const override;
    bool device_lost() const override;

    std::unique_ptr<IBuffer> create_buffer(const BufferDesc& desc, const void* data) override;
    std::unique_ptr<ITexture> create_texture(const TextureDesc& desc, const void* data) override;
    FrameAllocation alloc_frame_memory(usize size) override;
    void write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) override;
    void read_buffer(IBuffer& buffer, usize offset, void* data, usize size) override;
    void set_debug_name(IBuffer& buffer, std::string_view name) override;
    void set_debug_name(ITexture& texture, std::string_view name) override;
    ITexture& swapchain_color() override;
    ITexture& swapchain_depth() override;
    GpuMemoryStats gpu_memory_stats() const override;
    FrameRingStats frame_ring_stats() const override;
    GpuBaseline gpu_baseline() const override;

    void retire_resource(ID3D12Resource* resource);
    bool shutting_down() const { return shutting_down_; }

    std::unique_ptr<IGraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc) override;
    std::unique_ptr<IComputePipeline> create_compute_pipeline(
        const ComputePipelineDesc& desc) override;
    std::unique_ptr<ISampler> create_sampler(const SamplerDesc& desc) override;

    ID3D12GraphicsCommandList* d3d12_cmd_list() { return cmd_list_.get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE current_rtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE current_dsv() const;
    u32 frame_index() const;
    void present_back_buffer();
    f32 last_gpu_time_ms() const override;

    friend class D3D12CommandList;

private:
    bool create_swapchain();
    bool create_render_targets();
    bool create_depth_buffer();
    bool create_frame_resources();
    bool create_timestamp_resources();
    bool create_frame_ring();
    bool create_shader_heap();
    void bind_shader_srv(u32 slot, D3D12_CPU_DESCRIPTOR_HANDLE src, u32 table_root);
    void bind_compute_uav(u32 slot, ID3D12Resource* resource, usize size, u32 table_root);
    void bind_compute_storage_texture(u32 slot, ID3D12Resource* resource, Format format,
        u32 table_root);
    std::unique_ptr<ITexture> create_sampled_texture(const TextureDesc& desc, const void* data);
    std::unique_ptr<ITexture> create_shadow_texture(const TextureDesc& desc);
    std::unique_ptr<ITexture> create_color_shader_resource_texture(const TextureDesc& desc);
    std::unique_ptr<ITexture> create_storage_shader_resource_texture(const TextureDesc& desc);
    void wait_for_copy();
    void begin_copy();
    UINT64 end_copy();
    void cleanup_swapchain_resources();
    void cleanup_depth_buffer();
    bool release_command_list_resource_refs();
    bool device_removed() const;
    u32  next_shader_descriptor();
    void log_device_error(const char* what, HRESULT hr = E_FAIL);
    void report_debug_layer_messages() const;
    void log_resize_failure(const char* what, HRESULT hr, u32 width, u32 height) const;
    void wait_for_frame(u32 index);
    void wait_for_gpu();
    UINT64 signal_queue();
    UINT64 last_submitted_fence() const;
    void flush_retired();
    void read_gpu_time(u32 slot);
    bool upload_to_default(ID3D12Resource* dest, const void* data, usize size,
        D3D12_RESOURCE_STATES final_state);
    bool upload_texture(ID3D12Resource* dest, u32 width, u32 height, u32 mip_levels,
        u32 array_size, Format format, const void* data);
    ID3D12Resource* create_committed_buffer(D3D12_HEAP_TYPE heap, usize size,
        D3D12_RESOURCE_STATES initial_state,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<IDXGIAdapter3> adapter3_;

    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    UINT rtv_descriptor_size_ = 0;
    ComPtr<ID3D12Resource> render_targets_[3]{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles_[3]{};
    D3D12Texture color_targets_[3]{};
    D3D12Texture depth_target_{};

    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> depth_buffer_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle_{};

    ComPtr<ID3D12CommandAllocator> frame_allocators_[3]{};
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    ComPtr<ID3D12CommandAllocator> copy_allocator_;
    ComPtr<ID3D12GraphicsCommandList> copy_list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_values_[3]{};
    UINT64 fence_cursor_ = 0;

    ComPtr<ID3D12QueryHeap> timestamp_heap_;
    ComPtr<ID3D12Resource> timestamp_readback_[3]{};
    UINT64 gpu_timestamp_freq_ = 0;
    f32 last_gpu_ms_ = 0.f;

    std::unique_ptr<IBuffer> frame_ring_[3]{};
    usize frame_ring_offset_ = 0;
    // High-water mark across every frame, never reset - frame_ring_offset_ is
    // cleared each begin_frame, so a per-frame figure is gone before anything
    // can read it. This is the only reading of the engine's tightest ceiling.
    usize frame_ring_peak_ = 0;
    u64 frame_ring_exhausted_frames_ = 0;
    UINT64 copy_fence_value_ = 0;

    ComPtr<ID3D12DescriptorHeap> shader_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE shader_cpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE shader_gpu_{};
    UINT shader_descriptor_size_ = 0;
    u32 shader_srv_cursor_ = 0;

    struct RetiredResource {
        ID3D12Resource* resource = nullptr;
        UINT64 fence_value = 0;
    };
    std::vector<RetiredResource> retired_;
    bool shutting_down_ = false;
    bool logged_device_removed_ = false;
    // Reset each begin_frame; keeps an exhausted frame from spamming the log.
    bool frame_ring_exhausted_ = false;
    bool shader_srv_exhausted_ = false;
    // Latched once the device is gone. The frame loop reads this and stops
    // rather than spinning on a dead device.
    bool device_lost_ = false;
    DepthConvention depth_convention_ = DepthConvention::Standard;
    bool occluded_ = false;
    // False when begin_frame could not open the command list, so submit()
    // must not close or execute it.
    bool frame_recording_ = false;

    u32 frame_index_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
    GpuBaseline baseline_{};
    u32 present_interval_ = 1;
    bool allow_tearing_ = false;
    HWND hwnd_ = nullptr;

    D3D12CommandList cmd_wrapper_;
    D3D12Swapchain swapchain_wrapper_;
};

} // namespace engine::rhi::d3d12
