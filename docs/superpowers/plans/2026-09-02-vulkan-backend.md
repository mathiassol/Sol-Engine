# A second GPU backend — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans`
> to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking. There is no worktree and no PR — this repo is trunk-based, commit to
> `main` and push (CLAUDE.md). The "test" for every task is a **gate**; write it
> first and watch it fail.

**Goal:** `rhi-vulkan` renders and reads back the same image as `rhi-d3d12`,
through the same `rhi` contract and the same HLSL source, proven by one gate
function run twice — once per backend.

**Architecture:** An **offscreen** Vulkan device (no surface, no swapchain)
verified inside the ordinary `--gates` run, so the second backend is checked on
every run rather than behind a flag. Two contract gaps get filled first —
`window_handle == nullptr` meaning offscreen, and `read_texture` as the missing
twin of `read_buffer` — on D3D12, which establishes the reference numbers
through the new API *before* Vulkan exists to compare against.

**Tech Stack:** Vulkan 1.3 core (dynamic rendering), volk, vendored
Vulkan-Headers, DXC's SPIR-V backend from the Vulkan SDK, C++20, CMake.

Spec: [2026-09-02-vulkan-backend-design.md](../specs/2026-09-02-vulkan-backend-design.md)

---

## File structure

**New:**

| File | Responsibility |
|------|----------------|
| `packages/sandbox/content/shaders/backend_parity_gate.hlsl` | The one shader both backends draw. A `cbuffer` at `b0` on purpose, so the gate exercises descriptor plumbing rather than a resourceless triangle. |
| `packages/rhi-vulkan/CMakeLists.txt` | Package definition; points volk at the vendored header. |
| `packages/rhi-vulkan/include/engine/rhi/vulkan/rhi_vulkan.hpp` | `engine::rhi::vulkan::create_rhi()`. Mirrors the d3d12 header exactly. |
| `packages/rhi-vulkan/src/vulkan_common.hpp` | volk include, `to_string(VkResult)`, `vk_failed()` logging helper. The only place the vendored headers are included from. |
| `packages/rhi-vulkan/src/instance_vulkan.cpp` | volk init, instance, validation layer, debug messenger, physical-device selection. |
| `packages/rhi-vulkan/src/device_vulkan.hpp` | Every class in the backend. One header, because they reference each other. |
| `packages/rhi-vulkan/src/device_vulkan.cpp` | Logical device, queue, memory helper, buffers, textures, `read_texture`. |
| `packages/rhi-vulkan/src/pipeline_vulkan.cpp` | Descriptor-set layouts from the five counts, graphics pipeline creation. |
| `packages/rhi-vulkan/src/commands_vulkan.cpp` | The command list: dynamic rendering, barriers, binds, draws, and the not-yet-implemented virtuals. |
| `packages/rhi-vulkan/src/rhi_vulkan.cpp` | The factory. |
| `packages/rhi-vulkan/third_party/` | `vulkan/vk_platform.h`, `vulkan/vulkan_core.h`, `vk_video/*`, `volk.h`, `volk.c`, `LICENSE-vulkan-headers.txt`, `LICENSE-volk.txt`. |

Four `.cpp` files rather than one, deliberately: `rhi-d3d12/src/device_d3d12.cpp`
is 3,287 lines and the largest file in the engine. Splitting the second backend
by responsibility from the first commit avoids repeating that.

**Modified:**

| File | Change |
|------|--------|
| `packages/rhi/include/engine/rhi/rhi.hpp` | Document `window_handle == nullptr`. |
| `packages/rhi/include/engine/rhi/device.hpp` | `offscreen()`, `read_texture()`. |
| `packages/rhi-d3d12/src/device_d3d12.hpp/.cpp` | Offscreen branch, `offscreen()`, `read_texture()`. |
| `packages/shaders-dxc/src/shader_compiler_dxc.cpp` | The SPIR-V path. |
| `packages/shaders-dxc/include/engine/shaders/dxc/shader_compiler_dxc.hpp` | Nothing — the factory signature is unchanged. Listed so it is not edited by reflex. |
| `packages/sandbox/src/sandbox_common.hpp/.cpp` | Shader path constants, `count_lit_texels()`. |
| `packages/sandbox/src/gates/gates.hpp` | Three gate declarations. |
| `packages/sandbox/src/gates/gates_rhi.cpp` | Three gate definitions. |
| `packages/sandbox/src/gates/gate_registry.cpp` | Three `kGates` entries. |
| `packages/sandbox/src/main.cpp` | Three call sites. |
| `CMakeLists.txt` | `ENGINE_RHI_VULKAN`; widen the `shaders-dxc` gate. |
| `tools/check-invariants.ps1` | `$Layers` entry; Vulkan API-isolation rule; Vulkan terms in `rhi-vocabulary`. |
| `.github/workflows/ci.yml` | Build with `-DENGINE_RHI_VULKAN=ON`. |
| `docs/ROADMAP.md`, `docs/ENGINE_MAP.md` | The record. |
| `.claude/rules/renderer-boundaries.md` | The one-backend line is now two. |

---

## Task 1: The two contract gaps, on D3D12

**Cost:** medium · **Closes:** nothing yet — establishes the reference numbers.

Doing this on D3D12 first is the point. It means the gate, the shader, the
expected pixel values and the coverage count are all *measured on a working
backend* before Vulkan exists, so when the Vulkan gate disagrees there is no
question about which side is wrong.

**Files:**
- Create: `packages/sandbox/content/shaders/backend_parity_gate.hlsl`
- Modify: `packages/rhi/include/engine/rhi/rhi.hpp`,
  `packages/rhi/include/engine/rhi/device.hpp`,
  `packages/rhi-d3d12/src/device_d3d12.hpp`,
  `packages/rhi-d3d12/src/device_d3d12.cpp`,
  `packages/sandbox/src/sandbox_common.hpp`,
  `packages/sandbox/src/sandbox_common.cpp`,
  `packages/sandbox/src/gates/gates.hpp`,
  `packages/sandbox/src/gates/gates_rhi.cpp`,
  `packages/sandbox/src/gates/gate_registry.cpp`,
  `packages/sandbox/src/main.cpp`

- [ ] **Step 1.1: Write the shader both backends will draw**

`packages/sandbox/content/shaders/backend_parity_gate.hlsl`:

```hlsl
// The one shader both backends draw, so a divergence between them cannot be a
// difference in what they were asked to draw.
//
// A cbuffer at b0 on purpose. Without one this gate would exercise no
// descriptor plumbing at all, and translating resources.hpp's five counts into
// a descriptor-set layout is exactly what a second backend has to get right.
// Under SPIR-V, b0 lands at set 0 binding 0 - see the register shifts in
// shaders-dxc, which exist because the default HLSL mapping ignores the
// register type and would otherwise collide b0 with t0.
cbuffer ParityConstants : register(b0) {
    // Each channel is exactly representable in UNORM8 - 51/255, 153/255,
    // 204/255 - so the readback compares exact bytes instead of a tolerance,
    // and no channel sits on a .5 rounding tie.
    float4 tint;
};

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput vs_main(uint id : SV_VertexID) {
    // Clip space directly: no vertex buffer and no input layout, so the gate
    // covers one thing at a time. Top-left, top-right, bottom-left - the
    // upper-left half of the target, with the hypotenuse corner to corner.
    //
    // Which half it is matters. A backend that gets the Y direction wrong
    // draws the *lower-right* half, and the two interior probes below are
    // placed to catch exactly that rather than to average it away.
    const float2 corners[3] = {
        float2(-1.0,  1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
    };
    VSOutput output;
    output.pos = float4(corners[id], 0.0, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    return tint;
}
```

- [ ] **Step 1.2: Extend the contract**

In `packages/rhi/include/engine/rhi/rhi.hpp`, replace the `window_handle` line
of `DeviceDesc`:

```cpp
struct DeviceDesc {
    // Null means an offscreen device: no surface, no swapchain, no
    // presentation, everything else identical. swapchain(), swapchain_color()
    // and swapchain_depth() are then programming errors and assert by name -
    // a null-object swapchain whose present() quietly does nothing is the
    // failure mode this engine is built to avoid.
    //
    // Beyond a second backend this is real capability: a GPU gate that needs
    // no window.
    void* window_handle = nullptr;
```

In `packages/rhi/include/engine/rhi/device.hpp`, add to `IDevice` immediately
after `depth_convention()`:

```cpp
    // True when created with a null window_handle. See DeviceDesc.
    virtual bool offscreen() const = 0;
```

and immediately after `read_buffer`:

```cpp
    // Copies mip 0 of a texture into CPU memory, tightly packed, rows in
    // top-down order. Submits and waits, so it is for gates and tools and
    // never for a frame.
    //
    // The twin of read_buffer, which existed alone. Its absence is why the
    // MSAA gate reads its render target through a compute shader and a UAV -
    // a lot of machinery to fetch four numbers.
    //
    // False, with the reason logged, when `size` does not match
    // width * height * bytes_per_texel or the format is not one it can pack.
    // Never a partial read into a buffer the caller believes is full.
    virtual bool read_texture(ITexture& texture, void* out, usize size) = 0;
```

- [ ] **Step 1.3: Write the gate, and watch it fail to compile**

The gate is parameterised by device on purpose: **this same function runs
against the Vulkan device in Task 4.** Add to
`packages/sandbox/src/gates/gates_rhi.cpp` above `run_pix_gate`:

```cpp
bool run_backend_parity_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api, engine::u32& lit_out) {
    // The gate that makes "the contract survives a second backend" checkable
    // rather than asserted. One function, one shader, two devices - so a
    // divergence is a backend difference and cannot be a difference in the
    // test.
    //
    // Five assertions, none of which is "it did not crash":
    //   1. the device reports itself offscreen
    //   2. the shader compiled to the requested target
    //   3. an interior texel of the drawn half carries the cbuffer's tint,
    //      byte-exact - which proves the pipeline, the descriptor binding and
    //      the readback at once
    //   4. the mirrored texel in the *other* half is still the clear colour -
    //      which is what catches an inverted Y rather than averaging over it
    //   5. the covered-texel count, reported so the two backends can be
    //      compared by the caller

    constexpr engine::u32 kExtent = 64;
    // Exactly representable in UNORM8, no .5 ties.
    constexpr engine::f32 kTint[4] = {51.f / 255.f, 153.f / 255.f, 204.f / 255.f, 1.f};
    constexpr engine::u8 kTintBytes[4] = {51, 153, 204, 255};

    lit_out = 0;

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    // The right magic word for the target, so a backend can never be handed
    // the other API's bytecode from a stale cache or a mis-set desc.
    engine::u32 magic = 0;
    if (vs.data.size() >= 4) {
        std::memcpy(&magic, vs.data.data(), 4);
    }
    const engine::u32 expected_magic =
        target == engine::shaders::ShaderTarget::Spirv ? 0x07230203u : 0x43425844u;
    const bool bytecode_ok = compiled && magic == expected_magic;

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::BufferDesc constants{};
    constants.size = sizeof(engine::f32) * 4;
    constants.usage = engine::rhi::BufferUsage::Uniform;
    auto tint_buffer = device.create_buffer(constants, kTint);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    pipeline.uniform_buffer_count = 1;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth = engine::rhi::DepthTest::Disabled;
    pipeline.depth_write = false;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "backend_parity";
    auto pso = bytecode_ok ? device.create_graphics_pipeline(pipeline) : nullptr;

    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    const bool ready = bytecode_ok && target_texture && tint_buffer && pso;

    if (ready) {
        using State = engine::rhi::ResourceState;
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*target_texture, State::Common, State::RenderTarget);

        engine::rhi::RenderPassInfo pass{};
        pass.color = target_texture.get();
        pass.clear_color_target = true;
        pass.clear_depth = false;
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*pso);
        cmd.set_constant_buffer(0, *tint_buffer);
        cmd.draw(3, 0);
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    // (8, 8) is inside the drawn half; (55, 55) is its mirror in the half that
    // must stay clear. An inverted Y swaps which is which, so these two probes
    // are the Y-direction check.
    auto texel = [&pixels](engine::u32 x, engine::u32 y) {
        return &pixels[(static_cast<engine::usize>(y) * kExtent + x) * 4];
    };
    const engine::u8* inside = texel(8, 8);
    const engine::u8* outside = texel(55, 55);
    const bool tint_ok = read_ok && inside[0] == kTintBytes[0] && inside[1] == kTintBytes[1]
        && inside[2] == kTintBytes[2] && inside[3] == kTintBytes[3];
    const bool clear_ok = read_ok && outside[0] == 0 && outside[1] == 0 && outside[2] == 0
        && outside[3] == 255;

    engine::u32 lit = 0;
    if (read_ok) {
        lit = count_lit_texels(pixels.data(), kExtent, kExtent);
    }
    lit_out = lit;
    // Half the target, give or take the diagonal's own width. Derived from
    // geometry, not from whatever the first run happened to print.
    const bool coverage_ok = lit > (kExtent * kExtent / 2) - 2 * kExtent
        && lit < (kExtent * kExtent / 2) + 2 * kExtent;

    const bool offscreen_ok = device.offscreen();
    const bool passed =
        offscreen_ok && bytecode_ok && read_ok && tint_ok && clear_ok && coverage_ok;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Backend parity gate [%s]: offscreen=%s magic=0x%08X inside=(%u,%u,%u,%u) "
        "outside=(%u,%u,%u,%u) lit=%u (%s)",
        api, offscreen_ok ? "yes" : "no", magic, inside[0], inside[1], inside[2], inside[3],
        outside[0], outside[1], outside[2], outside[3], lit, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
```

- [ ] **Step 1.4: The shared coverage count**

In `packages/sandbox/src/sandbox_common.hpp`, beside the other shader paths:

```cpp
constexpr const char* kBackendParityGateShader = "/shaders/backend_parity_gate.hlsl";
```

and declare:

```cpp
// Texels whose alpha is 255 and whose RGB is not all-zero: covered by the draw
// rather than left at the clear colour. Shared so both backends are counted by
// the same code - a per-backend counter is a place for the comparison to be
// wrong in the measurement rather than in the backend.
engine::u32 count_lit_texels(const engine::u8* rgba, engine::u32 width, engine::u32 height);
```

In `packages/sandbox/src/sandbox_common.cpp`:

```cpp
engine::u32 count_lit_texels(const engine::u8* rgba, engine::u32 width, engine::u32 height) {
    engine::u32 lit = 0;
    const engine::usize count = static_cast<engine::usize>(width) * height;
    for (engine::usize i = 0; i < count; ++i) {
        const engine::u8* texel = rgba + i * 4;
        if (texel[0] != 0 || texel[1] != 0 || texel[2] != 0) {
            ++lit;
        }
    }
    return lit;
}
```

- [ ] **Step 1.5: Declare and register the gate**

`packages/sandbox/src/gates/gates.hpp`, beside the other RHI gates:

```cpp
// RHI #12: the same shader, the same asserted pixels, once per backend. Takes
// the device so one function covers both - see the definition.
bool run_backend_parity_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api, engine::u32& lit_out);
```

`packages/sandbox/src/gates/gate_registry.cpp`, beside `run_msaa_gate`:

```cpp
    {"run_backend_parity_gate", GateKind::Gpu, nullptr},
```

- [ ] **Step 1.6: Call it with an offscreen D3D12 device**

In `packages/sandbox/src/main.cpp`, after the MSAA gate call site:

```cpp
    // An offscreen device, not the sandbox's windowed one: the contract's new
    // null-window mode has to be exercised by something, and this gate is the
    // only caller that wants a device with no swapchain.
    // Declared at function scope, not inside the block: Task 2's SPIR-V gate
    // and Task 4's Vulkan parity call both compile this same shader, and
    // resolving the mount three times is three places for them to disagree.
    std::string parity_path;
    engine::u32 d3d12_lit = 0;
    if (!resolve_content(loader, kBackendParityGateShader, parity_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "backend_parity_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else {
        engine::rhi::DeviceDesc offscreen{};
        offscreen.window_handle = nullptr;
        offscreen.width = 64;
        offscreen.height = 64;
        offscreen.preferred_api = engine::rhi::GraphicsAPI::D3D12;
        auto offscreen_rhi = engine::rhi::d3d12::create_rhi();
        auto offscreen_device = offscreen_rhi ? offscreen_rhi->create_device(offscreen) : nullptr;
        if (!offscreen_device) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
                "Backend parity gate [d3d12]: offscreen device creation failed (FAIL)");
            gates_ok = false;
        } else if (!run_backend_parity_gate(*offscreen_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Dxil, "d3d12", d3d12_lit)
            && fail_on_gate) {
            return false;
        }
    }
```

- [ ] **Step 1.7: Build, and confirm the gate fails for the right reason**

```bash
cmake --build build --config Debug
```

Expected: **compile errors** — `offscreen()` and `read_texture()` are new pure
virtuals with no `D3D12Device` override. That is the red.

- [ ] **Step 1.8: The D3D12 offscreen branch**

In `packages/rhi-d3d12/src/device_d3d12.hpp`, add to `D3D12Device`'s public
section:

```cpp
    bool offscreen() const override { return hwnd_ == nullptr; }
    bool read_texture(ITexture& texture, void* out, usize size) override;
```

In `packages/rhi-d3d12/src/device_d3d12.cpp`, guard the four swapchain sites.
`init` (around line 1035–1074) wraps the swapchain block:

```cpp
    if (hwnd_ == nullptr) {
        // Offscreen: no surface, so no swapchain, no backbuffers and no
        // swapchain depth. Frame resources still exist - the command
        // allocators, fences and upload ring are what makes a frame, not the
        // presentation.
        if (!create_frame_resources()) {
            return false;
        }
        frame_index_ = 0;
        log(LogLevel::Info, LogChannel::Render, "D3D12 offscreen device initialized");
        return true;
    }
```

placed immediately before `DXGI_SWAP_CHAIN_DESC1 sd{};`. Then `begin_frame`'s
`frame_index_ = swapchain_->GetCurrentBackBufferIndex();` (line ~1380) becomes:

```cpp
    // Offscreen has no backbuffer to ask, so it cycles the slots itself. The
    // slot count is what bounds in-flight frames either way.
    frame_index_ = swapchain_ ? swapchain_->GetCurrentBackBufferIndex()
                              : (frame_index_ + 1) % kFrameCount;
```

`swapchain()`, `swapchain_color()` and `swapchain_depth()` each gain, as the
first line:

```cpp
    ENGINE_ASSERT_MSG(hwnd_ != nullptr, "offscreen device has no swapchain");
```

`present()` (line ~2763) and `resize()` (line ~1501) already guard on
`!swapchain_` — leave them.

- [ ] **Step 1.9: `read_texture` on D3D12**

Append to `packages/rhi-d3d12/src/device_d3d12.cpp`:

```cpp
bool D3D12Device::read_texture(ITexture& texture, void* out, usize size) {
    auto& d3d_texture = static_cast<D3D12Texture&>(texture);
    ENGINE_ASSERT(out != nullptr);
    if (d3d_texture.resource() == nullptr) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: texture has no resource");
        return false;
    }
    if (d3d_texture.sample_count() != 1) {
        log(LogLevel::Error, LogChannel::Render,
            "read_texture: source is multisampled - resolve it first");
        return false;
    }

    const u32 bytes_per_texel = format_bytes(d3d_texture.format());
    if (bytes_per_texel == 0) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: format cannot be packed");
        return false;
    }
    const usize tight_row = static_cast<usize>(d3d_texture.width()) * bytes_per_texel;
    const usize expected = tight_row * d3d_texture.height();
    if (size != expected) {
        char message[160];
        std::snprintf(message, sizeof(message),
            "read_texture: size %zu does not match %ux%u x%u bytes = %zu", size,
            d3d_texture.width(), d3d_texture.height(), bytes_per_texel, expected);
        log(LogLevel::Error, LogChannel::Render, message);
        return false;
    }

    // The copy destination's rows are aligned to 256 bytes, which is not the
    // tight pitch in general, so the rows are repacked below rather than
    // memcpy'd whole. GetCopyableFootprints is what says how wide the padded
    // rows actually are; computing it by hand is how a 63-wide texture reads
    // back sheared.
    D3D12_RESOURCE_DESC source_desc = d3d_texture.resource()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total_bytes = 0;
    device_->GetCopyableFootprints(&source_desc, 0, 1, 0, &footprint, nullptr, nullptr,
        &total_bytes);

    ID3D12Resource* staging = create_committed_buffer(
        D3D12_HEAP_TYPE_READBACK, static_cast<usize>(total_bytes),
        D3D12_RESOURCE_STATE_COPY_DEST);
    if (!staging) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: staging buffer failed");
        return false;
    }
    set_object_name(staging, "engine/read_texture_staging");

    begin_copy();
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = d3d_texture.resource();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = staging;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    copy_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    // end_copy() submits and returns a fence value; the caller waits. Same
    // idiom upload_to_default uses - it does not wait for you.
    const UINT64 fence_value = end_copy();
    wait_for_fence_blocking(fence_event_, fence_.get(), fence_value);

    void* mapped = nullptr;
    if (FAILED(staging->Map(0, nullptr, &mapped)) || !mapped) {
        log(LogLevel::Error, LogChannel::Render, "read_texture: Map failed");
        retire_resource(staging);
        return false;
    }
    const u8* rows = static_cast<const u8*>(mapped);
    u8* destination = static_cast<u8*>(out);
    for (u32 y = 0; y < d3d_texture.height(); ++y) {
        std::memcpy(destination + y * tight_row,
            rows + static_cast<usize>(y) * footprint.Footprint.RowPitch, tight_row);
    }
    staging->Unmap(0, nullptr);
    retire_resource(staging);
    return true;
}
```

This needs a `format_bytes` helper next to `to_dxgi` in the same file:

```cpp
u32 format_bytes(Format format) {
    switch (format) {
    case Format::RGBA8_UNORM:      return 4;
    case Format::RGBA8_UNORM_SRGB: return 4;
    case Format::RGBA16_FLOAT:     return 8;
    case Format::D32_FLOAT:        return 4;
    case Format::Unknown:          return 0;
    }
    return 0;
}
```

`retire_resource(staging)` rather than `delete`: the staging buffer is
released on the retirement list the copy path already owns, so it outlives the
GPU's use of it. `end_copy()` submits but does **not** wait — checked, not
assumed — hence the explicit `wait_for_fence_blocking` above the `Map`, which
is the same shape `upload_to_default` uses at
`device_d3d12.cpp:1996`.

- [ ] **Step 1.10: Build and watch the gate go from FAIL to pass**

```bash
cmake --build build --config Debug
```

then

```bash
./build/bin/Debug/sandbox.exe --gates
```

Expected: `Backend parity gate [d3d12]: offscreen=yes magic=0x43425844
inside=(51,153,204,255) outside=(0,0,0,255) lit=<n> (pass)`.

**Record the `lit` value here in the plan** — it is the number Task 4 has to
match:

```
measured lit (d3d12) = ____
```

- [ ] **Step 1.11: Falsify it**

Change the shader's `corners` to the *other* half — `float2(1.0, -1.0)` in place
of `float2(-1.0, 1.0)` — rebuild, and confirm the gate goes red at
`inside=(0,0,0,255) outside=(51,153,204,255)`. That is the proof the Y-direction
probes work. Revert.

- [ ] **Step 1.12: Debug layer, then commit**

```bash
ENGINE_GPU_DEBUG=1 ./build/bin/Debug/sandbox.exe --gates
```

Expected: `D3D12 debug layer: 0 message(s), 0 error(s), 0 warning(s)`. A
warning about a missing clear value on the parity target means the render-target
creation path needs the same baked clear value the others have.

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: `all 16 checks passed`. **Check first, commit second** — chaining the
two has pushed a red invariant before.

**Commit:** `feat(rhi): offscreen devices and read_texture on the contract`

---

## Task 2: Shaders #5 — SPIR-V through a second DXC

**Cost:** small · **Closes:** Shaders #5

**Files:**
- Modify: `packages/shaders-dxc/src/shader_compiler_dxc.cpp`,
  `packages/sandbox/src/gates/gates_rhi.cpp`,
  `packages/sandbox/src/gates/gates.hpp`,
  `packages/sandbox/src/gates/gate_registry.cpp`,
  `packages/sandbox/src/main.cpp`,
  `CMakeLists.txt`

- [ ] **Step 2.1: Write the gate first**

In `packages/sandbox/src/gates/gates_rhi.cpp`:

```cpp
bool run_spirv_gate(engine::shaders::IShaderCompiler& compiler,
    const std::string& shader_path) {
    // Shaders #5. The engine's DXC - the Windows SDK's - cannot emit SPIR-V at
    // all: it answers "SPIR-V CodeGen not available", and nothing in the DLL's
    // strings says so. So this asserts that a *different*, SPIR-V-capable DXC
    // was found and used, not merely that compile() returned true.
    //
    // Four assertions: SPIR-V comes back with the right magic word; it is not
    // the DXIL blob for the same shader; the DXIL path still works from the
    // same compiler instance, which is what proves the two DLLs coexist; and
    // the disk cache round-trips the SPIR-V rather than serving the DXIL.

    auto compile = [&](engine::shaders::ShaderTarget target,
                       engine::shaders::ShaderBytecode& out, bool& from_cache) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = "vs_main";
        desc.target_profile = "vs_6_0";
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        from_cache = compiler.last_compile_from_cache();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode spirv{};
    engine::shaders::ShaderBytecode dxil{};
    engine::shaders::ShaderBytecode spirv_again{};
    bool ignored = false;
    bool second_from_cache = false;
    const bool spirv_ok = compile(engine::shaders::ShaderTarget::Spirv, spirv, ignored);
    const bool dxil_ok = compile(engine::shaders::ShaderTarget::Dxil, dxil, ignored);
    const bool repeat_ok =
        compile(engine::shaders::ShaderTarget::Spirv, spirv_again, second_from_cache);

    engine::u32 spirv_magic = 0;
    engine::u32 dxil_magic = 0;
    if (spirv.data.size() >= 4) std::memcpy(&spirv_magic, spirv.data.data(), 4);
    if (dxil.data.size() >= 4) std::memcpy(&dxil_magic, dxil.data.data(), 4);

    const bool magic_ok = spirv_ok && spirv_magic == 0x07230203u;
    const bool dxil_still_ok = dxil_ok && dxil_magic == 0x43425844u;
    // Not the same bytes: the cache keys on target, so this catches a key that
    // stops doing so as much as it catches a wrong DLL.
    const bool distinct = spirv_ok && dxil_ok && spirv.data != dxil.data;
    const bool cache_ok = repeat_ok && spirv_again.data == spirv.data;

    const bool passed = magic_ok && dxil_still_ok && distinct && cache_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "SPIR-V gate: spirv=0x%08X (%zu bytes) dxil=0x%08X (%zu bytes) distinct=%s "
        "cache_round_trip=%s cached=%s (%s)",
        spirv_magic, spirv.data.size(), dxil_magic, dxil.data.size(),
        distinct ? "yes" : "no", cache_ok ? "yes" : "no", second_from_cache ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
```

Declare it in `gates.hpp`, add `{"run_spirv_gate", GateKind::Gpu, nullptr},` to
`kGates`, and call it in `main.cpp` **immediately before** the D3D12 parity gate
added in Step 1.6, so it shares that step's `parity_path` and runs before
anything depends on SPIR-V working:

```cpp
    if (!parity_path.empty() && !run_spirv_gate(compiler, parity_path) && fail_on_gate) {
        return false;
    }
```

This requires Step 1.6's `resolve_content` call to have already run, which is
why the two gates share one resolved path rather than each resolving its own.

`GateKind::Gpu` even though it touches no device: it needs a DLL that ships with
a GPU SDK, so it cannot run in the headless Linux job.

- [ ] **Step 2.2: Run it and watch it fail**

```bash
./build/bin/Debug/sandbox.exe --gates
```

Expected: `Shader target SPIR-V is not implemented (DXC backend emits DXIL
only)` then `SPIR-V gate: spirv=0x00000000 (0 bytes) ... (FAIL)`.

- [ ] **Step 2.3: Resolve and load the SPIR-V DXC**

In `packages/shaders-dxc/src/shader_compiler_dxc.cpp`, in the anonymous
namespace:

```cpp
using DxcCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

// Where a SPIR-V-capable DXC lives. The Windows SDK's copy - the one this
// package links implicitly - is built without -DENABLE_SPIRV_CODEGEN and
// answers "SPIR-V CodeGen not available"; the Vulkan SDK ships a second build
// with DirectX disabled instead. They are different binaries with the same
// name, and Windows dedups loaded modules by resolved path rather than base
// name, so both live in one process without a rename.
std::wstring find_spirv_dxc() {
    wchar_t override_path[512] = {};
    if (GetEnvironmentVariableW(L"ENGINE_DXC_SPIRV", override_path,
            static_cast<DWORD>(std::size(override_path))) > 0) {
        return override_path;
    }
    wchar_t sdk[480] = {};
    if (GetEnvironmentVariableW(L"VULKAN_SDK", sdk, static_cast<DWORD>(std::size(sdk))) > 0) {
        return std::wstring(sdk) + L"\\Bin\\dxcompiler.dll";
    }
    return {};
}
```

Add to `DxcShaderCompiler`'s private section:

```cpp
    // Resolved lazily and once: a DXIL-only build must not pay for a DLL it
    // never uses, and a machine without the Vulkan SDK must not log about it
    // on every startup.
    bool spirv_probed_ = false;
    ComPtr<IDxcCompiler3> spirv_compiler_;
    ComPtr<IDxcUtils> spirv_utils_;
    ComPtr<IDxcIncludeHandler> spirv_includes_;
```

and a method:

```cpp
    // True once a SPIR-V-capable DXC is loaded and has proved it. The proof
    // matters: the two builds share a name, an export table and a version
    // resource, so the only reliable question is whether it compiles.
    bool ensure_spirv_compiler() {
        if (spirv_probed_) {
            return spirv_compiler_ != nullptr;
        }
        spirv_probed_ = true;

        const std::wstring path = find_spirv_dxc();
        if (path.empty()) {
            log(LogLevel::Error, LogChannel::Render,
                "SPIR-V requested but no SPIR-V-capable DXC found. Install the Vulkan SDK "
                "(it sets VULKAN_SDK), or point ENGINE_DXC_SPIRV at a dxcompiler.dll built "
                "with -DENABLE_SPIRV_CODEGEN=ON.");
            return false;
        }

        const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            log(LogLevel::Error, LogChannel::Render,
                "SPIR-V DXC could not be loaded from the resolved path");
            return false;
        }
        auto create = reinterpret_cast<DxcCreateInstanceFn>(
            reinterpret_cast<void*>(GetProcAddress(module, "DxcCreateInstance")));
        if (!create) {
            log(LogLevel::Error, LogChannel::Render,
                "SPIR-V DXC has no DxcCreateInstance export");
            return false;
        }
        if (FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&spirv_utils_)))
            || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&spirv_compiler_)))
            || FAILED(spirv_utils_->CreateDefaultIncludeHandler(&spirv_includes_))) {
            log(LogLevel::Error, LogChannel::Render, "SPIR-V DXC would not instantiate");
            spirv_compiler_.Reset();
            return false;
        }
        log(LogLevel::Info, LogChannel::Render, "SPIR-V DXC loaded");
        return true;
    }
```

- [ ] **Step 2.4: Take the SPIR-V branch in `compile`**

Replace the rejection at `shader_compiler_dxc.cpp:70`:

```cpp
        const bool spirv = desc.target == ShaderTarget::Spirv;
        if (spirv && !ensure_spirv_compiler()) {
            error_log = "No SPIR-V-capable DXC is available";
            return false;
        }
```

and route the three members through locals chosen by target:

```cpp
        IDxcUtils* utils = spirv ? spirv_utils_.Get() : utils_.Get();
        IDxcCompiler3* compiler = spirv ? spirv_compiler_.Get() : compiler_.Get();
        IDxcIncludeHandler* includes = spirv ? spirv_includes_.Get() : includes_.Get();
```

replacing `utils_->`, `compiler_->` and `includes_.Get()` in the body. Then,
after the existing `-Zi/-Od/-Qembed_debug` block:

```cpp
        if (spirv) {
            args.push_back(L"-spirv");
            // The default HLSL->SPIR-V mapping sends register(xN, spaceM) to
            // set M binding N and *ignores the register type*, so b0, t0, u0
            // and s0 all collide at set 0 binding 0. Disjoint ranges per type
            // fix it: b at 0, t at 16, u at 32, s at 48. The tree's whole
            // surface is b0, t0-t6, s0-s2, u0-u1 and one t0 in space 1, so
            // these two spaces cover it with room to grow.
            for (const wchar_t* space : {L"0", L"1"}) {
                args.push_back(L"-fvk-t-shift");
                args.push_back(L"16");
                args.push_back(space);
                args.push_back(L"-fvk-u-shift");
                args.push_back(L"32");
                args.push_back(space);
                args.push_back(L"-fvk-s-shift");
                args.push_back(L"48");
                args.push_back(space);
            }
            // Keeps cbuffer packing matching the C++ structs the constants are
            // memcpy'd from.
            args.push_back(L"-fvk-use-dx-layout");
            // Deliberately NOT -fvk-invert-y. The Y flip is a negative
            // viewport height in the backend, so one shader source serves both
            // APIs - which is the property worth protecting.
        }
```

- [ ] **Step 2.5: Widen the CMake gate**

In `CMakeLists.txt`, replace the D3D12 block:

```cmake
option(ENGINE_RHI_D3D12 "Build D3D12 RHI backend" ON)
option(ENGINE_RHI_VULKAN "Build Vulkan RHI backend" ON)
# The shader compiler is DXC either way - it emits DXIL for D3D12 and SPIR-V
# for Vulkan from the same HLSL - so it belongs to both backends, not to
# D3D12. Gating it on D3D12 alone made a Vulkan-only configure fail to link.
if((ENGINE_RHI_D3D12 OR ENGINE_RHI_VULKAN) AND WIN32)
    add_subdirectory(packages/shaders-dxc)
endif()
if(ENGINE_RHI_D3D12 AND WIN32)
    add_subdirectory(packages/rhi-d3d12)
endif()
```

`packages/rhi-vulkan` is added in Task 3, not here.

- [ ] **Step 2.6: Green, invariants, commit**

```bash
cmake --build build --config Debug && ./build/bin/Debug/sandbox.exe --gates
```

Expected: `SPIR-V gate: spirv=0x07230203 (~700 bytes) dxil=0x43425844 (~2800
bytes) distinct=yes cache_round_trip=yes cached=yes (pass)`.

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

**Commit:** `feat(shaders): a SPIR-V compile path through a second DXC (Shaders #5)`

---

## Task 3: The `rhi-vulkan` package — vendoring, instance, device

**Cost:** medium · **Closes:** nothing yet

- [ ] **Step 3.1: Vendor the minimal set**

```bash
mkdir -p packages/rhi-vulkan/third_party/vulkan packages/rhi-vulkan/third_party/vk_video
cp "/c/VulkanSDK/1.4.357.0/Include/vulkan/vulkan_core.h" packages/rhi-vulkan/third_party/vulkan/
cp "/c/VulkanSDK/1.4.357.0/Include/vulkan/vk_platform.h" packages/rhi-vulkan/third_party/vulkan/
cp "/c/VulkanSDK/1.4.357.0/Include/vk_video/"* packages/rhi-vulkan/third_party/vk_video/
cp "/c/VulkanSDK/1.4.357.0/Include/Volk/volk.h" packages/rhi-vulkan/third_party/
cp "/c/VulkanSDK/1.4.357.0/Include/Volk/volk.c" packages/rhi-vulkan/third_party/
```

Write `packages/rhi-vulkan/third_party/README.md` naming each file's upstream
repository, version (`VK_HEADER_VERSION` / `VOLK_HEADER_VERSION` = 357) and
licence (Vulkan-Headers Apache-2.0, volk MIT), and stating that `vulkan.hpp`
and `vk_enum_string_helper.h` are deliberately not vendored. Copy the Apache-2.0
text to `LICENSE-vulkan-headers.txt`; volk carries its MIT notice in-file, so
note that rather than duplicating it.

**Do not reformat any vendored file.** `third_party` is excluded from
`Get-PackageSources`, so `format-hygiene` and the API-isolation scan skip it,
and the ROADMAP LOC audit excludes it too — verified before vendoring, not
after.

- [ ] **Step 3.2: The package**

`packages/rhi-vulkan/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)

engine_add_package(rhi-vulkan
    SOURCES
        src/instance_vulkan.cpp
        src/device_vulkan.cpp
        src/pipeline_vulkan.cpp
        src/commands_vulkan.cpp
        src/rhi_vulkan.cpp
        third_party/volk.c
    PUBLIC_DEPS engine::rhi engine::math
)

target_include_directories(rhi-vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# No find_package(Vulkan) and no vulkan-1.lib: volk resolves every entry point
# at runtime from the loader the driver installs, so this package builds on a
# machine with no Vulkan SDK. That is what lets CI compile it on every push
# instead of letting it rot behind an option nobody turns on.
target_compile_definitions(rhi-vulkan PRIVATE
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_WIN32_KHR
)
```

`packages/rhi-vulkan/include/engine/rhi/vulkan/rhi_vulkan.hpp`:

```cpp
#pragma once

#include <engine/rhi/rhi.hpp>

#include <memory>

namespace engine::rhi::vulkan {

std::unique_ptr<IRHI> create_rhi();

} // namespace engine::rhi::vulkan
```

Add to `CMakeLists.txt` beside the D3D12 block from Step 2.5:

```cmake
if(ENGINE_RHI_VULKAN AND WIN32)
    add_subdirectory(packages/rhi-vulkan)
endif()
```

- [ ] **Step 3.3: Update the invariants — before writing code, not after**

In `tools/check-invariants.ps1`:

`$Layers` gains `'rhi-vulkan' = 3` on the implementations line.

`$apiRules` gains a Vulkan rule, so Vulkan headers are as isolated as D3D12's:

```powershell
    @{ Pattern = '#include\s*[<"](vulkan/|volk\.h)'; Allowed = @('rhi-vulkan'); What = 'Vulkan' }
```

The `rhi-vocabulary` check's forbidden-term list gains the Vulkan terms, so the
public headers stay neutral in both directions rather than only away from D3D12:
`Vk[A-Z]`, `VK_`, `SPIR-V`, `descriptor set`. The binding-contract block in
`resources.hpp` is already the allowlisted region and already names Vulkan
concepts, so the allowlist needs no change — **confirm that by running the
check, not by reading it.**

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: `all 16 checks passed`, with `package-layers` at 27 packages.

- [ ] **Step 3.4: `vulkan_common.hpp`**

```cpp
#pragma once

// The only file that includes the vendored headers. Everything else in this
// package includes this one, so the vendored surface has exactly one door.
#define VK_NO_PROTOTYPES
#include <volk.h>

#include <engine/core/log.hpp>
#include <engine/core/types.hpp>

namespace engine::rhi::vulkan {

// The results that actually occur, named. vk_enum_string_helper.h covers every
// enum in the API and costs 817 KB to do it; this covers what this backend can
// produce and fits on a screen.
inline const char* to_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS:                        return "VK_SUCCESS";
    case VK_NOT_READY:                      return "VK_NOT_READY";
    case VK_TIMEOUT:                        return "VK_TIMEOUT";
    case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_UNKNOWN:                  return "VK_ERROR_UNKNOWN";
    default:                                return "VkResult(other)";
    }
}

// Logs and returns true on failure, so a call site reads as one `if`. Named for
// what it answers rather than as a macro, because a macro that hides a `return`
// is how a failure path stops being visible.
inline bool vk_failed(VkResult result, const char* what) {
    if (result == VK_SUCCESS) {
        return false;
    }
    char message[192];
    std::snprintf(message, sizeof(message), "Vulkan %s failed: %s", what, to_string(result));
    log(LogLevel::Error, LogChannel::Render, message);
    return true;
}

} // namespace engine::rhi::vulkan
```

- [ ] **Step 3.5: Instance, layer, messenger, physical device**

`packages/rhi-vulkan/src/instance_vulkan.cpp` — `volkInitialize()`, then an
instance at `VK_API_VERSION_1_3`. When `ENGINE_GPU_DEBUG=1` *and*
`vkEnumerateInstanceLayerProperties` reports `VK_LAYER_KHRONOS_validation`,
enable it plus `VK_EXT_debug_utils` and install a messenger that routes into
`engine::log` on `LogChannel::Render` at `Error` for error severity and `Warn`
for warning — the same treatment the D3D12 debug layer gets, so the same rule
applies: any message is a build-breaking bug. `volkLoadInstance()` after
creation.

Physical device: enumerate, reject `VK_PHYSICAL_DEVICE_TYPE_CPU`, prefer
`DISCRETE_GPU` over `INTEGRATED_GPU`, require `dynamicRendering` from
`VkPhysicalDeviceVulkan13Features`. Log the chosen `deviceName` and
`apiVersion`. Rejecting CPU devices mirrors the D3D12 backend skipping
`DXGI_ADAPTER_FLAG_SOFTWARE`, and it is why `--gates` cannot run on a hosted
runner on either backend.

- [ ] **Step 3.6: Logical device, queue, and the not-yet-implemented virtuals**

`device_vulkan.hpp` declares `VulkanDevice`, `VulkanBuffer`, `VulkanTexture`,
`VulkanPipeline`, `VulkanCommandList`, `VulkanSampler`, `VulkanComputePipeline`.
`device_vulkan.cpp` implements creation: one graphics queue,
`dynamicRendering = VK_TRUE`, `volkLoadDevice()`.

Every `IDevice` and `ICommandList` virtual the slice does not implement gets
this body shape:

```cpp
void VulkanCommandList::set_structured_buffer(u32 slot, IBuffer& buffer, usize offset_bytes) {
    (void)slot;
    (void)buffer;
    (void)offset_bytes;
    not_implemented("set_structured_buffer");
}
```

with, in `device_vulkan.cpp`:

```cpp
// Says so, once per name, the first time it is reached. A stub that silently
// returns is indistinguishable from a working implementation, both to a caller
// and to whoever reads it next - and this backend is deliberately partial, so
// there will be many of these for a while.
void not_implemented(const char* what) {
    static std::unordered_set<std::string> said;
    if (!said.insert(what).second) {
        return;
    }
    char message[160];
    std::snprintf(message, sizeof(message), "rhi-vulkan: %s is not implemented yet", what);
    log(LogLevel::Warn, LogChannel::Render, message);
}
```

- [ ] **Step 3.7: A device-creation gate, watched failing**

In `gates_rhi.cpp`:

```cpp
bool run_vulkan_device_gate() {
    // Standing the device up is its own gate, because everything after it is
    // meaningless if the instance, the physical-device choice or dynamic
    // rendering were not what was asked for. Asserts on what the device
    // reports, not on a non-null pointer.
    engine::rhi::DeviceDesc desc{};
    desc.window_handle = nullptr;
    desc.width = 64;
    desc.height = 64;
    desc.preferred_api = engine::rhi::GraphicsAPI::Vulkan;
    auto rhi = engine::rhi::vulkan::create_rhi();
    const bool factory_ok = rhi != nullptr && rhi->api() == engine::rhi::GraphicsAPI::Vulkan
        && !rhi->name().empty();
    auto device = rhi ? rhi->create_device(desc) : nullptr;

    // No Vulkan driver is an environment fact, not a defect. Skip by name, and
    // say `skip` rather than `pass` so the pass count cannot absorb it.
    if (factory_ok && !device) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Vulkan device gate: no Vulkan device available - install a Vulkan-capable "
            "driver (skip)");
        return true;
    }

    const bool offscreen_ok = device && device->offscreen();
    const bool baseline = device && device->gpu_baseline().shader_model >= 0x60;
    const bool not_lost = device && !device->device_lost();
    const bool passed = factory_ok && offscreen_ok && baseline && not_lost;

    char message[192];
    std::snprintf(message, sizeof(message),
        "Vulkan device gate: factory=%s offscreen=%s sm=0x%02X lost=%s (%s)",
        factory_ok ? "yes" : "no", offscreen_ok ? "yes" : "no",
        device ? device->gpu_baseline().shader_model : 0u,
        not_lost ? "no" : "YES", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
```

Declare it, register it as `Gpu`, and call it from `main.cpp` inside
`#ifdef ENGINE_HAS_VULKAN`.

The define goes in `cmake/EngineRuntimeApp.cmake` — **not** in
`packages/sandbox/CMakeLists.txt`, which is where it looks like it should be.
Beside the existing D3D12 block at line 68:

```cmake
    if(TARGET engine::rhi-vulkan)
        target_link_libraries(${TARGET} PRIVATE engine::rhi-vulkan engine::shaders-dxc)
        target_compile_definitions(${TARGET} PRIVATE ENGINE_HAS_VULKAN)
    endif()
```

No `engine_copy_dxc_runtime` call: that copies the Windows SDK's DXC next to the
exe, and the SPIR-V one is loaded from the Vulkan SDK by absolute path instead —
deliberately not copied, because two 14–20 MB DLLs with the same name in one
directory is a coin flip nobody should have to think about. Linking
`shaders-dxc` twice is harmless and states that both backends need it.

- [ ] **Step 3.8: Build, run, invariants, commit**

```bash
cmake -B build -G "Visual Studio 18 2026" -A x64 && cmake --build build --config Debug
ENGINE_GPU_DEBUG=1 ./build/bin/Debug/sandbox.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: `Vulkan device gate: factory=yes offscreen=yes sm=0x60 lost=no (pass)`,
the validation layer silent, `all 16 checks passed` with 27 packages.

**Commit:** `feat(rhi-vulkan): the package, the instance, and a device that reports itself`

---

## Task 4: Vulkan renders the same image

**Cost:** large · **Closes:** RHI #12 (the offscreen half)

Everything here exists to make **the Task 1 gate function** pass against the
Vulkan device with the same asserted pixels. Nothing more.

- [ ] **Step 4.1: Call the parity gate with the Vulkan device, and watch it fail**

In `main.cpp`, immediately after the D3D12 parity call site:

```cpp
    engine::u32 vulkan_lit = 0;
#ifdef ENGINE_HAS_VULKAN
    {
        engine::rhi::DeviceDesc vk_desc{};
        vk_desc.window_handle = nullptr;
        vk_desc.width = 64;
        vk_desc.height = 64;
        vk_desc.preferred_api = engine::rhi::GraphicsAPI::Vulkan;
        auto vk_rhi = engine::rhi::vulkan::create_rhi();
        auto vk_device = vk_rhi ? vk_rhi->create_device(vk_desc) : nullptr;
        if (!vk_device) {
            engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
                "Backend parity gate [vulkan]: no Vulkan device (skip)");
        } else if (!run_backend_parity_gate(*vk_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Spirv, "vulkan", vulkan_lit)
            && fail_on_gate) {
            return false;
        }
    }
#endif

    // The comparison the whole pass exists for. Reported even when it passes,
    // because "the two backends agree" is only meaningful with both numbers
    // next to each other. One diagonal's worth of tolerance: the fill rule at
    // a shared edge is specified in both APIs but not identically enough to
    // demand equality on a 45-degree line.
    if (d3d12_lit > 0 && vulkan_lit > 0) {
        const engine::u32 spread =
            d3d12_lit > vulkan_lit ? d3d12_lit - vulkan_lit : vulkan_lit - d3d12_lit;
        const bool agree = spread <= 64;
        char message[160];
        std::snprintf(message, sizeof(message),
            "Backend agreement gate: d3d12_lit=%u vulkan_lit=%u spread=%u (%s)",
            d3d12_lit, vulkan_lit, spread, agree ? "pass" : "FAIL");
        engine::log(agree ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render, message);
        gates_ok = agree && gates_ok;
    }
```

Run it. Expected: FAIL, with `not implemented` warnings naming exactly which
virtuals Task 4 has to fill. **That list is the task's own checklist** — write
it down.

- [ ] **Step 4.2: Memory and buffers**

In `device_vulkan.cpp`, a memory helper:

```cpp
// Finds a heap satisfying `required` for this allocation. VMA is the answer
// once the resource count justifies it - it ships with the SDK and is one
// header - but a handful of allocations does not justify a general-purpose
// allocator, and this is needed underneath one anyway.
u32 find_memory_type(VkPhysicalDevice gpu, u32 type_bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        const bool allowed = (type_bits & (1u << i)) != 0;
        const bool suits = (properties.memoryTypes[i].propertyFlags & required) == required;
        if (allowed && suits) {
            return i;
        }
    }
    return ~0u;
}
```

`create_buffer` maps `BufferUsage` to `VkBufferUsageFlags` and memory
properties: `Uniform` → `UNIFORM_BUFFER` + `HOST_VISIBLE | HOST_COHERENT` (the
frame ring's shape); `Vertex`/`Index`/`Storage` → the matching usage +
`DEVICE_LOCAL`, with a host-visible staging copy when `data` is given;
`Readback` → `TRANSFER_DST` + `HOST_VISIBLE | HOST_COHERENT`. The parity gate
needs `Uniform` and nothing else, so implement `Uniform` and `Readback` fully
and let the rest reach `not_implemented` until parity.

- [ ] **Step 4.3: Textures and `read_texture`**

`create_texture` for `TextureUsage::RenderTarget`: a `VkImage`
(`COLOR_ATTACHMENT_BIT | TRANSFER_SRC_BIT`, `VK_IMAGE_TILING_OPTIMAL`,
`sample_count` from the desc), device-local memory, and a `VkImageView`.

`read_texture` is `vkCmdCopyImageToBuffer` into a host-visible buffer, then map.
Unlike D3D12 there is **no row-pitch repack**: `bufferRowLength = 0` means
tightly packed, which is what the contract promises. Say that in a comment —
it is the kind of asymmetry that otherwise looks like a missing step.

- [ ] **Step 4.4: `ResourceState` → Vulkan barriers**

One function, `to_vulkan_barrier(ResourceState)`, returning layout, access mask
and stage mask together — because a barrier needs all three and returning them
from three functions is how they come to disagree:

```cpp
struct BarrierState {
    VkImageLayout layout;
    VkAccessFlags2 access;
    VkPipelineStageFlags2 stage;
};

// D3D12 has one state enum covering layout, visibility and stage; Vulkan splits
// them. So this is not a lookup table, it is the translation, and the three
// results are returned together because a barrier that gets two of them right
// is a barrier that does nothing.
BarrierState to_vulkan_barrier(ResourceState state) {
    switch (state) {
    case ResourceState::Common:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    case ResourceState::RenderTarget:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    case ResourceState::DepthWrite:
        return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT};
    case ResourceState::CopySrc:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT};
    case ResourceState::CopyDst:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT};
    case ResourceState::ShaderRead:
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT};
    case ResourceState::Storage:
        return {VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    case ResourceState::Present:
        // No swapchain offscreen, so this is unreachable rather than wrong.
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
    }
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
}
```

`transition(ITexture&, from, to)` becomes one `vkCmdPipelineBarrier2` with a
`VkImageMemoryBarrier2` built from both ends.

- [ ] **Step 4.5: Descriptor-set layout from the counts**

`pipeline_vulkan.cpp` — this is the translation the spec says this pass exists
to test:

```cpp
// resources.hpp's binding contract, implemented. The bindings match the
// register shifts shaders-dxc passes to DXC: b at 0, t at 16, u at 32, s at 48
// - so a change to either side without the other is a shader reading the wrong
// descriptor, which is why both carry the same comment naming the other.
//
// Set 0 only for now. storage_buffer_count lives in space 1 on D3D12 and would
// be set 1 here; the parity gate uses none, so it reaches not_implemented
// rather than being written blind.
constexpr u32 kBindingBase_Uniform = 0;
constexpr u32 kBindingBase_SampledTexture = 16;
constexpr u32 kBindingBase_StorageTexture = 32;
constexpr u32 kBindingBase_Sampler = 48;
```

For the graphics pipeline: `uniform_buffer_count` → that many
`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` bindings from `kBindingBase_Uniform`, stage
flags `VERTEX | FRAGMENT`; `sampled_texture_count` →
`VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` from base 16, **fragment only**, preserving
the asymmetry the contract calls out; `sampler_count` → immutable samplers from
`SamplerDesc`, which is the contract's other named asymmetry.

Then `VkPipelineLayout`, and a `VkGraphicsPipelineCreateInfo` with
`VkPipelineRenderingCreateInfo` for dynamic rendering, and:

```cpp
    // Negative height, positive y origin: Vulkan's NDC Y points the other way
    // from D3D's, and doing it here rather than with -fvk-invert-y is what lets
    // one HLSL source serve both backends. Core since Vulkan 1.1
    // (VK_KHR_maintenance1).
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<f32>(height);
    viewport.width = static_cast<f32>(width);
    viewport.height = -static_cast<f32>(height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
```

Depth compare maps straight through — `DepthTest::Greater` →
`VK_COMPARE_OP_GREATER` — because both APIs put depth in [0, 1] and reversed-Z
needs no adjustment. Note that in a comment; it is the RHI #15 payoff.

- [ ] **Step 4.6: Command list, dynamic rendering, descriptor sets**

`commands_vulkan.cpp` — `begin()`/`end()` on a per-frame `VkCommandPool`;
`begin_render_pass` as `vkCmdBeginRendering` with one
`VkRenderingAttachmentInfo` (`loadOp` `CLEAR` or `LOAD` from
`clear_color_target`, and `resolveImageView` from `RenderPassInfo::resolve`
when set — which is where the D3D12/Vulkan divergence RHI #18 was designed for
actually lands); `end_render_pass` as `vkCmdEndRendering`.

`set_constant_buffer` writes a descriptor into a per-frame `VkDescriptorPool`
allocated set and `vkCmdBindDescriptorSets`. One pool per frame slot, reset at
`begin_frame` — the same lifetime rule the D3D12 shader-visible heap uses, so
the two backends fail the same way if a caller binds more than a frame's worth.

`submit()` is `vkQueueSubmit` with a fence per slot; `wait_idle()` is
`vkQueueWaitIdle`. The slice submits and waits, so descriptor-set lifetime under
real 3-frame flight is deferred rather than solved badly — say so in a comment
next to the pool.

- [ ] **Step 4.7: Green on both backends, with the same numbers**

```bash
cmake --build build --config Debug
ENGINE_GPU_DEBUG=1 ./build/bin/Debug/sandbox.exe --gates
```

Expected, all three:

```
Backend parity gate [d3d12]:  offscreen=yes magic=0x43425844 inside=(51,153,204,255) outside=(0,0,0,255) lit=<n> (pass)
Backend parity gate [vulkan]: offscreen=yes magic=0x07230203 inside=(51,153,204,255) outside=(0,0,0,255) lit=<n> (pass)
Backend agreement gate: d3d12_lit=<n> vulkan_lit=<n> spread=<=64 (pass)
```

and the validation layer silent.

- [ ] **Step 4.8: Falsify the parity claim**

Delete the negative sign from the viewport height, rebuild, and confirm the
Vulkan parity gate goes red at `inside=(0,0,0,255) outside=(51,153,204,255)` —
the Y probes catching an inverted image rather than a coverage count averaging
it away. Restore.

- [ ] **Step 4.9: Release, invariants, commit**

```bash
cmake --build build --config Release --target game && ./build/bin/Release/game.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

**Commit:** `feat(rhi-vulkan): a second backend that draws the same image (RHI #12)`

---

## Task 5: Record it, and make CI compile it

**Cost:** small

- [ ] **Step 5.1: CI builds the Vulkan backend**

In `.github/workflows/ci.yml`, the `build` job's Configure step gains
`-DENGINE_RHI_VULKAN=ON` (it is the default, so state it to make the intent
explicit and to fail loudly if the default ever changes), and a comment saying
CI **compiles** but cannot **run** the Vulkan backend — no GPU, no SDK, so no
SPIR-V compiler and no device, exactly as `--gates` is already not run there.

Add `no-vulkan` to the `configure-options` matrix and `CMakePresets.json`,
matching `no-d3d12` — the modularity claim now has two backends to keep honest.

- [ ] **Step 5.2: The ROADMAP entry**

A Why / Choice / Gate / Do-not entry covering Shaders #5 and RHI #12 together.
It must carry the five measurements from the spec (the SPIR-V-incapable DXC, the
two coexisting DLLs, 23/23 shaders, [0,1] depth both, the SDK's contents), the
offscreen-first argument, and the two contract additions.

**Do-not** lines: do not add a second way to select depth direction *or* Y
direction — the viewport sign is the only place Y is flipped, and
`-fvk-invert-y` would put a second one in the shaders. Do not let the register
shifts in `shaders-dxc` and the binding bases in `pipeline_vulkan.cpp` drift —
they are the same numbers in two places and a mismatch is a shader reading the
wrong descriptor with nothing logged. Do not implement a Vulkan virtual by
returning silently; `not_implemented` exists so a partial backend cannot be
mistaken for a working one. Do not run `--gates` on a hosted runner for either
backend.

- [ ] **Step 5.3: The map**

Shaders #5 and RHI #12 to **Done**, with category subtotals and the header
totals recounted. RHI #13 (Metal/console) still names RHI #12 as its blocker —
re-read it and say what is actually true now that the contract has survived a
second API but presentation has not been ported.

Add a new **Ready** RHI row for the parity work this pass deliberately left:
presentation (surface, swapchain, acquire/present, resize) and the remaining
virtuals, naming this pass as what unblocked it.

- [ ] **Step 5.4: LOC recount**

```bash
pwsh -NoProfile -Command "$f = Get-ChildItem -Path packages -Recurse -File -Include '*.cpp','*.hpp','*.h','*.hlsl','*.hlsli' | Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' }; '{0} lines in {1} files' -f (($f | ForEach-Object { (Get-Content -LiteralPath $_.FullName).Count } | Measure-Object -Sum).Sum), $f.Count"
```

Update the audit paragraph with the recount and the new package count (27).
Vendored files are excluded, so the figure is engine code only — say so, since
a reader seeing a 2 MB vendor drop and an unchanged LOC line would otherwise
suspect the audit.

- [ ] **Step 5.5: The boundary rule**

`.claude/rules/renderer-boundaries.md` says *"One production GPU backend
(`rhi-d3d12`) until a second is justified as its own package (e.g.
`rhi-vulkan`) — grow the `rhi` interface now, implement a second backend only
when actually needed."* That sentence is now history. Rewrite it to say there
are two, that D3D12 remains the daily driver, and that a contract change now
costs two implementations — which is the reason the 1 Sep pass front-loaded
#15, #9 and #18.

- [ ] **Step 5.6: Commit**

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

**Commit:** `docs(roadmap): a second GPU backend, and what parity still needs`

---

## Definition of done

- [ ] Shaders #5 and RHI #12 are **Done**, subtotals and header totals recounted
- [ ] `Backend parity gate` passes for **both** backends with byte-identical
      `inside`/`outside` probes, and `Backend agreement gate` reports both
      coverage counts
- [ ] 0 `FAIL` in Debug and Release; D3D12 debug layer 0/0/0 **and** the Vulkan
      validation layer silent
- [ ] every new gate was watched failing, and the two parity claims were
      falsified on purpose (wrong triangle half; positive viewport height)
- [ ] `all 16 checks passed` under both shells, with 27 packages, a Vulkan
      API-isolation rule, and Vulkan terms forbidden in the public `rhi` headers
- [ ] CI compiles `rhi-vulkan` on `windows-latest` with no SDK installed, and
      `no-vulkan` configures
- [ ] every unimplemented Vulkan virtual says so by name, once
- [ ] the register shifts and the binding bases each carry a comment naming the
      other

## What this plan does not do

No presentation — no surface, no swapchain, no `--rhi` selection. No parity for
the other 82 gates. No VMA. No bind groups: A2 stays open, and this pass is what
produces the evidence for it rather than the prediction.
