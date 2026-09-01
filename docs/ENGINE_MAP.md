# Engine map

Everything a general-purpose engine in the Unity / Godot / Unreal category
eventually needs. Not a sprint. Not an org chart to copy on day one.

**This file is the work list.** Every **Ready** row in the tables below is a
fair next pick; **Later** rows name what blocks them. There is no dashboard or
canvas — read this file directly.

**How to use:** pick **one Ready** item, give it a **gate** (`--gates` and/or
a visible sandbox check with `ENGINE_GPU_DEBUG=1`), put it behind a package
interface when it is a system. Live numbered sequence stays in
[ROADMAP.md](ROADMAP.md). Triggers and "do not scaffold empty packages":
[PICKING.md](PICKING.md). `/roadmap <category> #<id>` runs one row from
research to shipped.

**Status**

| Mark | Meaning |
|------|---------|
| **Done** | In the tree; `--gates` covers it or it is the live path |
| **Ready** | Engine deps exist; this is a fair next pick. `--gates` / the sandbox is an allowed consumer — do not wait for a HUD, NPC, or menu that this row would create. |
| Later | Blocked: **Finish first** names a map row that is not Done, **or** a measurable wall that has not been hit |
| Far | Valid engine work; do not start until a game actually hurts without it |

86 Done · 39 Ready · 98 Later · 60 Far.
Read the Status column in the tables — it is the only copy. A hand-written
summary would drift from the tables under it.

## The `#` column is an id, not a position

Row **order** inside each category is **implementation order**: what you build
first so the next row is cheap. Row **numbers are permanent ids** and are
therefore not sequential — `Renderer #30` sits above `Renderer #15` because
spot lights come before cascades, not because it was added later.

Ids never change. Every `Category #N` reference in this file, in
[ROADMAP.md](ROADMAP.md), and in the commit log stays valid when rows are
reordered or inserted. Renumbering to make the column pretty would break all
of them.

## How **Finish first** works

Read it as one rule: **a `Category #N` reference in Finish first *is* a
blocker.** Nothing else in the column is.

- A blocker must be a row that is **not Done**. When it becomes Done, every
  row whose only remaining blocker was it becomes **Ready**.
- A **wall** is the other kind of gate: a measurable condition, written with no
  row reference ("a scene larger than one cascade", "two systems hitching").
  A row can be Later on a wall alone.
- **Never use a `#N` reference to say a row is *not* a blocker.** Write it
  without the reference instead. Referencing a row to disclaim it reads as a
  dependency to everything that parses this file, and produced two false
  dependency loops before this rule existed.
- **Never wait on a twin.** Where two rows describe halves of one feature
  (skins ↔ animation, quads ↔ text layout, decode ↔ streaming), the **earlier
  row in implementation order** carries no blocker and the later one waits on
  it. Both waiting on each other is a loop, and a loop means neither ever
  becomes Ready.
- A "missing consumer" we would write as the gate is not a blocker.

Phases 0–14 are **Done**. Next work is any **Ready** row you choose.

---

## 1. Foundation

Core loop, memory, math, diagnostics. Mostly shipped.

*9 done · 4 ready · 18 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Types, assert, log, clock, frame timer, frame arena | **Done** |  |
| 2 | Math: Mat4, Vec3, frustum, AABB | **Done** |  |
| 3 | Phased `Engine::run` (poll → fixed → update → extract → render) | **Done** |  |
| 4 | `--gates` + GPU debug layer as the stability method | **Done** |  |
| 5 | CPU profiler scopes + F3 overlay slots | **Done** |  |
| 8 | Config / cvars (named knobs without recompile) | **Done** |  |
| 9 | Deterministic fixed-step gameplay clock (already sketched; prove it with a sim) | **Done** |  |
| 6 | File logger (not only stderr) | **Done** |  |
| 7 | Crash dump / minidump on unhandled exception | **Done** |  |
| 10 | Interned string ids (`StringId`) with a debug-only reverse table | **Ready** |  |
| 11 | Versioned binary reader/writer primitives, shared by every cooked format | **Ready** |  |
| 12 | Tracked allocators: tag every arena, report live bytes and leaks at shutdown | **Ready** |  |
| 18 | MSVC `/fsanitize=address` over the full Windows gate run | Later | The Linux job sanitizes the ~37 CPU gates; this is the other 42, and the GPU code none of them touch. Needs incremental linking off and every module instrumented, including the DXC runtime that ships beside the exe. |
| 17 | Cvar writer: persist chosen knobs back to `config.cfg` | **Ready** |  |
| 13 | Per-thread scratch arenas (so a job can allocate without a lock) | Later | Jobs #2 pool. One worker needs no per-thread anything. |
| 14 | Field descriptors for POD structs, so save/load and prefabs stop hand-writing both sides | Later | Foundation #11 binary primitives, then a second format that would duplicate the walk. |
| 15 | Config hot-reload: re-read `config.cfg` on change without a restart | Later | Platform #12 filesystem watch. |
| 16 | Deterministic math helpers (fixed-point or strict-FP) for lockstep | Far | Networking #3 prediction. Nothing today needs bit-exact replay. |

---

## 2. Platform

Window, OS services, input devices. Win32 is the daily driver.

*4 done · 5 ready · 16 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `IPlatform` + Win32 window, resize, DPI, focus | **Done** |  |
| 2 | Raw keyboard + mouse | **Done** |  |
| 3 | Gamepad (XInput / equivalent) | **Done** |  |
| 6 | Fullscreen / borderless / vsync control on the interface | **Done** |  |
| 10 | Relative mouse mode: capture, hide, unbounded delta for FPS look | **Ready** |  |
| 11 | Monitor enumeration: list outputs, refresh rate, work area | **Ready** |  |
| 12 | Filesystem watch on the interface (shaders already poll; generalise it) | **Ready** |  |
| 13 | Install the crash handler through `IPlatform`, not in the app | **Ready** | |
| 8 | High-precision timer already via `Clock`; expose QPC if a system needs it | Later | A system `Clock` is too coarse for — audio latency or a net tick. Do not wrap QPC twice for its own sake. |
| 4 | Text input / IME (chat, name fields) | Later | UI #2 fonts — a caret needs a widget, not F3. Not an inspector. |
| 14 | System locale / preferred language query | Later | Localization #3 string tables. Nothing reads a locale yet. |
| 15 | Drag-and-drop a file onto the window | Far | A tool or game that opens user files. The sandbox loads from mounts. |
| 16 | Focus-loss throttling and battery/power state | Far | A shipped game complaining about heat or battery. |
| 7 | Clipboard, file dialogs | Far | A separate editor (Editor #4–#5) or a game that picks files. No in-engine inspector. |
| 5 | Multiple windows (editor + game view) | Far | A **separate** editor app (Editor #2), not an in-sandbox inspector. `IPlatform` is single-HWND today. |
| 9 | `platform-*` non-Windows (same `IPlatform`) | **Ready** | |

---

## 3. RHI (GPU backend)

Interface first, one production impl. Vulkan is a **package**, not a rewrite.

*11 done · 2 ready · 23 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `IRHI` / `IDevice` / `ICommandList`, 3-frame flight, present | **Done** |  |
| 2 | Graphics pipelines, CBV, SRV, debug names | **Done** |  |
| 3 | Sampler **objects** on the contract; `SamplerDesc` on PSOs | **Done** |  |
| 4 | Compute on the **interface** (D3D12 stub that fails loudly) | **Done** |  |
| 5 | Shader target enum (DXIL vs SPIR-V) | **Done** |  |
| 6 | **Implement** compute on D3D12 (PSO + dispatch that works) | **Done** |  |
| 7 | Cube maps + array textures on `TextureDesc` | **Done** |  |
| 14 | Instanced draws + root SRV structured buffers (per-instance data) | **Done** |  |
| 15 | Reversed-Z depth on the contract (near-plane precision, one flag) | **Done** |  |
| 16 | PSO disk cache keyed on the pipeline desc, so a cold start stops recompiling | **Ready** |  |
| 17 | GPU crash breadcrumbs (D3D12 DRED) captured on device-removed | **Ready** |  |
| 18 | MSAA render targets on `TextureDesc` | **Done** |  |
| 9 | UAV textures (compute write) | **Done** |  |
| 19 | Copy queue: uploads off the graphics timeline | Later | Uploads big enough to stall the graphics queue. Async loading is what would keep a copy queue busy, and it can start on the graphics queue. |
| 20 | Transient memory pool / resource aliasing for graph transients | Later | Renderer #18 depth+normals — enough transients that peak VRAM is the wall. |
| 21 | Indirect draw (`ExecuteIndirect`) on the contract | Later | A pass submitting more draws than the CPU can afford to record. Instanced draws already cut that cost a long way. |
| 22 | HDR output: HDR10 / scRGB swapchain formats | Later | Platform #11 monitor enumeration, to know the display can do it. |
| 23 | Occlusion and pipeline-statistics query pools | Later | Debug #6 memory/stat reporting, which is what would read them. |
| 8 | Dynamic sampler tables (`set_sampler` actually binds) | Later | A shader that **cannot** use static `SamplerDesc` on the PSO — many unique samplers, bindless-ish materials. `set_sampler` is a stub on purpose. |
| 10 | Timestamp queries already exist; expose them on `IRHI` if a second backend needs them | Far | RHI #12 (`rhi-vulkan`). D3D12 already timestamps for F3. |
| 11 | Mesh shaders / bindless heaps | Far |  |
| 12 | `rhi-vulkan` (SPIR-V compiler + this contract; D3D12 stays daily driver) | Far | Shaders #5 SPIR-V path. **Platform #9 was dropped from this list on 1 Sep 2026** — Vulkan runs on Windows, so a non-Windows platform package is not a prerequisite, and doing the second backend on Windows first validates the contract against one new variable instead of two. RHI #15, #9 and #18 went in ahead of it deliberately: each changes the contract and is API-neutral, so it costs one implementation now and two later. |
| 13 | Metal / console backends | Far | RHI #12 proves the contract survives a second backend first. |

---

## 4. Shaders

*3 done · 3 ready · 10 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | DXC SM 6.0 / DXIL, disk cache, async hot-reload | **Done** |  |
| 2 | `ShaderTarget` enum; SPIR-V rejected until a compiler exists | **Done** |  |
| 3 | Shader include graph already folded into cache keys | **Done** |  |
| 8 | Warnings as errors, and a gate that fails on any DXC diagnostic | **Ready** |  |
| 9 | Startup warm-up: compile every registered PSO before the first frame | **Ready** |  |
| 10 | Emit shader PDBs so PIX can show source | **Ready** |  |
| 4 | Permutations / #defines as data (not a new .hlsl per knob) | Later | Assets #10 skins **or** Renderer #16 alpha — a second `.hlsl` path. One `forward.hlsl` with PBR constants is enough until then. |
| 6 | Shader reflection (auto root layout from bytecode) | Later | Enough PSOs that hand-written root layouts hurt. Few pipelines today; layouts stay written by hand. |
| 5 | SPIR-V compile path (for Vulkan) | Far | A second GPU backend actually being started. D3D12 is the daily driver and `ShaderTarget` already rejects SPIR-V. This is the **first** step of that work, not a consequence of it — the Vulkan backend row waits on this one. |
| 7 | Node material graph (tools) | Far | Shaders #4 permutations as data — a graph emits permutations, so that has to work first. |

---

## 5. Renderer

Graph-owned frame. New engine pass = `add_pass` in
`packages/renderer/src/standard_frame.cpp`. Research notes:
[reasarch/GRAPICS-RESEARCH.md](../reasarch/GRAPICS-RESEARCH.md) — treat as
features, not a school pile.

*17 done · 4 ready · 40 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Render graph (declared reads/writes, transients, compile) | **Done** |  |
| 2 | Standard frame: shadow → forward → motion → sky → bloom → TAA → tonemap → AA → debug → overlay | **Done** |  |
| 3 | Extract: frustum skip, sun bounds, draw list (no `scene` include) | **Done** |  |
| 4 | Forward lighting: sun + ambient + point slots | **Done** |  |
| 5 | 1024² sun shadow map | **Done** |  |
| 6 | HDR scene color + ACES tonemap | **Done** |  |
| 7 | Materials as data (albedo + metallic + roughness) | **Done** |  |
| 8 | **PBR BRDF** (GGX / Cook-Torrance on existing metal-rough) | **Done** |  |
| 9 | Image-based lighting (cubemap mips + BRDF LUT split-sum) | **Done** |  |
| 10 | PCF (then PCSS if contacts still look like stamps) | **Done** |  |
| 11 | Bloom (HDR is already there) | **Done** |  |
| 12 | Cheap AA (FXAA or SMAA) before investing in TAA | **Done** |  |
| 13 | Motion vectors | **Done** |  |
| 14 | TAA (optional F5; default Off) | **Done** |  |
| 27 | Instanced draws: batch the extract by material/mesh key, one `draw_indexed` per batch | **Done** |  |
| 28 | Colour space: sRGB decode on colour textures, sRGB encode after tonemap | **Done** |  |
| 29 | Exposure control (one scalar before the tonemap; scales sun, sky and IBL together) | **Done** |  |
| 16 | Transparency / alpha (forward; keep out of deferred) | **Ready** |  |
| 30 | Spot lights, plus radius and attenuation on the same forward path | **Ready** |  |
| 31 | Emissive term on the material (a surface that feeds bloom without a lamp) | **Ready** |  |
| 32 | Debug view modes: albedo, normal, roughness, overdraw, cascade index | **Ready** |  |
| 33 | Alpha-tested cutout path (foliage, fences) — cheaper than sorted blending | Later | Renderer #16 alpha, which decides where the material flag lives. |
| 34 | Draw sorting: opaque front-to-back, transparent back-to-front | Later | Renderer #16 alpha — sorting only starts to matter once anything blends. |
| 38 | Depth prepass, once overdraw is measurable | Later | Renderer #32 debug views — the overdraw view is how you find out whether this pays for itself. |
| 36 | Render scale / dynamic resolution, with the AA passes following it | Later | Renderer #37 post chain, or every scale below 1 just looks soft. |
| 37 | Post: LUT colour grading, vignette, film grain, sharpening | Later | A look worth authoring. ACES plus exposure is the whole pipeline today. |
| 15 | Cascaded shadow maps (CSM) | Later | World #3 terrain, or a scene larger than one cascade. One 1024² map covers the husky pit. |
| 17 | More lights: clustered forward or Forward+ before a full G-buffer | Later | Prove the **4 point slots are the wall**. They are not — and Renderer #30 spot lights will say more about that than a paper will. |
| 18 | Depth+normals (or G-buffer) **for a reason** | Later | A consumer: SSAO, SSR, or deferred. PBR, PCF and bloom do not need a G-buffer. |
| 23 | Particles as a graph pass | Later | VFX #1 CPU particles, designed as `add_pass`. |
| 35 | GPU-driven culling (hierarchical Z, compute cull, indirect submit) | Later | RHI #21 indirect draw. Instanced draws moved the CPU wall a long way out. |
| 19 | SSAO | Later | Renderer #18 depth+normals. Do not add because a paper has it. |
| 22 | Contact shadows | Later | A reason screen-space contact beats one cascade. PCF is in. |
| 21 | SSR | Later | Renderer #18 (depth+normals or G-buffer). IBL is the usual fallback when SSR misses. |
| 20 | Deferred lighting (when forward light count is the wall) | Later | Renderer #17 clustered still not enough, **and** Renderer #18 G-buffer. |
| 24 | Multiple views / split-screen | Later | Split-screen gameplay, which needs only running the graph twice. A second view in its own window would additionally want multi-window support, but split-screen does not. |
| 39 | Custom depth / stencil for outlines and selection highlights | Far | A game that selects or highlights objects. |
| 40 | Temporal upscaling (FSR/DLSS-class) | Far | Renderer #36 render scale. |
| 25 | Volumetric fog / SSGI / full GI | Far |  |
| 26 | Virtual shadow maps, nanite-like vis, hardware RT | Far |  |

---

## 6. Assets and content pipeline

*6 done · 3 ready · 19 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Virtual mounts (`/content`, `/shaders`, `/debug`) | **Done** |  |
| 2 | OBJ, one-primitive glTF, PNG, mip generation | **Done** |  |
| 3 | glTF metal/rough **factors** | **Done** |  |
| 4 | glTF extras: more primitives, metal-rough **textures**, normal maps, **node transforms** | **Done** |  |
| 7 | Cooker: source → engine binary (meshes, textures, audio) | **Done** |  |
| 8 | Pak / archive for shipping (see **Build**) | **Done** |  |
| 10 | glTF skins / morphs (see **Animation**) | **Ready** |  |
| 12 | Content validation gate: every mount path in code resolves, no orphan asset ships | **Ready** |  |
| 13 | Tangent generation at import (replace the derivative TBN in the shader) | **Ready** |  |
| 5 | Texture cooker formats (BC7 etc.) | Later | GPU memory or pack size being the wall. One 2048² albedo costs 22 MB uncompressed; Debug #6 watermarks are how you prove it hurts. |
| 14 | Async asset loading off the main thread | Later | Jobs #2 pool. It can start on the graphics queue; a dedicated copy queue is an optimisation on top. |
| 15 | Mesh LOD generation at import, plus runtime LOD selection | Later | Scene #9 spatial index, so selection has a distance to work from. |
| 16 | Mesh optimisation at import: vertex-cache order, index welding | Later | Assets #15 LOD, which walks the same mesh data. |
| 17 | OGG/Vorbis decode for music (WAV PCM is in) | Later | A music track worth streaming. Short PCM one-shots already play, and this is the decode half of that. |
| 18 | Asset hot-reload for meshes and textures (shaders already do) | Later | Platform #12 filesystem watch. |
| 6 | Asset database / GUID / import settings | Later | More than a handful of files. Path strings on mounts are still enough. |
| 9 | Streaming (load next cell while playing) | Later | Scene #7 additive worlds. Do not parse glTF on the hitch path. |
| 19 | KTX2 / Basis container, so one file carries every platform's format | Far | Assets #5 BC7 first, and a second platform to need a second format. |
| 11 | Source control friendly `.meta` / `.import` | Far | Assets #6 asset database. |

---

## 7. Scene

Flat `World` today. Grow when the **coded demo** needs names, hierarchy, or a
file. Not so an inspector can live in the engine.

*5 done · 3 ready · 13 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `World`: instances (512), camera, sun, points, materials | **Done** |  |
| 2 | Names on instances (string or interned id) | **Done** |  |
| 3 | Hierarchy / parenting (transforms compose) | **Done** |  |
| 4 | Save / load a scene file | **Done** |  |
| 5 | Prefabs / templates | **Done** |  |
| 7 | World streaming / multiple scenes additive | **Ready** |  |
| 9 | Spatial index (BVH or grid) for culling and queries, rebuilt from the instance list | **Ready** |  |
| 10 | Stable instance ids that survive save/load and prefab instantiation | **Ready** |  |
| 6 | Layers / tags / visibility masks | Later | Scene #7 additive worlds — more than one view's worth of instances. Physics already has layers and masks. |
| 11 | Name lookup by hash table, and cached world matrices | Later | A scene past roughly 4,000 instances. The intern is O(n²) and the parent walk is uncached; both are invisible at 512. |
| 12 | Heap-backed `World`, past the fixed 512 array | Later | Scene #11 — do the cheap fixes first. This one costs the trivially-copyable property and the compile-time bound that protects `read_world`. |
| 13 | World origin rebasing for large maps (float precision at distance) | Far | A map big enough to see the wobble. 512 instances in a pit is not it. |
| 8 | ECS as a second scene truth | Far |  |

---

## 8. Gameplay, input, camera

*3 done · 3 ready · 12 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Fly camera + Z/X nudge | **Done** |  |
| 3 | Game camera types: follow, orbit, FPS | **Done** |  |
| 4 | Character controller (walk, jump) | **Done** |  |
| 2 | Input **actions** (Jump, Fire) bound to keys/gamepad | **Ready** |  |
| 6 | Gameplay components as data on instances (not a second ECS) | **Ready** |  |
| 8 | Camera shake / impulse on the existing camera types | **Ready** |  |
| 9 | Save game: a versioned player profile through the cooked-format primitives | Later | Foundation #11 binary primitives, so save files carry a version from the first one written. |
| 10 | Spring-arm camera that collides instead of clipping through geometry | Later | A level with walls to clip through. The pit has none. |
| 11 | Timers and one-shot scheduled callbacks on the fixed step | Later | Gameplay #6 components, which is where a timer would live. |
| 5 | Time scale / pause | Later | A pause **menu** (UI #4). Physics can already pause; the fly-cam sandbox does not need it. |
| 12 | Gameplay event bus (fire-and-forget messages between systems) | Later | Gameplay #6 components. Two systems that need to talk without including each other. |
| 7 | Scripting VM (Lua/C# later); C++ first | Far |  |

---

## 9. Animation

*0 done · 0 ready · 11 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Skinned mesh GPU path (palette or compute) | Later | Assets #10 glTF skins — the data has to exist before it can be posed. |
| 2 | Clip playback (sample tracks, loop) | Later | Animation #1 skinned path. |
| 7 | Animation clip import from glTF (tracks, interpolation modes) | Later | Assets #10 skins, which reads the same file. |
| 3 | Blend (two clips) | Later | Animation #2 clips. |
| 8 | Animation events / notifies (footstep, hit frame) | Later | Animation #2 clips, and Gameplay #12 event bus to fire into. |
| 5 | Root motion | Later | Animation #2 clips. The character controller can consume the delta. |
| 4 | State machine / blend tree | Later | Animation #3 blend. |
| 9 | Additive layers (aim offset, lean) on top of the base pose | Later | Animation #4 state machine. |
| 10 | Clip compression (quantised tracks, constant-track stripping) | Far | Enough clips that memory or load time is the wall. |
| 11 | Skeleton retargeting between rigs | Far | A second rig worth sharing clips with. |
| 6 | IK, look-at, procedural | Far |  |

---

## 10. Audio

*2 done · 2 ready · 9 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Interface: play one-shot 2D sound | **Done** |  |
| 2 | One backend (XAudio2) | **Done** |  |
| 3 | 3D positional (distance, listener on camera) | **Ready** |  |
| 7 | Voice limiting and priority (cap concurrent voices, drop the quietest) | **Ready** |  |
| 4 | Buses / mixer / volume groups | Later | Audio #3 positional, so there is more than one thing to balance. |
| 5 | Streaming music | Later | Assets #17 OGG decode. Short PCM one-shots already play. |
| 8 | Sound banks cooked as one file per group | Later | Audio #4 buses, which is what a bank would be grouped by. |
| 9 | Doppler and velocity on emitters | Later | Audio #3 positional. |
| 6 | Effects (reverb, occlusion) | Far | Audio #4 buses to insert an effect on. |

---

## 11. Physics

*5 done · 2 ready · 14 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Overlap tests (AABB / sphere) | **Done** |  |
| 2 | Rigid bodies + gravity (boxes, spheres) | **Done** |  |
| 3 | Character capsule + floor | **Done** |  |
| 4 | Triggers / callbacks | **Done** |  |
| 5 | Raycasts for guns / picking | **Done** |  |
| 8 | Raise the 256-body cap, or make overflow recoverable instead of an assert | **Ready** |  |
| 9 | Sleeping and islands, so idle bodies stop costing solver time | **Ready** |  |
| 10 | Convex hull colliders (AABB, sphere and capsule are the whole shape list today) | Later | Physics #8 capacity — a scene with enough bodies to want a tighter shape than a box. |
| 11 | Joints and constraints (hinge, ball, fixed) | Later | Physics #10 convex hulls, so there is something worth joining. |
| 12 | Continuous collision for fast movers (no tunnelling through walls) | Later | A projectile or vehicle that tunnels. Nothing moves fast enough yet. |
| 13 | Mesh colliders from cooked geometry | Later | Physics #10 convex hulls, and Assets #16 mesh optimisation to build from. |
| 14 | Deterministic step (fixed iteration order, no FP drift) for lockstep | Far | Networking #3 prediction, and Foundation #16 deterministic math. |
| 6 | Vehicles, cloth, destruction | Far | Physics #11 joints. |
| 7 | Third-party (Jolt/PhysX) behind the same interface | Far | The CPU solver being the wall. It is not — 256 bodies is. |

---

## 12. In-game UI

*1 done · 1 ready · 9 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Immediate overlay text (F3 already) | **Done** |  |
| 2 | Screen-space quads + font atlas | **Ready** |  |
| 6 | Text layout: wrapping, alignment, measured runs | Later | UI #2 font atlas. |
| 3 | Layout (stack, anchor) + input focus | Later | UI #2 quads and fonts. |
| 7 | Nine-slice and sprite atlas for panels and buttons | Later | UI #2 quads. |
| 4 | Menus, inventory, dialogue | Later | UI #3 layout. |
| 8 | World-space UI (nameplates, damage numbers) through the same batcher | Later | UI #3 layout, and Renderer #16 alpha to blend against the scene. |
| 9 | UI scale and safe-area handling for different displays | Later | UI #3 layout. |
| 5 | Retention-mode UI toolkit | Far | UI #4 menus proving the immediate mode is the wall. |

---

## 13. World and environment

*1 done · 1 ready · 9 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Sky (procedural or cubemap) | **Done** |  |
| 3 | Terrain / heightmap | **Ready** |  |
| 2 | Fog (simple exp, then height) | Later | World #3 terrain — a scene with depth to fog into. World #1 sky is already in. |
| 7 | Terrain LOD (clipmap or quadtree) once one heightmap is not enough | Later | World #3 terrain. |
| 8 | Foliage and prop scatter, drawn through the instanced path | Later | World #3 terrain to scatter onto. Renderer #27 instancing is already in. |
| 4 | Water | Later | Renderer #16 transparency. |
| 9 | Streaming volumes that drive Scene additive load/unload | Later | Scene #7 additive worlds. |
| 5 | Vegetation / wind | Far | World #8 scatter. |
| 6 | Weather / day-night as **data** driving sun + IBL | Far | World #2 fog, so there is an atmosphere to modulate. |

---

## 14. VFX

*0 done · 1 ready · 8 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | CPU particles drawn as a graph pass | **Ready** |  |
| 6 | Soft particles and depth-aware fade | Later | VFX #1 particles, and Renderer #18 depth to read. |
| 7 | Sub-UV / flipbook animation on particle material | Later | VFX #1 particles. |
| 4 | Trails, ribbons | Later | VFX #1 particles. |
| 2 | GPU particles (compute) | Later | RHI #9 UAV textures, and VFX #1 proving the CPU path is the wall. |
| 3 | Decals | Later | Renderer #18 depth+normals — a decal projects onto scene depth. |
| 8 | Mesh particles (instanced geometry per particle) | Later | VFX #1 particles. Renderer #27 instancing already carries the draw. |
| 5 | Post “juice” (hit flash, chromatic) | Far | Renderer #37 post chain to hang it on. |

---

## 15. AI and navigation

*0 done · 1 ready · 7 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Navmesh bake + path follow | **Ready** |  |
| 2 | Steering / avoidance | Later | AI #1 navmesh. |
| 5 | Nav obstacle carving (doors, destructibles) at runtime | Later | AI #1 navmesh. |
| 3 | Perception (see / hear volumes) | Later | AI #1 navmesh, so a perceiving agent has somewhere to go. |
| 6 | Blackboard: shared per-agent state the behaviour layer reads | Later | AI #3 perception, which is the first system with per-agent state worth sharing. |
| 4 | Behavior trees / utility AI | Far | AI #2 steering and AI #3 perception. |
| 7 | Crowd simulation (many agents sharing avoidance) | Far | AI #2 steering, and Physics #8 capacity to hold the agents. |

---

## 16. Networking

*0 done · 0 ready · 8 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Transport (UDP) + tick | Far |  |
| 2 | Replication of instance transforms | Far | Networking #1 transport. |
| 3 | Client prediction / interpolation | Far | Networking #2 replication. |
| 6 | Snapshot delta compression and bandwidth budget | Far | Networking #2 replication. |
| 7 | Lag compensation (rewind for hit validation) | Far | Networking #3 prediction, and Physics #14 a deterministic step. |
| 4 | Dedicated server target (headless, no RHI) | Far | Networking #1 transport, and a build config with no graphics backend. |
| 8 | Matchmaking / server browser | Far | Networking #4 dedicated server. |
| 5 | Steam / EOS / console online | Far | Build #12 store SDKs. |

---

## 17. Editor (separate app)

*1 done · 0 ready · 12 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Policy: engine ≠ editor. Demo games are code. No in-engine inspector. | **Done** |  |
| 2 | Separate editor app (own target; uses engine APIs; not sandbox) | Far | A game big enough that editing it in C++ hurts. It can start single-window — the multi-window platform row waits on this one, not the reverse. |
| 9 | Undo/redo command stack, before any tool can edit anything | Far | Editor #2 the app itself. |
| 10 | Transform gizmos (translate, rotate, scale) with snapping | Far | Editor #2, and Physics #5 raycasts for picking (already in). |
| 3 | Hierarchy view in that editor | Far | Editor #2. |
| 4 | Scene open/save in that editor | Far | Editor #2. Scene #4 save/load is already in. |
| 5 | Content browser in that editor | Far | Editor #2, and Assets #6 asset database. |
| 11 | Play-in-editor (run the game loop inside the tool) | Far | Editor #6 the game viewport. |
| 6 | Editor window + game viewport (multi-window) | Far | Platform #5 multiple windows, and Editor #3 a hierarchy worth docking beside a viewport. |
| 12 | Asset thumbnail generation for the browser | Far | Editor #5 content browser. |
| 7 | Material / shader graph UI | Far | Shaders #7 node graph. |
| 8 | Animation / landscape / sequencer editors | Far | Editor #2, and the runtime systems each one edits. |

---

## 18. Build and ship a `game.exe`

*12 done · 2 ready · 19 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | CMake app target distinct from `sandbox` (e.g. `game`) | **Done** |  |
| 2 | Release config: no GPU debug layer, NDEBUG, optimizations | **Done** |  |
| 3 | Content root that works from the exe directory (already true for mounts; prove it) | **Done** |  |
| 4 | Icon, version resource, window title as game identity | **Done** |  |
| 5 | Cooked asset pack next to (or inside) the exe | **Done** |  |
| 9 | Ship dxcompiler/D3D12 Agility if required; document GPU baseline | **Done** |  |
| 8 | Zip / installer (no Visual Studio on the player machine) | **Done** |  |
| 10 | Fullscreen options, quality presets | **Done** |  |
| 13 | CI job that configures (and ideally compiles) on Linux | **Done** |  |
| 14 | `CMakePresets.json` so the dev box and CI stop configuring differently | **Done** |  |
| 15 | Release workflow: a tag produces a downloadable build | **Done** |  |
| 16 | A setup check that names the missing prerequisite instead of failing inside CMake | **Done** |  |
| 7 | Logs + crash dumps in `%LOCALAPPDATA%` (or equivalent) | **Ready** | |
| 6 | Startup splash / loading that does not hitch the first frame silently | Later | A first-frame hitch worth hiding (many assets, or Assets #14 async load). Current load is tiny. |
| 17 | Non-Windows packaging (AppImage, .app bundle) | Later | Platform #9 a non-Windows platform package. |
| 18 | Symbol archiving, so a shipped crash dump can still be read | **Ready** | |
| 11 | Code signing | Far |  |
| 19 | Delta patching for updates | Far | Build #8 an installer to patch. |
| 12 | Store SDKs (Steam, etc.) | Far |  |

---

## 19. Jobs and threading

*1 done · 0 ready · 7 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | One DXC worker thread | **Done** |  |
| 2 | Small job pool when **two** systems hitch | Later | The **trigger**: two systems hitching. DXC already has a worker; one worker until then. |
| 5 | Parallel-for over a range, on top of the pool | Later | Jobs #2 pool. |
| 6 | Task graph: jobs with declared dependencies | Later | Jobs #5 parallel-for, and a workload with real ordering. |
| 3 | Renderer does not own threads; it submits | Later | Jobs #2 pool, so there is somewhere to submit to. Today the renderer is single-threaded submit — enforce the rule when that changes. |
| 7 | Parallel command-list recording (one list per thread, one submit) | Later | Jobs #6 task graph, and RHI support for more than one open list. |
| 4 | Fiber job stealing | Far | Jobs #6 task graph proving the simple pool is the wall. |

---

## 20. Debug and profiling

*4 done · 2 ready · 11 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | F3 CPU/GPU ms, F4 world AABBs, F5 AA mode | **Done** |  |
| 2 | D3D12 debug layer + `ENGINE_GPU_DEBUG` | **Done** |  |
| 3 | `--gates` as CI-shaped truth | **Done** |  |
| 4 | Named GPU PIX-style markers on the RHI | **Done** |  |
| 9 | Screenshot command that writes a PNG from the swapchain | **Ready** |  |
| 10 | Stat groups exported as CSV, so a frame-time regression is a diff | **Ready** |  |
| 6 | Memory watermarks (CPU + GPU) in overlay | Later | Foundation #12 tracked allocators, which is what would report the bytes. |
| 5 | Dump render graph to text/dot | Later | Graph complexity `--gates` cannot explain. The standard frame is already 20 passes, so this is close. |
| 7 | In-engine console | Later | UI #2 fonts and Platform #4 text input. `--set` covers cvars from the command line today. |
| 11 | Frame capture and deterministic replay of one recorded frame | Far | Foundation #16 deterministic math. |
| 8 | Remote telemetry | Far |  |

---

## 21. Localization, text, accessibility

*1 done · 0 ready · 8 rows.*

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | UTF-8 strings everywhere (already C++ side) | **Done** |  |
| 2 | Font rendering (see UI) | Later | UI #2 font atlas. |
| 3 | String tables / loc ids | Later | UI #6 text layout, so a translated string has somewhere to go. |
| 5 | Font fallback and a CJK-capable atlas | Later | Localization #3 string tables. |
| 6 | Plural and gender rules in the string layer | Later | Localization #3 string tables. |
| 7 | A gate that fails on a missing or unused loc id | Later | Localization #3 string tables. |
| 8 | Right-to-left text shaping | Far | Localization #5 font fallback. |
| 4 | Subtitles, colorblind palettes, UI scale | Far | UI #4 menus to host the settings. |

---

## Cross-cutting rules (never drop)

- Renderer does not include D3D12 or Vulkan headers.
- Dependencies only downward.
- One production GPU backend until a second is a **package**.
- Do not copy ECS + fibers + deferred+SSAO+editor in one session.
- Engine ≠ editor. Demo games are C++. No in-engine inspector.
- If the debug layer yells, that is the only task.
