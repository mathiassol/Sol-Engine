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

`add_pass` is where the pass is *registered*, but a working pass touches two
packages and four files. Missing a step fails **silently**: every
`should_execute` predicate treats a null pipeline as "feature disabled", so a
forgotten copy line produces a pass that never runs and never complains.

1. **Shader** → `packages/sandbox/content/shaders/<name>.hlsl`. Mounted as
   `/shaders/<name>.hlsl`. (Engine shaders living under the sandbox's content
   root is a known wart — do not "fix" it in passing.)
2. **Pipeline** → `packages/sandbox/src/main.cpp`: a `k<Name>Shader` path
   constant, a `make_<name>_pipeline_desc()`, and a compile + create block
   alongside the existing ones. Pipelines are created by the app, not the
   renderer.
3. **Plumbing** → add the `I<Thing>Pipeline*` field to **all four** structs it
   passes through, and to the three hand-written copy blocks between them:
   `ForwardDemo` (main.cpp) → `WorldExtractAssets` (world_extract.hpp) →
   `ExtractDesc` (renderer/extract.hpp) → `RenderSnapshot`
   (renderer/render_snapshot.hpp).
4. **Register** → `add_pass` in `setup_standard_frame`
   (`packages/renderer/src/standard_frame.cpp`), declaring every read and
   write explicitly. Transients come from `create_transient`. Note the caps:
   4 reads and 4 writes per pass, one color target, and `PassKind` is
   `{Graphics, Copy}` — there is no compute pass kind.
5. **Record** → declare the recorder in `render_snapshot.hpp`, define it in
   `render_graph.cpp`.
6. **Gate** → a `run_<name>_gate()` in `main.cpp` (see the gate protocol in
   CLAUDE.md), then run with `ENGINE_GPU_DEBUG=1`.

Budget note: per-draw constants come from a **1 MiB frame ring** (per
frame-in-flight slot) and each drawn instance already costs 1,280 bytes across
shadow + forward + motion — so the ring is the binding constraint at roughly
800 drawn instances. A new per-draw constant buffer eats into that. Exhaustion
drops draws rather than aborting, but a dropped draw is still a missing object:
prefer a single per-pass CBV over growing `FrameConstants` (already 400 bytes).
