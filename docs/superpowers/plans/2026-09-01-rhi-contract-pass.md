# RHI Contract Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** settle the three Ready RHI rows that change the *contract* — **#15
reversed-Z**, **#9 UAV textures**, **#18 MSAA** — before a second backend
exists, so `rhi-vulkan` implements a finished interface once instead of
renegotiating it three times with two implementations in the room.

**Why these three and not the other two.** All five Ready RHI rows touch the
backend, but only these three change what the *interface* says, and only these
three are concepts that are identical in both APIs — a storage image is a UAV, a
sample count is a sample count, reversed-Z is a near/far swap and a compare
flip in either. There is no design risk in settling them against one backend.

The other two are the opposite and are deliberately **not** here. **#17 GPU
crash breadcrumbs** is D3D12 DRED; Vulkan's equivalents are extensions with a
different data model, and shaping a breadcrumb interface with only DRED in view
is exactly how **A2** happened. **#16 PSO disk cache** shares a key but not a
blob — `VkPipelineCache` and `ID3D12PipelineLibrary` are different objects.
Both are better decided with two backends present.

**Not in scope, on purpose:** the binding model. A2's counts-versus-bind-groups
question cannot be answered without a second implementation, and this plan
deliberately does not pre-empt it.

---

## What was measured

Every touch point below was read out of the tree, not inferred.

### #15 — reversed-Z reaches six places

| Site | Today | Reversed-Z needs |
|------|-------|------------------|
| `packages/math/src/mat4.cpp` `Mat4::perspective` | `depth_range = near_z - far_z` | near and far swapped |
| `packages/rhi-d3d12/src/device_d3d12.cpp:572` | `ClearDepthStencilView(dsv, …, 1.0f, 0, …)` — **hard-coded** | `0.0f` |
| `packages/rhi/include/engine/rhi/resources.hpp:102` | `DepthTest { Disabled, Less, LessEqual, Equal }` | a greater-than direction |
| `resources.hpp:80` `shadow_comparison_sampler()` | `CompareOp::Less` | `CompareOp::Greater` |
| `packages/sandbox/src/sandbox_common.cpp:234` | `slope_scaled_depth_bias = 1.5f` | **negated** — the sign follows the depth direction |
| `packages/sandbox/content/shaders/forward.hlsl` | samples the shadow map | comparison direction follows the sampler |

Half-applying any of these is a silent visual bug: z-fighting, or a black
screen, with no error anywhere. That is why the switch is one value with a gate
over it rather than six independent edits.

Depth format is already `D32_FLOAT`, which is what makes reversed-Z worth doing
— the precision win comes from float depth clustering near zero.

### #9 — UAV textures, and a half-feature the graph is already carrying

`ResourceState::UnorderedAccess` already exists. `storage_texture_count` is
already on `ComputePipelineDesc`. What is missing:

- `TextureUsage` has no `UnorderedAccess` (it has RenderTarget, DepthStencil,
  ShaderResource, DepthShaderResource, ColorShaderResource)
- `ICommandList::set_unordered_access` takes an `IBuffer&` only — no texture
  overload
- the render graph's `Access` has no `UnorderedAccess`, so `state_for` cannot
  produce the state that already exists
- `resource_desc.SampleDesc.Count = 1` sites in the backend also lack
  `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`

**And this closes something already open.** `PassKind::Compute` shipped this
morning; a compute pass's declared *writes* are ordering-only, because no
`Access` maps to an unordered access. The graph is carrying half a feature right
now, and this row is its other half.

### #18 — MSAA

- `TextureDesc` has no `sample_count`; five `SampleDesc.Count = 1` sites in the
  backend
- `GraphicsPipelineDesc` has no sample count, and a PSO's sample count **must**
  match its render target's or creation fails
- `RenderPassInfo` is `{ color, depth, clear_color, clear_color_target,
  clear_depth }` — no resolve target
- `TransientDesc` is `{ name, format, usage, width, height, extent_div }` — no
  sample count, so the graph cannot request an MSAA transient
- `begin_render_pass` maps to `OMSetRenderTargets` plus clears, **not** D3D12's
  `BeginRenderPass`. So resolve is `ResolveSubresource` in `end_render_pass`
  today, and `pResolveAttachments` in Vulkan later — the contract does not have
  to know which.

---

## The two decisions, taken

**Reversed-Z is one convention on `DeviceDesc`, with a gate over all six sites.**
`DepthConvention { Standard, Reversed }` on `DeviceDesc` (which already carries
`present_interval`), queryable from `IDevice`. The renderer derives projection,
clear value and compare direction from that one value; the gate asserts all six
agree. The row says "one flag", and this is what makes it actually one. Rejected:
adding `Greater`/`GreaterEqual` to `DepthTest` and letting each pipeline choose —
smaller, but the six sites then agree only by hand and nothing checks them, which
is the failure class **A1** existed to remove. Also rejected: a `constexpr` in
the renderer, which leaves the backend unable to pick the right clear value.

**MSAA resolve is a target on `RenderPassInfo`.** The graph declares intent —
"this pass resolves into that texture" — and the backend picks the mechanism:
`ResolveSubresource` at end-of-pass now, `pResolveAttachments` natively in
Vulkan. Rejected: an explicit `resolve_texture(src, dst)` command, which forgoes
both APIs' built-in resolve and, on a tiler, forces the MSAA target out to
memory before resolving.

---

## Order, and why

**#15 → #9 → #18.**

Reversed-Z first because it is the one that changes *semantics* rather than
adding surface: it moves depth precision, shadow bias sign and compare direction
under everything else. Anything built on top of it inherits a settled depth
convention; anything built before it has to be re-verified after. It is also the
one most likely to disturb existing gates — shadow fitting, TAA reprojection —
and that is worth finding before two more features are layered on.

UAV textures second because it closes the compute half-feature already in the
tree, and because it is independent of both others.

MSAA last because it touches the most surface — `TextureDesc`,
`GraphicsPipelineDesc`, `RenderPassInfo`, `TransientDesc` and the graph — and
because MSAA depth inherits the reversed-Z clear value, so it wants #15 already
settled.

---

## Read this before Task 1

1. **A test is a gate**, and each task adds one. Write it, run it, **watch it
   fail**, then implement. `--gates` is at **79 (pass) / 0 FAIL** today.
2. **`check-invariants.ps1` passes 16/16**, and `gate-registry` (15) means every
   new gate must be declared in `gates/gates.hpp` *and* classified in `kGates`,
   or the build fails.
3. **`rhi-vocabulary` (16) will fight you.** No `D3D12`, `DXGI`, `SRV`, `UAV`,
   `CBV`, `root signature` or register syntax under `packages/rhi/include`
   outside the binding contract. Name things after what they mean — that is why
   this plan says *storage texture* and not *UAV* in every interface it touches.
4. **`docs/ROADMAP.md`'s LOC audit is an implicit dependency of every source
   change.** Check invariants *before* committing, not in the same command.
5. **Renderer never includes a graphics-API header**, and no app registers an
   engine pass. Both are machine-checked.
6. **Trunk-based.** Commit to `main`, push after each task.

### The verification block

```powershell
cmake --build --preset debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build --preset release-game
.\build\bin\Release\game.exe --gates
.\build\bin\Debug\sandbox.exe --gates-cpu
pwsh -NoProfile -File tools/check-invariants.ps1
```

`ENGINE_GPU_DEBUG=1` matters more than usual here: every task in this plan
changes resource states, depth semantics or sample counts, which is precisely
what the debug layer polices.

---

## Task 1: Reversed-Z as one convention (RHI #15)

**Cost:** the whole depth pipeline, at once · **Closes:** RHI #15

- [x] **1.1** Add `DepthConvention { Standard, Reversed }` to
  `packages/rhi/include/engine/rhi/rhi.hpp` beside `DeviceDesc`, a
  `depth_convention` field defaulting to `Standard`, and
  `IDevice::depth_convention()`. Default `Standard` so this task is inert until
  step 1.6 flips it — every step before that must leave 79 gates green.
- [x] **1.2** Add `DepthTest::Greater` and `GreaterEqual`, and `CompareOp`'s
  greater directions if absent. These are the mechanism, not the switch: the
  renderer picks which to use from the convention.
- [x] **1.3** Replace the hard-coded `1.0f` at `device_d3d12.cpp:572` with the
  device's convention — `1.0f` for Standard, `0.0f` for Reversed. This is the
  site that makes a half-applied convention a black screen.
- [x] **1.4** Add `Mat4::perspective_reversed_z` beside `Mat4::perspective` in
  `math`, keeping every existing NaN guard. Two named functions rather than a
  flag argument: `math` stays a library with no rendering policy in it, and the
  renderer is where the choice belongs.
- [x] **1.5** In `renderer`, derive from `IDevice::depth_convention()`: which
  projection builder to call, which `DepthTest` the standard frame's pipelines
  use, and the sign of `slope_scaled_depth_bias`. Change
  `shadow_comparison_sampler()` to take the convention rather than hard-coding
  `CompareOp::Less`.
- [x] **1.6** **Write the gate first and watch it fail.** `run_depth_convention_gate`
  asserts the six sites agree with `depth_convention()`: the projection maps near
  to the far-plane depth value, the clear value matches, the shadow sampler's
  compare direction matches, the bias sign matches, the pipelines' `DepthTest`
  matches, and — the one that catches a half-application — that flipping the
  convention flips *all six* and not five. Assert on the values, not on "it did
  not crash".
- [x] **1.7** Only now flip the sandbox's `DeviceDesc` to `Reversed`. Re-run
  every depth-sensitive gate: shadow, PCF, TAA, motion, frustum, HDR. Expect
  something here — this is the task most likely to surface a hidden assumption,
  and finding it is the point.

**Proof:** 80 `(pass)`, 0 `FAIL`, Debug and Release. Debug layer silent — a
mismatched depth compare or clear value is exactly what it reports. Then the
control that matters: set the convention back to `Standard`, confirm the gate
still passes and the scene still renders, and flip it forward again. A
convention that only works in one position is a constant, not a convention.

**Commit:** `feat(rhi): depth convention on the contract, reversed-Z behind it (RHI #15)`

---

## Task 2: Storage textures, and the compute half-feature (RHI #9)

**Cost:** contained — the state already exists · **Closes:** RHI #9

- [x] **2.1** Add `TextureUsage::UnorderedAccess` and set
  `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` on textures created with it.
  Note that a texture may need to be both storage and sampled — check whether
  the enum should become flags before assuming it stays exclusive.
- [x] **2.2** Add `ICommandList::set_unordered_access(u32 slot, ITexture&)`
  alongside the buffer overload, and the descriptor-heap slot behind it.
- [x] **2.3** Add `Access::UnorderedAccess` to the render graph and map it in
  `state_for` to the `ResourceState::UnorderedAccess` that already exists.
- [x] **2.4** Add a `sample_count`-free `storage` flag to `TransientDesc` so the
  graph can allocate a storage-capable transient.
- [x] **2.5** **This is what makes `PassKind::Compute`'s writes real.** Update
  `render_graph.cpp`'s compute branch and the comment that currently says a
  declared write is ordering-only, and update
  `.claude/rules/renderer-boundaries.md` step 4, which says the same.
- [x] **2.6** **Gate first.** `run_storage_texture_gate` dispatches a compute
  pass that writes a known pattern into a storage texture, then reads it back
  and asserts the exact values — not that the dispatch returned. Then assert the
  graph orders a graphics pass reading it after the compute pass writing it,
  which is the half of `PassKind::Compute` that could not be tested before.

**Proof:** 81 `(pass)`, 0 `FAIL`. Debug layer silent — a missing
`ALLOW_UNORDERED_ACCESS` or a wrong barrier is a debug-layer error, not a
crash. Readback values compared against a pattern computed on the CPU.

**Commit:** `feat(rhi): storage textures, and compute passes that really write (RHI #9)`

---

## Task 3: MSAA and resolve (RHI #18)

**Cost:** the widest surface of the three · **Closes:** RHI #18

- [x] **3.1** Add `sample_count` to `TextureDesc` (default 1) and thread it
  through all five `SampleDesc.Count = 1` sites in the backend.
- [x] **3.2** Add a matching `sample_count` to `GraphicsPipelineDesc`. A PSO
  whose sample count disagrees with its render target fails to create — so make
  the mismatch a logged error naming both numbers, not a null return.
- [x] **3.3** Add `ITexture* resolve` to `RenderPassInfo`, resolved in
  `end_render_pass` with `ResolveSubresource`. Reject at the contract level a
  resolve whose format or extent disagrees with the colour target, naming both.
- [x] **3.4** Add `sample_count` to `TransientDesc` and let the graph allocate
  MSAA transients. A pass writing an MSAA target and declaring a resolve target
  reads as two resources on the graph, so ordering and missing-producer
  detection cover it for free.
- [x] **3.5** **Gate first.** `run_msaa_gate` asserts: a 4× target reports its
  sample count; a PSO built for 1× against a 4× target is rejected with both
  numbers named; a resolve produces a single-sample texture of the right extent;
  and a geometric edge is measurably smoother after resolve than a 1× render of
  the same triangle — a real measurement, not "it drew".
- [x] **3.6** Do **not** switch the standard frame to MSAA. This row puts it on
  the contract; using it is a renderer decision with a real cost, and TAA
  already handles edges. Say so in the ROADMAP entry.

**Proof:** 82 `(pass)`, 0 `FAIL`, Debug and Release. Debug layer silent. The
smoothness assertion compares against a 1× render of the same geometry, so it
cannot pass by rendering nothing.

**Commit:** `feat(rhi): MSAA render targets and end-of-pass resolve (RHI #18)`

---

## Task 4: Record it, and correct the Vulkan row

**Cost:** small · **Closes:** nothing, fixes one wrong blocker

- [x] **4.1** A `docs/ROADMAP.md` Why / Choice / Gate / Do-not entry for all
  three. **Do-not** lines: do not add a second way to select depth direction; do
  not switch the standard frame to MSAA without a reason TAA cannot cover; do
  not let `TextureUsage` grow into a flags enum without checking every switch
  over it.
- [x] **4.2** **Correct RHI #12's *Finish first*.** It reads "Shaders #5 SPIR-V
  path, then Platform #9 a non-Windows platform package". Platform #9 is not a
  technical prerequisite — Vulkan runs on Windows, and doing it there first
  validates the contract with one new variable instead of two. Rewrite it to
  name Shaders #5 only, and say why Platform #9 was dropped.
- [x] **4.3** Record what this pass deliberately left for the second backend:
  #17 breadcrumbs, #16 PSO cache, and the A2 binding model.

**Commit:** `docs(roadmap): the RHI contract pass, and RHI #12's real blocker`

---

## Definition of done

- [x] RHI #9, #15 and #18 are **Done**, with the category subtotal and header
      totals recounted
- [x] **82 `(pass)`, 0 `FAIL`** in Debug and Release; debug layer 0/0/0 on every
      task, not just the last
- [x] reversed-Z is one value: flipping `DeviceDesc::depth_convention` flips all
      six sites, and the gate fails if any one of them is left behind
- [x] a compute pass writes a storage texture and a graphics pass reads it, in
      that order, proven by readback values rather than by absence of a crash
- [x] a PSO/target sample-count mismatch is rejected with both numbers named
- [x] each new gate was watched failing before it was trusted
- [x] `all 16 checks passed` under both shells; CI green including the Linux and
      sanitizer jobs *(run 33531065286: all jobs green)*
- [x] RHI #12's blockers say what is actually true

## What this plan does not do

No Vulkan. No SPIR-V. No bind groups — A2 stays open by design, because the
second backend is what validates it. No DRED breadcrumbs and no PSO cache, for
the reason at the top: both are shaped by the backend that implements them, and
deciding them now with one backend in view is the mistake this whole pass exists
to avoid repeating.
