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
- **Two** GPU backends since 2 Sep 2026: `rhi-d3d12` and `rhi-vulkan`. D3D12
  remains the daily driver and the only shipped player backend. Since RHI #24
  `rhi-vulkan` runs the **whole gate suite** — `solengine gates-vk` is
  `--rhi vulkan --gates` with the validation layer armed — and a
  `-DENGINE_RHI_D3D12=OFF` configure builds and passes it standalone. Every
  `--gates` run also stands a Vulkan device up inside it: `Backend parity gate`
  draws one HLSL source through both devices and asserts byte-identical
  readback.
  - **It does not yet render a live frame.** The present reports
    `VK_ERROR_DEVICE_LOST` on the first windowed frame, which is RHI #25. The
    gates cannot see it because they never call `Engine::render()`, and that is
    the lesson worth carrying: a green suite is not a working backend. Renderer
    #16 found it by looking at a frame, which is the only way it could have
    been found.
  - So a contract change costs **two** implementations, and **two gate runs**:
    `solengine gates-gpu` and `solengine gates-vk`, each with its debug layer
    on. Shipping GPU work with only one of them is how a defect reaches the
    other backend.
  - Treat the validation layer exactly like the D3D12 debug layer: any message
    is a build-breaking bug. RHI #24's swapchain synchronisation was found that
    way and nothing else would have found it.
  - A third backend waits on Platform #9, a non-Windows platform package — not
    on the RHI contract, which a second backend has now taken all the way
    through a frame. See ENGINE_MAP.md category 3.

## Adding a render pass — the whole checklist

`add_pass` is where the pass is *registered*, but a working pass spans two
packages and six files: the shader, the pipeline, two adjacent lines in
`frame_pipelines.hpp`, the registration, and the recorder — plus its gate.
A pass that compiles a shader must also name its `ShaderTarget`; the
`shader-target` invariant fails a default-constructed `ShaderCompileDesc` that
does not, because DXIL handed to a Vulkan device is refused at pipeline
creation and takes every gate after it out of the run silently.
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
   checked and cycle-checked like any other, and since RHI #9 it can write a
   transient too — create it with `storage = true` and declare
   `Access::StorageWrite`. Since RHI #18 a transient can also carry
   `sample_count > 1`; a multisampled one is never sampled directly, it is the
   source of a `RenderPassInfo::resolve` into a single-sample transient, and
   the pass's pipeline must declare the same `sample_count` or the backend
   rejects the bind by name.
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
