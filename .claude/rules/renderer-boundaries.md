---
paths:
  - "packages/renderer/**"
  - "packages/rhi/**"
  - "packages/rhi-d3d12/**"
  - "packages/engine/**"
  - "packages/shaders/**"
  - "packages/shaders-dxc/**"
  - "packages/debug-draw/**"
  # The sandbox is where a pass is most likely to be added by mistake, and it
  # owns the shaders and pipelines every pass needs — so these rules must load
  # here too.
  - "packages/sandbox/**"
---

# Renderer / RHI boundaries

From [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) and
[docs/STABILITY_NORTH_STAR.md](../../docs/STABILITY_NORTH_STAR.md) — the
swap test this engine is built to pass.

- `renderer` never includes a graphics-API header (`d3d12.h` or equivalent).
  It only sees `rhi`'s interfaces. If a renderer file needs a D3D12 type,
  that's a sign the abstraction belongs in `rhi` instead.
- A new engine pass is `add_pass` inside
  `packages/renderer/src/standard_frame.cpp` (`setup_standard_frame`) — never
  registered from `sandbox/src/main.cpp`.
- Frustum culling and sun-bounds logic live in `renderer::extract_visible`.
  `renderer` never includes `scene` — the sandbox copies `scene::World` into
  `ExtractInstance` and hands the renderer only that snapshot.
- Every pass declares its reads/writes on the graph explicitly. Avoid pass
  side effects that aren't expressed as a graph dependency.
- One production GPU backend (`rhi-d3d12`) until a second is justified as its
  own package (e.g. `rhi-vulkan`) — grow the `rhi` interface now, implement a
  second backend only when actually needed.

## Adding a render pass — the whole checklist

`add_pass` is where the pass is *registered*, but a working pass spans two
packages and five files: the shader, the pipeline, two adjacent lines in
`frame_pipelines.hpp`, the registration, and the recorder — plus its gate.
Missing a step is no longer silent. A field with no `kFramePipelines` entry
fails the `static_assert`; a pipeline that is never created turns
`Pipeline set gate` red and names itself. A `should_execute` predicate still
treats a null pipeline as "feature disabled", but the gate makes that state
unreachable, and `RenderGraph::execute` names any pass whose predicate
declines.

1. **Shader** → `packages/sandbox/content/shaders/<name>.hlsl`. Mounted as
   `/shaders/<name>.hlsl`. (Engine shaders living under the sandbox's content
   root is a known wart — do not "fix" it in passing.)
2. **Pipeline** → the `k<Name>Shader` path constant and
   `make_<name>_pipeline_desc()` go in
   `packages/sandbox/src/sandbox_common.hpp`/`.cpp`; the compile + create block
   goes in `packages/sandbox/src/main.cpp` alongside the existing ones.
   Pipelines are created by the app, not the renderer. Hand the new pipeline to `ForwardDemo::adopt`, which stores the
   identity and takes ownership; it replaces rather than appends, so shader
   hot-reload does not leak the pipeline it supersedes.
3. **Plumbing** → two adjacent lines in
   `packages/renderer/include/engine/renderer/frame_pipelines.hpp`: a field on
   `FramePipelines` and its `kFramePipelines` entry. Nothing else. The struct
   travels by value through `WorldExtractAssets` → `ExtractDesc` →
   `RenderSnapshot`, so there is no copy block to forget, and a field without a
   table entry fails the `static_assert`.
4. **Register** → `add_pass` in `setup_standard_frame`
   (`packages/renderer/src/standard_frame.cpp`), declaring every read and
   write explicitly. Transients come from `create_transient`. Note the caps:
   4 reads and 4 writes per pass, and one color target. `PassKind` is
   `{Graphics, Copy, Compute}`; a compute pass is ordered, missing-producer
   checked and cycle-checked like any other, but a write it declares is
   ordering-only until RHI #9 puts UAV textures on the contract.
5. **Record** → declare the recorder in `render_snapshot.hpp`, define it in
   `render_graph.cpp`.
6. **Gate** → a `run_<name>_gate()` in
   `packages/sandbox/src/gates/gates_renderer.cpp`, declared in
   `gates/gates.hpp` and called from the sequence in `main.cpp` (see the gate
   protocol in CLAUDE.md), then run with `ENGINE_GPU_DEBUG=1`.

Budget note: pass constants come from a **1 MiB frame ring** (per
frame-in-flight slot). Since instanced draws they are per *batch* (1,024 bytes
across shadow + forward + motion), and per drawn instance the frame spends only
the 144-byte `InstanceData`, uploaded once for the whole frame in
`RenderGraph::execute` — roughly 7,000 drawn instances. A new **per-draw**
constant buffer would put the old per-instance cost back; prefer a single
per-pass CBV, or a field on `InstanceData`, over growing `FrameConstants`
(336 bytes) or adding a third upload of the instance array. Exhaustion drops
draws rather than aborting, but a dropped batch is now a *group* of missing
objects, not one.

Instanced draws also mean a geometry pass records per `snapshot.batches`, not
per `snapshot.draws`, and must read `ctx.instances` for the frame's instance
slice rather than uploading its own.
