# Forward transparency — alpha blending on the forward path

Date: 2 Sep 2026
Status: spec

ENGINE_MAP **Renderer #16**, "Transparency / alpha (forward; keep out of
deferred)". Ready with no blockers.

Ends at: a material with `opacity < 1` draws through a blended forward
pipeline into `scene_color`, over the opaque geometry and the sky, and a gate
asserts the blend arithmetic against a CPU-computed reference.

---

## Scope, and what this row is not

The map splits transparency across three rows, and this one is the first:

| Row | Owns | State |
|-----|------|-------|
| **Renderer #16** | the alpha *blend* path — pipeline, material field, shader, pass, gate | this spec |
| Renderer #33 | alpha-**tested** cutout (`discard`), for foliage and fences | Later, on #16 |
| Renderer #34 | draw **sorting** — opaque front-to-back, transparent back-to-front | Later, on #16 |

So this row ships **unsorted**. That is not an oversight and not a shortcut
taken for time: Renderer #34's own note in the map reads *"sorting only starts
to matter once anything blends"*, which is the decision that transparency
arrives before its sort. Confirmed with the user before this spec was written.

What that costs, stated plainly so nobody has to discover it: transparent
surfaces are drawn in scene order, so **two transparent surfaces that overlap
each other blend in the wrong order**. Transparency against *opaque* geometry
and against the sky is correct, because depth testing still runs. A single
glass pane, a window on a building, a world-space UI panel — all correct. Two
panes of glass one behind the other — wrong, until #34.

Also out of scope, deliberately:

- **No `rhi` contract change.** See below; the contract already carries this.
- **No new blend mode.** `BlendMode` stays `{Opaque, Alpha}`. Additive and
  premultiplied belong to whichever row first has a consumer for them; adding
  an enumerator nothing uses would cost two backend implementations and a
  parity gate for nothing.
- **No deferred path.** The row title says so. There is no deferred renderer.
- **No depth prepass.** There isn't one today, and transparency does not need
  one.

## The contract already carries this

`packages/rhi/include/engine/rhi/resources.hpp` has had blending since before
this row existed:

```cpp
enum class BlendMode : u8 { Opaque, Alpha };
```

and `GraphicsPipelineDesc::blend` selects it. Both backends translate it, and
they were written to match — D3D12 sets `SRC_ALPHA` / `INV_SRC_ALPHA` /
`OP_ADD` with `SrcBlendAlpha = ONE`, and Vulkan sets exactly those factors.
There is one live consumer today: the stats overlay
(`packages/debug-draw/src/stats_overlay.cpp`), which is a complete working
precedent — `blend = Alpha`, an alpha in `SV_TARGET.a`, and a pass that does
not clear its colour target.

**So this row writes no `rhi` code, and adds no second way to composite.**
That matters more than the saved effort. The engine has exactly two
compositing mechanisms and they are cleanly separated by purpose:

1. **Hardware `BlendMode::Alpha`** — for drawing *geometry* into a target
   whose existing contents cannot be sampled, because they are the target.
   The overlay uses it. Forward transparency is the same problem.
2. **In-shader compositing with the source bound as an SRV** — for every
   *fullscreen* pass. Bloom upsample adds two SRVs; tonemap composites scene
   and bloom; TAA lerps against a ping-ponged history; SMAA blends weights.
   All of them are `BlendMode::Opaque` and fully overwrite their target.

Mechanism 1 is the one that applies. A third mechanism is the failure this
design exists to avoid.

## The eight touch points

`.claude/rules/renderer-boundaries.md` says a working pass spans the shader,
the pipeline, two lines of plumbing, the registration, the recorder, and its
gate. This row is that list plus the material field it carries.

### 1. `scene::Material` gains an opacity

`packages/scene/include/engine/scene/world.hpp`:

```cpp
struct Material {
    u32 albedo = 0;
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
    // 1 means opaque and takes the opaque pipeline. Anything less takes the
    // blended one. A float rather than a bool because the value is what the
    // shader multiplies by, so a separate flag could disagree with it.
    f32 opacity = 1.f;
};
```

Defaulting to `1.f` is what keeps every existing material and every existing
gate opaque and unchanged. `World` holds `Material materials[kMaxMaterials]`
by value, so this grows `World` by `4 * kMaxMaterials` bytes — accepted, and
the alternative (a parallel array, or a bitset of transparent materials) is
two things that can disagree.

**Why not a `BlendMode` enum on the material?** Because the pipeline choice is
derivable from the number the shader already needs, and two fields that must
agree is the pattern this codebase repeatedly records as the failure mode —
`DepthConvention` exists as one value for exactly that reason.

### 2. The scene → renderer bridge picks the pipeline

`packages/sandbox/src/world_extract.cpp` is where `scene` is copied into
`ExtractInstance` (the renderer never includes `scene`). Today it reads:

```cpp
item.pipeline = assets.pipelines.forward;
```

That single line becomes the choice:

```cpp
item.opacity = material.opacity;
item.pipeline = material.opacity < 1.f
    ? assets.pipelines.forward_transparent
    : assets.pipelines.forward;
```

If `forward_transparent` is null — a build where the pipeline failed to
create — this falls back to nothing and the instance disappears. It must
instead fall back to the opaque pipeline, so a missing pipeline is a visibly
wrong material rather than a missing object. `run_pipeline_set_gate` already
makes a null pipeline unreachable, but the fallback costs one `?:` and removes
a class of silent disappearance.

### 3. Opacity reaches the shader through free space

`InstanceData` is 144 bytes and asserted so. Its `material_params` is a
`float4` carrying `{metallic, roughness, 0, 0}` — **`.z` and `.w` are unused
padding**, and the HLSL mirror in `instancing.hlsli` documents them as spare.
Opacity goes in `.z`:

```cpp
instances[dst].material_params = {draw.metallic, draw.roughness, draw.opacity, 0.f};
```

This costs zero bytes and zero extra uploads.

**Why not `FrameConstants`?** Two reasons. It is per *batch*, not per
material, so it is the wrong home. And `sizeof(FrameConstants) == 336` is
asserted in **three separate gates** — `run_material_gate`, `run_taa_gate`
and `run_motion_gate` — so growing it is a three-gate edit for a value that
belongs per instance anyway. `renderer-boundaries.md` points at
`material_params` for precisely this case.

### 4. Two adjacent lines of plumbing

`packages/renderer/include/engine/renderer/frame_pipelines.hpp` gains a
`forward_transparent` field and its `kFramePipelines` entry. The
`static_assert` there fails a field with no table entry, and
`run_pipeline_set_gate` names the pipeline if `main.cpp` never creates it.
Nothing else: the struct travels by value through `WorldExtractAssets` →
`ExtractDesc` → `RenderSnapshot`, so there is no copy block to forget.

### 5. The pipeline: forward, with two fields changed

`make_forward_transparent_pipeline_desc()` in
`packages/sandbox/src/sandbox_common.cpp` is
`make_forward_pipeline_desc()` with:

```cpp
desc.blend = rhi::BlendMode::Alpha;
desc.depth_write = false;
```

Everything else identical — same shaders, same seven textures, same three
samplers, same `depth_closer(convention)`, same `RGBA16_FLOAT` target.

It is written as a function that *calls* the opaque maker and overrides those
two fields, not as a copy of it. Eleven `make_*_pipeline_desc` functions
already exist and a twelfth hand-copied one is a place for the two to drift —
and the thing most likely to drift is the depth convention, which is the
engine's single most load-bearing value.

**`depth_write = false` is not a preference, it is required.** The motion
vector pass draws with `DepthTest::Equal` against the depth buffer forward
produced, and `extract.cpp` documents the consequence: geometry that does not
rasterize identically to forward *silently writes nothing*. A transparent
surface writing depth would erase the motion vectors of everything behind it,
which shows up as TAA ghosting on objects that are not transparent — a
failure whose cause is nowhere near its symptom. `run_motion_gate` asserts the
`DepthTest::Equal` coupling; this spec adds the `depth_write` half.

Depth **testing** stays on. That is what makes transparency correct against
opaque geometry without any sorting.

### 6. The pass, registered after `sky`

`packages/renderer/src/standard_frame.cpp`, modelled on the `sky`
registration, which is already the engine's "append into `scene_color`
without clearing" template:

```cpp
RenderPassDesc transparent{};
transparent.name = "transparent";
transparent.writes[0] = {scene_color, Access::ColorWrite};
transparent.writes[1] = {depth, Access::DepthWrite};
transparent.write_count = 2;
transparent.reads[0] = {shadow, Access::ShaderRead};
transparent.read_count = 1;
transparent.clear_color_target = false;
transparent.clear_depth = false;
transparent.should_execute = [](const RenderSnapshot& s) {
    return s.pipelines.forward_transparent != nullptr;
};
transparent.execute = record_transparent_draws;
graph.add_pass(std::move(transparent));
```

`Access::DepthWrite` is declared even though the pipeline does not write
depth: the graph's access is about the *attachment binding*, and the pass must
bind the depth target to test against it. The pipeline's `depth_write = false`
is what actually stops the write.

**Position, and why:** after `sky`, before `bloom_down0`.

- After `forward`, obviously — it blends against opaque geometry.
- After `motion_vectors`, because that pass depends on forward's depth being
  exactly what forward rasterized. Transparent geometry contributes no motion
  vectors, which is the correct behaviour for this row and a known limitation
  of a single-layer motion buffer.
- After `sky`, because the sky fills every pixel forward did not, and
  transparency must blend against it. Sky before transparency is the only
  order that gives a glass pane the sky behind it.
- Before `bloom_down0`, the first reader of `scene_color`, so a bright
  transparent surface glows like everything else. This is a behavioural
  change bloom's gate does not measure, and it is the intended one.

Two gates assert pass *order* by name, `run_taa_gate` and
`run_motion_gate`, but all their comparisons are relative (`motion_i >
forward_i`), so inserting a pass between named ones satisfies them. Verify,
do not assume.

### 7. The shader: three lines

`packages/sandbox/content/shaders/forward.hlsl`. It currently samples albedo
as `.rgb`, discarding the texture's alpha, and returns a literal
`float4(lit, 1.0)`.

```hlsl
// vs_main: carry opacity alongside metallic and roughness
output.material = inst.material.xyz;      // was .xy

// ps_main: keep the sampled alpha
float4 albedo_sample = albedo_map.Sample(albedo_sampler, input.uv);

// ps_main: the alpha the blend consumes
return float4(lit, albedo_sample.a * input.material.z);
```

`PSInput::material` widens from `float2` to `float3`, staying
`nointerpolation` — it is constant across the instance.

**Texture alpha multiplies material opacity.** Both, not either: the texture
gives per-texel shape (a decal, a window frame) and the material gives a
uniform dimmer. Using only one of them means the other has to be faked
somewhere else. This is *blending* with texture alpha, which is distinct from
Renderer #33's alpha-**test** (`discard` on a threshold) — that row still has
work to do.

One consequence worth stating: the opaque pipeline runs the same shader, and
now returns `albedo_sample.a * 1.0` in its alpha channel instead of a literal
`1.0`. With `BlendMode::Opaque` the alpha channel is written but never read as
a blend factor, so opaque output is unchanged — but `scene_color` is
`RGBA16_FLOAT` and the alpha it stores changes for any opaque material with a
non-opaque albedo texture. Nothing samples `scene_color.a`: bloom, TAA and
tonemap all take `.rgb`. Checked, and asserted by the gate rather than left as
a claim.

### 8. The recorder

`record_transparent_draws`, declared in `render_snapshot.hpp` and defined in
`render_graph.cpp`. It is `record_opaque_draws` with a different batch filter:
iterate `snapshot.batches` and record those whose `pipeline` is the
transparent one.

Batching needs **no change at all**. `pipeline` is already part of
`same_key` in `extract.cpp`, so opaque and transparent instances split into
separate batches for free, and `first_instance` / `instance_count` stay
contiguous and exact. `run_instancing_gate`'s hard-coded 3 batches of 3/2/2
is untouched because its world has no transparent materials.

## The gate

`run_transparency_gate` in `packages/sandbox/src/gates/gates_renderer.cpp`,
declared in `gates.hpp`, classified `Gpu` in `kGates`, called from the
sequence in `main.cpp`. Written before the implementation and watched failing.

It asserts on **values derived independently**, not on the absence of a crash:

1. **The blend arithmetic, through a real readback.** Draw a full-target
   quad of a known colour into an offscreen `RGBA8_UNORM` target, then a
   half-target quad of a second known colour and a known alpha through a
   `BlendMode::Alpha` pipeline, and read the target back. Compare the
   overlapped texels against `src*a + dst*(1-a)` computed on the CPU, and the
   non-overlapped ones against `dst` unchanged. This is the assertion the row
   exists for, and the one that would catch a backend whose blend factors
   disagree.

   It uses its **own** shader and pipeline, `transparency_gate.hlsl`, not
   `forward.hlsl` and not `forward_transparent`. Two reasons, and they are the
   same reasons the existing parity gates each own a shader: the forward
   pipeline wants seven textures, three samplers, a structured instance buffer
   and an IBL set, none of which says anything about blending; and it targets
   `RGBA16_FLOAT`, where a readback comparison has to reason about float
   rounding instead of about the blend. A `RGBA8_UNORM` target makes the
   expected value exact integers, which is what makes the failure message
   worth reading. The `forward_transparent` *desc* is asserted separately, in
   item 3 — the gate checks the arithmetic and the state independently rather
   than entangling them.
2. **Both backends agree.** The gate takes `(IDevice&, IShaderCompiler&,
   path, ShaderTarget, api)` — the shape RHI #24 established — so the parity
   block runs it on D3D12 and Vulkan and the numbers sit next to each other.
   Both backends already implement `BlendMode::Alpha`; this is the first thing
   that checks they do it the same way.
3. **Depth write is off, and the convention is honoured.**
   `make_forward_transparent_pipeline_desc(...).depth_write == false` and
   `.depth == depth_closer(convention)`. The convention half also goes into
   `run_depth_convention_gate`'s existing list, so the new maker becomes the
   **seventh site** that gate covers rather than the first one that ignores
   it. That gate exists because five of six sites applied is z-fighting with
   no error anywhere.
4. **Opacity propagates.** Mutate a `Material::opacity` and assert it reaches
   `DrawItem::opacity` and `InstanceData::material_params.z`, mirroring
   `run_material_gate`'s existing harness for `roughness`.
5. **The pipeline choice follows the material.** An instance with
   `opacity < 1` extracts with the transparent pipeline and one with
   `opacity == 1` with the opaque one, and the two land in different batches.
6. **Pass order.** `transparent` sits after `sky` and before `bloom_down0`,
   asserted by resolving pass indices by name the way `run_motion_gate` does.

The message carries the real measurements on both pass and failure — the
sampled texels and the CPU reference, not `blended=yes`.

## Gates this could break, and the plan for each

From the survey of all 18 renderer gates:

| Gate | Risk | Plan |
|------|------|------|
| `run_pipeline_set_gate` | Goes red the moment `forward_transparent` joins `kFramePipelines` and before `main.cpp` creates it | By design. That is the "did you forget step 2" net; it turns green when the pipeline exists. |
| `run_depth_convention_gate` | A seventh convention site it does not know about | Extend its list, in the same commit. |
| `run_material_gate` | Owns "material data reaches the draw"; asserts `sizeof(FrameConstants) == 336` | `FrameConstants` is untouched. Adding `opacity` to `Material` is exactly what this gate's harness was built for — extend it rather than work around it. |
| `run_instancing_gate` | Hard-codes 3 batches of 3/2/2 | Its world has no transparent materials, so untouched. Verify, do not assume. |
| `run_motion_gate` | Asserts pass order and the `DepthTest::Equal` coupling | Relative comparisons survive an insertion. The `depth_write = false` requirement comes *from* this gate's coupling. |
| `run_taa_gate` | Asserts pass order and `sizeof(FrameConstants)` | Relative comparisons; struct untouched. |
| `run_bloom_gate` | Transparent surfaces now contribute to bloom | Intended, and not measured by that gate. Recorded here rather than discovered later. |
| `run_frustum_gate` | Content-dependent floors (`visible >= 5`, `skipped >= 16`) | Only breaks if the demo's instance visibility changes. The demo gains transparent materials, not fewer instances. |
| `run_hdr_gate` | Owns `scene_color`'s format and role | Format unchanged. |

## The sandbox demo

Something has to be transparent to look at. The demo world has 512 instances
across a small set of materials; one material becomes `opacity = 0.45f`. That
is the smallest change that exercises the path end to end in a real frame
rather than only in the gate, and it puts transparent surfaces in front of
and behind opaque ones so the depth test is visibly doing its job.

It also demonstrates the documented limitation honestly: where two of those
instances overlap, the blend order is scene order. Better seen in the sandbox
than discovered in a game.

## Verification

- `solengine gates-gpu` — D3D12, debug layer must report 0/0/0.
- `solengine gates-vk` — Vulkan, validation layer must be silent. Both, not
  one: the boundaries rule says a contract change costs two gate runs, and
  although this row changes no contract, it does add a pipeline state neither
  backend had a geometry consumer for.
- `solengine gates-release` — the Release `game.exe`.
- `pwsh -NoProfile -File tools/check-invariants.ps1` — 17 checks, including
  `shader-target` on the new pipeline's compile site.
- `solengine run-gpu` and `solengine run-vk` — look at it. A gate can assert
  the arithmetic; only a frame shows whether it looks like glass.

## Do-not, for the ROADMAP entry

- Do not sort here. Renderer #34 owns ordering, and a sort added in passing
  would make batch composition camera-dependent and `run_instancing_gate`
  view-dependent with it.
- Do not let the transparent pipeline write depth. The motion pass draws with
  `DepthTest::Equal`; a depth write from a transparent surface silently
  removes the motion vectors of everything behind it.
- Do not add a `BlendMode` enumerator without a consumer. Each one costs two
  backend implementations and a parity gate.
- Do not grow `FrameConstants`. Three gates assert its size, and per-material
  data belongs in `InstanceData::material_params`, which has free space.
- Do not hand-copy `make_forward_pipeline_desc`. Call it and override, or the
  depth convention drifts between the two.
- Do not add a second way to composite. Hardware `BlendMode::Alpha` for
  geometry, in-shader SRV compositing for fullscreen passes. Those two, and
  no third.
