# Vulkan parity — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:executing-plans`.
> Trunk-based — commit to `main` and push per phase, no branch, no PR. The test
> for every phase is a **gate**; write the assertion first and watch it fail.

**Goal:** `solengine run --rhi vulkan` renders the real frame, and `--gates`
passes on the Vulkan device with the validation layer silent.

**Architecture:** Grow `run_backend_parity_gate` subsystem by subsystem. Each
phase lands green **on the D3D12 build** — the gate runs both devices every
`--gates` — so a phase is done when both backends produce the same number, not
when it compiles. The sandbox switches over only in phase 11.

Spec: [2026-09-02-vulkan-parity-design.md](../specs/2026-09-02-vulkan-parity-design.md)

**A note on this plan's density.** The Vulkan-backend plan of RHI #12 reproduced
most of its code. Twelve phases of that would be a second implementation, and a
plan that has to be maintained alongside the code it describes goes stale. So
code appears here only where it is load-bearing or easy to get wrong — the
measured binding numbers, the barrier fix, the sync structure — and everything
else is named by file and function with its assertion. The assertions are the
part that must be exact.

## Measured facts this plan depends on

Do not re-derive these; they were dumped from real SPIR-V (see the spec).

| | |
|---|---|
| Vertex `Location` | The attribute's **index in `GraphicsPipelineDesc::attributes`**. `POSITION`→0, `NORMAL`→1, `TEXCOORD`→2, matching the HLSL struct's declaration order, which the engine's arrays already follow. |
| `uniform_buffer_count` | set 0, binding `0 + i` |
| `sampled_texture_count` | set 0, binding `16 + i`, `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, fragment-only |
| `sampler_count` | set 0, binding `48 + i`, `VK_DESCRIPTOR_TYPE_SAMPLER`, **immutable** |
| `storage_buffer_count` | **set 1**, binding `16 + i`, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, all stages |
| `storage_texture_count` | set 0, binding `32 + i`, `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` |

The bases already exist as `kBindingBase*` in `device_vulkan.hpp` and are
mirrored by the `-fvk-*-shift` flags in `shaders-dxc`. Both sites carry a
comment naming the other; keep that true.

---

## Phase 1: the barrier defect

**Closes:** nothing. Fixes committed code before anything depends on it.

`to_vulkan_barrier` in `commands_vulkan.cpp` maps `ShaderRead` to
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` regardless of aspect, and
`transition()` derives the aspect separately from the format. Two derivations of
one fact.

- [ ] **1.1** Give `to_vulkan_barrier` the aspect: `BarrierState to_vulkan_barrier(ResourceState, VkImageAspectFlags)`,
  and return the aspect in `BarrierState` so `transition()` stops deriving it.
  `ShaderRead` on a depth aspect becomes
  `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL`; on colour it stays
  `SHADER_READ_ONLY_OPTIMAL`.
- [ ] **1.2** Rebuild, run `solengine gates-gpu`. Expect **87 pass / 0 FAIL**,
  unchanged — nothing samples depth on Vulkan yet, so this phase is a
  correctness fix with no observable effect. Say that in the commit rather than
  implying a gate proved it.
- [ ] **1.3** Commit: `fix(rhi-vulkan): the barrier's layout follows the image aspect`

## Phase 2: staged uploads and device-local buffers

- [ ] **2.1** `create_buffer` for `Vertex`, `Index`, `Storage`: device-local
  `VkBuffer` with the matching usage plus `TRANSFER_DST`, and when `data` is
  given a host-visible staging buffer, `vkCmdCopyBuffer` on the device's command
  buffer, submit, wait, free. Mirror `upload_to_default`'s shape in
  `device_d3d12.cpp` — including that the staging buffer must outlive the copy.
- [ ] **2.2** `copy_buffer` already works; `copy_texture` gains
  `vkCmdCopyImage` with the extent and format asserts the D3D12 side makes.
- [ ] **2.3** No new gate. Proven by phase 3, which is the first thing that
  reads one. Do not add an assertion that only checks a non-null pointer.
- [ ] **2.4** Commit: `feat(rhi-vulkan): device-local buffers with staged uploads`

## Phase 3: vertex input and indexed draws

**Assertion:** an indexed draw whose per-vertex data is *used*, so a wrong
`location` shows up as a wrong colour rather than a missing triangle.

- [ ] **3.1** New `packages/sandbox/content/shaders/parity_mesh_gate.hlsl`: a
  `VSInput` of `POSITION`/`NORMAL`/`TEXCOORD` in that order, a VS that passes
  normal and uv through, a PS returning
  `float4(uv.x, uv.y, normal.z, 1)`. Then each attribute's location is
  legible in the output: a swapped normal and uv changes the pixel.
- [ ] **3.2** Extend `run_backend_parity_gate` with an indexed-mesh section, or
  add `run_parity_mesh_gate` with the same `(IDevice&, ShaderTarget, api,
  out)` shape. Two triangles forming a quad, 4 vertices and 6 indices, and
  probe four texels whose expected bytes come from the uv/normal maths.
  **Watch it fail on Vulkan first** — `create_graphics_pipeline with vertex
  attributes` is a `not_implemented` today.
- [ ] **3.3** `VkVertexInputAttributeDescription` per attribute with
  `location = i`, `offset = attributes[i].offset`, format from
  `VertexFormat`; one binding with the stride from `set_vertex_buffer`.
  `set_vertex_buffer` / `set_index_buffer` / `draw_indexed`.
  `draw_indexed`'s `instance_count` maps to `vkCmdDrawIndexed`'s
  `instanceCount` with `firstInstance = 0` — the contract has no
  `start_instance` precisely because the three APIs disagree about it.
- [ ] **3.4** Assert the load-bearing coincidence rather than relying on it. In
  `create_graphics_pipeline`, log an error naming the index and semantic when
  `attributes[i]` is not the *i*-th semantic the shader declares — which cannot
  be read from SPIR-V without a reflection pass, so instead assert the
  weaker checkable thing: `semantic_index == 0` for every attribute, since a
  non-zero index (`TEXCOORD1`) breaks declaration-order equivalence. Nothing in
  the tree uses one; the error is what stops the first one being silent.
- [ ] **3.5** Both backends green, same probes. Commit:
  `feat(rhi-vulkan): vertex input and indexed draws (RHI #24)`

## Phase 4: textures, mips, cubes, arrays, samplers

The largest phase. Split the commit if it helps, but keep the assertion whole.

- [ ] **4.1** `create_texture` for every `TextureUsage`:
  `ShaderResource` and `ColorShaderResource` (sampled + colour attachment),
  `DepthStencil` and `DepthShaderResource` (depth attachment + sampled),
  `StorageShaderResource` (storage + sampled). `TextureDimension::Cube` → 6
  array layers and `VK_IMAGE_VIEW_TYPE_CUBE`; `Tex2DArray` →
  `VK_IMAGE_VIEW_TYPE_2D_ARRAY`. `mip_levels` on the image and the view.
- [ ] **4.2** `create_texture` with initial data: one staging buffer, one
  `vkCmdCopyBufferToImage` region **per mip level and array layer**, offsets
  walked the way `upload_texture` walks them in `device_d3d12.cpp`. This is
  where a cube map with a mip chain goes wrong quietly, so the gate reads a
  specific texel of a specific level of a specific face.
- [ ] **4.3** `create_sampler` — filter, address mode, and `compareEnable` /
  `compareOp` from `SamplerDesc::compare`, which is what
  `shadow_comparison_sampler` sets. `AddressMode::Border` needs a border
  colour: `VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE`, matching the D3D12 side —
  check `to_d3d_sampler` and match it, because a shadow map's border decides
  whether everything outside the map is lit or shadowed.
- [ ] **4.4** Immutable samplers: `pImmutableSamplers` on the set-0 layout at
  binding `48 + i`, created from `desc.samplers[i]` and owned by the pipeline.
- [ ] **4.5** `set_shader_resource` writes a `SAMPLED_IMAGE` descriptor at
  `16 + slot` into the frame's set 0.
- [ ] **4.6** Assertion: extend the parity gate to sample a 2×2 texture with
  two mip levels through a linear sampler and a cube face through another, and
  probe byte-exact. The mip values come from `build_rgba8_mip_chain`, so the
  expected bytes are the ones the colour-space gate already asserts (188 for
  sRGB, 127 for linear) — reuse those numbers rather than inventing new ones.
- [ ] **4.7** Commit: `feat(rhi-vulkan): sampled textures, mips, cube maps and samplers (RHI #24)`

## Phase 5: structured buffers

- [ ] **5.1** Set 1 in the pipeline layout when `storage_buffer_count > 0`:
  `STORAGE_BUFFER` at binding `16 + i`, `VK_SHADER_STAGE_ALL`. Two
  `VkDescriptorSetLayout`s and two `vkCmdBindDescriptorSets` targets.
- [ ] **5.2** `set_structured_buffer` writes into set 1.
- [ ] **5.3** Assertion: the parity gate draws 3 instances of the quad reading
  a per-instance tint from a `StructuredBuffer` via `SV_InstanceID`, and probes
  one texel inside each instance's region. This also re-checks the contract's
  `start_instance` omission: `firstInstance = 0` on both backends means
  `SV_InstanceID` agrees, which is the thing D3D and Vulkan disagree about.
- [ ] **5.4** Commit: `feat(rhi-vulkan): structured buffers in descriptor set 1 (RHI #24)`

## Phase 6: compute

- [ ] **6.1** `create_compute_pipeline` — same count-to-layout translation,
  `VK_SHADER_STAGE_COMPUTE_BIT`, entry point `cs_main`.
- [ ] **6.2** `set_compute_pipeline`, `dispatch`,
  `set_unordered_access(IBuffer&)` → `STORAGE_BUFFER`,
  `set_unordered_access(ITexture&)` → `STORAGE_IMAGE` at `32 + slot`.
  `set_shader_resource` already handles the compute bind point on D3D12; do the
  same here.
- [ ] **6.3** Assertion: run `storage_texture_gate.hlsl` on the Vulkan device
  and assert the **same four probe values** the D3D12 storage-texture gate
  reports (`1,256,775,1795`). Same shader, same numbers, two APIs.
- [ ] **6.4** Commit: `feat(rhi-vulkan): compute pipelines and storage resources (RHI #24)`

## Phase 7: depth targets and the shadow comparison

- [ ] **7.1** Depth attachments in `begin_render_pass` already have a code path;
  make it real — `D32_FLOAT` images with `DEPTH_STENCIL_ATTACHMENT` usage, the
  aspect-aware barrier from phase 1, and the clear value already following
  `depth_convention()`.
- [ ] **7.2** Assertion: a depth-tested parity draw — two overlapping triangles
  at different depths, asserting **the nearer one wins under the device's own
  convention**, which is reversed-Z here. The engine's `Depth convention gate`
  already measures `near=0.099 far=0.001`; the parity version asserts the
  same ordering on both backends rather than re-deriving the numbers.
- [ ] **7.3** Commit: `feat(rhi-vulkan): depth targets and reversed-Z parity (RHI #24)`

## Phase 8: MSAA resolve

- [ ] **8.1** `RenderPassInfo::resolve` →
  `VkRenderingAttachmentInfo::resolveImageView` with
  `resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT` and
  `resolveImageLayout = COLOR_ATTACHMENT_OPTIMAL`. Multisampled images need
  `sample_count` on the `VkImageCreateInfo`, which `create_texture` already
  passes.
- [ ] **8.2** Assertion: the MSAA gate's own numbers on Vulkan —
  `blend=64 lit=2016` resolved against `blend=0 lit=2016` single-sample. Same
  as D3D12, and the same falsification applies: dropping the resolve must turn
  it red.
- [ ] **8.3** Commit: `feat(rhi-vulkan): end-of-pass resolve on the subpass (RHI #24)`

## Phase 9: the frame ring

- [ ] **9.1** `alloc_frame_memory` — one host-visible `VkBuffer` per frame slot
  at `kFrameRingBytes` (1 MiB, match the D3D12 constant by name not by value),
  a bump offset reset in `begin_frame`, 256-byte alignment, and exhaustion
  returning `{}` with the same one-shot error and the same
  `FrameRingStats` bookkeeping — `peak_bytes` never reset,
  `exhausted_frames` counted.
- [ ] **9.2** Assertion: `frame_ring_stats()` on Vulkan reports a capacity equal
  to D3D12's and a peak that moves after an allocation. The engine's
  `Frame ring budget gate` is the real consumer and runs in phase 12.
- [ ] **9.3** Commit: `feat(rhi-vulkan): the per-slot frame upload ring (RHI #24)`

## Phase 10: descriptor sets across frames in flight

The one thing the offscreen slice could not have found: it submitted and waited
inside one frame, so one pool reset per `begin_frame` was sufficient by
accident.

- [ ] **10.1** One `VkDescriptorPool` per frame slot; `begin_frame` resets
  **that slot's** pool after waiting its fence. A set allocated in flight must
  not be freed while the GPU reads it — which is the same rule the D3D12
  shader-visible heap follows, so the two backends fail the same way when a
  caller binds more than a frame's worth.
- [ ] **10.2** Per-slot command buffers and fences, replacing the single
  `cmd_buffer_` and `fence_`. `kFrameCount` slots, matching D3D12.
- [ ] **10.3** Assertion: a parity section that runs three frames without
  `wait_idle` between them and asserts the third frame's readback — which
  requires the ring and the pools to have cycled correctly. This is the first
  parity assertion that is about *time* rather than about a pixel.
- [ ] **10.4** Commit: `feat(rhi-vulkan): per-slot pools, command buffers and fences (RHI #24)`

## Phase 11: presentation, and `--rhi vulkan`

- [x] **11.1** Vendor `vulkan/vulkan_win32.h` (16 KB) into
  `packages/rhi-vulkan/third_party/vulkan/` and note it in that directory's
  README, which currently says it joins "when presentation does".
- [x] **11.2** Instance extensions `VK_KHR_surface` +
  `VK_KHR_win32_surface`; device extension `VK_KHR_swapchain`. A non-null
  `window_handle` stops being refused and creates a surface.
- [x] **11.3** Swapchain: `kFrameCount` images, `VK_FORMAT_B8G8R8A8_UNORM` or
  the surface's preferred UNORM — **not** an `_SRGB` format, matching the note
  in `resources.hpp` that a presented surface stays UNORM and the encode is
  in-shader. `presentMode` from `present_interval`: 0 → `IMMEDIATE` if
  supported else `FIFO`, 1 → `FIFO`.
- [x] **11.4** Per-slot acquire and render-finished semaphores;
  `vkAcquireNextImageKHR` in `begin_frame`, `vkQueuePresentKHR` in
  `ISwapchain::present`. `swapchain_color()` returns the acquired image's
  `VulkanTexture`; `swapchain_depth()` returns the device's own depth buffer.
  `resize` recreates on `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`.
- [x] **11.5** `--rhi vulkan|d3d12` in `main.cpp` picks the factory into
  `Modules`, defaulting to D3D12. Add it to `solengine.bat`'s help and a
  `run-vk` command.
- [x] **11.6** Delete `DeviceDesc::preferred_api` and its four call sites.
- [x] **11.7** Commit: `feat(rhi-vulkan): surface, swapchain and --rhi vulkan (RHI #24)`

## Phase 12: the suite, and the record

- [ ] **12.1** `solengine run --rhi vulkan --gates` with
  `ENGINE_GPU_DEBUG=1`. Expect failures; each one is a finding. Fix, and
  **record what each was** — this list is the row's real output, because the
  gates were written against D3D12 with no second backend in mind.
- [ ] **12.2** All 82 registered gates green on Vulkan, validation layer
  silent. Then the same on D3D12, unchanged, and Release.
- [ ] **12.3** CI: a `run-vk` equivalent is not runnable on a hosted runner
  (no GPU), so nothing changes there — but say so in the workflow comment
  beside the existing `--gates` note.
- [ ] **12.4** ROADMAP Why/Choice/Gate/Do-not entry. **Do-not** lines: do not
  let the `-fvk-*-shift` flags and `kBindingBase*` drift; do not add a
  suballocator to one backend only; do not assume a semantic maps to a
  location without the declaration-order property holding.
- [ ] **12.5** RHI #24 → **Done**, subtotals and header totals recounted, LOC
  audit refreshed, spec `Status: implemented`, RHI #13's blocker re-read again
  now that a second backend has been through a whole frame.
- [ ] **12.6** Commit: `docs(roadmap): the engine renders on two backends (RHI #24)`

---

## Definition of done

- [ ] `solengine run --rhi vulkan` renders the sandbox's real frame
- [ ] `--gates` green on the Vulkan device, all 82 registered, validation silent
- [ ] `--gates` still green on D3D12 in Debug **and** Release, unchanged
- [ ] every phase's parity assertion runs on both devices every `--gates`
- [ ] no `not_implemented` reachable by the standard frame; whatever remains is
      named in the ROADMAP entry with the reason
- [ ] `preferred_api` gone, `--rhi` documented in README and `solengine help`
- [ ] 16/16 invariants under both shells; CI green
- [ ] every gate watched failing first, and phases 3, 7 and 8 falsified after
