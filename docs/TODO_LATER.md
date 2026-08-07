# To Be Done Later

Packages and systems deferred until the engine reaches the point that needs them.
Do not scaffold these prematurely — add them when the trigger condition is met.

---

## `scene`

**Add when:** The renderer needs cameras, transforms, mesh instances, and lights every frame.

**Responsibility:** Scene graph or flat instance lists, camera, light structs, `SceneView` snapshot.

**Depends on:** `math`, `core`

**Provides to:** `renderer` (read-only per frame), `engine`

**Notes:**
- Renderer reads snapshot; scene never calls renderer.
- Start with structs + arrays, not ECS.

---

## `jobs`

**Add when:** Sync asset load, shader compile, or culling blocks the main thread.

**Responsibility:** Job scheduling, work-stealing queue, task graph sync.

**Depends on:** `core`

**Used by:** `assets-filesystem`, `renderer` (cull prep), `shaders` (background compile)

**Notes:**
- Interface `jobs` + impl `jobs-*` if needed.
- Renderer submits work; it does not own threads.

---

## Back of the box (features, not packages)

These are **not** roadmap phases. They do not strengthen the core and should wait until [WHATS_NEXT.md](WHATS_NEXT.md) core gates are met.

| Feature | Why defer |
|---------|-----------|
| **SSAO** | Isolated post effect; needs G-buffer + jobs; no leverage on foundation |
| Shadow mapping | Extra passes, scene, jobs |
| Bloom / tonemap / post stack | Needs HDR pipeline + mature graph |
| Deferred rendering | Material system + graph maturity first |
| Physics, audio, networking | Separate domains |
| Full editor | Sandbox + hot-reload + debug overlay suffice for now |
| Second GPU API | One backend working first |
| Bindless descriptors | No content volume to justify complexity |

---

## Other candidates (no trigger yet)

| Package | Trigger |
|---------|---------|
| `rhi-vulkan` | Second backend needed (Windows D3D12 first) |
| `editor` | In-engine tooling beyond sandbox |
| `audio` | First sound requirement |
