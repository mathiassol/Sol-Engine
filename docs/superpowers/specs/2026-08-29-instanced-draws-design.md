# Instanced draws (Renderer #27 / RHI #14)

Date: 29 Aug 2026
Status: implemented

## Sources

- D3D12 `DrawIndexedInstanced`, `SV_InstanceID`, root SRVs
  (`D3D12_ROOT_PARAMETER_TYPE_SRV`), HLSL `StructuredBuffer` packing rules.
- The three backends' base-instance semantics, because they disagree:
  D3D excludes `StartInstanceLocation` from `SV_InstanceID`; Vulkan folds
  `firstInstance` into `gl_InstanceIndex`; Metal splits it out as a separate
  `[[base_instance]]` input.
- Scene #1's own Do-not: "the next ceiling is the 1 MiB frame constant ring at
  ~816 drawn instances, and its failure mode is silently dropped draws."

## Not this

- GPU-driven rendering: no `ExecuteIndirect`, no GPU culling, no persistent
  instance buffer.
- Sorting draws — front-to-back, by state, or otherwise.
- Per-batch culling. The cull stays per instance and runs *before* batching.
- Bindless / descriptor-indexed materials (RHI #8, #11 remain Later/Far).

## Decision

**Batch in extract, after the cull.** Membership depends on what survived, so
it cannot be cached across frames. Key = pipeline + vertex buffer + index
buffer + albedo + metallic-roughness + normal map + index count + vertex
stride. Grouping is a linear scan by key equality, not run-length (the demo
alternates albedo between neighbours, so run-length gave 33 batches for 33
draws) and not a sort (the key is pointers; ordering them would make batch
composition depend on allocator addresses and make any batch-count gate flaky).
Batch order is first-appearance order = scene order = stable.

**One batch list for all three geometry passes.** Per-pass batching would let
shadow, forward and motion group differently, and motion draws with
`DepthTest::Equal`: geometry that does not rasterize identically to forward
writes nothing and reports nothing.

**Per-instance data in a root SRV `StructuredBuffer`.** `InstanceData` is
`{model, prev_model, material_params}` = 144 bytes, tightly packed. A cbuffer
array would pad to 16-byte registers and cap at 64 KiB; a descriptor table
would cost a descriptor per bind plus an indirection. The root SRV is a raw GPU
VA in the root signature, bound once per pass, at `space1` so it never collides
with the material SRVs in `space0`. `ShaderVisibility` is per root parameter,
so `ALL` on a root descriptor is legal — the pixel-only restriction people
remember applies to descriptor *tables*.

**Base instance travels in the pass constants**, never as
`StartInstanceLocation`, because of the three-way disagreement above. Shaders
call `sol_instance(id, instance_base)` from `instancing.hlsli` and mean the
same thing on every backend the RHI grows.

**The instance array uploads once per frame**, in `RenderGraph::execute`, and
rides on `PassContext::instances`. The ring hands out frame-lifetime memory, so
three passes reading identical bytes pay once; per-pass upload cost 3×144 bytes
per instance and would have given back most of the ceiling this buys.

## Gate

`run_instancing_gate` — builds a scene that *must* split (same mesh, two
albedos), then asserts:

- the split happened and batch sizes are what the scene implies (`3/2/2`),
- every drawn instance appears in exactly one batch's slice (coverage),
- `first_instance + SV_InstanceID` addresses the row the CPU wrote
  (shader mapping).

`Instancing gate: drawn=7 batches=3 sizes=3/2/2 split_on_texture=yes
coverage=yes shader_mapping=yes (pass)`.

`run_frustum_gate` gained `batches=`; `run_instance_capacity_gate` is unchanged
in what it asserts. Ran with `ENGINE_GPU_DEBUG=1`: 0 messages, 0 errors, 0
warnings — a number that is only meaningful because `~D3D12Device` now counts
the info queue unconditionally instead of only after some other call failed.

## Out of scope

Indirect draws, per-batch culling, cached batches, sorting, and the
O(n × batches) scan in extract — invisible at 512 instances and a handful of
keys, wants a hash the moment either grows an order of magnitude.
