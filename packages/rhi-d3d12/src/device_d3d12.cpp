#include <engine/rhi/d3d12/device_d3d12.hpp>

#include <engine/core/log.hpp>

#include <d3d12shader.h>
#include <cstring>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace engine::rhi::d3d12 {

namespace {

constexpr u32 kFrameCount = 3;

void transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
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

bool gpu_debug_enabled() {
    char value[8] = {};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0 && value[0] == '1';
}

void pump_win32_messages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void wait_with_message_pump(HANDLE event, ID3D12Fence* fence, UINT64 value) {
    if (fence->GetCompletedValue() >= value) {
        return;
    }

    for (;;) {
        fence->SetEventOnCompletion(value, event);
        const DWORD result = MsgWaitForMultipleObjects(1, &event, FALSE, 16, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) {
            return;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            pump_win32_messages();
            continue;
        }
        if (result == WAIT_TIMEOUT) {
            if (fence->GetCompletedValue() >= value) {
                return;
            }
            continue;
        }
        return;
    }
}

} // namespace

D3D12Buffer::D3D12Buffer(ID3D12Resource* resource, usize size)
    : resource_(resource), size_(size) {}

D3D12Buffer::~D3D12Buffer() {
    if (resource_) resource_->Release();
}

usize D3D12Buffer::size() const { return size_; }
ID3D12Resource* D3D12Buffer::resource() const { return resource_; }

D3D12Pipeline::D3D12Pipeline(ID3D12PipelineState* pso, ID3D12RootSignature* root_sig)
    : pso_(pso), root_sig_(root_sig) {}

D3D12Pipeline::~D3D12Pipeline() {
    if (pso_) pso_->Release();
    if (root_sig_) root_sig_->Release();
}

ID3D12PipelineState* D3D12Pipeline::pso() const { return pso_; }
ID3D12RootSignature* D3D12Pipeline::root_signature() const { return root_sig_; }

D3D12CommandList::D3D12CommandList(D3D12Device& device) : device_(device) {}

void D3D12CommandList::begin() { recording_ = true; }

void D3D12CommandList::end() {
    if (in_pass_) end_render_pass();
    recording_ = false;
}

void D3D12CommandList::begin_render_pass(const RenderPassInfo& info) {
    auto* cmd = device_.d3d12_cmd_list();
    auto rtv = device_.current_rtv();
    auto dsv = device_.current_dsv();
    auto w = device_.width();
    auto h = device_.height();

    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<f32>(w);
    viewport.Height   = static_cast<f32>(h);
    viewport.MaxDepth = 1.f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    cmd->RSSetScissorRects(1, &scissor);

    if (info.clear_depth) {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        if (info.clear_color_target) {
            const f32 clear_color[] = {info.clear_color.r, info.clear_color.g,
                info.clear_color.b, info.clear_color.a};
            cmd->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        }
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    } else {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        if (info.clear_color_target) {
            const f32 clear_color[] = {info.clear_color.r, info.clear_color.g,
                info.clear_color.b, info.clear_color.a};
            cmd->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        }
    }
    in_pass_ = true;
}

void D3D12CommandList::end_render_pass() { in_pass_ = false; }

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
    auto* cmd = device_.d3d12_cmd_list();
    cmd->SetGraphicsRootSignature(d3d_pipeline.root_signature());
    cmd->SetPipelineState(d3d_pipeline.pso());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    device_.d3d12_cmd_list()->SetGraphicsRootConstantBufferView(slot, address);
}

void D3D12CommandList::draw(u32 vertex_count, u32 start_vertex) {
    device_.d3d12_cmd_list()->DrawInstanced(vertex_count, 1, start_vertex, 0);
}

void D3D12CommandList::draw_indexed(u32 index_count, u32 start_index, i32 base_vertex) {
    device_.d3d12_cmd_list()->DrawIndexedInstanced(index_count, 1, start_index, base_vertex, 0);
}

D3D12Swapchain::D3D12Swapchain(D3D12Device& device) : device_(device) {}
void D3D12Swapchain::present() {}
u32 D3D12Swapchain::current_back_buffer_index() const { return device_.frame_index(); }

D3D12Device::D3D12Device()
    : cmd_wrapper_(*this), swapchain_wrapper_(*this) {}

D3D12Device::~D3D12Device() {
    if (fence_) wait_for_gpu();
    cleanup_swapchain_resources();
    if (fence_event_) CloseHandle(fence_event_);
    if (fence_) fence_->Release();
    if (cmd_list_) cmd_list_->Release();
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (frame_allocators_[i]) frame_allocators_[i]->Release();
    }
    if (rtv_heap_) rtv_heap_->Release();
    cleanup_depth_buffer();
    if (swapchain_) swapchain_->Release();
    if (queue_) queue_->Release();
    if (device_) device_->Release();
}

bool D3D12Device::init(const DeviceDesc& desc) {
    width_  = desc.width;
    height_ = desc.height;
    hwnd_   = static_cast<HWND>(desc.window_handle);

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

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        adapter->GetDesc1(&adapter_desc);
        if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
            adapter->Release();
            break;
        }
        adapter->Release();
        adapter = nullptr;
    }

    if (!device_) {
        factory->Release();
        log(LogLevel::Error, LogChannel::Render, "D3D12 device creation failed");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)))) {
        factory->Release();
        log(LogLevel::Error, LogChannel::Render, "Command queue creation failed");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = width_;
    sd.Height      = height_;
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrameCount;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* swapchain1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        queue_, hwnd_, &sd, nullptr, nullptr, &swapchain1);
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();

    if (FAILED(hr)) {
        log(LogLevel::Error, LogChannel::Render, "Swapchain creation failed");
        return false;
    }

    swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain_));
    swapchain1->Release();

    if (!create_render_targets() || !create_depth_buffer() || !create_frame_resources()) {
        return false;
    }

    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    log(LogLevel::Info, LogChannel::Render, "D3D12 device initialized");
    return true;
}

bool D3D12Device::create_render_targets() {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = kFrameCount;
    heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)))) {
        return false;
    }

    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])))) {
            return false;
        }
        device_->CreateRenderTargetView(render_targets_[i], nullptr, handle);
        rtv_handles_[i] = handle;
        handle.ptr += rtv_descriptor_size_;
    }
    return true;
}

bool D3D12Device::create_depth_buffer() {
    cleanup_depth_buffer();

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap_)))) {
        return false;
    }

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
            IID_PPV_ARGS(&depth_buffer_)))) {
        return false;
    }

    dsv_handle_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth_buffer_, &dsv_desc, dsv_handle_);
    return true;
}

void D3D12Device::cleanup_depth_buffer() {
    if (depth_buffer_) {
        depth_buffer_->Release();
        depth_buffer_ = nullptr;
    }
    if (dsv_heap_) {
        dsv_heap_->Release();
        dsv_heap_ = nullptr;
    }
    dsv_handle_ = {};
}

bool D3D12Device::create_frame_resources() {
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (FAILED(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame_allocators_[i])))) {
            return false;
        }
    }

    if (FAILED(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame_allocators_[0], nullptr,
            IID_PPV_ARGS(&cmd_list_)))) {
        return false;
    }
    cmd_list_->Close();

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        return false;
    }

    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) return false;

    fence_values_[0] = fence_values_[1] = fence_values_[2] = 0;
    return true;
}

void D3D12Device::cleanup_swapchain_resources() {
    for (u32 i = 0; i < kFrameCount; ++i) {
        if (render_targets_[i]) {
            render_targets_[i]->Release();
            render_targets_[i] = nullptr;
        }
    }
}

ISwapchain& D3D12Device::swapchain() { return swapchain_wrapper_; }
ICommandList& D3D12Device::command_list() { return cmd_wrapper_; }

void D3D12Device::begin_frame() {
    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    wait_for_frame(frame_index_);

    frame_allocators_[frame_index_]->Reset();
    cmd_list_->Reset(frame_allocators_[frame_index_], nullptr);

    transition(cmd_list_, render_targets_[frame_index_],
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void D3D12Device::end_frame() {
    transition(cmd_list_, render_targets_[frame_index_],
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    cmd_list_->Close();

    ID3D12CommandList* lists[] = {cmd_list_};
    queue_->ExecuteCommandLists(1, lists);

    swapchain_->Present(1, 0);

    const UINT64 signal_value = fence_values_[frame_index_] + 1;
    queue_->Signal(fence_, signal_value);
    fence_values_[frame_index_] = signal_value;
}

void D3D12Device::wait_idle() {
    wait_for_gpu();
}

bool D3D12Device::resize(u32 width, u32 height) {
    if (width == 0 || height == 0) return true;
    if (width_ == width && height_ == height) return true;

    const u32 old_width  = width_;
    const u32 old_height = height_;

    wait_for_gpu();
    cleanup_swapchain_resources();
    cleanup_depth_buffer();

    if (FAILED(swapchain_->ResizeBuffers(kFrameCount, width, height,
            DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
        log(LogLevel::Error, LogChannel::Render, "Swapchain resize failed — recovering");

        if (FAILED(swapchain_->ResizeBuffers(kFrameCount, old_width, old_height,
                DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
            return false;
        }

        width_  = old_width;
        height_ = old_height;
        frame_index_ = swapchain_->GetCurrentBackBufferIndex();
        if (!create_render_targets() || !create_depth_buffer()) {
            return false;
        }
        return false;
    }

    width_  = width;
    height_ = height;
    frame_index_ = swapchain_->GetCurrentBackBufferIndex();

    if (!create_render_targets() || !create_depth_buffer()) {
        return false;
    }
    return true;
}

std::unique_ptr<IBuffer> D3D12Device::create_buffer(const BufferDesc& desc, const void* data) {
    const usize aligned_size = (desc.size + 255) & ~static_cast<usize>(255);

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width     = aligned_size;
    resource_desc.Height    = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout    = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)))) {
        return nullptr;
    }

    if (data) {
        void* mapped = nullptr;
        D3D12_RANGE read_range{0, 0};
        if (SUCCEEDED(resource->Map(0, &read_range, &mapped))) {
            std::memcpy(mapped, data, desc.size);
            resource->Unmap(0, nullptr);
        }
    }

    return std::make_unique<D3D12Buffer>(resource, aligned_size);
}

void D3D12Device::write_buffer(IBuffer& buffer, usize offset, const void* data, usize size) {
    auto& d3d_buffer = static_cast<D3D12Buffer&>(buffer);
    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    if (SUCCEEDED(d3d_buffer.resource()->Map(0, &read_range, &mapped))) {
        std::memcpy(static_cast<u8*>(mapped) + offset, data, size);
        D3D12_RANGE write_range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)};
        d3d_buffer.resource()->Unmap(0, &write_range);
    }
}

std::unique_ptr<IGraphicsPipeline> D3D12Device::create_graphics_pipeline(
    std::span<const u8> vertex_shader,
    std::span<const u8> pixel_shader) {

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
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

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig;
    pso_desc.VS = {vertex_shader.data(), vertex_shader.size()};
    pso_desc.PS = {pixel_shader.data(), pixel_shader.size()};
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.InputLayout = {input_layout, 2};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        root_sig->Release();
        return nullptr;
    }

    return std::make_unique<D3D12Pipeline>(pso, root_sig);
}

std::unique_ptr<IGraphicsPipeline> D3D12Device::create_forward_pipeline(
    std::span<const u8> vertex_shader,
    std::span<const u8> pixel_shader) {

    D3D12_ROOT_PARAMETER root_param{};
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_param.Descriptor.ShaderRegister = 0;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 1;
    root_desc.pParameters   = &root_param;
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

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig;
    pso_desc.VS = {vertex_shader.data(), vertex_shader.size()};
    pso_desc.PS = {pixel_shader.data(), pixel_shader.size()};
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso_desc.InputLayout = {input_layout, 2};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        root_sig->Release();
        return nullptr;
    }

    return std::make_unique<D3D12Pipeline>(pso, root_sig);
}

std::unique_ptr<IGraphicsPipeline> D3D12Device::create_overlay_pipeline(
    std::span<const u8> vertex_shader,
    std::span<const u8> pixel_shader) {

    D3D12_ROOT_PARAMETER root_param{};
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_param.Descriptor.ShaderRegister = 0;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 1;
    root_desc.pParameters   = &root_param;
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

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig;
    pso_desc.VS = {vertex_shader.data(), vertex_shader.size()};
    pso_desc.PS = {pixel_shader.data(), pixel_shader.size()};
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.InputLayout = {input_layout, 1};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        root_sig->Release();
        return nullptr;
    }

    return std::make_unique<D3D12Pipeline>(pso, root_sig);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::current_rtv() const { return rtv_handles_[frame_index_]; }
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::current_dsv() const { return dsv_handle_; }
u32 D3D12Device::width() const { return width_; }
u32 D3D12Device::height() const { return height_; }
u32 D3D12Device::frame_index() const { return frame_index_; }
void D3D12Device::present() {}

void D3D12Device::wait_for_frame(u32 index) {
    wait_with_message_pump(fence_event_, fence_, fence_values_[index]);
}

void D3D12Device::wait_for_gpu() {
    if (!fence_ || !queue_) return;
    const UINT64 value = fence_values_[frame_index_] + 1;
    queue_->Signal(fence_, value);
    fence_values_[frame_index_] = value;
    wait_with_message_pump(fence_event_, fence_, value);
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
