---
paths:
  - "packages/renderer/**"
  - "packages/rhi/**"
  - "packages/rhi-d3d12/**"
  - "packages/engine/**"
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
