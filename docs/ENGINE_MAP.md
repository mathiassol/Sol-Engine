# Engine map

Everything a general-purpose engine in the Unity / Godot / Unreal category
eventually needs. Not a sprint. Not an org chart to copy on day one.

**This file is the work list.** Every **Ready** row in the tables below is a
fair next pick; **Later** rows name what blocks them. There is no dashboard or
canvas — read this file directly.

**How to use:** pick **one Ready** item, give it a **gate** (`--gates` and/or
a visible sandbox check with `ENGINE_GPU_DEBUG=1`), put it behind a package
interface when it is a system. Live numbered sequence stays in
[ROADMAP.md](ROADMAP.md). Triggers and “do not scaffold empty packages”:
[TODO_LATER.md](TODO_LATER.md).

**Order** inside each category is **implementation order** (what you build
first so the next item is cheap). It is not a promise to finish the category
before touching another.

**Status**

| Mark | Meaning |
|------|---------|
| **Done** | In the tree; `--gates` covers it or it is the live path |
| **Ready** | Engine deps exist; this is a fair next pick. `--gates` / the sandbox is an allowed consumer — do not wait for a HUD, NPC, or menu that this row would create. |
| Later | **Finish first** names a map row that is not Done, or a real scale wall (one cascade still covers the pit). |
| Far | Valid engine work; do not start until a game actually hurts without it |

**Finish first** is filled only on Later rows. Refs look like `Renderer #8`
(same file, that category’s numbered row). Ready/Done have no blocker. If
the named dependency is already **Done**, the row is **Ready**. Never wait
on a twin Later (skins↔animation, UI↔actions, logger↔appdata) — the
**earlier** row in implementation order is Ready, the later one waits on it.
A “missing consumer” we would write as the gate is not a blocker.

When a Ready row becomes **Done**, every Later whose only remaining Finish
first is that row becomes **Ready**. That is the path until Far.

Phases 0–14 are **Done**. Next work is any **Ready** row you choose.

Read the Status column in the tables below for that list — it is the only
copy. A hand-written summary here would drift from the tables above it, so
there isn't one.

---

## 1. Foundation

Core loop, memory, math, diagnostics. Mostly shipped.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Types, assert, log, clock, frame timer, frame arena | **Done** | |
| 2 | Math: Mat4, Vec3, frustum, AABB | **Done** | |
| 3 | Phased `Engine::run` (poll → fixed → update → extract → render) | **Done** | |
| 4 | `--gates` + GPU debug layer as the stability method | **Done** | |
| 5 | CPU profiler scopes + F3 overlay slots | **Done** | |
| 6 | File logger (not only stderr) | **Ready** | |
| 7 | Crash dump / minidump on unhandled exception | Later | Foundation #6 (somewhere to write the dump). App-data can be `%LOCALAPPDATA%/Sol` until Build #7. |
| 8 | Config / cvars (named knobs without recompile) | **Done** | |
| 9 | Deterministic fixed-step gameplay clock (already sketched; prove it with a sim) | **Done** | |

---

## 2. Platform

Window, OS services, input devices. Win32 is the daily driver.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `IPlatform` + Win32 window, resize, DPI, focus | **Done** | |
| 2 | Raw keyboard + mouse | **Done** | |
| 3 | Gamepad (XInput / equivalent) | **Done** | |
| 4 | Text input / IME (chat, name fields) | Later | UI #2 fonts (a caret needs a widget, not F3). Not an inspector. |
| 5 | Multiple windows (editor + game view) | Far | A **separate** editor app (Editor #2), not an in-sandbox inspector. `IPlatform` is single-HWND today. |
| 6 | Fullscreen / borderless / vsync control on the interface | **Done** | |
| 7 | Clipboard, file dialogs | Far | A separate editor (Editor #4–#5) or a game that picks files. No in-engine inspector. |
| 8 | High-precision timer already via `Clock`; expose QPC if a system needs it | Later | A system that `Clock` is too coarse for (Audio latency, net tick). Do not wrap QPC a second time for its own sake. |
| 9 | `platform-*` non-Windows (same `IPlatform`) | Far | |

---

## 3. RHI (GPU backend)

Interface first, one production impl. Vulkan is a **package**, not a rewrite.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `IRHI` / `IDevice` / `ICommandList`, 3-frame flight, present | **Done** | |
| 2 | Graphics pipelines, CBV, SRV, debug names | **Done** | |
| 3 | Sampler **objects** on the contract; `SamplerDesc` on PSOs | **Done** | |
| 4 | Compute on the **interface** (D3D12 stub that fails loudly) | **Done** | |
| 5 | Shader target enum (DXIL vs SPIR-V) | **Done** | |
| 6 | **Implement** compute on D3D12 (PSO + dispatch that works) | **Done** | |
| 7 | Cube maps + array textures on `TextureDesc` | **Done** | |
| 8 | Dynamic sampler tables (`set_sampler` actually binds) | Later | A shader that **cannot** use static `SamplerDesc` on the PSO (many unique samplers, bindless-ish materials). Draws still use static samplers; `set_sampler` is a stub on purpose. |
| 9 | UAV textures (compute write) | Later | A compute pass that **writes a texture** (VFX #2 GPU particles, or compute bloom). Buffer UAVs already work. Extra formats (BC7) are Assets #5, not this row. |
| 10 | Timestamp queries already exist; expose them on `IRHI` if a second backend needs them | Far | RHI #12 (`rhi-vulkan`). D3D12 already timestamps for F3. |
| 11 | Mesh shaders / bindless heaps | Far | |
| 12 | `rhi-vulkan` (SPIR-V compiler + this contract; D3D12 stays daily driver) | Far | |
| 13 | Metal / console backends | Far | |
| 14 | Instanced draws + root SRV structured buffers (per-instance data) | **Done** | |

---

## 4. Shaders

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | DXC SM 6.0 / DXIL, disk cache, async hot-reload | **Done** | |
| 2 | `ShaderTarget` enum; SPIR-V rejected until a compiler exists | **Done** | |
| 3 | Shader include graph already folded into cache keys | **Done** | |
| 4 | Permutations / #defines as data (not a new .hlsl per knob) | Later | Assets #10 skins **or** Renderer #16 alpha (a second `.hlsl` path). One `forward.hlsl` with PBR constants is enough until then. |
| 5 | SPIR-V compile path (for Vulkan) | Far | RHI #12 (`rhi-vulkan`). D3D12 is the daily driver. `ShaderTarget` already rejects SPIR-V. |
| 6 | Shader reflection (auto root layout from bytecode) | Later | Enough PSOs that hand-written root layouts hurt (many materials, compute). Few pipelines today; layouts stay written by hand. |
| 7 | Node material graph (tools) | Far | |

---

## 5. Renderer

Graph-owned frame. New engine pass = `add_pass` in
`packages/renderer/src/standard_frame.cpp`. Research notes:
[reasarch/GRAPICS-RESEARCH.md](../reasarch/GRAPICS-RESEARCH.md) — treat as
features, not a school pile.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Render graph (declared reads/writes, transients, compile) | **Done** | |
| 2 | Standard frame: shadow → forward → motion → sky → bloom → TAA → tonemap → AA → debug → overlay | **Done** | |
| 3 | Extract: frustum skip, sun bounds, draw list (no `scene` include) | **Done** | |
| 4 | Forward lighting: sun + ambient + point slots | **Done** | |
| 5 | 1024² sun shadow map | **Done** | |
| 6 | HDR scene color + ACES tonemap | **Done** | |
| 7 | Materials as data (albedo + metallic + roughness) | **Done** | |
| 8 | **PBR BRDF** (GGX / Cook-Torrance on existing metal-rough) | **Done** | |
| 9 | Image-based lighting (cubemap mips + BRDF LUT split-sum) | **Done** | |
| 10 | PCF (then PCSS if contacts still look like stamps) | **Done** | |
| 11 | Bloom (HDR is already there) | **Done** | |
| 12 | Cheap AA (FXAA or SMAA) before investing in TAA | **Done** | |
| 13 | Motion vectors | **Done** | |
| 14 | TAA (optional F5; default Off) | **Done** | |
| 15 | Cascaded shadow maps (CSM) | Later | World #3 terrain (or a scene larger than one cascade). One 1024² map covers the husky pit. |
| 16 | Transparency / alpha (forward; keep out of deferred) | **Ready** | |
| 17 | More lights: clustered forward or Forward+ before a full G-buffer | Later | Prove the **4 point slots are the wall**. They are not. |
| 18 | Depth+normals (or G-buffer) **for a reason** | Later | A consumer: SSAO, SSR, or deferred. PBR/PCF/bloom do not need a G-buffer. |
| 19 | SSAO | Later | Renderer #18 depth+normals. Do not add because a paper has it. |
| 20 | Deferred lighting (when forward light count is the wall) | Later | Renderer #17 clustered still not enough, **and** #18 G-buffer. |
| 21 | SSR | Later | Renderer #18 (depth+normals or G-buffer). IBL is the usual fallback when SSR misses. |
| 22 | Contact shadows | Later | A reason screen-space contact beats one cascade. PCF is in. |
| 23 | Particles as a graph pass | Later | VFX #1 (CPU particles) designed as `add_pass`. |
| 24 | Multiple views / split-screen | Later | Platform #5 multi-window **or** split-screen gameplay, plus running the graph twice. One camera, one swapchain view. |
| 25 | Volumetric fog / SSGI / full GI | Far | |
| 26 | Virtual shadow maps, nanite-like vis, hardware RT | Far | |
| 27 | Instanced draws: batch the extract by material/mesh key, one `draw_indexed` per batch | **Done** | |
| 28 | Colour space: sRGB decode on colour textures, sRGB encode after tonemap | **Done** | |

Do not skip PBR to start deferred. Do not add SSAO because a paper has it.

Row numbers are ids, not a sort order - #27 is Done and lands after the Far
rows because renumbering would break every `Renderer #N` reference in
[ROADMAP.md](ROADMAP.md) and the specs.

---

## 6. Assets and content pipeline

CPU data separate from GPU resources. Hot reload already matters.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Virtual mounts (`/content`, `/shaders`, `/debug`) | **Done** | |
| 2 | OBJ, one-primitive glTF, PNG, mip generation | **Done** | |
| 3 | glTF metal/rough **factors** | **Done** | |
| 4 | glTF extras: more primitives, metal-rough **textures**, normal maps, **node transforms** | **Done** | |
| 5 | Texture cooker formats (BC7 etc.) | Later | PNG + generated mips work until GPU memory or pack size is the wall. Not blocked by UAV writes (RHI #9). |
| 6 | Asset database / GUID / import settings | Later | More than a handful of files. Cooker (#7) is Done; path strings on mounts are still enough. |
| 7 | Cooker: source → engine binary (meshes, textures, audio) | **Done** | |
| 8 | Pak / archive for shipping (see **Build**) | **Done** | |
| 9 | Streaming (load next cell while playing) | Later | Scene #7 additive worlds. Cooker (#7) is Done; do not parse glTF on the hitch path. |
| 10 | glTF skins / morphs (see **Animation**) | **Ready** | |
| 11 | Source control friendly `.meta` / `.import` | Far | |

---

## 7. Scene

Flat `World` today. Grow when the **coded demo** needs names, hierarchy, or a
file. Not so an inspector can live in the engine.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | `World`: instances (512), camera, sun, points, materials | **Done** | |
| 2 | Names on instances (string or interned id) | **Done** | |
| 3 | Hierarchy / parenting (transforms compose) | **Done** | |
| 4 | Save / load a scene file | **Done** | |
| 5 | Prefabs / templates | **Done** | |
| 6 | Layers / tags / visibility masks | Later | Scene #7 additive worlds (more than one view worth of instances). Physics already has layers/masks. |
| 7 | World streaming / multiple scenes additive | **Ready** | |
| 8 | ECS as a second scene truth | Far | |

Renderer never includes `scene`. Extract copies into a snapshot.

---

## 8. Gameplay, input, camera

The sandbox is a proving ground. Walk/jump, follow/orbit/FPS cameras, and
an XInput pad are in. Actions and instance data are **Ready**.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Fly camera + Z/X nudge | **Done** | |
| 2 | Input **actions** (Jump, Fire) bound to keys/gamepad | **Ready** | |
| 3 | Game camera types: follow, orbit, FPS | **Done** | |
| 4 | Character controller (walk, jump) | **Done** | |
| 5 | Time scale / pause | Later | A pause **menu** (UI #4). Physics #2 can pause; the fly-cam sandbox does not. |
| 6 | Gameplay components as data on instances (not a second ECS) | **Ready** | |
| 7 | Scripting VM (Lua/C# later); C++ first | Far | |

---

## 9. Animation

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Skinned mesh GPU path (palette or compute) | Later | Assets #10 glTF skins. Compute dispatch exists if you want a compute skin path; a vertex-shader bone palette also works. No skin data, no skin PSO. |
| 2 | Clip playback (sample tracks, loop) | Later | Animation #1 skinned draw + clip tracks in the asset. Nothing to sample onto. |
| 3 | Blend (two clips) | Later | Animation #2 one clip playing. |
| 4 | State machine / blend tree | Later | Animation #3 two-clip blend (or at least two clips). |
| 5 | Root motion | Later | Animation #2 clips. Character controller can consume the delta. |
| 6 | IK, look-at, procedural | Far | |

---

## 10. Audio

`audio` interface + `audio-xaudio2`. 2D one-shot is in; Space beeps in the sandbox.
3D positional is **Ready**.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Interface: play one-shot 2D sound | **Done** | |
| 2 | One backend (XAudio2) | **Done** | |
| 3 | 3D positional (distance, listener on camera) | **Ready** | |
| 4 | Buses / mixer / volume groups | Later | Audio #3 (two sources: 2D beep + a 3D cue). |
| 5 | Streaming music | Later | A decoder / stream of cooked PCM. Assets #7 cooker is Done; playback of short PCM is already in. |
| 6 | Effects (reverb, occlusion) | Far | |

---

## 11. Physics

Interface + simple colliders first. Middleware only if the simple layer
hurts. Overlap queries, translation-only rigid bodies, a Y-up capsule, sensor
enter/exit, and closest-hit rays are in (`physics` / `physics-cpu`). Angular/OBB
and Jolt wait.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Overlap tests (AABB / sphere) | **Done** | |
| 2 | Rigid bodies + gravity (boxes, spheres) | **Done** | |
| 3 | Character capsule + floor | **Done** | |
| 4 | Triggers / callbacks | **Done** | |
| 5 | Raycasts for guns / picking | **Done** | |
| 6 | Vehicles, cloth, destruction | Far | |
| 7 | Third-party (Jolt/PhysX) behind the same interface | Far | |

---

## 12. In-game UI

Not the editor. HUD, menus, pause.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Immediate overlay text (F3 already) | **Done** | |
| 2 | Screen-space quads + font atlas | **Ready** | |
| 3 | Layout (stack, anchor) + input focus | Later | UI #2 quads + fonts. F3 is immediate text, not widgets. |
| 4 | Menus, inventory, dialogue | Later | UI #3 layout + focus, and Gameplay #2 actions for navigate/confirm. |
| 5 | Retention-mode UI toolkit | Far | |

---

## 13. World and environment

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Sky (procedural or cubemap) | **Done** | |
| 2 | Fog (simple exp, then height) | Later | World #3 terrain (a scene with depth to fog into). Sky #1 is already in. |
| 3 | Terrain / heightmap | **Ready** | |
| 4 | Water | Later | Renderer #16 transparency. |
| 5 | Vegetation / wind | Far | |
| 6 | Weather / day-night as **data** driving sun + IBL | Far | |

---

## 14. VFX

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | CPU particles drawn as a graph pass | **Ready** | |
| 2 | GPU particles (compute) | Later | VFX #1 on CPU first. Real compute dispatch exists. |
| 3 | Decals | Later | Scene depth available to a project pass — usually Renderer #18 depth+normals. No depth copy for projection. |
| 4 | Trails, ribbons | Later | VFX #1 CPU particles (a ribbon is a spawn). |
| 5 | Post “juice” (hit flash, chromatic) | Far | |

---

## 15. AI and navigation

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Navmesh bake + path follow | **Ready** | |
| 2 | Steering / avoidance | Later | AI #1 path follow (something on a path to steer). |
| 3 | Perception (see / hear volumes) | Later | AI #1 agents. Physics #4 triggers and Physics #5 raycasts are in. |
| 4 | Behavior trees / utility AI | Far | |

---

## 16. Networking

Only when a game needs it. Separate domain.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Transport (UDP) + tick | Far | |
| 2 | Replication of instance transforms | Far | |
| 3 | Client prediction / interpolation | Far | |
| 4 | Dedicated server target | Far | |
| 5 | Steam / EOS / console online | Far | |

---

## 17. Editor (separate app)

The engine and the editor are **100% separate**. The sandbox and `game.exe`
are games written in C++ against engine APIs. That is the workflow for a
long time. An editor, if it ever exists, is **another executable** that
links the engine the way a game does — it makes engine capabilities
accessible. It is not a package inside the engine, and it is not an
in-sandbox inspector.

F3 / F4 / F5 and shader hot-reload are **engine debug** (Debug #1), not
editor UI. Do not start Editor #2 because picking a husky in the demo
would be convenient.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | Policy: engine ≠ editor. Demo games are code. No in-engine inspector. | **Done** | |
| 2 | Separate editor app (own target; uses engine APIs; not sandbox) | Far | Authoring without a compile actually hurts. Never an in-engine widget. |
| 3 | Hierarchy view in that editor | Far | Editor #2 and Scene #2 names. |
| 4 | Scene open/save in that editor | Far | Editor #2 and Scene #4. |
| 5 | Content browser in that editor | Far | Editor #2, Assets #6, Platform #7. |
| 6 | Editor window + game viewport (multi-window) | Far | Editor #2 as a real app, then Platform #5. |
| 7 | Material / shader graph UI | Far | Editor #2. |
| 8 | Animation / landscape / sequencer editors | Far | Editor #2. |

---

## 18. Build and ship a `game.exe`

The sandbox is not the product. A player should run a **game** binary with
**cooked content**, not a Debug sandbox from a repo.

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | CMake app target distinct from `sandbox` (e.g. `game`) | **Done** | |
| 2 | Release config: no GPU debug layer, NDEBUG, optimizations | **Done** | |
| 3 | Content root that works from the exe directory (already true for mounts; prove with an install-layout test) | **Done** | |
| 4 | Icon, version resource, window title as game identity | **Done** | |
| 5 | Cooked asset pack next to (or inside) the exe | **Done** | |
| 6 | Startup splash / loading that does not hitch the first frame silently | Later | A first-frame hitch worth hiding (many assets or cooker). Current load is tiny. |
| 7 | Logs + crash dumps in `%LOCALAPPDATA%` (or equivalent) | Later | Foundation #6 file logger and #7 minidump. |
| 8 | Zip / installer (no Visual Studio on the player machine) | **Ready** | |
| 9 | Ship dxcompiler/D3D12 Agility if required; document GPU baseline | **Done** | |
| 10 | Fullscreen options, quality presets | **Ready** | |
| 11 | Code signing | Far | |
| 12 | Store SDKs (Steam, etc.) | Far | |

---

## 19. Jobs and threading

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | One DXC worker thread | **Done** | |
| 2 | Small job pool when **two** systems hitch | Later | The **trigger**: two systems hitching. DXC already has a worker. One worker until then. |
| 3 | Renderer does not own threads; it submits | Later | Jobs #2 so there is a pool to submit to. Today the renderer is single-threaded submit — enforce the rule when jobs exist. |
| 4 | Fiber job stealing | Far | |

---

## 20. Debug and profiling

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | F3 CPU/GPU ms, F4 world AABBs, F5 AA mode | **Done** | |
| 2 | D3D12 debug layer + `ENGINE_GPU_DEBUG` | **Done** | |
| 3 | `--gates` as CI-shaped truth | **Done** | |
| 4 | Named GPU PIX-style markers on the RHI | **Done** | |
| 5 | Dump render graph to text/dot | Later | Graph complexity `--gates` cannot explain (many passes). The 20-pass standard frame is readable in code. |
| 6 | Memory watermarks (CPU + GPU) in overlay | Later | Allocators that can **report** (RHI resource tracking). F3 does not need bytes yet. |
| 7 | In-engine console | Later | Platform #4 text input, UI #2 fonts. Foundation #8 cvars is Done — the registry it reads (`cvar_count`/`cvar_at`) already exists. |
| 8 | Remote telemetry | Far | |

---

## 21. Localization, text, accessibility

| # | Item | Status | Finish first |
|---|------|--------|--------------|
| 1 | UTF-8 strings everywhere (already C++ side) | **Done** | |
| 2 | Font rendering (see UI) | Later | UI #2 font atlas — same work. |
| 3 | String tables / loc ids | Later | Player-facing copy: UI #4 menus. No loc-able strings in the sandbox. |
| 4 | Subtitles, colorblind palettes, UI scale | Far | |

---

## Cross-cutting rules (never drop)

- Renderer does not include D3D12 or Vulkan headers.
- Dependencies only downward.
- One production GPU backend until a second is a **package**.
- Do not copy ECS + fibers + deferred+SSAO+editor in one session.
- Engine ≠ editor. Demo games are C++. No in-engine inspector.
- If the debug layer yells, that is the only task.
