# Forward transparency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to
> implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking. **Do not** use `using-git-worktrees` or
> `finishing-a-development-branch` — this repo is trunk-based, commit to `main`
> and push. See CLAUDE.md's skill mapping table.

**Goal:** A material with `opacity < 1` draws through a blended forward
pipeline into `scene_color`, over the opaque geometry and the sky, with a gate
asserting the blend arithmetic against a CPU-computed reference on both
backends.

**Architecture:** No `rhi` change. `BlendMode::Alpha` already exists and both
backends translate it to identical factors; this row adds a second forward
pipeline that sets it, a `transparent` graph pass after `sky`, and an opacity
that travels `scene::Material` → `ExtractInstance` → `DrawItem` →
`InstanceData::material_params.z` → the pixel shader's alpha.

**Spec:** [2026-09-02-forward-transparency-design.md](../specs/2026-09-02-forward-transparency-design.md)

**Tech Stack:** C++20, HLSL SM 6.0 compiled by DXC to DXIL and SPIR-V, D3D12
and Vulkan behind `engine::rhi`.

---

## What "the test" means here

There is no test framework. A gate is a plain function in
`packages/sandbox/src/gates/gates_renderer.cpp`, declared in `gates/gates.hpp`,
classified in `kGates` (`gates/gate_registry.cpp`), and called from the
sequence in `main.cpp`. It asserts on values and logs `(pass)` / `(FAIL)`.
See CLAUDE.md's "What a gate is".

**The red-first moment is Task 2, and it uses a gate that already exists.**
Adding a field to `FramePipelines` turns `run_pipeline_set_gate` red and makes
it name the missing pipeline — that gate exists to be the "you forgot to create
it" net. Tasks 4, 5 and 6 each add an assertion to the new gate that fails
before the code it describes exists.

Task 1's blend-arithmetic assertions **pass on the first run**, because the
contract already carries `BlendMode::Alpha`. That is not a TDD failure — it is
the baseline measurement the rest of the row depends on, and it is the first
thing in the engine to check that the two backends blend *identically*. Run it
and read the numbers before building anything on top.

## File structure

| File | Action | Responsibility |
|------|--------|----------------|
| `packages/sandbox/content/shaders/transparency_gate.hlsl` | create | Gate-only: a cbuffer-driven quad in a flat colour. Owns nothing about the frame. |
| `packages/sandbox/src/gates/gates_renderer.cpp` | modify | `run_transparency_gate`; extend `run_depth_convention_gate` and `run_material_gate`. |
| `packages/sandbox/src/gates/gates.hpp` | modify | Declare `run_transparency_gate`. |
| `packages/sandbox/src/gates/gate_registry.cpp` | modify | Classify it `Gpu`. |
| `packages/renderer/include/engine/renderer/frame_pipelines.hpp` | modify | `forward_transparent` field + table entry. |
| `packages/sandbox/src/sandbox_common.hpp` / `.cpp` | modify | `make_forward_transparent_pipeline_desc`. |
| `packages/scene/include/engine/scene/world.hpp` | modify | `Material::opacity`. |
| `packages/renderer/include/engine/renderer/extract.hpp` | modify | `ExtractInstance::opacity`. |
| `packages/renderer/include/engine/renderer/render_snapshot.hpp` | modify | `DrawItem::opacity`; declare `record_transparent_draws`. |
| `packages/renderer/src/extract.cpp` | modify | Carry opacity into `DrawItem` and `material_params.z`. |
| `packages/renderer/src/standard_frame.cpp` | modify | Register the `transparent` pass. |
| `packages/renderer/src/render_graph.cpp` | modify | `record_transparent_draws`. |
| `packages/sandbox/src/world_extract.cpp` | modify | Pick the pipeline from `material.opacity`. |
| `packages/sandbox/src/main.cpp` | modify | Create the pipeline; call the gate. |

---

## Task 1: The blend-arithmetic gate

Establishes that both backends blend identically, before anything depends on
it. Self-contained: its own shader, its own pipeline, its own 64×64 target.

**Files:**
- Create: `packages/sandbox/content/shaders/transparency_gate.hlsl`
- Modify: `packages/sandbox/src/gates/gates_renderer.cpp`
- Modify: `packages/sandbox/src/gates/gates.hpp`
- Modify: `packages/sandbox/src/gates/gate_registry.cpp`
- Modify: `packages/sandbox/src/main.cpp`

- [x] **Step 1.1: Write the gate shader**

Create `packages/sandbox/content/shaders/transparency_gate.hlsl`:

```hlsl
// Gate-only. Draws an axis-aligned quad in a flat colour, both taken from the
// cbuffer, so one shader covers the opaque underlay and the blended overlay by
// changing constants rather than by having two shaders that could differ.
//
// Deliberately not forward.hlsl: that wants seven textures, three samplers, a
// structured instance buffer and an IBL set, none of which says anything about
// blending. What this gate measures is the blend, so everything else is noise.

cbuffer Params : register(b0) {
    // Quad extents in NDC: (min_x, min_y, max_x, max_y).
    float4 rect;
    // Returned verbatim as SV_TARGET, alpha included. The alpha is the blend
    // factor under BlendMode::Alpha.
    float4 tint;
};

struct PSInput {
    float4 pos : SV_POSITION;
};

PSInput vs_main(uint id : SV_VertexID) {
    // Two triangles as a strip-order quad, indexed off SV_VertexID so the gate
    // needs no vertex or index buffer at all.
    const float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };
    const float2 c = corners[id];
    PSInput output;
    output.pos = float4(lerp(rect.x, rect.z, c.x), lerp(rect.y, rect.w, c.y), 0.0, 1.0);
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    return tint;
}
```

- [x] **Step 1.2: Declare the gate**

In `packages/sandbox/src/gates/gates.hpp`, beside the other renderer gates, add:

```cpp
// Alpha blending, measured rather than asserted: an opaque underlay, a blended
// overlay, and a readback compared against src*a + dst*(1-a) computed on the
// CPU. Takes `target` and `api` so the same function runs on both backends and
// the two sets of numbers sit next to each other - the shape RHI #24
// established, and required now that both backends have a geometry consumer for
// BlendMode::Alpha for the first time.
bool run_transparency_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);
```

- [x] **Step 1.3: Classify it in the registry**

In `packages/sandbox/src/gates/gate_registry.cpp`, in `kGates`, beside the other
renderer entries:

```cpp
    {"run_transparency_gate", GateKind::Gpu, nullptr},
```

- [x] **Step 1.4: Add the shader path constant**

In `packages/sandbox/src/sandbox_common.hpp`, beside `kForwardShader`:

```cpp
inline constexpr const char* kTransparencyGateShader = "/shaders/transparency_gate.hlsl";
```

- [x] **Step 1.5: Write the gate**

In `packages/sandbox/src/gates/gates_renderer.cpp`, add at the end of the file
(before the closing namespace, if any):

```cpp
bool run_transparency_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // Four things, none of which is "it did not crash":
    //
    //   1. the overlapped texels equal src*a + dst*(1-a), computed on the CPU
    //   2. the non-overlapped texels are still the underlay, so the blend did
    //      not smear outside the geometry
    //   3. the alpha channel equals 1*a + 1*(1-a) = 1. This is the non-obvious
    //      half of the existing blend state - SrcBlendAlpha is ONE, not
    //      SRC_ALPHA - and it is the field most likely to differ between two
    //      backends written from the same description
    //   4. an Opaque pipeline with the same shader and the same alpha does
    //      *not* blend, which is what proves assertion 1 measured the blend
    //      state rather than the shader
    //
    // Channel values chosen so the reference lands on integers: every one is
    // even and the alpha is exactly 0.5, so src*a + dst*(1-a) is a whole
    // number in every channel. The comparison still allows +/-1 because the
    // blend runs in float and the store rounds - a wrong blend factor is out
    // by tens, so a byte of slack costs the gate nothing.
    constexpr engine::u32 kExtent = 64;
    constexpr engine::u8 kDst[4] = {200, 100, 50, 255};
    constexpr engine::u8 kSrc[4] = {50, 200, 100, 255};
    constexpr engine::f32 kAlpha = 0.5f;

    auto expected = [](engine::u8 src, engine::u8 dst) {
        const engine::f32 blended =
            (static_cast<engine::f32>(src) / 255.f) * kAlpha
            + (static_cast<engine::f32>(dst) / 255.f) * (1.f - kAlpha);
        return static_cast<engine::i32>(blended * 255.f + 0.5f);
    };

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

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    // Two constant buffers, so both draws are recorded before a single submit.
    // Writing one buffer twice inside a command list would race the GPU.
    struct Params {
        engine::f32 rect[4];
        engine::f32 tint[4];
    };
    auto make_params = [](const engine::f32 (&rect)[4], const engine::u8 (&rgb)[4],
                           engine::f32 alpha) {
        Params p{};
        for (engine::u32 i = 0; i < 4; ++i) {
            p.rect[i] = rect[i];
        }
        p.tint[0] = static_cast<engine::f32>(rgb[0]) / 255.f;
        p.tint[1] = static_cast<engine::f32>(rgb[1]) / 255.f;
        p.tint[2] = static_cast<engine::f32>(rgb[2]) / 255.f;
        p.tint[3] = alpha;
        return p;
    };
    // Full target, then the left half. The right half is the control: it must
    // come back as the underlay, untouched.
    constexpr engine::f32 kFullRect[4] = {-1.f, -1.f, 1.f, 1.f};
    constexpr engine::f32 kHalfRect[4] = {-1.f, -1.f, 0.f, 1.f};
    const Params under = make_params(kFullRect, kDst, 1.f);
    const Params over = make_params(kHalfRect, kSrc, kAlpha);

    engine::rhi::BufferDesc cb{};
    cb.size = sizeof(Params);
    cb.usage = engine::rhi::BufferUsage::Uniform;
    auto under_buffer = device.create_buffer(cb, &under);
    auto over_buffer = device.create_buffer(cb, &over);

    auto make_pipeline = [&](engine::rhi::BlendMode blend, const char* name) {
        engine::rhi::GraphicsPipelineDesc desc{};
        desc.vertex_shader = std::span<const engine::u8>(vs.data);
        desc.pixel_shader = std::span<const engine::u8>(ps.data);
        desc.uniform_buffer_count = 1;
        desc.color_format = engine::rhi::Format::RGBA8_UNORM;
        desc.depth = engine::rhi::DepthTest::Disabled;
        desc.depth_write = false;
        desc.cull = engine::rhi::CullMode::None;
        desc.blend = blend;
        desc.debug_name = name;
        return desc;
    };
    auto opaque_pso = compiled
        ? device.create_graphics_pipeline(
              make_pipeline(engine::rhi::BlendMode::Opaque, "transparency_gate_opaque"))
        : nullptr;
    auto blend_pso = compiled
        ? device.create_graphics_pipeline(
              make_pipeline(engine::rhi::BlendMode::Alpha, "transparency_gate_alpha"))
        : nullptr;

    const bool ready = compiled && target_texture && under_buffer && over_buffer
        && opaque_pso && blend_pso;

    std::vector<engine::u8> blended_pixels(
        static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    std::vector<engine::u8> opaque_pixels(
        static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_blended = false;
    bool read_opaque = false;

    // Draw the underlay, then the overlay, then read back. `overlay_pso` is the
    // only thing that differs between the two runs - assertion 4.
    auto draw_pair = [&](engine::rhi::IGraphicsPipeline& overlay_pso,
                          std::vector<engine::u8>& out) {
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
        cmd.set_pipeline(*opaque_pso);
        cmd.set_constant_buffer(0, *under_buffer);
        cmd.draw(6, 0);
        cmd.set_pipeline(overlay_pso);
        cmd.set_constant_buffer(0, *over_buffer);
        cmd.draw(6, 0);
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        return device.read_texture(*target_texture, out.data(), out.size());
    };

    if (ready) {
        read_blended = draw_pair(*blend_pso, blended_pixels);
        read_opaque = draw_pair(*opaque_pso, opaque_pixels);
    }

    auto texel = [](const std::vector<engine::u8>& pixels, engine::u32 x, engine::u32 y) {
        return &pixels[(static_cast<engine::usize>(y) * kExtent + x) * 4];
    };
    // (16, 32) is inside the overlay; (48, 32) is the control half.
    const engine::u8* over_texel = texel(blended_pixels, 16, 32);
    const engine::u8* control = texel(blended_pixels, 48, 32);
    const engine::u8* opaque_texel = texel(opaque_pixels, 16, 32);

    auto within_one = [](engine::i32 got, engine::i32 want) {
        const engine::i32 delta = got - want;
        return delta <= 1 && delta >= -1;
    };

    const engine::i32 want_r = expected(kSrc[0], kDst[0]);
    const engine::i32 want_g = expected(kSrc[1], kDst[1]);
    const engine::i32 want_b = expected(kSrc[2], kDst[2]);
    const bool blend_ok = read_blended
        && within_one(over_texel[0], want_r)
        && within_one(over_texel[1], want_g)
        && within_one(over_texel[2], want_b);
    // 1*a + 1*(1-a) = 1. SrcBlendAlpha is ONE, so this is 255 rather than the
    // 0.5 the colour channels were scaled by.
    const bool blend_alpha_ok = read_blended && within_one(over_texel[3], 255);
    const bool control_ok = read_blended && control[0] == kDst[0] && control[1] == kDst[1]
        && control[2] == kDst[2];
    // The same shader and the same alpha through an Opaque pipeline writes src
    // straight through. Without this, assertion 1 could be satisfied by a
    // shader that happened to output the blended value itself.
    const bool opaque_ok = read_opaque && opaque_texel[0] == kSrc[0]
        && opaque_texel[1] == kSrc[1] && opaque_texel[2] == kSrc[2];

    const bool passed = ready && read_blended && read_opaque && blend_ok && blend_alpha_ok
        && control_ok && opaque_ok;

    char message[288];
    std::snprintf(message, sizeof(message),
        "Transparency gate [%s]: blended=(%u,%u,%u,%u) want=(%d,%d,%d,255) "
        "control=(%u,%u,%u) want=(%u,%u,%u) opaque_passthrough=(%u,%u,%u) (%s)",
        api, over_texel[0], over_texel[1], over_texel[2], over_texel[3],
        want_r, want_g, want_b,
        control[0], control[1], control[2], kDst[0], kDst[1], kDst[2],
        opaque_texel[0], opaque_texel[1], opaque_texel[2],
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
```

- [x] **Step 1.6: Call it from the gate sequence**

In `packages/sandbox/src/main.cpp`, inside `setup_forward_demo`, beside the
other per-backend gate calls (the `run_storage_texture_gate` / `run_msaa_gate`
pattern), resolve the shader and call it:

```cpp
    std::string transparency_gate_path;
    if (!resolve_content(loader, kTransparencyGateShader, transparency_gate_path)) {
        return false;
    }
```

and in the `else if` chain that already calls the live-device gates:

```cpp
    } else if (!run_transparency_gate(*device, compiler, transparency_gate_path,
                   shader_target_for(*device), api_name_for(*device))
        && fail_on_gate) {
        return false;
```

Then in the parity block, beside `run_storage_texture_gate(*vk_device, ...)`:

```cpp
        } else if (!transparency_gate_path.empty()
            && !run_transparency_gate(*vk_device, compiler, transparency_gate_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
```

and in the D3D12 parity block, beside its `run_storage_texture_gate` call, the
same with `ShaderTarget::Dxil` and `"d3d12"`.

- [x] **Step 1.7: Build and run it on both backends**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates-gpu
```

```bash
./solengine.bat gates-vk
```

Expected: `Transparency gate [d3d12]` and `Transparency gate [vulkan]` both
`(pass)`, with `blended=(125,150,75,255) want=(125,150,75,255)`. Read the two
lines and confirm the numbers are **the same on both backends** — that
agreement is the point of this task. Debug layer 0/0/0, validation silent.

If either fails, stop: the contract's `BlendMode::Alpha` does not do what the
rest of this plan assumes, and that is a `rhi` bug to fix before continuing.

- [x] **Step 1.8: Commit**

```bash
git add packages/sandbox/content/shaders/transparency_gate.hlsl packages/sandbox/src/gates/gates_renderer.cpp packages/sandbox/src/gates/gates.hpp packages/sandbox/src/gates/gate_registry.cpp packages/sandbox/src/sandbox_common.hpp packages/sandbox/src/main.cpp
git commit -m "test(renderer): measure alpha blending on both backends (Renderer #16)"
```

---

## Task 2: `forward_transparent` on `FramePipelines` — the red

**Files:**
- Modify: `packages/renderer/include/engine/renderer/frame_pipelines.hpp`

- [x] **Step 2.1: Add the field and its table entry**

In `packages/renderer/include/engine/renderer/frame_pipelines.hpp`, add the
field immediately after `forward`:

```cpp
    rhi::IGraphicsPipeline* forward = nullptr;
    // The same shader with BlendMode::Alpha and depth_write off. Consumed
    // per-batch like `forward`, chosen in world_extract.cpp from the
    // material's opacity.
    rhi::IGraphicsPipeline* forward_transparent = nullptr;
```

and the matching entry immediately after `forward`'s:

```cpp
    {&FramePipelines::forward, "forward"},
    {&FramePipelines::forward_transparent, "forward_transparent"},
```

- [x] **Step 2.2: Build and watch `run_pipeline_set_gate` go red**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates
```

Expected: `Pipeline set gate` **FAILs** and names `forward_transparent` as the
missing pipeline, because nothing creates it yet. This is the red. The
`static_assert` in the header would have caught a field with no table entry at
compile time; the gate catches a table entry nothing fills.

Do not commit a red tree. Continue to Task 3.

---

## Task 3: The pipeline

**Files:**
- Modify: `packages/sandbox/src/sandbox_common.hpp`
- Modify: `packages/sandbox/src/sandbox_common.cpp`
- Modify: `packages/sandbox/src/main.cpp`
- Modify: `packages/sandbox/src/gates/gates_renderer.cpp`

- [x] **Step 3.1: Declare the maker**

In `packages/sandbox/src/sandbox_common.hpp`, immediately after
`make_forward_pipeline_desc`'s declaration:

```cpp
// The forward pipeline with alpha blending and no depth write.
//
// Calls make_forward_pipeline_desc and overrides two fields rather than
// copying its body: the two must not drift, and the field most likely to drift
// is the depth convention, which is the engine's most load-bearing value.
engine::rhi::GraphicsPipelineDesc make_forward_transparent_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention convention);
```

- [x] **Step 3.2: Define it**

In `packages/sandbox/src/sandbox_common.cpp`, immediately after
`make_forward_pipeline_desc`'s definition:

```cpp
engine::rhi::GraphicsPipelineDesc make_forward_transparent_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention convention) {
    engine::rhi::GraphicsPipelineDesc desc = make_forward_pipeline_desc(vs, ps, convention);
    desc.blend = engine::rhi::BlendMode::Alpha;
    // Not a preference - a requirement. The motion pass draws with
    // DepthTest::Equal against the depth buffer forward produced, and
    // extract.cpp records that geometry which does not rasterize identically
    // to forward silently writes nothing. A transparent surface writing depth
    // would erase the motion vectors of everything behind it, which shows up
    // as TAA ghosting on objects that are not transparent.
    //
    // Depth *testing* stays on. That is what makes transparency correct
    // against opaque geometry with no sorting at all.
    desc.depth_write = false;
    desc.debug_name = "forward_transparent";
    return desc;
}
```

- [x] **Step 3.3: Create it in the app**

In `packages/sandbox/src/main.cpp`, in `setup_forward_demo`, immediately after
the block that creates the `forward` pipeline and calls
`demo->adopt(&engine::renderer::FramePipelines::forward, std::move(p))`, add
the same shape for the transparent one. It reuses `vs_bytecode` and
`ps_bytecode` — the same shader, so no second compile:

```cpp
    {
        auto p = device->create_graphics_pipeline(make_forward_transparent_pipeline_desc(
            std::span<const engine::u8>(vs_bytecode.data),
            std::span<const engine::u8>(ps_bytecode.data), device->depth_convention()));
        if (!p) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
                "Forward transparent pipeline creation failed");
            return false;
        }
        demo->adopt(&engine::renderer::FramePipelines::forward_transparent, std::move(p));
    }
```

- [x] **Step 3.4: Add it to the depth-convention gate's list**

In `packages/sandbox/src/gates/gates_renderer.cpp`, in
`run_depth_convention_gate`, extend `pipelines_ok`:

```cpp
    const bool pipelines_ok =
        make_forward_pipeline_desc(none, none, live).depth == want_closer
        && make_forward_transparent_pipeline_desc(none, none, live).depth == want_closer
        // The transparent maker is the one place depth_write is deliberately
        // false, so this gate is where that is pinned. Nothing else checks it,
        // and a depth write from a transparent surface removes the motion
        // vectors of everything behind it with no error anywhere.
        && make_forward_transparent_pipeline_desc(none, none, live).depth_write == false
        && make_shadow_pipeline_desc(none, live).depth == want_closer
        && make_sky_pipeline_desc(none, none, live).depth == want_closer_eq
        && (make_shadow_pipeline_desc(none, live).slope_scaled_depth_bias < 0.f)
            == (live == DepthConvention::Reversed);
```

- [x] **Step 3.5: Build and confirm green**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates-gpu
```

Expected: `Pipeline set gate` back to `(pass)`, now covering 14 pipelines.
`Depth convention gate` still `(pass)`. Total 98 pass, 0 FAIL.

- [x] **Step 3.6: Commit**

```bash
git add packages/renderer/include/engine/renderer/frame_pipelines.hpp packages/sandbox/src/sandbox_common.hpp packages/sandbox/src/sandbox_common.cpp packages/sandbox/src/main.cpp packages/sandbox/src/gates/gates_renderer.cpp
git commit -m "feat(renderer): a blended forward pipeline, depth-tested but not depth-writing (Renderer #16)"
```

---

## Task 4: Opacity from material to instance data

**Files:**
- Modify: `packages/scene/include/engine/scene/world.hpp`
- Modify: `packages/renderer/include/engine/renderer/extract.hpp`
- Modify: `packages/renderer/include/engine/renderer/render_snapshot.hpp`
- Modify: `packages/renderer/src/extract.cpp`
- Modify: `packages/sandbox/src/world_extract.cpp`
- Modify: `packages/sandbox/src/gates/gates_renderer.cpp`

- [x] **Step 4.1: Extend `run_material_gate` first — the red**

In `packages/sandbox/src/gates/gates_renderer.cpp`, in `run_material_gate`,
add a third extract after the existing `before` / `after` pair. It mirrors
their shape exactly — own world copy, own arena, own snapshot — because that
is how this gate already probes a material field:

```cpp
    // Opacity travels the same road as roughness, so it is checked the same
    // way. The road is Material -> ExtractInstance -> DrawItem ->
    // InstanceData::material_params.z, and a break anywhere along it is a
    // transparent object drawn opaque, which reads as a content mistake.
    //
    // Only material 0, not every material: the pipeline-split assertion below
    // needs some opaque batches left to split away from.
    constexpr engine::f32 kProbeOpacity = 0.375f;
    engine::scene::World translucent = copy;
    translucent.materials[0].opacity = kProbeOpacity;
    engine::Arena arena_translucent(256 * 1024);
    engine::renderer::RenderSnapshot translucent_snapshot{};
    translucent_snapshot.width = 1280;
    translucent_snapshot.height = 720;
    sandbox::extract_world(translucent, camera.position, assets, false, nullptr,
        arena_translucent, translucent_snapshot);

    // The value reaches both the draw and the instance array. Every draw is
    // either the probe value or 1 - anything else means the field is being
    // overwritten somewhere along the road.
    bool opacity_reaches_draw = !translucent_snapshot.draws.empty();
    for (const engine::renderer::DrawItem& draw : translucent_snapshot.draws) {
        if (draw.opacity != kProbeOpacity && draw.opacity != 1.f) {
            opacity_reaches_draw = false;
        }
    }
    bool opacity_reaches_instance = !translucent_snapshot.instances.empty();
    bool probe_instance_found = false;
    for (const engine::renderer::InstanceData& inst : translucent_snapshot.instances) {
        if (inst.material_params.z == kProbeOpacity) {
            probe_instance_found = true;
        } else if (inst.material_params.z != 1.f) {
            opacity_reaches_instance = false;
        }
    }
    opacity_reaches_instance = opacity_reaches_instance && probe_instance_found;

    // The pipeline choice, not just the value. An opacity below 1 must select
    // a different pipeline, and a different pipeline must split the batch,
    // because `pipeline` is part of extract.cpp's same_key. This is what makes
    // the transparent pass see any batches at all - without it the value could
    // arrive correctly and nothing would ever draw blended.
    const bool pipeline_split
        = translucent_snapshot.batches.size() > before.batches.size();
```

Add the three new booleans to the gate's `passed`, and the real numbers to its
message:

```cpp
    const bool passed = table_ok && handles_ok && draws_ok && changed && layout_ok
        && gltf_ok && opacity_reaches_draw && opacity_reaches_instance && pipeline_split;
    char message[288];
    std::snprintf(message, sizeof(message),
        "Material gate: materials=%u handles=%s roughness_is_data=%s opacity_is_data=%s "
        "batches=%zu->%zu layout=%s (%s)",
        world.material_count, handles_ok ? "yes" : "no",
        (draws_ok && changed) ? "yes" : "no",
        (opacity_reaches_draw && opacity_reaches_instance) ? "yes" : "no",
        before.batches.size(), translucent_snapshot.batches.size(),
        layout_ok ? "336" : "bad",
        passed ? "pass" : "FAIL");
```

Note the existing message prints `layout_ok ? "400" : "bad"` — a stale literal
from before the instancing refactor, when `FrameConstants` was 400. It is 336
now, and the gate asserts 336 while printing 400. Fix it while here: a gate
message that prints the wrong number is worse than one that prints none.

- [x] **Step 4.2: Build and watch it fail**

```bash
cmake --build build --config Debug 2>&1 | head -20
```

Expected: a **compile error** — `Material` has no member `opacity`, `DrawItem`
has no member `opacity`. That is the red for this task: the assertion names
fields that do not exist yet.

- [x] **Step 4.3: Add the field to `scene::Material`**

In `packages/scene/include/engine/scene/world.hpp`:

```cpp
struct Material {
    u32 albedo = 0;
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    // 1 means opaque and takes the opaque pipeline; anything less takes the
    // blended one. A float rather than a bool because the value is what the
    // shader multiplies its alpha by - a separate flag could disagree with it,
    // which is the failure DepthConvention exists as one value to avoid.
    f32 opacity = 1.f;
};
```

- [x] **Step 4.4: Add it to `ExtractInstance`**

In `packages/renderer/include/engine/renderer/extract.hpp`, beside `roughness`:

```cpp
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    f32 opacity = 1.f;
```

- [x] **Step 4.5: Add it to `DrawItem`**

In `packages/renderer/include/engine/renderer/render_snapshot.hpp`, beside
`roughness`:

```cpp
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    f32 opacity = 1.f;
```

- [x] **Step 4.6: Carry it through extract**

In `packages/renderer/src/extract.cpp`, where `DrawItem` is filled from
`ExtractInstance`, add `item.opacity = instance.opacity;` beside the
`roughness` copy (match the surrounding spelling — the loop may use different
variable names).

Then in the instance-fill loop, replace the `material_params` line:

```cpp
                    // .z was unused padding. Opacity goes here rather than in
                    // FrameConstants: that struct is per *batch*, so it is the
                    // wrong home, and sizeof(FrameConstants) == 336 is
                    // asserted in three separate gates.
                    instances[dst].material_params
                        = {draw.metallic, draw.roughness, draw.opacity, 0.f};
```

- [x] **Step 4.7: Copy it in the scene bridge and pick the pipeline**

In `packages/sandbox/src/world_extract.cpp`, replace:

```cpp
        item.pipeline = assets.pipelines.forward;
```

with:

```cpp
        // The one line that routes a material to a pipeline. Falls back to the
        // opaque one when the transparent pipeline is absent, so a build that
        // failed to create it draws a visibly wrong material rather than
        // making the object disappear. run_pipeline_set_gate makes that
        // unreachable; the fallback costs a `?:` and removes the class.
        const bool transparent = material.opacity < 1.f
            && assets.pipelines.forward_transparent != nullptr;
        item.pipeline = transparent ? assets.pipelines.forward_transparent
                                    : assets.pipelines.forward;
```

and add beside the `roughness` copy:

```cpp
        item.opacity = material.opacity;
```

- [x] **Step 4.8: Build and confirm green**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates-gpu
```

Expected: `Material gate` `(pass)` with the two new opacity values in its
message. 98 pass, 0 FAIL. `Instancing gate` still exactly 3 batches of 3/2/2 —
its world has no transparent materials, so batching is untouched. Confirm that
line rather than assuming it.

- [x] **Step 4.9: Commit**

```bash
git add packages/scene/include/engine/scene/world.hpp packages/renderer/include/engine/renderer/extract.hpp packages/renderer/include/engine/renderer/render_snapshot.hpp packages/renderer/src/extract.cpp packages/sandbox/src/world_extract.cpp packages/sandbox/src/gates/gates_renderer.cpp
git commit -m "feat(scene): material opacity, carried to the instance array (Renderer #16)"
```

---

## Task 5: The shader

**Files:**
- Modify: `packages/sandbox/content/shaders/forward.hlsl`

- [x] **Step 5.1: Widen the interpolant and use the alpha**

In `packages/sandbox/content/shaders/forward.hlsl`, three edits.

`PSInput`:

```hlsl
    // Constant across the instance, so nointerpolation - the pixel shader
    // needs it and SV_InstanceID is a vertex-stage input only.
    // x = metallic, y = roughness, z = opacity.
    nointerpolation float3 material : TEXCOORD2;
```

`vs_main`:

```hlsl
    output.material = inst.material.xyz;
```

`ps_main` — keep the sampled alpha instead of discarding it, and return it
scaled by the material's opacity:

```hlsl
    float4 albedo_sample = albedo_map.Sample(albedo_sampler, input.uv);
    float3 albedo = albedo_sample.rgb;
```

and the return:

```hlsl
    // Texture alpha times material opacity, both: the texture gives per-texel
    // shape (a window frame, a decal) and the material gives a uniform
    // dimmer. Using only one means faking the other somewhere else.
    //
    // The opaque pipeline runs this same shader. Under BlendMode::Opaque the
    // alpha channel is written but never read as a blend factor, so opaque
    // output is unchanged - and nothing samples scene_color.a: bloom, TAA and
    // tonemap all take .rgb.
    return float4(lit, albedo_sample.a * input.material.z);
```

- [x] **Step 5.2: Build, and check every shader still compiles for both targets**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates-vk
```

Expected: no `not SPIR-V` and no compile errors. The forward shader now
compiles to both DXIL and SPIR-V with the widened interpolant; a SM 6.0
`float3` interpolant is unremarkable in both, but confirm rather than assume,
because this is the only shader edit in the row.

- [x] **Step 5.3: Commit**

```bash
git add packages/sandbox/content/shaders/forward.hlsl
git commit -m "feat(shaders): forward alpha from texture and material opacity (Renderer #16)"
```

---

## Task 6: The pass and its recorder

**Files:**
- Modify: `packages/renderer/include/engine/renderer/render_snapshot.hpp`
- Modify: `packages/renderer/src/render_graph.cpp`
- Modify: `packages/renderer/src/standard_frame.cpp`
- Modify: `packages/sandbox/src/gates/gates_renderer.cpp`

- [x] **Step 6.1: Assert the pass order first — the red**

In `packages/sandbox/src/gates/gates_renderer.cpp`, in `run_transparency_gate`,
add a pass-order section. There is no `pass_index` helper — `run_motion_gate`
walks `probe.pass_count()` inline, so use that same mechanism rather than
inventing a second one:

```cpp
    // Position, and why each neighbour matters:
    //   after forward - it blends against opaque geometry
    //   after motion  - the motion pass needs forward's depth untouched
    //   after sky     - the sky fills what forward did not, and a glass pane
    //                   must have the sky behind it
    //   before bloom  - a bright transparent surface should glow like
    //                   everything else
    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc frame{};
    frame.log_ready = false;
    const bool graph_ok = engine::renderer::setup_standard_frame(probe, std::move(frame));
    int forward_i = -1;
    int sky_i = -1;
    int transparent_i = -1;
    int bloom_i = -1;
    for (engine::u32 i = 0; i < probe.pass_count(); ++i) {
        const std::string_view name = probe.pass_name(i);
        if (name == "forward") {
            forward_i = static_cast<int>(i);
        } else if (name == "sky") {
            sky_i = static_cast<int>(i);
        } else if (name == "transparent") {
            transparent_i = static_cast<int>(i);
        } else if (name == "bloom_down0") {
            bloom_i = static_cast<int>(i);
        }
    }
    const bool order_ok = graph_ok && forward_i >= 0 && sky_i > forward_i
        && transparent_i > sky_i && bloom_i > transparent_i;
```

Add `order_ok` to `passed`, and all four indices to the message, so a wrong
order says which pass sits where rather than just `order=no`.

- [x] **Step 6.2: Build and watch it fail**

```bash
cmake --build build --config Debug && ./solengine.bat gates
```

Expected: `Transparency gate` **FAILs** with `transparent_i=-1` — the pass does
not exist. That is the red.

- [x] **Step 6.3: Declare the recorder**

In `packages/renderer/include/engine/renderer/render_snapshot.hpp`, beside the
other recorders:

```cpp
void record_opaque_draws(PassContext& ctx);
void record_transparent_draws(PassContext& ctx);
```

- [x] **Step 6.4: Define it**

In `packages/renderer/src/render_graph.cpp`, `record_opaque_draws` and
`record_transparent_draws` differ only in which batches they take, so factor
the body rather than copying it. Rename the existing body to a static helper
taking a predicate, and define both in terms of it:

```cpp
// The two geometry passes over `scene_color` share every constant, every
// binding and every batch-recording rule; they differ only in which pipeline's
// batches they take. Factored rather than copied, because the thing most
// likely to drift between two copies is the lighting constants, and a
// divergence there is transparent surfaces lit differently from opaque ones.
static void record_forward_draws(PassContext& ctx, bool transparent) {
    // ... the existing record_opaque_draws body verbatim, except the
    // record_draws call, which becomes:
    rhi::IGraphicsPipeline* want = transparent ? ctx.snapshot.pipelines.forward_transparent
                                               : ctx.snapshot.pipelines.forward;
    record_draws(ctx, nullptr, constants,
        [](FrameConstants&, const DrawBatch&) {},
        /* ... the existing bind lambda verbatim ... */);
}

void record_opaque_draws(PassContext& ctx) { record_forward_draws(ctx, false); }
void record_transparent_draws(PassContext& ctx) { record_forward_draws(ctx, true); }
```

`record_draws` iterates every batch and uses `batch.pipeline` when handed
`nullptr`, so it needs a way to skip the batches belonging to the other pass.
Add a batch filter to `record_draws`'s existing loop:

```cpp
template <typename Constants, typename FillFn, typename BindFn>
void record_draws(PassContext& ctx, rhi::IGraphicsPipeline* pipeline, Constants& constants,
    FillFn&& fill, BindFn&& bind, rhi::IGraphicsPipeline* only_pipeline = nullptr) {
    // ... unchanged ...
    for (const DrawBatch& batch : ctx.snapshot.batches) {
        // `only_pipeline` splits one batch list between two passes. Null means
        // every batch, which is what shadow and motion want - they draw all
        // geometry, transparent included, with one pipeline of their own.
        if (only_pipeline && batch.pipeline != only_pipeline) {
            continue;
        }
        rhi::IGraphicsPipeline* pso = pipeline ? pipeline : batch.pipeline;
        // ... unchanged ...
```

and pass `want` as `only_pipeline` from `record_forward_draws`.

**Note on shadow and motion:** both pass a non-null `pipeline` and no
`only_pipeline`, so they keep drawing every batch including the transparent
ones. That is correct for shadows — a tinted window should still cast
something — and for motion it is what keeps `DepthTest::Equal` matching
forward's rasterization. Leave both alone.

- [x] **Step 6.5: Register the pass**

In `packages/renderer/src/standard_frame.cpp`, immediately after the `sky`
registration:

```cpp
    // Transparency, after the sky and before bloom reads scene_color.
    //
    // DepthWrite is declared here because the pass must *bind* the depth
    // target to test against it; the pipeline's depth_write = false is what
    // stops the write. The graph's access is about the attachment binding.
    //
    // Unsorted, deliberately: Renderer #34 owns draw ordering. Transparency
    // against opaque geometry and the sky is correct because depth testing
    // still runs; two transparent surfaces that overlap each other blend in
    // scene order until that row lands.
    RenderPassDesc transparent{};
    transparent.name = "transparent";
    transparent.writes[0] = {scene_color, Access::ColorWrite};
    transparent.writes[1] = {depth, Access::DepthWrite};
    transparent.write_count = 2;
    transparent.reads[0] = {shadow, Access::ShaderRead};
    transparent.read_count = 1;
    transparent.clear_color_target = false;
    transparent.clear_depth = false;
    transparent.should_execute = [](const RenderSnapshot& snapshot) {
        return snapshot.pipelines.forward_transparent != nullptr;
    };
    transparent.execute = record_transparent_draws;
    graph.add_pass(std::move(transparent));
```

- [x] **Step 6.6: Build and confirm green**

```bash
cmake --build build --config Debug
```

```bash
./solengine.bat gates-gpu
```

Expected: `Transparency gate` `(pass)` with `transparent_i` between `sky` and
`bloom_down0`. `Graph compiled: 26 passes` instead of 25. `Motion gate` and
`TAA gate` still `(pass)` — their pass-order comparisons are relative, so an
insertion satisfies them, but confirm both lines.

- [x] **Step 6.7: Commit**

```bash
git add packages/renderer/include/engine/renderer/render_snapshot.hpp packages/renderer/src/render_graph.cpp packages/renderer/src/standard_frame.cpp packages/sandbox/src/gates/gates_renderer.cpp
git commit -m "feat(renderer): a transparent pass after the sky (Renderer #16)"
```

---

## Task 7: Something transparent in the sandbox

**Files:**
- Modify: `packages/sandbox/src/main.cpp` (or wherever the demo world's
  materials are built — find it with
  `grep -rn "materials\[" packages/sandbox/src/`)

- [x] **Step 7.1: Make one demo material transparent**

Give one of the demo world's materials `opacity = 0.45f`, with a comment:

```cpp
    // One transparent material, so the path is exercised in a real frame and
    // not only in the gate. 0.45 is low enough to see through and high enough
    // to tint. It also shows the documented limitation honestly: where two of
    // these instances overlap, the blend order is scene order until
    // Renderer #34.
    world.materials[<index>].opacity = 0.45f;
```

Pick the index whose instances sit both in front of and behind opaque ones, so
the depth test is visibly doing its job.

- [x] **Step 7.2: Look at it on both backends**

```bash
./solengine.bat run-gpu
```

```bash
./solengine.bat run-vk
```

Expected: those instances are see-through, tinted, correctly occluded by opaque
geometry in front of them, and showing the sky through them where nothing is
behind. No debug-layer or validation output. **A gate can assert the
arithmetic; only a frame shows whether it looks like glass.** If the objects
are invisible, the pipeline fell back or the pass is not executing; if they are
opaque, the alpha is not reaching the shader.

- [x] **Step 7.3: Confirm the frustum gate's floors still hold**

```bash
./solengine.bat gates-gpu
```

Expected: `Frustum gate` `(pass)` with `visible >= 5` and `skipped >= 16`. The
demo gains a transparent material, not fewer instances, so this should be
untouched — confirm, because those floors are content-dependent.

- [x] **Step 7.4: Commit**

```bash
git add packages/sandbox/src/main.cpp
git commit -m "feat(sandbox): a transparent material in the demo world (Renderer #16)"
```

---

## Task 8: Verify and ship

- [x] **Step 8.1: All three gate suites**

```bash
./solengine.bat gates-gpu
```

```bash
./solengine.bat gates-vk
```

```bash
./solengine.bat gates-release
```

Expected: D3D12 and Release at 99 pass / 0 FAIL / 0 skip; Vulkan at 98 pass /
0 FAIL / 1 skip (the mesh-reload VRAM skip). Debug layer 0/0/0, validation
silent. Both new gate lines present on both backends with matching numbers.

- [x] **Step 8.2: Invariants**

```bash
pwsh -NoProfile -File tools/check-invariants.ps1
```

Expected: all 17 pass, including `shader-target` over the new gate shader's
compile site and `roadmap-audit` once Step 8.3 is done.

- [x] **Step 8.3: Refresh the ROADMAP LOC audit**

The audit paragraph in `docs/ROADMAP.md` is an implicit dependency of every
source change and `roadmap-audit` fails if it is stale. Recount and update
total lines, file count, `sandbox`, `main.cpp`, and the per-package figures
that moved.

- [x] **Step 8.4: Ship it**

Run `/ship-feature`, which writes the ROADMAP Why/Choice/Gate/Do-not entry,
flips Renderer #16 to **Done** with recounted subtotals and header totals, sets
the spec's `Status: implemented`, commits and pushes.

The Do-not lines for that entry are already drafted at the end of the spec.
Renderer #33 and #34 both name Renderer #16 as their blocker and both are
`Later`; the `map-dependencies` invariant requires a `Later` row whose named
blockers are all `Done` to be flipped to **Ready**, so both must move in the
same commit.

---

## Self-review

**Spec coverage.** All eight touch points map to a task: `Material::opacity`
(4), pipeline choice in the bridge (4), `material_params.z` (4), plumbing (2),
the pipeline maker (3), the pass (6), the shader (5), the recorder (6). The
gate's six assertions map to Task 1 (blend arithmetic, alpha channel, control
half, opaque passthrough), Task 3 (`depth_write` and the depth convention),
Task 4 (opacity propagation) and Task 6 (pass order). The gate-risk table maps
to the verification step in each task that touches the gate named. The sandbox
demo is Task 7.

**Gap found and closed.** The first draft's Task 4 proved opacity reached the
draw but never that the pipeline differed, leaving the spec's gate assertion 5
uncovered — a state where the value arrives correctly and nothing ever draws
blended. Task 4, Step 4.1 now asserts `pipeline_split` on the batch count.

**Type consistency.** `opacity` is `f32` and spelled the same in
`scene::Material`, `ExtractInstance`, `DrawItem` and
`material_params.z`. `make_forward_transparent_pipeline_desc` has the same
three-parameter signature as `make_forward_pipeline_desc` throughout.
`record_transparent_draws` matches its declaration. `only_pipeline` is the same
name in `record_draws`'s signature and at both call sites.

**Placeholders.** One remains, deliberately: Task 7's demo material index,
which depends on how the sandbox builds its world and is best chosen by looking
at which instances sit in front of and behind opaque ones. A guessed index is
worse than an instruction to pick a good one. Every other step carries real
code checked against the file it edits — including `run_material_gate`'s
`before` / `copy` variable names and `run_motion_gate`'s `probe.pass_count()`
walk, both quoted from the current source rather than assumed.
