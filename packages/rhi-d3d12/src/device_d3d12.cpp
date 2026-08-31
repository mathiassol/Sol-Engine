#include "device_d3d12.hpp"

#include <engine/core/assert.hpp>
#include <engine/core/log.hpp>
#include <engine/math/mip.hpp>

#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace engine::rhi::d3d12 {

namespace {

constexpr u32 kFrameCount = 3;
constexpr usize kBufferAlign = 256;

// Per-frame-slot budgets. Both used to be tight enough that ordinary content
// hit them, and both used to abort on exhaustion. They are now sized off the
// actual per-draw cost and degrade instead of terminating.
//
// Since instanced draws, the standard frame spends per drawn *instance* only
// the 144-byte `InstanceData` (model, prev_model, material), uploaded once for
// the whole frame - shadow, forward and motion read the same array.
//
// The per-pass constants are now per *batch*:
//     shadow  ShadowConstants    80 ->  256 aligned
//     forward FrameConstants    336 ->  512 aligned
//     motion  motion::Constants 160 ->  256 aligned
//                                     ---------------
//                                      1024 bytes per batch
// plus a fixed ~3 KB for sky, bloom (5 down + 4 up), TAA, tonemap and overlay,
// plus 576 bytes per debug AABB when F4 is on. Batch count is bounded by
// distinct material/mesh keys, not by scene size, so instances dominate: 1 MiB
// covers roughly 7,000 drawn instances with every pass active - against 800
// before batching, which was the tightest ceiling in the engine. Three slots
// costs 3 MiB of upload heap.
constexpr usize kFrameRingBytes = 1024 * 1024;
constexpr usize kFrameRingBytesPerInstance = 144;

// One shader-visible descriptor per SRV bind per frame.
//
// The forward pass binds 7 SRVs per *batch* since instanced draws, so scene
// size no longer drives this - distinct material/mesh keys do. It used to be
// per drawn instance and was the binding ceiling: 4096 slots was 581 drawn,
// below the 512 instance cap once anything else in the frame took descriptors.
// 8192 at 32 bytes is 256 KiB per slot (768 KiB across the three); it is kept
// at 8192 because the per-instance path returns the moment a scene has as many
// distinct materials as objects, which is exactly the case batching cannot
// help.
constexpr u32 kMaxShaderSrvsPerFrame = 8192;

constexpr u32 kMaxMips = 16;

// Root-signature budget, shared by the graphics and compute paths. Each
// constant buffer takes one root parameter and each SRV/UAV takes one
// single-range descriptor table, so the *sum* is what must fit - not each
// count independently.
constexpr u32 kMaxRootParams = 16;
constexpr u32 kMaxRootRanges = 8;

u32 full_mip_count(u32 width, u32 height) {
    u32 levels = 1;
    while ((width > 1 || height > 1) && levels < kMaxMips) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

u32 format_bytes_per_pixel(Format format) {
    switch (format) {
    case Format::RGBA16_FLOAT:
        return 8;
    case Format::RGBA8_UNORM:
        return 4;
    default:
        return 4;
    }
}

u32 resolve_mip_count(const TextureDesc& desc) {
    if (desc.mip_levels == 1) {
        return 1;
    }
    const u32 full = full_mip_count(desc.width, desc.height);
    if (desc.mip_levels == 0 || desc.mip_levels > full) {
        return full;
    }
    return desc.mip_levels;
}

void d3d_barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
}

D3D12_RESOURCE_STATES to_d3d_state(ResourceState state) {
    switch (state) {
    case ResourceState::Common:       return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::Present:      return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthWrite:   return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::CopySrc:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDst:      return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::ShaderRead:       return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    case ResourceState::UnorderedAccess:  return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_TEXTURE_ADDRESS_MODE to_d3d_address(AddressMode mode) {
    switch (mode) {
    case AddressMode::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case AddressMode::Wrap:
    default:                  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

D3D12_COMPARISON_FUNC to_d3d_compare(CompareOp op) {
    switch (op) {
    case CompareOp::Less:         return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
    case CompareOp::Never:
    default:                      return D3D12_COMPARISON_FUNC_NEVER;
    }
}

D3D12_FILTER to_d3d_filter(const SamplerDesc& desc) {
    const bool comparison = desc.compare != CompareOp::Never;
    const bool linear = desc.filter == FilterMode::Linear;
    if (comparison) {
        return linear ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                      : D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }
    return linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
}

void fill_static_sampler(D3D12_STATIC_SAMPLER_DESC& out, const SamplerDesc& desc, u32 register_index) {
    out = {};
    out.Filter = to_d3d_filter(desc);
    const D3D12_TEXTURE_ADDRESS_MODE address = to_d3d_address(desc.address);
    out.AddressU = address;
    out.AddressV = address;
    out.AddressW = address;
    out.MaxLOD = D3D12_FLOAT32_MAX;
    out.ComparisonFunc = to_d3d_compare(desc.compare);
    out.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    out.ShaderRegister = register_index;
    out.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
}

void fill_runtime_sampler(D3D12_SAMPLER_DESC& out, const SamplerDesc& desc) {
    out = {};
    out.Filter = to_d3d_filter(desc);
    const D3D12_TEXTURE_ADDRESS_MODE address = to_d3d_address(desc.address);
    out.AddressU = address;
    out.AddressV = address;
    out.AddressW = address;
    out.MaxLOD = D3D12_FLOAT32_MAX;
    out.ComparisonFunc = to_d3d_compare(desc.compare);
    out.BorderColor[0] = 1.f;
    out.BorderColor[1] = 1.f;
    out.BorderColor[2] = 1.f;
    out.BorderColor[3] = 1.f;
}

DXGI_FORMAT to_dxgi(Format format) {
    switch (format) {
    case Format::RGBA8_UNORM:  return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::D32_FLOAT:    return DXGI_FORMAT_D32_FLOAT;
    case Format::Unknown:      return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

IDXGIFactory4* create_dxgi_factory() {
    IDXGIFactory4* factory = nullptr;

#ifdef _DEBUG
    if (SUCCEEDED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)))) {
        return factory;
    }
#endif

    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        return factory;
    }

    return nullptr;
}

bool query_shader_model(ID3D12Device* device, u32& out_shader_model) {
    if (!device) {
        return false;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL sm{};
    sm.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
        sm.HighestShaderModel = D3D_SHADER_MODEL_6_0;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
            return false;
        }
    }
    out_shader_model = static_cast<u32>(sm.HighestShaderModel);
    return out_shader_model >= kGpuShaderModel_6_0;
}

bool factory_allows_tearing(IDXGIFactory4* factory) {
    if (!factory) {
        return false;
    }
    IDXGIFactory5* factory5 = nullptr;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) || !factory5) {
        return false;
    }
    BOOL allow = FALSE;
    const HRESULT hr = factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow));
    factory5->Release();
    return SUCCEEDED(hr) && allow == TRUE;
}

bool gpu_debug_enabled() {
#ifdef NDEBUG
    return false;
#else
    char value[8] = {};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0 && value[0] == '1';
#endif
}

bool debug_names_enabled() {
#ifdef _DEBUG
    return true;
#else
    return gpu_debug_enabled();
#endif
}

void copy_debug_name(char* dest, usize dest_bytes, std::string_view name) {
    ENGINE_ASSERT(dest != nullptr && dest_bytes > 0);
    const std::string_view label = name.empty() ? std::string_view("event") : name;
    const usize n = label.size() < dest_bytes - 1 ? label.size() : dest_bytes - 1;
    std::memcpy(dest, label.data(), n);
    dest[n] = '\0';
}

// PIX ANSI (metadata 1): C string. RenderDoc and PIX decode this without WinPixEventRuntime.
constexpr UINT kPixEventAnsi = 1;

void pix_begin_event(ID3D12GraphicsCommandList* cmd, const char* name) {
    if (!cmd || !name) {
        return;
    }
    cmd->BeginEvent(kPixEventAnsi, name, static_cast<UINT>(std::strlen(name) + 1));
}

void pix_end_event(ID3D12GraphicsCommandList* cmd) {
    if (cmd) {
        cmd->EndEvent();
    }
}

void pix_set_marker(ID3D12GraphicsCommandList* cmd, const char* name) {
    if (!cmd || !name) {
        return;
    }
    cmd->SetMarker(kPixEventAnsi, name, static_cast<UINT>(std::strlen(name) + 1));
}

void set_object_name(ID3D12Object* object, std::string_view name) {
    if (!object || name.empty() || !debug_names_enabled()) {
        return;
    }

    wchar_t wide[128];
    const int count = MultiByteToWideChar(
        CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide, 127);
    if (count <= 0) {
        return;
    }
    wide[count] = L'\0';
    object->SetName(wide);
}

const char* buffer_debug_name(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Vertex:  return "engine/vb";
    case BufferUsage::Index:   return "engine/ib";
    case BufferUsage::Uniform: return "engine/cb";
    case BufferUsage::Storage: return "engine/sb";
    case BufferUsage::Readback: return "engine/rb";
    }
    return "engine/buffer";
}

D3D12_RESOURCE_STATES default_buffer_state(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Vertex:
    case BufferUsage::Uniform:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case BufferUsage::Index:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case BufferUsage::Storage:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case BufferUsage::Readback:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

bool fence_completed(ID3D12Fence* fence, UINT64 value) {
    const UINT64 completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX) {
        return false;
    }
    return completed >= value;
}

bool wait_for_fence_blocking(HANDLE event, ID3D12Fence* fence, UINT64 value) {
    const UINT64 completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX) {
        return false;
    }
    if (completed >= value) {
        return true;
    }
    if (FAILED(fence->SetEventOnCompletion(value, event))) {
        return fence_completed(fence, value);
    }
    WaitForSingleObject(event, INFINITE);
    return fence_completed(fence, value);
}

void dump_info_queue_messages(ID3D12Device* device) {
    if (!device || !gpu_debug_enabled()) {
        return;
    }
    ID3D12InfoQueue* info = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info))) || !info) {
        return;
    }
    const UINT64 count = info->GetNumStoredMessages();
    const UINT64 begin = count > 8 ? count - 8 : 0;
    for (UINT64 i = begin; i < count; ++i) {
        SIZE_T bytes = 0;
        info->GetMessage(i, nullptr, &bytes);
        if (bytes == 0) {
            continue;
        }
        std::vector<uint8_t> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(info->GetMessage(i, message, &bytes)) && message->pDescription) {
            log(LogLevel::Error, LogChannel::Render, message->pDescription);
        }
    }
    info->Release();
}

} // namespace

D3D12Buffer::D3D12Buffer(D3D12Device* device, ID3D12Resource* resource, usize size, bool cpu_visible,
    bool persist_map)
    : device_(device), resource_(resource), size_(size), cpu_visible_(cpu_visible) {
    if (persist_map && cpu_visible_ && resource_) {
        D3D12_RANGE read_range{0, 0};
        if (FAILED(resource_->Map(0, &read_range, &mapped_))) {
            mapped_ = nullptr;
        }
    }
}

D3D12Buffer::~D3D12Buffer() {
    if (mapped_ && resource_) {
        resource_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
    if (!resource_) {
        return;
    }
    if (device_ && !device_->shutting_down()) {
        device_->retire_resource(resource_);
        resource_ = nullptr;
        return;
    }
    resource_->Release();
    resource_ = nullptr;
}

usize D3D12Buffer::size() const { return size_; }
ID3D12Resource* D3D12Buffer::resource() const { return resource_; }
bool D3D12Buffer::cpu_visible() const { return cpu_visible_; }
void* D3D12Buffer::mapped() const { return mapped_; }

void D3D12Buffer::set_debug_name(std::string_view name) {
    set_object_name(resource_, name);
}

D3D12Texture::D3D12Texture(D3D12Device* device, ID3D12Resource* resource, u32 width, u32 height,
    Format format, ID3D12DescriptorHeap* view_heap, D3D12_CPU_DESCRIPTOR_HANDLE view,
    bool is_depth, bool owns_resource, D3D12_GPU_DESCRIPTOR_HANDLE gpu_view,
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu, u32 mip_levels, u32 array_size,
    TextureDimension dimension)
    : device_(device)
    , resource_(resource)
    , view_heap_(view_heap)
    , view_(view)
    , gpu_view_(gpu_view)
    , srv_cpu_(srv_cpu.ptr ? srv_cpu : (gpu_view.ptr ? view : D3D12_CPU_DESCRIPTOR_HANDLE{}))
    , width_(width)
    , height_(height)
    , mip_levels_(mip_levels > 0 ? mip_levels : 1)
    , array_size_(array_size > 0 ? array_size : 1)
    , dimension_(dimension)
    , format_(format)
    , is_depth_(is_depth)
    , owns_resource_(owns_resource) {}

D3D12Texture::~D3D12Texture() {
    if (owns_resource_ && resource_) {
        if (device_ && !device_->shutting_down()) {
            device_->retire_resource(resource_);
        } else {
            resource_->Release();
        }
        resource_ = nullptr;
    }
}

void D3D12Texture::bind_external(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE view,
    u32 width, u32 height, Format format, bool is_depth) {
    resource_ = resource;
    view_ = view;
    width_ = width;
    height_ = height;
    format_ = format;
    is_depth_ = is_depth;
    mip_levels_ = 1;
    array_size_ = 1;
    dimension_ = TextureDimension::Tex2D;
    owns_resource_ = false;
    view_heap_.reset();
}

void D3D12Texture::detach() {
    resource_ = nullptr;
    view_ = {};
    width_ = 0;
    height_ = 0;
}

u32 D3D12Texture::width() const { return width_; }
u32 D3D12Texture::height() const { return height_; }
u32 D3D12Texture::mip_levels() const { return mip_levels_; }
u32 D3D12Texture::array_size() const { return array_size_; }
TextureDimension D3D12Texture::dimension() const { return dimension_; }
Format D3D12Texture::format() const { return format_; }
ID3D12Resource* D3D12Texture::resource() const { return resource_; }
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Texture::view() const { return view_; }
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Texture::gpu_view() const { return gpu_view_; }
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Texture::srv_cpu() const { return srv_cpu_; }
void D3D12Texture::set_srv(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    srv_heap_.reset(heap);
    srv_cpu_ = cpu;
}
ID3D12DescriptorHeap* D3D12Texture::descriptor_heap() const { return view_heap_.get(); }
bool D3D12Texture::is_depth() const { return is_depth_; }

void D3D12Texture::set_debug_name(std::string_view name) {
    set_object_name(resource_, name);
}

D3D12Sampler::D3D12Sampler(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
    const SamplerDesc& desc)
    : heap_(heap), cpu_(cpu), desc_(desc) {}

D3D12Pipeline::D3D12Pipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig,
    u32 srv_table_root, u32 structured_root, D3D12_PRIMITIVE_TOPOLOGY topology)
    : pso_(pso), root_sig_(root_sig), srv_table_root_(srv_table_root),
      structured_root_(structured_root), topology_(topology) {}

D3D12Pipeline::~D3D12Pipeline() = default;

ID3D12PipelineState* D3D12Pipeline::pso() const { return pso_.get(); }
ID3D12RootSignature* D3D12Pipeline::root_signature() const { return root_sig_.get(); }
u32 D3D12Pipeline::srv_table_root() const { return srv_table_root_; }
u32 D3D12Pipeline::structured_root() const { return structured_root_; }
D3D12_PRIMITIVE_TOPOLOGY D3D12Pipeline::topology() const { return topology_; }

D3D12ComputePipeline::D3D12ComputePipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig,
    u32 uav_table_root, u32 srv_table_root)
    : pso_(pso), root_sig_(root_sig), uav_table_root_(uav_table_root), srv_table_root_(srv_table_root) {}

D3D12ComputePipeline::~D3D12ComputePipeline() = default;

ID3D12PipelineState* D3D12ComputePipeline::pso() const { return pso_.get(); }
ID3D12RootSignature* D3D12ComputePipeline::root_signature() const { return root_sig_.get(); }
u32 D3D12ComputePipeline::uav_table_root() const { return uav_table_root_; }
u32 D3D12ComputePipeline::srv_table_root() const { return srv_table_root_; }

D3D12CommandList::D3D12CommandList(D3D12Device& device) : device_(device) {}

void D3D12CommandList::begin() {
    recording_ = true;
    event_depth_ = 0;
    last_marker_[0] = '\0';
}

void D3D12CommandList::end() {
    if (in_pass_) end_render_pass();
    ENGINE_ASSERT(event_depth_ == 0);
    recording_ = false;
}

void D3D12CommandList::begin_render_pass(const RenderPassInfo& info) {
    auto* cmd = device_.d3d12_cmd_list();
    auto* color = info.color ? static_cast<D3D12Texture*>(info.color) : nullptr;
    auto* depth = info.depth ? static_cast<D3D12Texture*>(info.depth) : nullptr;

    const u32 w = color ? color->width() : (depth ? depth->width() : device_.width());
    const u32 h = color ? color->height() : (depth ? depth->height() : device_.height());

    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<f32>(w);
    viewport.Height   = static_cast<f32>(h);
    viewport.MaxDepth = 1.f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    cmd->RSSetScissorRects(1, &scissor);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = color ? color->view() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = depth ? depth->view() : D3D12_CPU_DESCRIPTOR_HANDLE{};

    if (color && depth) {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    } else if (color) {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    } else if (depth) {
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    }

    if (color && info.clear_color_target) {
        const f32 clear_color[] = {info.clear_color.r, info.clear_color.g,
            info.clear_color.b, info.clear_color.a};
        cmd->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    }
    if (depth && info.clear_depth) {
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
    in_pass_ = true;
}

void D3D12CommandList::end_render_pass() { in_pass_ = false; }

void D3D12CommandList::transition(ITexture& texture, ResourceState from, ResourceState to) {
    if (from == to) {
        return;
    }
    auto& d3d_texture = static_cast<D3D12Texture&>(texture);
    ENGINE_ASSERT(d3d_texture.resource() != nullptr);
    d3d_barrier(device_.d3d12_cmd_list(), d3d_texture.resource(),
        to_d3d_state(from), to_d3d_state(to));
}

void D3D12CommandList::copy_texture(ITexture& src, ITexture& dst) {
    auto& src_tex = static_cast<D3D12Texture&>(src);
    auto& dst_tex = static_cast<D3D12Texture&>(dst);
    ENGINE_ASSERT(src_tex.resource() != nullptr);
    ENGINE_ASSERT(dst_tex.resource() != nullptr);
    ENGINE_ASSERT(src_tex.width() == dst_tex.width());
    ENGINE_ASSERT(src_tex.height() == dst_tex.height());
    ENGINE_ASSERT(src_tex.format() == dst_tex.format());
    ENGINE_ASSERT(src_tex.array_size() == dst_tex.array_size());
    ENGINE_ASSERT(src_tex.dimension() == dst_tex.dimension());
    device_.d3d12_cmd_list()->CopyResource(dst_tex.resource(), src_tex.resource());
}

void D3D12CommandList::transition(IBuffer& buffer, ResourceState from, ResourceState to) {
    if (from == to) {
        return;
    }
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    ENGINE_ASSERT(d3d_buffer.resource() != nullptr);
    d3d_barrier(device_.d3d12_cmd_list(), d3d_buffer.resource(),
        to_d3d_state(from), to_d3d_state(to));
}

void D3D12CommandList::copy_buffer(IBuffer& src, IBuffer& dst, usize size) {
    auto& src_buf = static_cast<D3D12Buffer&>(src);
    auto& dst_buf = static_cast<D3D12Buffer&>(dst);
    ENGINE_ASSERT(src_buf.resource() != nullptr);
    ENGINE_ASSERT(dst_buf.resource() != nullptr);
    ENGINE_ASSERT(size > 0);
    ENGINE_ASSERT(size <= src_buf.size());
    ENGINE_ASSERT(size <= dst_buf.size());
    device_.d3d12_cmd_list()->CopyBufferRegion(
        dst_buf.resource(), 0, src_buf.resource(), 0, size);
}

void D3D12CommandList::set_viewport(u32 width, u32 height) {
    auto* cmd = device_.d3d12_cmd_list();
    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<f32>(width);
    viewport.Height   = static_cast<f32>(height);
    viewport.MaxDepth = 1.f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    cmd->RSSetScissorRects(1, &scissor);
}

void D3D12CommandList::set_pipeline(IGraphicsPipeline& pipeline) {
    auto& d3d_pipeline = static_cast<D3D12Pipeline&>(pipeline);
    bound_pipeline_ = &d3d_pipeline;
    bound_compute_ = nullptr;
    auto* cmd = device_.d3d12_cmd_list();
    cmd->SetGraphicsRootSignature(d3d_pipeline.root_signature());
    cmd->SetPipelineState(d3d_pipeline.pso());
    cmd->IASetPrimitiveTopology(d3d_pipeline.topology());
}

void D3D12CommandList::set_compute_pipeline(IComputePipeline& pipeline) {
    auto& d3d_pipeline = static_cast<D3D12ComputePipeline&>(pipeline);
    bound_compute_ = &d3d_pipeline;
    bound_pipeline_ = nullptr;
    auto* cmd = device_.d3d12_cmd_list();
    cmd->SetComputeRootSignature(d3d_pipeline.root_signature());
    cmd->SetPipelineState(d3d_pipeline.pso());
}

void D3D12CommandList::set_vertex_buffer(u32 slot, IBuffer& buffer, u32 stride_bytes,
    usize offset_bytes) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = d3d_buffer.resource()->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes    = static_cast<UINT>(d3d_buffer.size() - offset_bytes);
    view.StrideInBytes  = stride_bytes;

    device_.d3d12_cmd_list()->IASetVertexBuffers(slot, 1, &view);
}

void D3D12CommandList::set_index_buffer(IBuffer& buffer, usize offset_bytes) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = d3d_buffer.resource()->GetGPUVirtualAddress() + offset_bytes;
    view.SizeInBytes    = static_cast<UINT>(d3d_buffer.size() - offset_bytes);
    view.Format         = DXGI_FORMAT_R32_UINT;
    device_.d3d12_cmd_list()->IASetIndexBuffer(&view);
}

void D3D12CommandList::set_constant_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        d3d_buffer.resource()->GetGPUVirtualAddress() + offset_bytes;
    auto* cmd = device_.d3d12_cmd_list();
    if (bound_compute_ != nullptr) {
        cmd->SetComputeRootConstantBufferView(slot, address);
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(slot, address);
}

void D3D12CommandList::set_shader_resource(u32 slot, ITexture& texture) {
    ENGINE_ASSERT(bound_pipeline_ != nullptr);
    ENGINE_ASSERT_MSG(bound_pipeline_->srv_table_root() != ~0u,
        "pipeline has no shader-resource table");
    auto& d3d_texture = static_cast<D3D12Texture&>(texture);
    ENGINE_ASSERT_MSG(d3d_texture.srv_cpu().ptr != 0, "texture has no shader-resource view");
    device_.bind_shader_srv(slot, d3d_texture.srv_cpu(), bound_pipeline_->srv_table_root());
}

void D3D12CommandList::set_unordered_access(u32 slot, IBuffer& buffer) {
    ENGINE_ASSERT(bound_compute_ != nullptr);
    ENGINE_ASSERT_MSG(bound_compute_->uav_table_root() != ~0u,
        "compute pipeline has no unordered-access table");
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    ENGINE_ASSERT(d3d_buffer.resource() != nullptr);
    device_.bind_compute_uav(slot, d3d_buffer.resource(), d3d_buffer.size(),
        bound_compute_->uav_table_root());
}

void D3D12CommandList::set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    ENGINE_ASSERT(bound_pipeline_ != nullptr);
    ENGINE_ASSERT_MSG(bound_pipeline_->structured_root() != ~0u,
        "pipeline declares no structured buffers (GraphicsPipelineDesc::structured_buffer_count)");
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    ENGINE_ASSERT(d3d_buffer.resource() != nullptr);
    // A root SRV takes a raw GPU virtual address - no descriptor, no heap, no
    // barrier. The frame ring lives on an upload heap permanently in
    // GENERIC_READ, so a slice of it can be handed over directly.
    device_.d3d12_cmd_list()->SetGraphicsRootShaderResourceView(
        bound_pipeline_->structured_root() + slot,
        d3d_buffer.resource()->GetGPUVirtualAddress() + offset_bytes);
}

void D3D12CommandList::draw(u32 vertex_count, u32 start_vertex) {
    device_.d3d12_cmd_list()->DrawInstanced(vertex_count, 1, start_vertex, 0);
}

void D3D12CommandList::draw_indexed(u32 index_count, u32 start_index, i32 base_vertex,
    u32 instance_count) {
    if (instance_count == 0) {
        return;  // an empty batch is not an error
    }
    // StartInstanceLocation stays 0 on purpose - see ICommandList::draw_indexed.
    device_.d3d12_cmd_list()->DrawIndexedInstanced(index_count, instance_count, start_index,
        base_vertex, 0);
}

void D3D12CommandList::dispatch(u32 group_count_x, u32 group_count_y, u32 group_count_z) {
    ENGINE_ASSERT(bound_compute_ != nullptr);
    ENGINE_ASSERT(group_count_x > 0 && group_count_y > 0 && group_count_z > 0);
    device_.d3d12_cmd_list()->Dispatch(group_count_x, group_count_y, group_count_z);
}

void D3D12CommandList::begin_event(std::string_view name) {
    ENGINE_ASSERT(event_depth_ < kMaxDebugEvents);
    copy_debug_name(event_stack_[event_depth_], kDebugNameBytes, name);
    const char* label = event_stack_[event_depth_];
    event_depth_ += 1;
    if (recording_) {
        pix_begin_event(device_.d3d12_cmd_list(), label);
    }
}

void D3D12CommandList::end_event() {
    ENGINE_ASSERT(event_depth_ > 0);
    event_depth_ -= 1;
    if (recording_) {
        pix_end_event(device_.d3d12_cmd_list());
    }
}

void D3D12CommandList::set_marker(std::string_view name) {
    copy_debug_name(last_marker_, kDebugNameBytes, name);
    if (recording_) {
        pix_set_marker(device_.d3d12_cmd_list(), last_marker_);
    }
}

u32 D3D12CommandList::debug_event_depth() const {
    return event_depth_;
}

std::string_view D3D12CommandList::debug_event_name() const {
    if (event_depth_ == 0) {
        return {};
    }
    return event_stack_[event_depth_ - 1];
}

std::string_view D3D12CommandList::last_debug_marker() const {
    if (last_marker_[0] == '\0') {
        return {};
    }
    return last_marker_;
}

D3D12Swapchain::D3D12Swapchain(D3D12Device& device) : device_(device) {}

void D3D12Swapchain::present() {
    device_.present_back_buffer();
}

u32 D3D12Swapchain::current_back_buffer_index() const { return device_.frame_index(); }

D3D12Device::D3D12Device()
    : cmd_wrapper_(*this), swapchain_wrapper_(*this) {}

D3D12Device::~D3D12Device() {
    shutting_down_ = true;
    if (fence_) {
        wait_for_gpu();
    }

    // "The debug layer stayed silent" is this project's hard rule for a GPU
    // change, and until now nothing checked it: the info queue was only ever
    // drained when some *other* call already failed, so a run could accumulate
    // debug-layer errors and still look clean. Drain it once at shutdown so
    // the claim is observed rather than assumed.
    report_debug_layer_messages();

    flush_retired();
    cleanup_swapchain_resources();
    cleanup_depth_buffer();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
}

void D3D12Device::report_debug_layer_messages() const {
    if (!device_ || !gpu_debug_enabled()) {
        return;
    }
    ID3D12InfoQueue* info = nullptr;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&info))) || !info) {
        return;
    }
    const UINT64 total = info->GetNumStoredMessages();
    UINT64 errors = 0;
    UINT64 warnings = 0;
    for (UINT64 i = 0; i < total; ++i) {
        SIZE_T bytes = 0;
        if (FAILED(info->GetMessage(i, nullptr, &bytes)) || bytes == 0) {
            continue;
        }
        std::vector<u8> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(info->GetMessage(i, message, &bytes))) {
            continue;
        }
        if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION
            || message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
            ++errors;
        } else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
            ++warnings;
        }
    }

    char summary[160];
    std::snprintf(summary, sizeof(summary),
        "D3D12 debug layer: %llu message(s), %llu error(s), %llu warning(s)",
        static_cast<unsigned long long>(total), static_cast<unsigned long long>(errors),
        static_cast<unsigned long long>(warnings));
    log(errors > 0 ? LogLevel::Error : LogLevel::Info, LogChannel::Render, summary);
    if (errors > 0 || warnings > 0) {
        dump_info_queue_messages(device_.get());
    }
    info->Release();
}

bool D3D12Device::init(const DeviceDesc& desc) {
    width_  = desc.width;
    height_ = desc.height;
    hwnd_   = static_cast<HWND>(desc.window_handle);
    present_interval_ = desc.present_interval == 0 ? 0u : 1u;

#ifdef _DEBUG
    if (gpu_debug_enabled()) {
        if (ID3D12Debug* debug = nullptr; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debug->Release();
            log(LogLevel::Info, LogChannel::Render, "D3D12 debug layer enabled (ENGINE_GPU_DEBUG=1)");
        }
    }
#endif

    IDXGIFactory4* factory = create_dxgi_factory();
    if (!factory) {
        log(LogLevel::Error, LogChannel::Render, "DXGI factory creation failed");
        return false;
    }

    // Stop on any failure, not just DXGI_ERROR_NOT_FOUND. On another error
    // EnumAdapters1 leaves `adapter` untouched, which is null on the first
    // iteration and the already-released pointer from the previous one after
    // that - a null deref or a use-after-free.
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)) && adapter != nullptr; ++i) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        adapter->GetDesc1(&adapter_desc);
        // Each Release is paired with a null, so `adapter` is either owned or
        // null at every point - including when the loop exits early.
        if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            adapter = nullptr;
            continue;
        }
        if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.put())))) {
            adapter->Release();
            adapter = nullptr;
            continue;
        }
        u32 shader_model = 0;
        if (!query_shader_model(device_.get(), shader_model)) {
            device_.reset();
            adapter->Release();
            adapter = nullptr;
            continue;
        }
        baseline_.feature_level = kGpuFeatureLevel_11_0;
        baseline_.shader_model = shader_model;
        // Optional: only used for gpu_memory_stats(). A null adapter3_ is
        // handled there.
        adapter->QueryInterface(IID_PPV_ARGS(adapter3_.put()));
        adapter->Release();
        adapter = nullptr;
        break;
    }
    if (adapter) {
        adapter->Release();
    }

    if (!device_) {
        factory->Release();
        log(LogLevel::Error, LogChannel::Render,
            "D3D12 device creation failed (need Feature Level 11_0 and Shader Model 6.0; OS D3D12, no Agility SDK)");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.put())))) {
        factory->Release();
        log(LogLevel::Error, LogChannel::Render, "Command queue creation failed");
        return false;
    }

    set_object_name(device_.get(), "engine/device");
    set_object_name(queue_.get(), "engine/direct_queue");

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = width_;
    sd.Height      = height_;
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrameCount;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    allow_tearing_ = factory_allows_tearing(factory);
    if (allow_tearing_) {
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    IDXGISwapChain1* swapchain1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        queue_.get(), hwnd_, &sd, nullptr, nullptr, &swapchain1);
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();

    if (FAILED(hr)) {
        log(LogLevel::Error, LogChannel::Render, "Swapchain creation failed");
        return false;
    }

    // Everything below dereferences swapchain_ immediately, so an unchecked
    // QueryInterface here is a null deref one line later.
    const HRESULT swap_qi = swapchain1->QueryInterface(IID_PPV_ARGS(swapchain_.put()));
    swapchain1->Release();
    if (FAILED(swap_qi) || !swapchain_) {
        log_device_error("IDXGISwapChain3 not available", swap_qi);
        return false;
    }

    if (!create_render_targets() || !create_depth_buffer() || !create_frame_resources()) {
        return false;
    }

    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    char message[128];
    std::snprintf(message, sizeof(message),
        "D3D12 device initialized (FL 11_0 SM %u.%u, OS runtime)",
        (baseline_.shader_model >> 4) & 0xFu, baseline_.shader_model & 0xFu);
    log(LogLevel::Info, LogChannel::Render, message);
    return true;
}

bool D3D12Device::create_render_targets() {
    if (!rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = kFrameCount;
        heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

        if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(rtv_heap_.put())))) {
            return false;
        }
        set_object_name(rtv_heap_.get(), "engine/rtv_heap");
        rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(render_targets_[i].put())))) {
            return false;
        }
        device_->CreateRenderTargetView(render_targets_[i].get(), nullptr, handle);
        rtv_handles_[i] = handle;
        handle.ptr += rtv_descriptor_size_;

        char name[32];
        std::snprintf(name, sizeof(name), "engine/rt%u", i);
        set_object_name(render_targets_[i].get(), name);
        color_targets_[i].bind_external(render_targets_[i].get(), rtv_handles_[i],
            width_, height_, Format::RGBA8_UNORM, false);
    }
    return true;
}

bool D3D12Device::create_depth_buffer() {
    cleanup_depth_buffer();

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(dsv_heap_.put())))) {
        return false;
    }
    set_object_name(dsv_heap_.get(), "engine/dsv_heap");

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width              = width_;
    resource_desc.Height             = height_;
    resource_desc.DepthOrArraySize   = 1;
    resource_desc.MipLevels          = 1;
    resource_desc.Format             = DXGI_FORMAT_D32_FLOAT;
    resource_desc.SampleDesc.Count   = 1;
    resource_desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format               = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth   = 1.f;

    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
            IID_PPV_ARGS(depth_buffer_.put())))) {
        return false;
    }
    set_object_name(depth_buffer_.get(), "engine/depth");

    dsv_handle_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth_buffer_.get(), &dsv_desc, dsv_handle_);
    depth_target_.bind_external(depth_buffer_.get(), dsv_handle_, width_, height_, Format::D32_FLOAT, true);
    return true;
}

void D3D12Device::cleanup_depth_buffer() {
    depth_target_.detach();
    depth_buffer_.reset();
    dsv_heap_.reset();
    dsv_handle_ = {};
}

bool D3D12Device::create_frame_resources() {
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (FAILED(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(frame_allocators_[i].put())))) {
            return false;
        }
    }

    if (FAILED(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame_allocators_[0].get(), nullptr,
            IID_PPV_ARGS(cmd_list_.put())))) {
        return false;
    }
    cmd_list_->Close();
    set_object_name(cmd_list_.get(), "engine/cmd_list");
    set_object_name(frame_allocators_[0].get(), "engine/allocator0");
    set_object_name(frame_allocators_[1].get(), "engine/allocator1");
    set_object_name(frame_allocators_[2].get(), "engine/allocator2");

    if (FAILED(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(copy_allocator_.put())))) {
        return false;
    }
    if (FAILED(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, copy_allocator_.get(), nullptr,
            IID_PPV_ARGS(copy_list_.put())))) {
        return false;
    }
    copy_list_->Close();
    set_object_name(copy_allocator_.get(), "engine/copy_allocator");
    set_object_name(copy_list_.get(), "engine/copy_list");

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.put())))) {
        return false;
    }
    set_object_name(fence_.get(), "engine/fence");

    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) return false;

    fence_values_[0] = fence_values_[1] = fence_values_[2] = 0;
    return create_timestamp_resources() && create_frame_ring() && create_shader_heap();
}

bool D3D12Device::create_timestamp_resources() {
    D3D12_QUERY_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = kFrameCount * 2;
    if (FAILED(device_->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(timestamp_heap_.put())))) {
        return false;
    }
    set_object_name(timestamp_heap_.get(), "engine/timestamp_heap");

    for (u32 i = 0; i < kFrameCount; ++i) {
        timestamp_readback_[i].reset(create_committed_buffer(
            D3D12_HEAP_TYPE_READBACK, 2 * sizeof(UINT64),
            D3D12_RESOURCE_STATE_COPY_DEST));
        if (!timestamp_readback_[i]) {
            return false;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "engine/timestamp_rb%u", i);
        set_object_name(timestamp_readback_[i].get(), name);
    }

    if (FAILED(queue_->GetTimestampFrequency(&gpu_timestamp_freq_)) || gpu_timestamp_freq_ == 0) {
        gpu_timestamp_freq_ = 1;
    }
    return true;
}

bool D3D12Device::create_frame_ring() {
    BufferDesc desc{};
    desc.size = kFrameRingBytes;
    desc.usage = BufferUsage::Uniform;
    for (u32 i = 0; i < kFrameCount; ++i) {
        frame_ring_[i] = create_buffer(desc, nullptr);
        if (!frame_ring_[i]) {
            return false;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "engine/frame_ring%u", i);
        set_debug_name(*frame_ring_[i], name);
    }
    return true;
}

bool D3D12Device::create_shader_heap() {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = kFrameCount * kMaxShaderSrvsPerFrame;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(shader_heap_.put())))) {
        return false;
    }
    set_object_name(shader_heap_.get(), "engine/shader_srv_heap");
    shader_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    shader_cpu_ = shader_heap_->GetCPUDescriptorHandleForHeapStart();
    shader_gpu_ = shader_heap_->GetGPUDescriptorHandleForHeapStart();
    return true;
}

// Next free shader-visible descriptor in this frame slot's window.
//
// On exhaustion the last slot in the window is reused rather than aborting: a
// draw sampling the wrong texture is a bad frame, an unbound descriptor table
// is a GPU fault, and a terminated process is neither recoverable nor
// debuggable on a player's machine.
u32 D3D12Device::next_shader_descriptor() {
    const u32 frame_base = frame_index_ * kMaxShaderSrvsPerFrame;
    const u32 frame_end = frame_base + kMaxShaderSrvsPerFrame;
    if (shader_srv_cursor_ < frame_base || shader_srv_cursor_ >= frame_end) {
        if (!shader_srv_exhausted_) {
            shader_srv_exhausted_ = true;
            char message[176];
            std::snprintf(message, sizeof(message),
                "Shader descriptor window exhausted: %u per frame. Reusing the last slot, "
                "so some draws will sample the wrong resource - raise kMaxShaderSrvsPerFrame.",
                kMaxShaderSrvsPerFrame);
            log(LogLevel::Error, LogChannel::Render, message);
        }
        return frame_end - 1;
    }
    const u32 index = shader_srv_cursor_;
    shader_srv_cursor_ += 1;
    return index;
}

void D3D12Device::bind_shader_srv(u32 slot, D3D12_CPU_DESCRIPTOR_HANDLE src, u32 table_root) {
    ENGINE_ASSERT(shader_heap_.get() != nullptr);
    ENGINE_ASSERT(src.ptr != 0);
    const u32 index = next_shader_descriptor();

    D3D12_CPU_DESCRIPTOR_HANDLE dest = shader_cpu_;
    dest.ptr += static_cast<SIZE_T>(index) * shader_descriptor_size_;
    device_->CopyDescriptorsSimple(1, dest, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = {shader_heap_.get()};
    cmd_list_->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = shader_gpu_;
    gpu.ptr += static_cast<UINT64>(index) * shader_descriptor_size_;
    cmd_list_->SetGraphicsRootDescriptorTable(table_root + slot, gpu);
}

void D3D12Device::bind_compute_uav(u32 slot, ID3D12Resource* resource, usize size, u32 table_root) {
    ENGINE_ASSERT(shader_heap_.get() != nullptr);
    ENGINE_ASSERT(resource != nullptr);
    ENGINE_ASSERT(size >= 4);
    const u32 index = next_shader_descriptor();

    D3D12_CPU_DESCRIPTOR_HANDLE dest = shader_cpu_;
    dest.ptr += static_cast<SIZE_T>(index) * shader_descriptor_size_;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = static_cast<UINT>(size / 4);

    device_->CreateUnorderedAccessView(resource, nullptr, &uav, dest);

    ID3D12DescriptorHeap* heaps[] = {shader_heap_.get()};
    cmd_list_->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = shader_gpu_;
    gpu.ptr += static_cast<UINT64>(index) * shader_descriptor_size_;
    cmd_list_->SetComputeRootDescriptorTable(table_root + slot, gpu);
}

void D3D12Device::cleanup_swapchain_resources() {
    for (u32 i = 0; i < kFrameCount; ++i) {
        color_targets_[i].detach();
        render_targets_[i].reset();
    }
}

bool D3D12Device::release_command_list_resource_refs() {
    if (!cmd_list_ || !frame_allocators_[frame_index_]) {
        return false;
    }
    // Reset the list *before* the allocator so DXGI can drop backbuffer refs.
    if (FAILED(cmd_list_->Reset(frame_allocators_[frame_index_].get(), nullptr))) {
        return false;
    }
    cmd_list_->Close();
    if (copy_list_ && copy_allocator_
        && SUCCEEDED(copy_list_->Reset(copy_allocator_.get(), nullptr))) {
        copy_list_->Close();
    }
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (frame_allocators_[i]) {
            frame_allocators_[i]->Reset();
        }
    }
    if (copy_allocator_) {
        copy_allocator_->Reset();
    }
    return true;
}

ISwapchain& D3D12Device::swapchain() { return swapchain_wrapper_; }
ICommandList& D3D12Device::command_list() { return cmd_wrapper_; }

void D3D12Device::begin_frame() {
    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    wait_for_frame(frame_index_);
    flush_retired();
    read_gpu_time(frame_index_);
    frame_ring_offset_ = 0;
    shader_srv_cursor_ = frame_index_ * kMaxShaderSrvsPerFrame;
    frame_ring_exhausted_ = false;
    shader_srv_exhausted_ = false;

    if (FAILED(frame_allocators_[frame_index_]->Reset())) {
        log_device_error("Command allocator reset failed");
        return;
    }
    // Recording into a list that failed to open produces a list that cannot be
    // closed, which then gets submitted anyway. Bail instead.
    if (FAILED(cmd_list_->Reset(frame_allocators_[frame_index_].get(), nullptr))) {
        log_device_error("Command list reset failed");
        frame_recording_ = false;
        return;
    }
    frame_recording_ = true;
    cmd_list_->EndQuery(timestamp_heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index_ * 2);
}

void D3D12Device::submit() {
    if (!frame_recording_) {
        return;
    }
    cmd_list_->EndQuery(timestamp_heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index_ * 2 + 1);
    cmd_list_->ResolveQueryData(timestamp_heap_.get(), D3D12_QUERY_TYPE_TIMESTAMP,
        frame_index_ * 2, 2, timestamp_readback_[frame_index_].get(), 0);

    // Close() fails on an unbalanced barrier, an invalid recording, or a
    // removed device. Executing a list that is not closed is undefined
    // behaviour, so this check is the difference between a logged bad frame
    // and a driver fault.
    if (FAILED(cmd_list_->Close())) {
        log_device_error("Command list close failed - frame not submitted");
        frame_recording_ = false;
        return;
    }
    frame_recording_ = false;

    ID3D12CommandList* lists[] = {cmd_list_.get()};
    queue_->ExecuteCommandLists(1, lists);

    fence_values_[frame_index_] = signal_queue();
}

void D3D12Device::end_frame() {
    submit();
    swapchain_wrapper_.present();
}

f32 D3D12Device::last_gpu_time_ms() const {
    return last_gpu_ms_;
}

void D3D12Device::read_gpu_time(u32 slot) {
    if (slot >= kFrameCount || !timestamp_readback_[slot] || gpu_timestamp_freq_ == 0) {
        return;
    }
    UINT64 pair[2]{};
    void* mapped = nullptr;
    if (FAILED(timestamp_readback_[slot]->Map(0, nullptr, &mapped)) || !mapped) {
        return;
    }
    std::memcpy(pair, mapped, sizeof(pair));
    timestamp_readback_[slot]->Unmap(0, nullptr);
    if (pair[1] > pair[0]) {
        last_gpu_ms_ = static_cast<f32>(
            (static_cast<double>(pair[1] - pair[0]) * 1000.0)
            / static_cast<double>(gpu_timestamp_freq_));
    }
}

void D3D12Device::wait_idle() {
    wait_for_gpu();
    flush_retired();
}

bool D3D12Device::device_removed() const {
    return device_ && device_->GetDeviceRemovedReason() != S_OK;
}

// One place to record a device-level failure. Latches `device_lost_` when the
// device is actually gone, so the frame loop can stop instead of spinning on a
// dead device, and logs the removal reason once - after removal every call
// fails and an unlatched log would flood.
void D3D12Device::log_device_error(const char* what, HRESULT hr) {
    const bool removed = device_removed();
    if (removed) {
        device_lost_ = true;
    }
    if (removed && logged_device_removed_) {
        return;
    }
    if (removed) {
        logged_device_removed_ = true;
    }
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
    char msg[224];
    std::snprintf(msg, sizeof(msg), "%s (hr=0x%08X, device_removed=0x%08X)",
        what, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
    log(LogLevel::Error, LogChannel::Render, msg);
    dump_info_queue_messages(device_.get());
}

void D3D12Device::log_resize_failure(const char* what, HRESULT hr, u32 width, u32 height) const {
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
    char msg[192];
    std::snprintf(msg, sizeof(msg),
        "%s %ux%u failed (hr=0x%08X, removed=0x%08X)",
        what, width, height, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
    log(LogLevel::Error, LogChannel::Render, msg);
    dump_info_queue_messages(device_.get());
}

bool D3D12Device::resize(u32 width, u32 height) {
    if (width == 0 || height == 0) return true;
    if (width_ == width && height_ == height) return true;
    if (!swapchain_ || device_removed()) {
        if (!logged_device_removed_) {
            logged_device_removed_ = true;
            log_resize_failure("Swapchain resize skipped — device removed at", E_FAIL, width, height);
        }
        return false;
    }

    const u32 old_width  = width_;
    const u32 old_height = height_;

    wait_for_gpu();
    if (device_removed()) {
        log_resize_failure("Swapchain resize aborted — GPU wait removed device at",
            device_->GetDeviceRemovedReason(), width, height);
        return false;
    }
    if (!release_command_list_resource_refs()) {
        log_resize_failure("Swapchain resize aborted — command list reset at", E_FAIL, width, height);
        return false;
    }

    cleanup_swapchain_resources();
    cleanup_depth_buffer();

    DXGI_SWAP_CHAIN_DESC scd{};
    swapchain_->GetDesc(&scd);
    const UINT flags = scd.Flags;

    HRESULT hr = swapchain_->ResizeBuffers(kFrameCount, width, height,
            DXGI_FORMAT_UNKNOWN, flags);
    bool applied = SUCCEEDED(hr);
    if (!applied) {
        log_resize_failure("Swapchain resize to", hr, width, height);
        if (!device_removed()) {
            hr = swapchain_->ResizeBuffers(kFrameCount, old_width, old_height,
                    DXGI_FORMAT_UNKNOWN, flags);
            if (FAILED(hr)) {
                log_resize_failure("Swapchain recovery resize", hr, old_width, old_height);
            }
        }
        width_  = old_width;
        height_ = old_height;
    } else {
        width_  = width;
        height_ = height;
    }

    const UINT64 idle = fence_cursor_;
    for (u32 i = 0; i < kFrameCount; ++i) {
        fence_values_[i] = idle;
    }

    if (device_removed()) {
        return false;
    }

    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    if (!create_render_targets() || !create_depth_buffer()) {
        log(LogLevel::Error, LogChannel::Render, "Swapchain buffers missing after resize");
        return false;
    }
    return applied;
}

std::unique_ptr<IBuffer> D3D12Device::create_buffer(const BufferDesc& desc, const void* data) {
    if (desc.size == 0) {
        return nullptr;
    }

    const usize aligned_size = (desc.size + (kBufferAlign - 1)) & ~(kBufferAlign - 1);

    if (desc.usage == BufferUsage::Uniform) {
        ID3D12Resource* resource = create_committed_buffer(
            D3D12_HEAP_TYPE_UPLOAD, aligned_size, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!resource) {
            return nullptr;
        }
        set_object_name(resource, buffer_debug_name(desc.usage));
        auto buffer = std::make_unique<D3D12Buffer>(this, resource, aligned_size, true);
        if (data) {
            if (void* mapped = buffer->mapped()) {
                std::memcpy(mapped, data, desc.size);
            }
        }
        return buffer;
    }

    if (desc.usage == BufferUsage::Readback) {
        ID3D12Resource* resource = create_committed_buffer(
            D3D12_HEAP_TYPE_READBACK, aligned_size, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!resource) {
            return nullptr;
        }
        set_object_name(resource, buffer_debug_name(desc.usage));
        return std::make_unique<D3D12Buffer>(this, resource, aligned_size, true, false);
    }

    const D3D12_RESOURCE_FLAGS flags = desc.usage == BufferUsage::Storage
        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        : D3D12_RESOURCE_FLAG_NONE;

    // COMMON, not COPY_DEST. D3D12 ignores the initial state of a buffer on a
    // DEFAULT heap and creates it in COMMON regardless, warning every time -
    // 314 of them in one --gates run, which is most of what was hiding in the
    // debug layer. Buffers promote implicitly, so the upload path's
    // COPY_DEST -> final barrier is unaffected.
    ID3D12Resource* resource = create_committed_buffer(
        D3D12_HEAP_TYPE_DEFAULT, aligned_size, D3D12_RESOURCE_STATE_COMMON, flags);
    if (!resource) {
        return nullptr;
    }
    set_object_name(resource, buffer_debug_name(desc.usage));

    if (data) {
        if (!upload_to_default(resource, data, desc.size, default_buffer_state(desc.usage))) {
            retire_resource(resource);
            return nullptr;
        }
    } else {
        begin_copy();
        d3d_barrier(copy_list_.get(), resource, D3D12_RESOURCE_STATE_COPY_DEST,
            default_buffer_state(desc.usage));
        end_copy();
    }

    return std::make_unique<D3D12Buffer>(this, resource, aligned_size, false);
}

std::unique_ptr<ITexture> D3D12Device::create_texture(const TextureDesc& desc, const void* data) {
    if (desc.width == 0 || desc.height == 0 || desc.format == Format::Unknown) {
        return nullptr;
    }
    if (desc.dimension != TextureDimension::Tex2D && desc.usage != TextureUsage::ShaderResource) {
        log(LogLevel::Error, LogChannel::Render,
            "D3D12 cube / array textures are sampled-only for now");
        return nullptr;
    }
    if (desc.usage == TextureUsage::ShaderResource) {
        return create_sampled_texture(desc, data);
    }
    if (desc.usage == TextureUsage::DepthShaderResource) {
        return create_shadow_texture(desc);
    }
    if (desc.usage == TextureUsage::ColorShaderResource) {
        return create_color_shader_resource_texture(desc);
    }

    const bool is_depth = desc.usage == TextureUsage::DepthStencil
        || desc.format == Format::D32_FLOAT;
    const DXGI_FORMAT dxgi = to_dxgi(desc.format);

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = is_depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* view_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&view_heap)))) {
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = dxgi;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = is_depth
        ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = dxgi;
    if (is_depth) {
        clear_value.DepthStencil.Depth = 1.f;
    } else {
        clear_value.Color[3] = 1.f;
    }

    const D3D12_RESOURCE_STATES initial = is_depth
        ? D3D12_RESOURCE_STATE_DEPTH_WRITE
        : D3D12_RESOURCE_STATE_COMMON;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            initial, &clear_value, IID_PPV_ARGS(&resource)))) {
        view_heap->Release();
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE view = view_heap->GetCPUDescriptorHandleForHeapStart();
    if (is_depth) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
        dsv_desc.Format = dxgi;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(resource, &dsv_desc, view);
        set_object_name(resource, "engine/transient_depth");
        set_object_name(view_heap, "engine/transient_dsv");
    } else {
        device_->CreateRenderTargetView(resource, nullptr, view);
        set_object_name(resource, "engine/transient_color");
        set_object_name(view_heap, "engine/transient_rtv");
    }

    return std::make_unique<D3D12Texture>(this, resource, desc.width, desc.height,
        desc.format, view_heap, view, is_depth, true);
}

void D3D12Device::write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    ENGINE_ASSERT_MSG(d3d_buffer.cpu_visible(), "write_buffer requires an upload-heap uniform buffer");
    ENGINE_ASSERT(data != nullptr);
    ENGINE_ASSERT(offset <= d3d_buffer.size());
    ENGINE_ASSERT(size <= d3d_buffer.size() - offset);

    if (u8* mapped = static_cast<u8*>(d3d_buffer.mapped())) {
        std::memcpy(mapped + offset, data, size);
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    if (SUCCEEDED(d3d_buffer.resource()->Map(0, &read_range, &mapped))) {
        std::memcpy(static_cast<u8*>(mapped) + offset, data, size);
        D3D12_RANGE write_range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)};
        d3d_buffer.resource()->Unmap(0, &write_range);
    }
}

void D3D12Device::read_buffer(IBuffer& buffer, usize offset, void* data, usize size) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    ENGINE_ASSERT(data != nullptr);
    ENGINE_ASSERT(offset <= d3d_buffer.size());
    ENGINE_ASSERT(size <= d3d_buffer.size() - offset);
    ENGINE_ASSERT_MSG(d3d_buffer.mapped() == nullptr,
        "read_buffer expects a readback buffer that is not persistently mapped");

    void* mapped = nullptr;
    D3D12_RANGE read_range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)};
    if (FAILED(d3d_buffer.resource()->Map(0, &read_range, &mapped)) || !mapped) {
        log(LogLevel::Error, LogChannel::Render, "D3D12 read_buffer: Map failed");
        return;
    }
    std::memcpy(data, static_cast<const u8*>(mapped) + offset, size);
    d3d_buffer.resource()->Unmap(0, nullptr);
}

FrameAllocation D3D12Device::alloc_frame_memory(usize size) {
    ENGINE_ASSERT(size > 0);
    ENGINE_ASSERT_MSG(frame_ring_[frame_index_] != nullptr, "frame constant ring is not initialized");
    const usize aligned = (size + (kBufferAlign - 1)) & ~(kBufferAlign - 1);

    // How much a frame needs depends on how much is on screen, so running out
    // is a content outcome, not a bug. Return an empty slice; the caller skips
    // that draw and the frame is missing an object instead of the process
    // being gone. Callers must check `buffer`.
    if (aligned > kFrameRingBytes - frame_ring_offset_) {
        if (!frame_ring_exhausted_) {
            frame_ring_exhausted_ = true;
            frame_ring_exhausted_frames_ += 1;
            char message[192];
            std::snprintf(message, sizeof(message),
                "Frame constant ring exhausted: %zu of %zu bytes used (~%zu instances). "
                "Dropping draws this frame - raise kFrameRingBytes.",
                frame_ring_offset_, kFrameRingBytes,
                frame_ring_offset_ / kFrameRingBytesPerInstance);
            log(LogLevel::Error, LogChannel::Render, message);
        }
        return {};
    }

    FrameAllocation allocation{};
    allocation.buffer = frame_ring_[frame_index_].get();
    allocation.offset = frame_ring_offset_;
    frame_ring_offset_ += aligned;
    if (frame_ring_offset_ > frame_ring_peak_) {
        frame_ring_peak_ = frame_ring_offset_;
    }
    return allocation;
}

void D3D12Device::set_debug_name(IBuffer& buffer, std::string_view name) {
    static_cast<D3D12Buffer&>(buffer).set_debug_name(name);
}

void D3D12Device::set_debug_name(ITexture& texture, std::string_view name) {
    static_cast<D3D12Texture&>(texture).set_debug_name(name);
}

ITexture& D3D12Device::swapchain_color() {
    return color_targets_[frame_index_];
}

ITexture& D3D12Device::swapchain_depth() {
    return depth_target_;
}

FrameRingStats D3D12Device::frame_ring_stats() const {
    FrameRingStats stats{};
    stats.peak_bytes = frame_ring_peak_;
    stats.capacity_bytes = kFrameRingBytes;
    stats.exhausted_frames = frame_ring_exhausted_frames_;
    return stats;
}

GpuMemoryStats D3D12Device::gpu_memory_stats() const {
    GpuMemoryStats stats{};
    if (!adapter3_) {
        return stats;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        return stats;
    }
    stats.local_usage_bytes  = info.CurrentUsage;
    stats.local_budget_bytes = info.Budget;
    return stats;
}

GpuBaseline D3D12Device::gpu_baseline() const {
    return baseline_;
}

void D3D12Device::retire_resource(ID3D12Resource* resource) {
    if (!resource) {
        return;
    }
    retired_.push_back(RetiredResource{resource, last_submitted_fence()});
}

UINT64 D3D12Device::signal_queue() {
    const UINT64 value = ++fence_cursor_;
    const HRESULT hr = queue_->Signal(fence_.get(), value);
    if (FAILED(hr)) {
        // Recording this value as the slot's fence would make wait_for_frame
        // block forever on a signal that is never coming. Keep the old value:
        // the slot's previous work is genuinely complete.
        log_device_error("Queue signal failed - frame fence not advanced", hr);
        return fence_values_[frame_index_];
    }
    fence_values_[frame_index_] = value;
    return value;
}

void D3D12Device::wait_for_copy() {
    if (copy_fence_value_ == 0) {
        return;
    }
    wait_for_fence_blocking(fence_event_, fence_.get(), copy_fence_value_);
}

void D3D12Device::begin_copy() {
    wait_for_copy();
    // Same two checks the frame path makes in begin_frame(). A list that failed
    // to open cannot be closed, and end_copy() below refuses to submit it - so
    // no recording flag is needed here, only the log line that says which step
    // went wrong.
    if (FAILED(copy_allocator_->Reset())) {
        log_device_error("Copy allocator reset failed - uploads this batch are dropped");
        return;
    }
    if (FAILED(copy_list_->Reset(copy_allocator_.get(), nullptr))) {
        log_device_error("Copy list reset failed - uploads this batch are dropped");
    }
}

UINT64 D3D12Device::end_copy() {
    // The frame path has checked this since day one, for the reason in the
    // comment above submit(): executing a list that is not closed is undefined
    // behaviour and typically removes the device - which would then be reported
    // as "GPU device lost ... not a content error", the opposite of the truth.
    // The copy path is where uploads of mesh, texture and mip data are
    // submitted, so it needs the same guard.
    //
    // Signal the queue even when nothing was submitted: callers wait on the
    // returned fence value, and returning an unsignalled one would hang them.
    if (FAILED(copy_list_->Close())) {
        log_device_error("Copy list close failed - copy not submitted");
        copy_fence_value_ = signal_queue();
        return copy_fence_value_;
    }
    ID3D12CommandList* lists[] = {copy_list_.get()};
    queue_->ExecuteCommandLists(1, lists);
    copy_fence_value_ = signal_queue();
    return copy_fence_value_;
}

UINT64 D3D12Device::last_submitted_fence() const {
    UINT64 value = fence_cursor_;
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (fence_values_[i] > value) {
            value = fence_values_[i];
        }
    }
    return value;
}

void D3D12Device::flush_retired() {
    if (!fence_ || retired_.empty()) {
        return;
    }

    const UINT64 completed = fence_->GetCompletedValue();
    usize keep = 0;
    for (usize i = 0; i < retired_.size(); ++i) {
        if (retired_[i].fence_value <= completed) {
            if (retired_[i].resource) {
                retired_[i].resource->Release();
            }
        } else {
            retired_[keep++] = retired_[i];
        }
    }
    retired_.resize(keep);
}

ID3D12Resource* D3D12Device::create_committed_buffer(D3D12_HEAP_TYPE heap, usize size,
    D3D12_RESOURCE_STATES initial_state, D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = heap;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width     = size;
    resource_desc.Height    = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout    = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags     = flags;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            initial_state, nullptr, IID_PPV_ARGS(&resource)))) {
        return nullptr;
    }
    return resource;
}

bool D3D12Device::upload_to_default(ID3D12Resource* dest, const void* data, usize size,
    D3D12_RESOURCE_STATES final_state) {
    const usize aligned_size = (size + (kBufferAlign - 1)) & ~(kBufferAlign - 1);
    ID3D12Resource* staging = create_committed_buffer(
        D3D12_HEAP_TYPE_UPLOAD, aligned_size, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!staging) {
        return false;
    }
    set_object_name(staging, "engine/staging");

    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    if (FAILED(staging->Map(0, &read_range, &mapped))) {
        retire_resource(staging);
        return false;
    }
    std::memcpy(mapped, data, size);
    staging->Unmap(0, nullptr);

    begin_copy();
    copy_list_->CopyBufferRegion(dest, 0, staging, 0, size);
    d3d_barrier(copy_list_.get(), dest, D3D12_RESOURCE_STATE_COPY_DEST, final_state);
    const UINT64 fence_value = end_copy();
    retired_.push_back(RetiredResource{staging, fence_value});
    return true;
}

bool D3D12Device::upload_texture(ID3D12Resource* dest, u32 width, u32 height, u32 mip_levels,
    u32 array_size, Format format, const void* data) {
    ENGINE_ASSERT(data != nullptr);
    ENGINE_ASSERT(mip_levels > 0 && mip_levels <= kMaxMips);
    ENGINE_ASSERT(array_size > 0);
    const u32 subresource_count = mip_levels * array_size;
    ENGINE_ASSERT(subresource_count <= kMaxMips * 16);

    const DXGI_FORMAT dxgi = to_dxgi(format);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(array_size);
    desc.MipLevels = static_cast<UINT16>(mip_levels);
    desc.Format = dxgi;
    desc.SampleDesc.Count = 1;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[kMaxMips * 16]{};
    UINT num_rows[kMaxMips * 16]{};
    UINT64 row_bytes[kMaxMips * 16]{};
    UINT64 upload_bytes = 0;
    device_->GetCopyableFootprints(&desc, 0, subresource_count, 0, footprints, num_rows, row_bytes,
        &upload_bytes);

    ID3D12Resource* staging = create_committed_buffer(
        D3D12_HEAP_TYPE_UPLOAD, static_cast<usize>(upload_bytes),
        D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!staging) {
        return false;
    }
    set_object_name(staging, "engine/texture_staging");

    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    if (FAILED(staging->Map(0, &read_range, &mapped))) {
        retire_resource(staging);
        return false;
    }

    const auto* src = static_cast<const u8*>(data);
    usize src_off = 0;
    for (u32 slice = 0; slice < array_size; ++slice) {
        u32 mip_w = width;
        u32 mip_h = height;
        for (u32 mip = 0; mip < mip_levels; ++mip) {
            const u32 sub = mip + slice * mip_levels;
            auto* dst = static_cast<u8*>(mapped) + footprints[sub].Offset;
            const UINT packed_row = static_cast<UINT>(mip_w * format_bytes_per_pixel(format));
            for (UINT row = 0; row < num_rows[sub]; ++row) {
                std::memcpy(dst + row * footprints[sub].Footprint.RowPitch,
                    src + src_off + row * packed_row, static_cast<usize>(row_bytes[sub]));
            }
            src_off += static_cast<usize>(mip_w) * mip_h * format_bytes_per_pixel(format);
            mip_w = mip_w > 1 ? mip_w / 2 : 1;
            mip_h = mip_h > 1 ? mip_h / 2 : 1;
        }
    }
    staging->Unmap(0, nullptr);

    begin_copy();
    for (u32 sub = 0; sub < subresource_count; ++sub) {
        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource = dest;
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = sub;
        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = staging;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = footprints[sub];
        copy_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
    }
    d3d_barrier(copy_list_.get(), dest, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    const UINT64 fence_value = end_copy();
    retired_.push_back(RetiredResource{staging, fence_value});
    return true;
}

std::unique_ptr<ITexture> D3D12Device::create_shadow_texture(const TextureDesc& desc) {
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ID3D12DescriptorHeap* dsv_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)))) {
        return nullptr;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)))) {
        dsv_heap->Release();
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.f;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value, IID_PPV_ARGS(&resource)))) {
        dsv_heap->Release();
        srv_heap->Release();
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(resource, &dsv_desc, dsv);

    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu = srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(resource, &srv, srv_cpu);

    set_object_name(resource, "engine/shadow_depth");
    set_object_name(dsv_heap, "engine/shadow_dsv");
    set_object_name(srv_heap, "engine/shadow_srv");

    auto texture = std::make_unique<D3D12Texture>(this, resource, desc.width, desc.height,
        desc.format, dsv_heap, dsv, true, true);
    texture->set_srv(srv_heap, srv_cpu);
    return texture;
}

std::unique_ptr<ITexture> D3D12Device::create_color_shader_resource_texture(const TextureDesc& desc) {
    const DXGI_FORMAT dxgi = to_dxgi(desc.format);

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)))) {
        return nullptr;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)))) {
        rtv_heap->Release();
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = dxgi;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
        rtv_heap->Release();
        srv_heap->Release();
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(resource, nullptr, rtv);

    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu = srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = dxgi;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(resource, &srv, srv_cpu);

    set_object_name(resource, "engine/hdr_color");
    set_object_name(rtv_heap, "engine/hdr_rtv");
    set_object_name(srv_heap, "engine/hdr_srv");

    auto texture = std::make_unique<D3D12Texture>(this, resource, desc.width, desc.height,
        desc.format, rtv_heap, rtv, false, true);
    texture->set_srv(srv_heap, srv_cpu);
    return texture;
}

std::unique_ptr<ITexture> D3D12Device::create_sampled_texture(const TextureDesc& desc,
    const void* data) {
    const DXGI_FORMAT dxgi = to_dxgi(desc.format);
    const u32 array_size = texture_array_size(desc);
    if (desc.dimension == TextureDimension::Cube && array_size != 6) {
        log(LogLevel::Error, LogChannel::Render, "D3D12 cube texture requires array_size 6");
        return nullptr;
    }
    if (desc.dimension == TextureDimension::Tex2D && desc.array_size > 1) {
        log(LogLevel::Error, LogChannel::Render,
            "D3D12 Tex2D cannot use array_size > 1; use Tex2DArray");
        return nullptr;
    }

    // Both 8-bit colour formats are four bytes per texel, so the same box filter
    // serves them; `srgb` only decides whether it averages light or bytes.
    const bool is_srgb = desc.format == Format::RGBA8_UNORM_SRGB;
    const bool rgba8 = desc.format == Format::RGBA8_UNORM || is_srgb;
    const bool can_mips = data && rgba8 && array_size == 1;
    const u32 mips = can_mips ? resolve_mip_count(desc) : (desc.mip_levels == 0 ? 1 : desc.mip_levels);
    std::vector<u8> mip_bytes;
    const void* upload_data = data;
    if (can_mips && mips > 1) {
        mip_bytes = math::build_rgba8_mip_chain(data, desc.width, desc.height, mips, is_srgb);
        upload_data = mip_bytes.data();
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* view_heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&view_heap)))) {
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = static_cast<UINT16>(array_size);
    resource_desc.MipLevels = static_cast<UINT16>(mips);
    resource_desc.Format = dxgi;
    resource_desc.SampleDesc.Count = 1;

    const D3D12_RESOURCE_STATES initial = data
        ? D3D12_RESOURCE_STATE_COPY_DEST
        : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            initial, nullptr, IID_PPV_ARGS(&resource)))) {
        view_heap->Release();
        return nullptr;
    }

    if (data && !upload_texture(resource, desc.width, desc.height, mips, array_size, desc.format,
            upload_data)) {
        resource->Release();
        view_heap->Release();
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = view_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = view_heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = dxgi;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (desc.dimension == TextureDimension::Cube) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = mips;
    } else if (desc.dimension == TextureDimension::Tex2DArray) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MipLevels = mips;
        srv.Texture2DArray.ArraySize = array_size;
    } else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = mips;
    }
    device_->CreateShaderResourceView(resource, &srv, cpu);
    set_object_name(resource, "engine/sampled");
    set_object_name(view_heap, "engine/sampled_srv");

    return std::make_unique<D3D12Texture>(this, resource, desc.width, desc.height,
        desc.format, view_heap, cpu, false, true, gpu, D3D12_CPU_DESCRIPTOR_HANDLE{}, mips,
        array_size, desc.dimension);
}

std::unique_ptr<IGraphicsPipeline> D3D12Device::create_graphics_pipeline(
    const GraphicsPipelineDesc& desc) {
    ENGINE_ASSERT(!desc.vertex_shader.empty());
    ENGINE_ASSERT(desc.attribute_count <= GraphicsPipelineDesc::kMaxAttributes);
    ENGINE_ASSERT(desc.sampler_count <= GraphicsPipelineDesc::kMaxSamplers);

    // One root parameter per constant buffer plus one per SRV, all into the
    // same array. Two independent `<= 8` checks let 2 + 7 through and wrote
    // past the end; the forward pipeline already sits at exactly 1 + 7, so
    // this was one texture away from a 16-byte stack overwrite carrying a
    // pointer, immediately before a driver call.
    ENGINE_ASSERT_MSG(
        desc.constant_buffer_count + desc.shader_resource_count
            + desc.structured_buffer_count <= kMaxRootParams,
        "graphics pipeline exceeds the root parameter budget "
        "(constant_buffer_count + shader_resource_count + structured_buffer_count)");
    ENGINE_ASSERT_MSG(desc.shader_resource_count <= kMaxRootRanges,
        "graphics pipeline exceeds the SRV descriptor-range budget");

    D3D12_ROOT_PARAMETER root_params[kMaxRootParams]{};
    for (u32 i = 0; i < desc.constant_buffer_count; ++i) {
        root_params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[i].Descriptor.ShaderRegister = i;
        root_params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_DESCRIPTOR_RANGE srv_ranges[kMaxRootRanges]{};
    u32 root_count = desc.constant_buffer_count;
    u32 srv_table_root = ~0u;
    if (desc.shader_resource_count > 0) {
        srv_table_root = root_count;
        for (u32 i = 0; i < desc.shader_resource_count; ++i) {
            srv_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srv_ranges[i].NumDescriptors = 1;
            srv_ranges[i].BaseShaderRegister = i;
            srv_ranges[i].RegisterSpace = 0;
            srv_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            root_params[root_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_params[root_count].DescriptorTable.NumDescriptorRanges = 1;
            root_params[root_count].DescriptorTable.pDescriptorRanges = &srv_ranges[i];
            root_params[root_count].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            ++root_count;
        }
    }

    // Root SRVs last, so srv_table_root keeps its meaning. Space 1 keeps them
    // clear of the t0.. texture registers, and ALL visibility is the whole
    // point - a vertex shader cannot read the pixel-visible tables above.
    u32 structured_root = ~0u;
    if (desc.structured_buffer_count > 0) {
        structured_root = root_count;
        for (u32 i = 0; i < desc.structured_buffer_count; ++i) {
            root_params[root_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            root_params[root_count].Descriptor.ShaderRegister = i;
            root_params[root_count].Descriptor.RegisterSpace = 1;
            root_params[root_count].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            ++root_count;
        }
    }

    D3D12_STATIC_SAMPLER_DESC samplers[GraphicsPipelineDesc::kMaxSamplers]{};
    for (u32 i = 0; i < desc.sampler_count; ++i) {
        fill_static_sampler(samplers[i], desc.samplers[i], i);
    }

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = root_count;
    root_desc.pParameters = root_count > 0 ? root_params : nullptr;
    root_desc.NumStaticSamplers = desc.sampler_count;
    root_desc.pStaticSamplers = desc.sampler_count > 0 ? samplers : nullptr;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* root_blob = nullptr;
    ID3DBlob* root_error = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &root_blob, &root_error))) {
        if (root_error) root_error->Release();
        return nullptr;
    }

    ID3D12RootSignature* root_sig = nullptr;
    if (FAILED(device_->CreateRootSignature(0, root_blob->GetBufferPointer(),
            root_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)))) {
        root_blob->Release();
        return nullptr;
    }
    root_blob->Release();

    D3D12_INPUT_ELEMENT_DESC input_layout[GraphicsPipelineDesc::kMaxAttributes]{};
    for (u32 i = 0; i < desc.attribute_count; ++i) {
        const VertexAttribute& attribute = desc.attributes[i];
        const char* semantic = "POSITION";
        switch (attribute.semantic) {
        case VertexSemantic::Position: semantic = "POSITION"; break;
        case VertexSemantic::Normal:   semantic = "NORMAL"; break;
        case VertexSemantic::Color:    semantic = "COLOR"; break;
        case VertexSemantic::TexCoord: semantic = "TEXCOORD"; break;
        }

        DXGI_FORMAT format = DXGI_FORMAT_R32G32B32_FLOAT;
        switch (attribute.format) {
        case VertexFormat::Float2: format = DXGI_FORMAT_R32G32_FLOAT; break;
        case VertexFormat::Float3: format = DXGI_FORMAT_R32G32B32_FLOAT; break;
        case VertexFormat::Float4: format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        }

        input_layout[i].SemanticName = semantic;
        input_layout[i].SemanticIndex = attribute.semantic_index;
        input_layout[i].Format = format;
        input_layout[i].InputSlot = 0;
        input_layout[i].AlignedByteOffset = attribute.offset;
        input_layout[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        input_layout[i].InstanceDataStepRate = 0;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig;
    pso_desc.VS = {desc.vertex_shader.data(), desc.vertex_shader.size()};
    if (!desc.pixel_shader.empty()) {
        pso_desc.PS = {desc.pixel_shader.data(), desc.pixel_shader.size()};
    }
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.FrontCounterClockwise = TRUE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.RasterizerState.SlopeScaledDepthBias = desc.slope_scaled_depth_bias;
    switch (desc.cull) {
    case CullMode::None:  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
    case CullMode::Back:  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
    case CullMode::Front: pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
    }

    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (desc.blend == BlendMode::Alpha) {
        pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    pso_desc.SampleMask = UINT_MAX;
    if (desc.depth != DepthTest::Disabled) {
        pso_desc.DepthStencilState.DepthEnable = TRUE;
        pso_desc.DepthStencilState.DepthWriteMask = desc.depth_write
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
        D3D12_COMPARISON_FUNC depth_func = D3D12_COMPARISON_FUNC_LESS;
        if (desc.depth == DepthTest::LessEqual) {
            depth_func = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        } else if (desc.depth == DepthTest::Equal) {
            depth_func = D3D12_COMPARISON_FUNC_EQUAL;
        }
        pso_desc.DepthStencilState.DepthFunc = depth_func;
        pso_desc.DSVFormat = to_dxgi(desc.depth_format);
    } else {
        pso_desc.DepthStencilState.DepthEnable = FALSE;
        pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    }

    pso_desc.InputLayout = {desc.attribute_count > 0 ? input_layout : nullptr, desc.attribute_count};
    D3D12_PRIMITIVE_TOPOLOGY ia_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (desc.topology == PrimitiveTopology::LineList) {
        ia_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    }
    if (desc.color_format == Format::Unknown) {
        pso_desc.NumRenderTargets = 0;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    } else {
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = to_dxgi(desc.color_format);
    }
    pso_desc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        root_sig->Release();
        return nullptr;
    }

    char name[32] = "pipeline";
    if (!desc.debug_name.empty()) {
        const usize n = desc.debug_name.size() < sizeof(name) - 1
            ? desc.debug_name.size() : sizeof(name) - 1;
        std::memcpy(name, desc.debug_name.data(), n);
        name[n] = '\0';
    }
    char root_name[64];
    char pso_name[64];
    std::snprintf(root_name, sizeof(root_name), "engine/root_%s", name);
    std::snprintf(pso_name, sizeof(pso_name), "engine/pso_%s", name);
    set_object_name(root_sig, root_name);
    set_object_name(pso, pso_name);
    return std::make_unique<D3D12Pipeline>(pso, root_sig, srv_table_root, structured_root,
        ia_topology);
}

std::unique_ptr<IComputePipeline> D3D12Device::create_compute_pipeline(const ComputePipelineDesc& desc) {
    if (desc.compute_shader.empty()) {
        log(LogLevel::Error, LogChannel::Render, "D3D12 create_compute_pipeline: empty bytecode");
        return nullptr;
    }
    ENGINE_ASSERT_MSG(desc.constant_buffer_count + desc.shader_resource_count
            + desc.unordered_access_count <= kMaxRootParams,
        "compute pipeline exceeds the root parameter budget "
        "(constant_buffer_count + shader_resource_count + unordered_access_count)");
    ENGINE_ASSERT_MSG(desc.shader_resource_count <= kMaxRootRanges
            && desc.unordered_access_count <= kMaxRootRanges,
        "compute pipeline exceeds the descriptor-range budget");

    D3D12_ROOT_PARAMETER root_params[kMaxRootParams]{};
    D3D12_DESCRIPTOR_RANGE uav_ranges[kMaxRootRanges]{};
    D3D12_DESCRIPTOR_RANGE srv_ranges[kMaxRootRanges]{};
    u32 root_count = 0;
    for (u32 i = 0; i < desc.constant_buffer_count; ++i) {
        root_params[root_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[root_count].Descriptor.ShaderRegister = i;
        root_params[root_count].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ++root_count;
    }

    u32 uav_table_root = ~0u;
    if (desc.unordered_access_count > 0) {
        uav_table_root = root_count;
        for (u32 i = 0; i < desc.unordered_access_count; ++i) {
            uav_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uav_ranges[i].NumDescriptors = 1;
            uav_ranges[i].BaseShaderRegister = i;
            uav_ranges[i].RegisterSpace = 0;
            uav_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            root_params[root_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_params[root_count].DescriptorTable.NumDescriptorRanges = 1;
            root_params[root_count].DescriptorTable.pDescriptorRanges = &uav_ranges[i];
            root_params[root_count].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            ++root_count;
        }
    }

    u32 srv_table_root = ~0u;
    if (desc.shader_resource_count > 0) {
        srv_table_root = root_count;
        for (u32 i = 0; i < desc.shader_resource_count; ++i) {
            srv_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srv_ranges[i].NumDescriptors = 1;
            srv_ranges[i].BaseShaderRegister = i;
            srv_ranges[i].RegisterSpace = 0;
            srv_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            root_params[root_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_params[root_count].DescriptorTable.NumDescriptorRanges = 1;
            root_params[root_count].DescriptorTable.pDescriptorRanges = &srv_ranges[i];
            root_params[root_count].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            ++root_count;
        }
    }

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = root_count;
    root_desc.pParameters = root_count > 0 ? root_params : nullptr;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* root_blob = nullptr;
    ID3DBlob* root_error = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &root_blob, &root_error))) {
        if (root_error) root_error->Release();
        log(LogLevel::Error, LogChannel::Render, "D3D12 create_compute_pipeline: root signature failed");
        return nullptr;
    }

    ID3D12RootSignature* root_sig = nullptr;
    if (FAILED(device_->CreateRootSignature(0, root_blob->GetBufferPointer(),
            root_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)))) {
        root_blob->Release();
        return nullptr;
    }
    root_blob->Release();

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig;
    pso_desc.CS = {desc.compute_shader.data(), desc.compute_shader.size()};

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(device_->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        root_sig->Release();
        log(LogLevel::Error, LogChannel::Render, "D3D12 create_compute_pipeline: PSO failed");
        return nullptr;
    }

    char name[32] = "compute";
    if (!desc.debug_name.empty()) {
        const usize n = desc.debug_name.size() < sizeof(name) - 1
            ? desc.debug_name.size() : sizeof(name) - 1;
        std::memcpy(name, desc.debug_name.data(), n);
        name[n] = '\0';
    }
    char root_name[64];
    char pso_name[64];
    std::snprintf(root_name, sizeof(root_name), "engine/root_%s", name);
    std::snprintf(pso_name, sizeof(pso_name), "engine/pso_%s", name);
    set_object_name(root_sig, root_name);
    set_object_name(pso, pso_name);
    return std::make_unique<D3D12ComputePipeline>(pso, root_sig, uav_table_root, srv_table_root);
}

std::unique_ptr<ISampler> D3D12Device::create_sampler(const SamplerDesc& desc) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.NumDescriptors = 1;
    ID3D12DescriptorHeap* heap = nullptr;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap))) || !heap) {
        log(LogLevel::Error, LogChannel::Render, "D3D12 create_sampler: descriptor heap failed");
        return nullptr;
    }
    D3D12_SAMPLER_DESC sampler_desc{};
    fill_runtime_sampler(sampler_desc, desc);
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateSampler(&sampler_desc, cpu);
    return std::make_unique<D3D12Sampler>(heap, cpu, desc);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::current_rtv() const { return rtv_handles_[frame_index_]; }
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::current_dsv() const { return dsv_handle_; }
u32 D3D12Device::width() const { return width_; }
u32 D3D12Device::height() const { return height_; }
bool D3D12Device::device_lost() const { return device_lost_ || device_removed(); }
u32 D3D12Device::frame_index() const { return frame_index_; }

void D3D12Device::present_back_buffer() {
    if (!swapchain_) {
        return;
    }
    UINT flags = 0;
    if (present_interval_ == 0 && allow_tearing_) {
        flags = DXGI_PRESENT_ALLOW_TEARING;
    }
    const HRESULT hr = swapchain_->Present(present_interval_, flags);

    // Present is where a TDR, a driver update or a GPU hang first shows up.
    // Discarding this result meant the loop kept calling into a dead device
    // forever: every call failed silently and the window froze on its last
    // frame until the process was killed.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        device_lost_ = true;
        log_device_error("Present failed - GPU device lost", hr);
        return;
    }
    if (hr == DXGI_STATUS_OCCLUDED) {
        // Fully covered or minimised: the swapchain stops throttling, so an
        // unthrottled loop burns a core. Ask for vblank pacing next present.
        occluded_ = true;
        return;
    }
    occluded_ = false;
    if (FAILED(hr)) {
        log_device_error("Present failed", hr);
    }
}

void D3D12Device::set_present_interval(u32 interval) {
    present_interval_ = interval == 0 ? 0u : 1u;
}

u32 D3D12Device::present_interval() const {
    return present_interval_;
}

void D3D12Device::wait_for_frame(u32 index) {
    wait_for_fence_blocking(fence_event_, fence_.get(), fence_values_[index]);
}

void D3D12Device::wait_for_gpu() {
    if (!fence_ || !queue_ || device_removed()) return;
    const UINT64 value = ++fence_cursor_;
    if (FAILED(queue_->Signal(fence_.get(), value))) {
        return;
    }
    fence_values_[frame_index_] = value;
    wait_for_fence_blocking(fence_event_, fence_.get(), value);
}

class D3D12RHI final : public IRHI {
public:
    std::unique_ptr<IDevice> create_device(const DeviceDesc& desc) override {
        auto device = std::make_unique<D3D12Device>();
        if (!device->init(desc)) return nullptr;
        return device;
    }

    std::string_view name() const override { return "d3d12"; }
    GraphicsAPI api() const override { return GraphicsAPI::D3D12; }
};

std::unique_ptr<IRHI> create_rhi() {
    return std::make_unique<D3D12RHI>();
}

} // namespace engine::rhi::d3d12
