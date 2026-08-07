# What's Next

Foundation-first roadmap. Each phase has a **gate** — a concrete sandbox behavior that proves it works.
If it doesn't strengthen the core or unblock the next layer, defer it.

See also: [Philosophy.md](../Philosophy.md), [packageRules.md](packageRules.md), [TODO_LATER.md](TODO_LATER.md).

---

## Core done criteria (target)

Sandbox opens a window, runs a stable loop, loads mesh + shader from disk, renders with camera + depth, survives resize and shader hot-reload, shows frame stats — **without rewriting architecture**.

---

## Where we are ✅

| Area | Status |
|------|--------|
| Foundation (`core`, `math`, `platform-win32`) | Done — time, arena, log, input, filesystem, resize |
| GPU path (`rhi-d3d12`, `shaders-dxc`) | Done — swapchain, depth, CBV, indexed draw, 3-frame flight |
| Forward pass | Done — OBJ mesh, camera, constant buffer |
| Assets | Done — `assets-obj`, `assets-gpu` upload |
| Dev tooling | Done — shader hot-reload, F3 stats overlay (`debug-draw`) |
| Engine loop | Partial — phased loop exists; render extraction not formalized |

---

## Phase 1 — GPU resource discipline (next)

**Why:** Upload heap for everything won't scale. Production engines separate staging, track lifetimes, and defer frees.

| Task | Package | Gate |
|------|---------|------|
| Staging upload path (CPU → copy → default heap) | `rhi-d3d12` / `assets-gpu` | Reload same mesh 100× without VRAM growth |
| Deferred GPU release (N frames after last use) | `rhi` or `gpu-lifetime` | Destroy mesh handle; VRAM freed after frame ring |
| D3D12 object names (debug builds) | `rhi-d3d12` | PIX / debug layer shows readable names |
| `resize()` returns bool, engine retries | `engine` | ✅ fixed — failed resize recovers |

**Do not:** bindless heaps, texture streaming, second API.

---

## Phase 2 — Content and asset handles

**Why:** Hardcoded paths and raw buffers block real content workflow.

| Task | Package | Gate |
|------|---------|------|
| Content mount roots (`/content`, `/shaders`) | `assets-filesystem` | Paths work regardless of CWD if mount configured |
| Typed handles (`MeshHandle`, stable reload) | `assets` | Same handle after re-upload |
| Working directory helper or engine `content_root` | `engine` / `sandbox` | Run from any CWD |
| Pipeline disk cache (hash → blob) | `shaders` | Second launch skips recompile |

**Do not:** full asset database, async IO (until jobs exist).

---

## Phase 3 — Render graph becomes real

**Why:** Pass list works for two passes; barriers and transient RTs need the graph to own lifetimes.

| Task | Package | Gate |
|------|---------|------|
| Pass declares reads/writes + format | `renderer` | Graph detects missing transition |
| Transient depth/color allocation | `renderer` | Add second pass without manual barrier code |
| Render snapshot struct (camera + draw list) | `engine` → `renderer` | Renderer never reads mutable scene state |
| Textured forward pass | `rhi` + `sandbox` | One albedo texture, UVs in mesh |

**Do not:** deferred shading, SSAO, shadows, post stack.

---

## Phase 4 — Scene (only when renderer asks)

**Trigger:** Every frame the renderer needs cameras, transforms, mesh instances, lights.

| Task | Package | Gate |
|------|---------|------|
| `SceneView` snapshot (cameras, draws, lights) | `scene` | Forward pass reads snapshot only |
| Transform hierarchy (or flat list first) | `scene` | Move entity, see it in render |
| Multiple mesh instances | `scene` + `sandbox` | Two cubes, different transforms |

See [TODO_LATER.md](TODO_LATER.md#scene). **Do not** add ECS until arrays prove insufficient.

---

## Phase 5 — Jobs (only when main thread blocks)

**Trigger:** Sync mesh load or shader compile causes visible hitches.

| Task | Package | Gate |
|------|---------|------|
| `IJobSystem` + thread pool | `jobs` | Load mesh on worker, main thread uploads when ready |
| Parallel frustum cull (optional) | `jobs` + `renderer` | Measurable win with 1000+ instances |

See [TODO_LATER.md](TODO_LATER.md#jobs).

---

## Phase 6 — Debug tooling (expand)

| Task | Package | Gate |
|------|---------|------|
| GPU timestamp queries | `rhi-d3d12` + `debug-draw` | Overlay shows GPU ms |
| Debug draw lines/boxes | `debug-draw` | Toggle wireframe AABB |
| Render graph cycle detection | `renderer` | Dev build traps invalid pass order |

---

## Back of the box 🗄️

Features that **do not** strengthen the core. Add only when core gates above are met and you have content that needs them.

| Defer | Why |
|-------|-----|
| **SSAO** | Standalone effect; needs G-buffer, depth prepass, jobs — zero leverage on core |
| Shadows | Needs scene + jobs + extra passes |
| Post-processing (bloom, tonemap) | Needs stable HDR + graph |
| Deferred rendering | Needs material system + graph maturity |
| Physics, audio, networking | Separate domains |
| Editor | Sandbox + hot-reload + debug draw covers early dev |
| Second GPU API | One backend fully working first |
| Bindless / mega-heaps | Complexity without content volume |

---

## How to pick the next task

1. **Topmost incomplete item in the lowest incomplete phase.**
2. One package, one job ([packageRules.md](packageRules.md)).
3. Must have a gate you can verify in sandbox.
4. If no gate → [TODO_LATER.md](TODO_LATER.md).

**Recommended next session:** Phase 1 staging upload + deferred release.
