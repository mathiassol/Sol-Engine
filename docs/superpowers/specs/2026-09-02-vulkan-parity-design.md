# Vulkan parity — the engine renders on the second backend

Date: 2 Sep 2026
Status: implemented

ENGINE_MAP **RHI #24**, created Ready by RHI #12 earlier today. #12 proved the
`rhi` contract survives a second API for the surface one triangle needs; this
row takes it all the way through a frame and switches the sandbox over.

Ends at: `solengine run --rhi vulkan` renders the real frame, and
`--gates` passes on the Vulkan device.

---

## What the research settled

Five measurements, again before designing. Four of them retired risks; one
found a defect in code already committed.

**1. Vertex locations follow the HLSL struct's declaration order.** This was the
highest-risk unknown: the `rhi` contract describes attributes by *semantic*
(`VertexSemantic::Position`) and Vulkan wants a `Location` number. Dumped from
`forward.hlsl`'s `vs_main`:

```
OpDecorate %in_var_POSITION Location 0
OpDecorate %in_var_NORMAL   Location 1
OpDecorate %in_var_TEXCOORD Location 2
```

Declaration order, and the engine already declares
`attributes[0..2] = {Position, Normal, TexCoord}` in that same order. So
**`location` is the attribute's array index** — no semantic-to-location table,
and no `[[vk::location]]` in the shaders. That coincidence is load-bearing, so
it gets asserted rather than assumed (below).

**2. The whole descriptor layout falls out of the register shifts.** Measured
from `forward.hlsl`'s `ps_main` and `vs_main`:

| HLSL | SPIR-V | Contract count |
|------|--------|----------------|
| `b0` FrameConstants | set 0, binding 0 | `uniform_buffer_count` |
| `t0`–`t6` albedo … normal map | set 0, bindings 16–22 | `sampled_texture_count` |
| `s0`–`s2` albedo, shadow, IBL | set 0, bindings 48–50 | `sampler_count` |
| `t0, space1` `sol_instances` | **set 1**, binding 16 | `storage_buffer_count` |

The `-fvk-t-shift 16 / -fvk-u-shift 32 / -fvk-s-shift 48` scheme built for
Shaders #5 holds unchanged, and space 1 becoming descriptor set 1 is exactly
what `resources.hpp`'s binding contract predicted for storage buffers. So
parity needs **two** descriptor sets, not one.

**3. Samplers are separate descriptors, not combined.** HLSL splits
`Texture2D` from `SamplerState`, so SPIR-V gets `SAMPLED_IMAGE` at 16+ and
`SAMPLER` at 48+ rather than `COMBINED_IMAGE_SAMPLER`. That is the branch the
contract's table left open, and it is the one that makes the contract's
*immutable sampler* rule implementable: `pImmutableSamplers` on the set layout,
baked from `SamplerDesc`, never bound per draw.

**4. Mip chains are already CPU-built.** `build_rgba8_mip_chain` produces every
level and `TextureDesc::mip_levels` uploads them, so there is no
`vkCmdBlitImage` chain to write — just N staged copies. One less subsystem than
expected.

**5. A defect in RHI #12's committed code.** `to_vulkan_barrier` maps
`ResourceState::ShaderRead` to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` with
no regard for the image's aspect. Correct for colour, and the offscreen slice
only ever had colour — but the shadow map is a sampled **depth** image, and the
barrier also has to pick `VK_IMAGE_ASPECT_DEPTH_BIT`, which `transition()`
currently derives separately from the format. The two derivations can disagree.
Recorded here because it is the first thing parity trips over and it was
invisible with one colour texture.

## Allocation: per-resource, matching D3D12 — not VMA

VMA is bundled with the SDK and the obvious reflex at this resource count. It is
the wrong call *here*, for a reason worth writing down:
`rhi-d3d12` uses `CreateCommittedResource` per resource, which is the same
one-allocation-per-resource strategy. Putting a suballocator behind one backend
and not the other makes them structurally different, so any later memory or
timing comparison between them measures the allocator rather than the API.

VMA belongs to a row that gives **both** backends a heap — placed resources on
D3D12, VMA on Vulkan — and that is a separate decision. Until then the Vulkan
side stays deliberately naive and matched.

## Backend selection

`--rhi vulkan` picks which `create_rhi()` goes into `engine::Modules`, which is
how the engine already resolves a backend — `Engine::init` just calls
`modules_.rhi->create_device(...)`.

`DeviceDesc::preferred_api` is **deleted**. It reads like a request and selects
nothing; the installed factory has always decided. A field that looks load-
bearing and is ignored is worse than no field — the same judgement that made
`window_handle == nullptr` mean something rather than stay undefined. Removing
it touches four call sites.

`GraphicsAPI` stays: `IRHI::api()` is how a caller *asks* what it got, which is
real.

## How this is gated

The pass is large, so it is gated by growth rather than at the end. RHI #12
left `run_backend_parity_gate` taking an `IDevice&` and a `ShaderTarget`, so it
already runs against both backends every `--gates`. Each subsystem below
extends that gate — or adds a sibling with the same shape — and **lands green
on the D3D12 build before the sandbox ever switches over**. A subsystem is not
done because it compiles; it is done when both backends produce the same
number.

The final assertion is different in kind: `--gates` on the Vulkan device, all
82 registered gates, with the validation layer silent. That is the one that
cannot be faked, because the gates were written against D3D12 without a second
backend in mind.

## Subsystems, in dependency order

Each is a phase in the plan, each ends with a parity assertion.

1. **Barriers, corrected.** `to_vulkan_barrier` gains the aspect, so depth and
   colour cannot disagree. Prerequisite for anything with a depth texture.
2. **Staged uploads and device-local buffers** — `create_buffer` for Vertex,
   Index and Storage with an initial-data staging copy. Unblocks 3 and 5.
3. **Vertex input and indexed draws** — `attribute_count`, `set_vertex_buffer`,
   `set_index_buffer`, `draw_indexed`. Parity: an indexed quad, same coverage
   both backends. Asserts `location == array index` by drawing geometry whose
   normal and uv actually matter.
4. **Sampled textures, mips, cube maps, arrays, samplers** — the big one.
   `create_texture` for every `TextureUsage`, `create_sampler` including the
   comparison sampler, immutable samplers on the set layout. Parity: sample a
   two-level mip chain and a cube face, byte-exact.
5. **Structured buffers** — descriptor set 1. Parity: per-instance data read
   through `SV_InstanceID`, which also re-checks the `start_instance` omission
   the contract made for exactly this reason.
6. **Compute** — `create_compute_pipeline`, `dispatch`, storage images and
   storage buffers. Parity: the existing `storage_texture_gate.hlsl` probes,
   same values both backends.
7. **Depth targets** — depth attachments, sampled depth, the shadow-comparison
   sampler under reversed-Z. Parity: a depth-tested draw where the nearer
   fragment wins, under the device's own convention.
8. **MSAA resolve** — `RenderPassInfo::resolve` as
   `VkRenderingAttachmentInfo::resolveImageView`. Parity: the numbers the
   existing MSAA gate already measures.
9. **The frame ring** — `alloc_frame_memory`, a host-visible buffer per frame
   slot with a bump pointer, exhaustion returning an empty slice exactly as
   D3D12 does. Parity: the frame-ring budget gate's headroom figure.
10. **Descriptor sets under frames in flight** — one pool per frame slot, reset
    on that slot's `begin_frame`. The offscreen slice submitted and waited, so
    this is genuinely new and cannot be proven offscreen.
11. **Presentation** — `VK_KHR_win32_surface`, swapchain, acquire and
    render-finished semaphores, per-slot fences, resize. `--rhi vulkan`.
12. **The suite** — `--gates` on Vulkan, validation silent, and the same
    ROADMAP/ENGINE_MAP closeout.

## Out of scope, deliberately

- **VMA and a heap allocator**, for the reason above — it is a both-backends
  row.
- **Timestamps.** `last_gpu_time_ms()` stays 0 on Vulkan. RHI #10 already says
  the blocker is a consumer, and the F3 overlay reading 0 on one backend is
  honest rather than wrong.
- **`gpu_memory_stats` usage.** Needs `VK_EXT_memory_budget`; budget alone is
  reported and usage stays 0 rather than estimated.
- **Bind groups (A2).** Two descriptor sets synthesised from five counts is now
  measured to work. The evidence for A2 grows; the decision is still not this
  row's.
- **`VK_EXT_debug_utils` object names.** `set_debug_name` stays a no-op on
  Vulkan; a missing name changes no behaviour.

## Risks

| Risk | Standing |
|------|----------|
| Semantic-to-location mapping needs a table or shader edits | **Retired.** Declaration order, and the engine already matches it. |
| The register shifts do not survive real shaders | **Retired.** Seven textures, three samplers and a space-1 buffer all land where predicted. |
| Mip generation needs a GPU blit chain | **Retired.** Already CPU-built. |
| Immutable samplers are not expressible | **Retired.** Separate `SAMPLER` descriptors take `pImmutableSamplers`. |
| `to_vulkan_barrier` is aspect-blind | **Open, and phase 1.** A defect in committed code, found by planning this row. |
| Descriptor-set lifetime across frames in flight | Open. Phase 10, and the one thing the offscreen slice could not have found. |
| The 82 gates assume D3D12 behaviour somewhere | Open by nature. This is the risk the row exists to convert into facts. |
