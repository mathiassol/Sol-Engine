# Sol Engine roadmap

**Decision log.** Every shipped feature has a Why / Choice / Gate (met) /
Do-not entry here. This file records *why the tree looks the way it does* — it
is not the work list. The backlog is [ENGINE_MAP.md](ENGINE_MAP.md): pick one
**Ready** row. There is no dashboard or canvas; these two files are read
directly.

Last updated: 31 Aug 2026.

---

## What this engine is

A **general-purpose game engine** — the same product category as Unity, Godot,
and Unreal — written in C++20, owned by you. The long-term shape is a runtime
you can ship games on: window and input, content from disk, a scene you can
edit, a renderer you can extend, tools, and (when the RHI contract is ready)
more than one platform/GPU backend.

The sandbox is the **proving ground**, not the product. A lit scene with
`--gates` is how we prove a layer. It is not a reason to stop adding engine
systems.

**How we avoid the last two failures:**

1. **Stability.** RAII, explicit ownership, a heartbeat loop, D3D12 debug
   layer, and a gate you can run. Unpredictable lifetime and “it works on my
   machine” are how previous engines died.
2. **Architecture you can rip.** Interface packages vs implementations,
   dependencies only downward, renderer never includes a graphics API.
   Replacing the renderer approach, adding a pass, or adding Vulkan later
   should be a package-sized job, not a whole-tree rewrite. That modularity
   is designed **now**, even when only D3D12 is implemented.

Stability is the *method*. A full engine is the *goal*. Those are not opposites.

Copy Unity/Godot/Unreal’s **shape** (modules, a loop, a scene, a renderer
behind an RHI, tools). Do not copy their **org chart** onto an empty tree
(ECS + fibers + deferred+SSAO+editor on day one). That is how small engines
become untestable — not because those systems are forbidden forever.

Written philosophy already matches this: [Philosophy.md](../Philosophy.md),
[Scaffold.md](../Scaffold.md), [packageRules.md](packageRules.md).

---

## Audit — foundation today (after phase 14)

Measured 2 Sep 2026: **32,767 lines** of C++/HLSL in **174 files**, **27
packages** (engine sources; the ~2 MB of vendored Vulkan headers and volk under
`packages/rhi-vulkan/third_party/` are **not** counted, the same way `cgltf.h`
is not — so a vendor drop does not move this figure). `sandbox` is still the
largest at 10,997 — 34% — but it is no longer one file: `main.cpp` is 1,676
lines, the seven `gates/gates_*.cpp` hold the 86 declared gates, and its
`content/shaders/*.hlsl` (1,297 lines in 23 files) counts here too.
**`rhi-vulkan` is 3,843 — 12%, and has overtaken `rhi-d3d12`'s 3,433** after
parity, which is the honest cost of a second backend that presents and runs the
whole gate suite rather than one that only renders offscreen; `renderer` is
3,087 (9%); `core` is 1,436; `physics-cpu` is 1,435. `game.exe` reuses sandbox
sources (install layout, no extra .cpp).

Every per-package figure above was recounted on 31 Aug and every one had
drifted — the slot claimed `rhi-d3d12` 2,589 against an actual 2,998 and
`sandbox` 6,220 against 7,089. The "down from 13%" claim it carried for
`rhi-d3d12` was wrong in the same pass: the share is 13%, not 11%. Recount this
slot; do not trust it.

The per-package figures above are **not** machine-checked — only the total,
file count and package count are, by the `roadmap-audit` invariant. That is
why they drift, and why the paragraph above records what each one actually was.

Roughly 3,000 of `sandbox`'s lines are the gate suite itself, which is compiled
into `game.exe` too — the player binary carries the tests.

The previous figure in this slot (16,078 / 124) had gone stale by more than one
row — it predated several shipped features, not just cvars. Recount with the
command in the `ship-feature` skill rather than adjusting it by hand.

| Layer | What is real | What is missing for a general engine |
|-------|----------------|--------------------------------------|
| Loop | Phased `Engine::run`, frame arena, F3, `--gates`, async DXC worker, **cvars** (`config.cfg` + `--set`), **file logger** (`<exe_dir>/logs`, rotated, flushed per line) | No gameplay beyond fly camera + Z/X; no crash dump |
| GPU | D3D12 RHI, 3-frame flight, mips, **SamplerDesc**, **compute PSO + dispatch**, **cube / array textures**, **GGX PBR**, **16-tap Vogel PCF**, **split-sum IBL**, **Karis bloom**, **Karis TAA** (optional F5, default Off, exclusive with SMAA 1x / FXAA), **motion vectors** (RGBA16 UV, object+camera), RGBA16 + ACES, SM 6.0 | UAV textures, BC7 |
| Graph | Declared reads/writes, transients, **standard frame in renderer** | max 4 refs; no compute **passes** in the graph yet |
| Scene | Names, hierarchy, `solscene` save/load, prefab extract/instantiate | Streaming, ECS |
| Content | Mounts, OBJ, glTF (one primitive + metal/rough factors), PNG, shader disk cache, `SOLC` cooker, `SOLP` pak next to the exe | Skins |
| Physics | Overlap, SI bodies, Y-up capsule, sensor enter/exit, closest-hit rays | Angular/OBB, Jolt |
| Debug | F3 overlay, F4 AABBs, F5 AA, **PIX command-list events** | No editor, no graph dump as a product UI |
| Swap | Live passes + extract in `renderer`; `ShaderTarget` enum; samplers + **working** D3D12 compute; cubes on `TextureDesc` | SPIR-V compiler; no `rhi-vulkan` yet |

**Phases 0–14 are foundation, a swappable live frame, materials, and an RHI
contract a second backend can target.** Audio #1–#2 and Physics #1–#5
(overlap + bodies + capsule + triggers + rays) are **Done**. Next work is
**one Ready row** from [ENGINE_MAP.md](ENGINE_MAP.md) (UI, skins, particles,
actions, logger, …).
Picking rules: [PICKING.md](PICKING.md).

---

## How to use this file

Phases 0–14 are all **Done**. The numbered-phase model is finished; work is now
picked from the map.

1. Pick **one Ready row** from [ENGINE_MAP.md](ENGINE_MAP.md). Picking rules:
   [PICKING.md](PICKING.md).
2. Finish its **gate** (`--gates` and/or a visible sandbox check with
   `ENGINE_GPU_DEBUG=1`).
3. Add its Why / Choice / Gate (met) / Do-not entry here, flip the row to
   **Done** in ENGINE_MAP.md, and recount **line counts**
   (`packages/**/*.{cpp,hpp,h,hlsl,hlsli}`, exclude `build/` and `third_party/`).
   `/ship-feature` does all of this.
4. Do not start the next row in the same session unless the gate is green.

**Stability rules (never drop these):** debug-layer-clean GPU changes, RAII,
no hidden globals, renderer does not include D3D12/Vulkan headers.

**Not a never-list:** Vulkan, audio, physics, PBR, deferred, TAA. An **editor
is a separate app** (far), not an engine package. Those are **on the engine
map**. Implement one production backend first; grow the **interface** before
the second backend. Add systems behind packages.

---

## Sequence

| Phase | Name | Status | Gate (one sentence) |
|-------|------|--------|---------------------|
| 0–4 | Foundation | **Done** | Sandbox runs, textured huskies, resize, F3, `--gates` |
| 5 | See the world | **Done** | F4 boxes, floor, aspect-correct camera |
| 6 | Light | **Done** | Sun + ambient + point slots; Lambert |
| 7 | Shadow | **Done** | 1024² sun depth; comparison sample |
| 8 | HDR | **Done** | RGBA16 scene_color + ACES tonemap |
| 9 | Real DXC | **Done** | dxcompiler SM 6.0 / DXIL, cache hits |
| 10 | Content | **Done** | Mips + one glTF through mounts |
| 11 | Scale | **Done** | Async DXC; 64 instances + frustum skip |
| 12 | Make swap real | **Done** | Adding a pass does not mean editing the sandbox blob; extract + graph live in renderer |
| 13 | Materials | **Done** | Shading reads a material, not a hardcoded albedo slot |
| 14 | RHI contract | **Done** | `ICommandList` can express samplers + compute; shader desc is not DXIL-only |
| **15+** | **Engine + graphics** | **Next** | Any Ready row. TAA is Done. CSM waits on a larger scene. No in-engine editor. |

---

## Phase 12 — Make swap real (done)

**Why:** The package diagram said the renderer was swappable while
`sandbox/src/main.cpp` owned the live passes and extract.

**Gate (met):** `setup_standard_frame` registers shadow → forward → sky →
tonemap → debug → overlay. `extract_visible` does frustum skip and sun bounds. Sandbox
copies `World` into `ExtractInstance` only. `--gates` logs `Swap gate: standard
frame owned by renderer`. GPU debug clean. Renderer does not include `scene`.

**Swap test:** a new engine pass is `add_pass` in
`packages/renderer/src/standard_frame.cpp`.

---

## Phase 13 — Materials (done)

**Why:** Shading was `forward.hlsl` plus an albedo index on `Instance`. glTF
already had metal/rough factors; we ignored them.

**Gate (met):** `World` holds up to 16 `Material`s. `Instance.material` is a
handle (albedo + metallic + roughness). Extract copies metal/rough into
`DrawItem`; `FrameConstants.material_params` feeds the forward shader.
Changing roughness on a material changes extract data without rewriting HLSL
from C++. glTF loads `metallic_factor` / `roughness_factor`. `--gates` logs
`Material gate: materials=5 handles=yes roughness_is_data=yes`. GPU debug
clean.

**Do not (still):** a full node material graph, bindless mega-heap.

---

## Phase 14 — RHI contract (done)

**Why:** The command list could not express compute or sampler objects. The
comparison sampler was a D3D12-only implicit. Shader compile was a DXC
profile string with no target enum.

**Gate (met):** `SamplerDesc` on `GraphicsPipelineDesc` (forward: linear +
comparison; tonemap: linear). `IDevice::create_sampler` returns a sampler
object. `ICommandList` has `set_sampler`, `set_compute_pipeline`, and
`dispatch`. D3D12 `create_compute_pipeline` returns null and logs. Shader
compile desc has `ShaderTarget` (DXIL vs SPIR-V); DXC rejects SPIR-V.
`--gates` logs `RHI contract gate: sampler=yes compute_stub=yes spirv_rejected=yes`.
GPU debug clean. **No `rhi-vulkan` package.**

**Do not (still):** two production backends at once. One backend stays hard.

---

## Renderer #8 — PBR BRDF (done)

**Why:** Forward was Lambert plus a small Blinn term. Materials already stored
metallic/roughness; shading did not use a Cook-Torrance BRDF.

**Gate (met):** Filament/glTF GGX: α = roughness², Smith-GGX height-correlated V,
Schlick F, Lambert/π diffuse with (1−F)(1−metallic). Same BRDF on sun and point
lights. `--gates` logs `PBR gate: ggx_peak=yes metal_f0=yes points_spec=yes
energy=kd_1-F alpha=r^2 (pass)`. CPU (`pbr.hpp`) stays in sync with
`forward.hlsl`. Indirect lighting is split-sum IBL (map row #9, **Done**).

**Do not (still):** Disney extra lobes, multi-scatter compensation.

---

## Build #1–#3 — `game.exe` Release (done)

**Why:** The sandbox is a Debug proving ground. A player should run an optimized
binary with content next to the exe, not a repo checkout.

**Gate (met):** CMake target `game` (option `ENGINE_BUILD_GAME`) is distinct from
`sandbox`. POST_BUILD copies `content/` and `debug/` plus DXC DLLs next to
`game.exe`. Release: `/O2`, NDEBUG, no D3D12 debug layer even if
`ENGINE_GPU_DEBUG=1`. `--gates` on `game.exe` logs
`Build gate: target=game layout=install (pass)` and `Game gates passed`.
`cmake --install` produces the same layout. Sandbox still uses the repo tree.

**Do not (still):** installer, `%LOCALAPPDATA%` logs. Identity, `content.pak`,
and DXC DLLs next to the exe are Done.

---

## Build #4 — game identity (done)

**Why:** Explorer and the taskbar should show a game, not a nameless
console-adjacent tool.

**Gate (met):** Both runtime exes embed `VERSIONINFO` from CMake `0.1.0`.
`game.exe` also embeds `sol.ico` (`IDI_APP_ICON` 101) and titles the window
`Sol`. Sandbox stays `Engine Sandbox` with no branded icon. `--gates` logs
`Identity gate: title=... icon=yes|no version=0.1.0 (pass)`.

**Do not (still):** store art, code signing.

---

## Build #5 — cooked pack next to the exe (done)

**Why:** A player binary should ship engine-native blobs, not a folder of
glTF/PNG sources as the only pack.

**Gate (met):** Host `cook.exe` writes `build/cooked/content.pak` (`SOLC`
cube, husky, albedo, beep). POST_BUILD copies it next to `sandbox.exe` and
`game.exe`. `--gates` logs `Pack gate: file=yes peek=yes get=yes (pass)`.
Sandbox still mounts loose files for the live draw.

**Do not (still):** zip/installer, loading the forward pass from SOLC.

---

## Build #9 — GPU baseline and required DLLs (done)

**Why:** A player PC has no Visual Studio and no Windows SDK on PATH. DXC must
travel with the exe. Agility is only worth shipping when the OS D3D12 runtime
cannot do what we compile.

**Gate (met):** POST_BUILD and `cmake --install` copy `dxcompiler.dll` and
`dxil.dll` next to the runtime exes; missing DXC is a configure error.
Device create requires Feature Level 11_0 and Shader Model 6.0 on inbox OS
D3D12. No `D3D12Core.dll`. `--gates` logs
`Ship gate: dxc=yes dxil=yes agility=os sm=6.0 fl=11_0 (pass)`.
Player text: [GPU_BASELINE.md](GPU_BASELINE.md).

**Do not (still):** Agility SDK, app-local `vcruntime`, zip/installer.

---

## Debug #4 — named GPU markers (done)

**Why:** Resource `SetName` labels objects in PIX. The GPU *timeline* needs
command-list events so a capture shows `shadow`, `forward`, bloom mips, and
the rest as nested ranges.

**Gate (met):** `ICommandList::begin_event` / `end_event` / `set_marker` map to
D3D12 PIX ANSI events (no WinPixEventRuntime). `RenderGraph::execute` wraps
every executed pass. `--gates` logs
`Pix gate: begin=yes nest=yes marker=yes depth=0 (pass)`.

**Do not (still):** graph dump (Debug #5), shipping a PIX capture DLL.

---

## Renderer #10 — PCF (done)

**Why:** Sun shadows were a single `SampleCmp` tap (hardware 2×2 only). Edges
looked like stamps.

**Gate (met):** 16-tap Vogel disk, per-pixel Jimenez IGN rotation, 3 texel
radius, comparison sampler still LINEAR. CPU (`pcf.hpp`) stays in sync with
`forward.hlsl`. `--gates` logs
`PCF gate: vogel=yes ign=yes edge_filter=yes taps=16 (pass)`. GPU debug clean.
FrameConstants still 400 bytes.

**Do not (still):** PCSS / DPCF blocker search, VSM, extra shadow maps.

---

## Renderer #9 — Image-based lighting (done)

**Why:** Punctual PBR still used a flat RGB ambient. Dielectrics had no
environment specular; metals looked matte unless a light hit them. Copying
Filament’s 30000 lux default would blow this unitless RGB scene (sun ~4.8,
ACES). Intensity is **1**. IBL has **no sun disk** (the directional sun stays
separate; the visible disk is skybox-only). Dielectric F0 stays **0.04**.

**Gate (met):** Karis / UE / glTF split-sum: cosine irradiance cube, GGX
prefiltered cube (lod = perceptual roughness × 4), BRDF LUT. `--gates` logs
`IBL gate: intensity=1 split_sum=yes lut=yes cubes=yes (pass)`. GPU debug
clean. FrameConstants still 400 bytes. World skybox is World #1 (**Done**).

**Do not (still):** Filament lux, Disney extra lobes, multi-scatter
compensation, HDRI/EXR loader, cooked IBL pak.

---

## World #1 — Sky (done)

**Why:** IBL lights the meshes; the backdrop was still a clear color. A
visible sky that does not share IBL source radiance, or that draws after
ACES, or that samples GGX mips, is how skies and reflections disagree.

**Gate (met):** Fullscreen triangle in HDR `scene_color` after forward, before
tonemap. Sharp **source cubemap** SRV (128², same `ibl::sky_radiance` bake,
not GGX mips). Intensity 1. **Skybox-only sun disk** (Filament `showSun`);
IBL still has no disk so the directional (~4.8) is not double-counted. Clip
`z = w`, `LessEqual`, depth write off. `--gates` logs
`Sky gate: cubemap=yes source_not_ggx=yes intensity=1 sun_disk=skybox (pass)`.
GPU debug clean.

**Do not (still):** Hosek-Wilkie / Preetham, HDRI/EXR loader (swap the SRV
when that exists), a sun disk **in IBL**, a `world` package with no impl.

---

## Renderer #11 — Bloom (done)

**Why:** HDR `scene_color` + ACES without a pyramid just clips the sun. Bloom
the bright parts, not the whole husky. A threshold of 0 (HDRP
energy-conserving) would veil the scene. Adding bloom after ACES looks like an
LDR overlay. A giant Gaussian is not the modern path.

**Gate (met):** Extract + first downsample together: quadratic soft-knee
(threshold 1, knee 0.5), 13-tap Karis luma-weighted average on mip 0 (kills
HDR fireflies), then four more 13-tap downs (no Karis). 9-tap tent upsample
adds the matching down level (scatter). Composite `scene + bloom * 0.06` in
HDR *before* ACES. Five mips, graphics fullscreen passes, RGBA16 transients
that follow the swapchain (`extent_div`). `--gates` logs
`Bloom gate: karis=yes knee=0.5 intensity=hdr_add mips=5 (pass)`. GPU debug
clean. FrameConstants still 400 bytes.

**Do not (still):** UAV / compute bloom (texture UAVs still missing), dirt
lens, anamorphic, intensity 1 as an HDR add.

---

## Renderer #12 — Cheap AA (done)

**Why:** Aliasing on husky silhouettes is the cheapest remaining image
defect. TAA wants motion vectors (Renderer #13). CMAA2 wants texture UAVs.
MSAA is a hardware layer this RHI does not expose.

Modern engines keep **one** post-AA slot: Unity URP is FXAA / SMAA / TAA /
MSAA (MSAA may sit under FXAA/SMAA, never under TAA). HDRP is TAA *or* SMAA
early, FXAA only as a final pass. Godot is FXAA *or* SMAA for screen-space.
Unreal is one scalability method. Sol follows that: Off / FXAA / SMAA, never
stacked. TAA later joined the same enum; it is not a second pass on top.

**Choice:** default **SMAA 1x** — *superseded: the default is now `Off`. When
TAA joined the same enum (Renderer #14 below), the launch path became `Off` and
every AA mode moved behind F5 / `r.aa`. The rest of this entry still stands.*
(Jimenez morphological, Medium: luma threshold
0.1, search 8, Rec.709 luma). Sharper than FXAA; Unity URP/HDRP and Godot use
it as the spatial quality option. Official High AreaTex/SearchTex LUTs are
skipped — analytical weights, documented as Medium-quality 1x. **FXAA 3.11**
is the Low preset (one pass). Run on LDR *after* ACES, *before* F3/F4, so
edge detection is perceptual and debug/UI stay sharp.

**Gate (met):** exclusive mode, after tonemap, pipelines live. F5 cycles
Off → FXAA → SMAA → TAA. `--gates` logs
`AA gate: default=off exclusive=yes after_tonemap=yes fxaa=yes (pass)`.
Graph is 25 passes (TAA ping-pong after bloom; copy / FXAA / three SMAA;
one AA path executes). GPU debug
clean. FrameConstants still 400 bytes; AA has its own 16-byte CBV.

**Do not (still):** TAA stacked on SMAA, MSAA, CMAA2, FSR/DLSS, spatial AA
before tonemap or after overlay.

---

## Renderer #13 — Motion vectors (done)

**Why:** TAA (and later motion blur) need a per-pixel UV offset from last
frame. Cheap AA does not provide that. Unity URP stores `curr_uv - prev_uv`
in RG; object motion needs previous world matrices, not only camera.

**Choice:** Geometry velocity pass after opaque forward, before sky. RGBA16
transient (no RG16 on the RHI). Depth equal, no depth write. Own 256-byte
CBV so `FrameConstants` stays 400. `MotionHistory` keeps previous view-proj
and 64 instance models. First sighting is zero motion. Unjittered. Sky
pixels stay 0. Encoding is D3D UV (`ndc * (0.5, -0.5) + 0.5`).

**Gate (met):** `--gates` logs
`Motion gate: uv=yes camera=yes object=yes history=yes equal=yes pass=yes (pass)`.
Graph 25 passes. GPU debug clean.

**Do not (still):** motion blur, skins, RG16, camera-only fullscreen from depth.

---

## Renderer #14 — Karis TAA (done)

**Why:** SMAA/FXAA only look at this frame. Thin husky edges and specular
still crawl. Motion vectors are in. Karis / UE4 temporal supersampling is the
native-res quality path (not SMAA T2x, not an upscaler).

**Choice:** Exclusive enum Off / FXAA / SMAA / TAA (**default Off** — Karis
stays on F5, not the launch path). Halton(2,3)
jitter on forward, sky rays, and motion *raster* (so depth-equal coverage
matches color). Stored motion UV deltas stay unjittered. Resolve in HDR after
bloom, before ACES: unjitter the current 3×3 tent in UV, YCoCg AABB clip,
Catmull-Rom history, history weight 0.95. Two imported RGBA16 ping-pong RTs
(graph cannot read/write the same pair). F5 cycles all four. SMAA/FXAA remain
the spatial presets. Not stacked.

**Gate (met):** `--gates` logs
`TAA gate: default=off optional=yes hdr=yes jitter=yes clip=ycocg pass=yes (pass)`.
Graph 25 passes. GPU debug clean. FrameConstants still 400 bytes; TAA has its
own 48-byte CBV.

**Do not (still):** variance clip, sharpen, TAAU/FSR2/TSR, stacking on SMAA,
sky camera-velocity pass, MSAA.

---

## Audio #1–#2 — 2D one-shot + XAudio2 (done)

**Why:** The engine had no sound. Package rules forbid an empty interface
with no impl, so the `audio` contract and one Windows backend landed together.

**Choice:** XAudio2 (Windows SDK). Not WASAPI, not miniaudio — no extra
`third_party`. PCM is 16-bit mono or stereo. `create_sound` copies the
buffer. Voices retire on the main thread (`tick` after `on_update`); the
XAudio2 callback only sets an atomic.

**Gate (met):** `--gates` logs
`Audio gate: wav=yes oneshot=yes backend=xaudio2 (pass)`. Space plays an
880 Hz beep when the window is focused. `Engine::audio()` is the injection
point; sandbox / `game.exe` link `audio-xaudio2`.

**Do not (still):** 3D positional, buses/mixer, streaming music, effects.

---

## Physics #1 — overlap queries + CPU BVH (done)

**Why:** Gameplay had no collision world. Scene AABBs are for frustum/F4, not
queries. Package rules forbid an empty interface, so `physics` and
`physics-cpu` landed together.

**Choice:** Dynamic AABB tree (Catto / Box2D), fat leaves, SAH insert. AABB
and sphere shapes. Layers + masks. Not Jolt — that stays Physics #7 behind
the same `IPhysics`. Collision is not stored on `scene::Instance`.

**Gate (met):** `--gates` logs
`Physics gate: aabb=yes sphere=yes mask=yes move=yes gen=yes backend=cpu (pass)`.
`Engine::physics()` is the injection point; sandbox / `game.exe` link
`physics-cpu`.

**Do not (still):** enter/exit events, raycasts, mesh colliders,
angular/OBB, Jolt. (Rigid bodies landed as Physics #2. Capsule as Physics #3.
Triggers as Physics #4. Rays as Physics #5.)

---

## Physics #2 — rigid bodies + gravity (done)

**Why:** Overlaps had no motion. Games need gravity and contact response
before a character controller.

**Choice:** Catto sequential impulses (GDC 2005/2009, Box2D PGS) plus NGS
position correction. Semi-implicit Euler on the engine **fixed** step.
Translation only — AABB/sphere stay axis-aligned; angular waits on OBB.
Static / Dynamic / Kinematic (Jolt types). Not penalty springs, not Jolt.

**Gate (met):** `--gates` logs
`Physics body gate: gravity=yes rest=yes floor=yes sphere=yes box=yes (pass)`.
Overlap gate still passes. `Engine::fixed_update` calls `IPhysics::step`.

**Do not (still):** angular velocity, sleeping, CCD, joints,
Jolt. (Capsule landed as Physics #3. Triggers as Physics #4. Rays as Physics #5.)

---

## Physics #3 — character capsule + floor (done)

**Why:** A player collider that is a tall AABB catches on edges. Games need
a Y-up capsule (segment ⨁ radius) that can rest on a static floor before a
character controller.

**Choice:** Jolt parameterization — `radius` plus cylinder `half_height`
(caps at `±half_height`, not Unity’s total height). Contacts from Ericson
closest points (segment–point, segment–segment, segment–AABB), then the
existing SI solver. Not GJK. Rotation still frozen.

**Gate (met):** `--gates` logs
`Physics capsule gate: overlap=yes rest=yes floor=yes not_aabb=yes (pass)`.
Overlap and body gates still pass. Rest height is `floor_top + half_height +
radius`. A corner query that a tight AABB would hit, misses.

**Do not (still):** Angular / tilted capsules, Jolt. (Triggers landed as Physics #4. Rays as
Physics #5. Character `Move()` landed as Gameplay #4.)

---

## Physics #4 — triggers / enter-exit (done)

**Why:** Overlap queries are one-shot. Games need “entered the volume” and
“left the volume” without polling every body every frame.

**Choice:** `BodyDesc.sensor` still skips impulses. After `step()`, diff the
current overlapping sensor pairs against the previous set. Snapshot
`TriggerEvent` Enter/Exit (`a.id < b.id`). Poll with `trigger_events`. No
`std::function`. Stay is silent. Solids do not emit.

**Gate (met):** `--gates` logs
`Physics trigger gate: enter=yes stay=yes exit=yes mask=yes solid=yes (pass)`.
Overlap, body, and capsule gates still pass.

**Do not (still):** Stay events, listener objects, static-only pairs,
Jolt. (Rays landed as Physics #5. Character `Move()` landed as Gameplay #4.)

---

## Physics #5 — raycasts (done)

**Why:** Guns and picking need a closest-hit segment query, not another
overlap volume.

**Choice:** Walk the existing dynamic AABB tree with a Kay–Kajiya slab test,
then ray vs tight AABB / sphere / Y-up capsule (Ericson). Closest hit only.
`ignore` skips the shooter. Origin inside a shape misses that body. Sensors
are still shapes; mask them out if a gun should ignore volumes.

**Gate (met):** `--gates` logs
`Physics raycast gate: aabb=yes sphere=yes capsule=yes closest=yes mask=yes miss=yes (pass)`.
Prior physics gates still pass.

**Do not (still):** RaycastAll, shapecast, mesh colliders, picking UI, Jolt.

---

## Gameplay #4 — character controller (done)

**Why:** A capsule that rests on a floor is not a character. Games need walk,
jump, step, and slope on a kinematic body — Unity `Move()`, not a dynamic
ragdoll.

**Choice:** `packages/gameplay` (like `scene`, not a `gameplay-*` backend).
Kinematic capsule, collide-and-slide with raycasts at four heights (feet plus
the capsule spheres; no shapecast). Step offset retries the remaining XZ from a
raised pose. Slope
limit is `normal.y >= cos(limit)`. Jump is raw Space while grounded. Sandbox
**Tab** toggles walk mode so fly-cam WASD is not stolen.

**Gate (met):** `--gates` logs
`Character gate: walk=yes jump=yes step=yes slope=yes grounded=yes (pass)`.
Prior physics gates still pass. Husky 0 follows the capsule on the checker
floor.

**Do not (still):** Input actions, shapecast, land
dust, 3D audio on the capsule. (Follow / orbit / FPS landed as Gameplay #3.
XInput pad landed as Platform #3.)

---

## Gameplay #3 — follow / orbit / FPS cameras (done)

**Why:** A walker with a fly cam is still a debug view. Games need a camera
on the pawn: boom behind (follow), sphere around (orbit), eye on the capsule
(FPS).

**Choice:** `GameCamera` in `packages/gameplay`. Follow keeps a fixed height
and only yaws the boom; orbit moves the eye on a sphere so pitch changes
height; FPS is `target + eye_height` with yaw/pitch look. Snap, no spring.
Sandbox **Tab** uses the game camera while walking; **Enter** cycles modes.
Fly cam stays when walk is off.

**Gate (met):** `--gates` logs
`Camera gate: follow=yes orbit=yes fps=yes (pass)`.
Character gate still passes.

**Do not (still):** Actions, camera collision, mouse-wheel zoom,
splitscreen. (XInput pad landed as Platform #3.)

---

## Platform #3 — gamepad (done)

**Why:** A walker that only takes WASD is still keyboard-only. Games need a
pad on the pawn — analog walk, look, jump — not a second fly-cam WASD.

**Choice:** XInput 1.4, four slots, Xbox layout. `GamepadState` on
`InputState` (snapshot). Circular stick deadzone, trigger deadzone, same
button edges as keys. `platform-win32` polls; unfocused windows zero pads.
No XInput types in public headers. Sandbox walk mode: left stick wish
(analog), D-pad digital, right stick look, **A** jump, **Start** walk,
**Y** camera. Fly cam is not pad-steered.

**Gate (met):** `--gates` logs
`Gamepad gate: deadzone=yes button=yes poll=yes connected=* (pass)`.
`connected` is informational; no hardware required. Character and camera
gates still pass.

**Do not (still):** Input actions / remapping, rumble, DirectInput, using
the pad as fly-cam WASD.

---

## Assets #4 — glTF extras (done)

**Why:** One triangle primitive and albedo-only PBR is not a content path.
Games need every primitive, packed metal-rough maps, and tangent-space
normals.

**Choice:** Concatenate all triangle primitives into one `MeshData`.
`GltfLoadResult::primitives[]` keeps index ranges, URIs, and factors.
Missing MR samples as white (factors pass through); missing normal is
`(0.5, 0.5, 1)`. Forward binds 1×1 defaults on `t5`/`t6` and builds TBN from
screen-space derivatives — `VertexPN` and `SOLC` stay 32-byte verts. The
sandbox husky is still one primitive.

**Gate (met):** `--gates` logs
`glTF extras gate: prims=yes mr=yes normal=yes (pass)`.
Prior glTF / cook / PBR gates still pass.

**Do not (still):** Skins, morphs, BC7, alpha, vertex tangents, per-primitive
draws for the 63 huskies.

---

## Foundation #8 — config / cvars (done)

**Why:** Every knob was a recompile. Build #10 (fullscreen options, quality
presets) and Debug #7 (in-engine console) both named this row as the thing
they were waiting for.

**Choice:** The registry lives in `core`, layer 0, and parses config **text**
only — it never opens a file. `engine` reads the bytes through
`platform::IFileSystem` and hands the text down, so `core` keeps zero OS
dependencies and the package graph gains no edge. A self-registering `Cvar`
holds a tagged union of `bool`/`i32`/`f32`/`string`.

Precedence is one comparison: a write is accepted only when its source is at
least the current source (`Default < File < CommandLine < Code`). That is why
`Engine::init` may load `config.cfg` **after** the command line and `--set`
still wins — no future reordering of startup can silently flip precedence.

`config.cfg` is searched in two places, first existing file wins: a
discoverable repo root, then the content root. The second is what ships next
to `game.exe`; the first exists because `sandbox.exe`'s content root resolves
into `build/bin/Debug`, where a clean build would delete a developer's knobs.

`EngineConfig` keeps every field as the code-level default; a cvar overrides
one only when `source() != Default`.

**Gate (met):** `--gates` logs
`Cvar gate: count=18 registry=yes text=yes types=yes precedence=yes scope=yes args=yes file=yes missing=yes (pass)`.
Silent under `ENGINE_GPU_DEBUG=1`; `game.exe --gates` passes.

Two gates that asserted live startup state a knob may now change were made
knob-aware rather than deleted: `run_window_gate` (`windowed=` → `startup=`)
and `run_aa_gate`. Each now asserts the factory default while its knob is
untouched, or what the knob asked for once it is set — otherwise any
`config.cfg` setting `r.vsync 0` would have left `--gates` permanently red.

**Do not (still):** In-engine console (Debug #7) and options menu / quality
presets (Build #10) — this row is the registry they consume, nothing more. No
change callbacks (consumers poll), no saving cvars back to disk, no env vars
(`ENGINE_GPU_DEBUG` is read inside the D3D12 and DXC backends before any
registry exists), no `Vec3` cvars (`math` sits above `core`), no
cheat/readonly flags, and no general command-line parser.

---

## Scene #1 — instance capacity 64 -> 512 (done)

**Why:** `scene::kMaxInstances = 64` was the real ceiling on what this engine
could render - not the renderer, which is considerably further along. The demo
sat exactly on it (63 huskies + 1 floor), so `add_instance` was one call from
`std::abort()`.

**Choice:** 512, not 4096, and a fixed array rather than heap-backing. 512 is
the largest value that clears every *other* ceiling with margin; 4096 crashes
`--gates` on the stack before it reaches a GPU. Heap-backing `World` fixes only
the stack problems, costs the trivially-copyable property, and removes the
compile-time bound that currently protects the untrusted `read_world` /
`instantiate_prefab` paths. It is the right move later, past ~4096.

The cap was never one number. Four things moved with it:

- `kHuskyCount` was literally `kMaxInstances - 1` - the demo scene size *was*
  the cap, so raising one spawned 4095 huskies. Now a literal 63.
- `motion::kHistorySlots` stayed at 64 and is indexed by scene instance id.
  Past it, instances silently get `prev_model == model`: no crash, no warning,
  just TAA reprojecting them wrongly. Now 512 and `static_assert`ed against
  `kMaxInstances` in `world_extract.cpp`, beside the same guard the point-light
  counts already had.
- `ExtractInstance storage[kMaxInstances]{}` was a brace-initialized stack
  array in a function called every frame - 86 KiB memset per frame at 512, and
  a silent stack overflow somewhere past 3,000. Now arena-allocated, which
  fails soft.
- `kMaxShaderSrvsPerFrame` was 4096. The forward pass binds 7 SRVs per drawn
  instance, so that was 581 drawn - *below* the new cap. Now 8192 (~1,167
  drawn), costing 768 KiB of descriptor heap.

**Gate (met):** `Instance capacity gate: cap=512 drawn=512 history_tracked=512/512
slots=512 arena_overflow=no (pass)`. It fills the scene to capacity, extracts
twice with movement in between, and checks every instance's `prev_model`
actually tracked - which is what catches a stale `kHistorySlots`. Watched fail
first at `history_tracked=64/512`.

**Do not (still):** this is one increment, not a scaling programme. The next
ceiling is the 1 MiB frame constant ring at ~816 drawn instances, and its
failure mode is silently dropped draws. Going past that wants instanced draws
(so per-draw constants become per-batch), and going past ~4096 additionally
wants a heap-backed `World`, a name hash table, and cached world matrices - the
name intern is O(n^2) and the parent walk is uncached. Both are invisible at
512.

---

## Stability — frame-ring budget gate (done)

Not an ENGINE_MAP row: this closes the **G3 ceiling** on Stability from the
29 Aug `/analizeMax` audit — "a foreseeable failure you can name has no gate,
test or check covering it". The named failure was frame-ring exhaustion.

**Why:** The 1 MiB per-slot frame constant ring is the tightest ceiling in the
engine by the author's own account, and nothing measured it. Exhaustion logs,
but only once it is already dropping draws — and since instanced draws a
dropped batch is a *group* of objects vanishing, not one. Worse, the argument
that the current cap is safe was pure arithmetic: Renderer #27 raised
`kMaxInstances` 64 -> 512, and the claim that this landed near 57% occupancy
came from a hand calculation nobody had checked against the hardware.

**Choice:** a `FrameRingStats` accessor on `rhi::IDevice`, plus a gate that
budgets the worst case from the real constants.

- *A new accessor, not a field on `GpuMemoryStats`.* That struct is fed by a
  DXGI adapter query about video memory; the ring is the backend's own
  bump-allocator bookkeeping. One struct fed by two sources of truth is how a
  stats accessor starts lying. Cost: one virtual on a lean interface.
- *`peak_bytes` is a high-water mark across all frames, never reset.* The ring
  itself resets every `begin_frame`, so a per-frame figure is gone before
  anything can read it.
- *The gate models the worst case rather than measuring one.* This is the part
  worth reading, because the measured version was built first and then
  deliberately abandoned — see below.

Worst case is driven by *batch* count, not instance count: per-batch constants
are 1,024 bytes across shadow + forward + motion, while an instance is only the
144-byte `InstanceData`. Batch identity includes `index_count` and every bound
texture, so a scene with as many distinct materials as objects gets one batch
per instance. That is exactly the case batching cannot help, and it is what the
budget assumes.

**Why the measured gate was abandoned — unfinished business.** The first
version drove a real 512-batch frame through the compiled graph and read the
peak. It worked, and it measured **600,832 bytes** against the model's 601,856 —
1,024 bytes apart, one alignment quantum, which is the best calibration this
model will ever get. It is not kept because executing the standard frame from
inside the gate sequence produced **1,023 D3D12 debug-layer errors**, all
`CopyDescriptorsSimple: SrcDescriptorRangeStart points to a descriptor heap
type that is CPU write only, so reading it (in this case a copy source) is
invalid`. Bisected: with the gate disabled the debug layer reports 0 messages,
so the gate causes them.

That is a real finding and it is **not diagnosed**. Either the graph has a
latent first-execute problem with descriptor staging, or there is an
undocumented ordering requirement for driving it outside `Engine::run`. Worth
knowing before anyone else tries to execute the graph from a tool or a test.

One trap found on the way, worth recording so the next person does not repeat
it: `RenderGraph::execute` owns the *whole* frame envelope — it calls
`begin_frame` and `end_frame` itself, which is why `Engine::run` never touches
them. Wrapping it in another pair opens a second frame over the first and logs
`Command allocator reset failed`.

**Gate (met):** `Frame ring budget gate: batches=512 per_batch=1024
instances=73728 fixed=3840 worst=601856/1048576 headroom=42.6% (min 15%)
exhausted=0`.

The 15% margin is the whole point: it goes red *before* the ring can run dry,
so the next capacity raise fails here instead of silently dropping draws.
Verified failing: with the ring temporarily shrunk to 680 KiB the gate reported
`headroom=13.6% (min 15%)` and exited 1 — while `exhausted=0`, i.e. it caught
the problem before any work was actually lost.

70 gates pass in Debug and Release `game`, exit 0. D3D12 debug layer 0 messages,
0 errors, 0 warnings. Invariants 10/10.

**Do-not:** Do not fold ring stats into `GpuMemoryStats`.

Do not drive `RenderGraph::execute` from a gate until the descriptor-heap errors
above are diagnosed. The measurement is attractive and the side effects are not
understood.

Do not treat the model as self-validating. It agreed with one real measurement
to within an alignment quantum, and that is the only empirical anchor it has. If
a pass starts allocating from the ring in a way the model does not know about,
the model will keep passing while being wrong — so any new ring consumer must
be added to it.

---

## Renderer #29 — exposure control (done)

**Why:** Renderer #28 applied the display encode Narkowicz's note asks for and
left the exposure multiply the same sentence asks for still missing, so the
sandbox rendered correct but over-exposed. The retune in `efb9fd6` then measured
why tuning could not substitute: cutting the sun 2.4x moved mean frame
luminance 212/255 -> 206/255, because `world.sun.color` drives only the direct
punctual term. Most of the light is the baked sky cubemap and the split-sum IBL
derived from it, both taking their magnitude from `sky_radiance()` in
`renderer/src/ibl.cpp`, neither reachable from any sandbox constant.

Three independent light magnitudes, no single knob. Exposure is the knob.

**Choice:** a cvar in EV stops, applied post-composite at each site that first
reads `scene_color`. Full reasoning in
[the design spec](superpowers/specs/2026-08-31-exposure-control-design.md).

- *EV in the app, a linear multiplier in the renderer.* `r.exposure` is stops
  because each ±1 halves or doubles and that is tunable by feel;
  `RenderSnapshot::exposure` is `2^ev` because the renderer has no business
  knowing about photographic conventions. It defaults to 1.0, so the renderer
  ships neutral and only an app changes it.
- *Post-exposure, not pre-exposure.* Scaling radiance in `forward.hlsl` and
  `sky.hlsl` would have been two shader edits with no new constant buffer and
  nothing downstream touched — materially the cheapest option, and rejected
  because `scene_color` would stop holding radiance and start holding exposed
  radiance. That bakes a camera setting into the scene representation, and has
  to be undone when auto-exposure needs to measure un-exposed luminance.
- *Not a dedicated pass.* A full-screen RGBA16 transient, ~15 MB/frame at 720p
  and a 26th pass, to do what two multiplies do.
- *Applied at the three first-read sites, never to bloom's output.* Bloom's
  first downsample reads `scene_color` and exposes it, so bloom arrives already
  exposed at both composite sites; scaling it again would square the exposure in
  the glow. The rule is "expose scene once, at first read."
- *`bloom_intensity` folded into the new tonemap CBV.* It was already duplicated
  — `bloom::kIntensity` in C++ against a literal in `tonemap.hlsl` — with
  nothing keeping them equal. The buffer had to exist for exposure regardless.

Bloom's threshold moves to the correct side of exposure as a side effect worth
naming: `kThreshold = 1.0` used to mean "brighter than 1.0 in absolute scene
radiance whatever the camera is doing", and now means "would clip on the
sensor".

The two AA paths composite bloom in *different* shaders — non-TAA in
`tonemap.hlsl`, TAA inside `taa.hlsl` — so both had to apply exposure
identically or F5 would change image brightness. The gate asserts they agree
numerically rather than leaving it to inspection.

**Gate (met):** `Exposure gate` asserts, with no GPU readback:
`ev(0,-1,1)=yes` (the EV conversion), `clamp=yes` (an absurd knob value cannot
put inf into `scene_color`), `plumbed=0.375/0.375`, `paths_agree=yes`,
`intensity=0.060`, `bloom_first=0.375 bloom_later=1.000`, and `cvar=yes`.

`plumbed` is the load-bearing one and it failed first, exactly as intended:
before `extract.cpp` copied the field it read `plumbed=1.000/0.375`. That is the
failure finding A1 of the 29 Aug audit named — a field added to three of the
four plumbing structs, silently disabling the feature — caught here by a number
rather than by someone noticing the image did not change.

69 gates pass in Debug and Release `game`, exit 0. D3D12 debug layer 0 messages,
0 errors, 0 warnings. Invariants 10/10.

**Tuning:** the shipped default is **-2.0 EV** (a 0.25x multiplier), picked by
screenshot sweep rather than derived. Measured mean frame luminance across the
sweep: 203/255 at 0 EV, 168 at -1.0, 147 at -1.5, **126 at -2.0**. At -2.0 the
sky keeps a gradient instead of clipping to white, the sun reads as a disc with
a halo rather than a blown band, and the floor holds contrast into the distance.

Worth recording about the method: tuning under `efb9fd6` was a
build-screenshot-judge loop at minutes per iteration. As a cvar it is
`--set r.exposure=<ev>` with no rebuild, and the whole four-point sweep above
ran in one pass.

**Do-not:** Do not add auto-exposure here. It needs a luminance reduction, which
wants a compute pass, and `PassKind` is `{Graphics, Copy}` — the graph cannot
express one (29 Aug audit, finding A2). It is blocked on unrelated work and is a
separate feature.

Do not put exposure on `scene::World` yet. Per-scene exposure is the right home
if scenes ever need their own authored look, but it means designing the
`.solscene` format and extending the round-trip gate for a value nobody authors.

Do not retune `sun` or `ambient` again. Exposure is now the knob for overall
brightness; that is the point of it. The values from `efb9fd6` stay.

Do not scale bloom by exposure at a composite site. It is already exposed. The
gate's `bloom_first` / `bloom_later` pair exists to catch a second application.

---

## Renderer #28 — colour space: sRGB in, sRGB out (done)

**Why:** The engine ran a physically-based lighting model in the wrong colour
space at both ends, and nothing detected it. Albedo was created as
`RGBA8_UNORM` and sampled straight into linear PBR maths, so every diffuse and
Fresnel term was computed on sRGB-encoded values — sRGB 0.5 is linear 0.214, so
midtones were inflated by more than a factor of two. Nothing applied a transfer
function on output either: `ldr_color` and the swapchain are `UNORM`, no shader
encoded, and the tonemap curve in use is Narkowicz's ACES fit, which is
linear-in/linear-out — its author states the gamma correction must be applied
afterwards. A third defect sat between them: the CPU mip chain box-filtered raw
encoded bytes, which is not averaging light.

The two large errors partially cancelled, which is exactly why this shipped
unnoticed from the first commit. The split-sum IBL, GGX specular, Smith
visibility and energy-conservation terms all had gates asserting the *formulas*
while being fed and consumed in the wrong space. Of 67 gates, none mentioned
colour space, and no doc in the tree mentioned sRGB or gamma at all — this was
never a decision, it was an omission. Found as finding S1 of the 29 Aug
`/analizeMax` audit, the only Critical finding in it.

**Choice:** hardware sRGB on input, explicit encode in the tonemap shaders on
output. The full reasoning is in
[the design spec](superpowers/specs/2026-08-29-colour-space-design.md); the two
rejected alternatives matter most.

- *Hardware sRGB texture format for the decode, not a shader `pow`.* The
  hardware applies the transfer function on sample **before** filtering, so
  bilinear and trilinear interpolation happen in linear space. A shader decode
  filters encoded values and then decodes the blend, which is wrong at every
  magnification and every mip transition, and costs ALU for the privilege.
- *Encode in-shader, not via an sRGB render target.* This was the close call.
  Flip-model swapchains do not accept `_SRGB` formats, so the hardware route
  means an `_SRGB` RTV over the `UNORM` buffer — and all three
  `CreateRenderTargetView` calls pass `nullptr` for the desc, so it would mean
  new RHI surface for format-aliased views. The deciding factor was not cost:
  FXAA and SMAA read `ldr_color` through an SRV, and an sRGB view would decode
  it back to linear, putting their luma edge-detection heuristics in the wrong
  space. Avoiding that needs typeless resources with two views per resource. It
  would also have silently changed how the debug lines and stats overlay look.
- *Piecewise curve, not `pow(x, 1/2.2)`.* At linear 0.001 the standard gives
  `12.92 × 0.001 = 0.01292`; a pow approximation gives 0.0195, over 50% too
  bright in exactly the range shadow detail lives. Since the input decode is
  hardware sRGB, which is piecewise, an approximate encode would leave the two
  ends of the pipeline disagreeing in the darks.
- *Mip builder moved from `rhi-d3d12` to `math`.* Not tidying: a gate cannot
  assert a static function inside a backend translation unit, so leaving it
  there would have made the central claim of this change — that mips average
  light, not bytes — untestable. It is pure pixel arithmetic with no graphics
  dependency. `rhi-d3d12` gained one `engine::math` edge, rank 3 → 1, downward.

Alpha is averaged directly in both paths. sRGB formats transform RGB only, so
gamma-correcting alpha would corrupt it, and the gate asserts that separately.

**Gate (met):** `Colour space gate` asserts four things against numbers derived
from IEC 61966-2-1 rather than from this code:
`mid=0.214041` (`srgb_to_linear(0.5)`), `toe=0.01292` (the linear segment — a
`pow` approximation gives 0.0195, so only the piecewise curve passes),
`mip_srgb=188 mip_linear=127 mip_alpha=127` (a 2×2 half-black half-white image:
averaging bytes gives 127, averaging light gives 188, and alpha stays 127), and
`hlsl=(0.5000,0.01292,0.2140,0.7000)` — the HLSL curve read back through a
compute dispatch and compared against the C++ one. That last assertion is the
load-bearing one: the curve necessarily exists twice, in `engine::math` and in
`common.hlsli`, and nothing else would stop them drifting apart.

`run_hdr_gate`'s sun threshold was re-derived as part of the retune. It used to
require `x >= 3.5, y >= 3.0, z >= 2.5`, which was not a property of HDR at all —
it was the pre-sRGB tuning written down as an assertion, and it would reject any
correctly exposed scene. It now requires every channel above 1.0, which is the
actual claim: scene radiance leaves LDR range, so RGBA16 `scene_color` and the
tonemap are doing real work.

The `Albedo PNG gate` additionally asserts `srgb=yes` — that the albedo texture
really is created `RGBA8_UNORM_SRGB`. The colour space gate cannot cover this:
it runs before any albedo exists, so without this assertion a revert of the
input half would have left every gate green. Verified by reverting the format
and watching it report `srgb=no (FAIL)`.

68 gates pass in Debug and in Release `game`, exit 0. D3D12 debug layer reports
0 messages, 0 errors, 0 warnings. The pre-existing `Mip gate: mips=12
expected=12` covers the trap that `can_mips` tested `format == RGBA8_UNORM`
exactly, which would have silently dropped the albedo's whole chain.

**Do-not:** Do not add exposure control as part of this. Narkowicz's note calls
for an exposure multiply as well as the gamma correction, but exposure is a
separate feature and colour-space correctness does not need it.

Do not retune `sun` or `ambient` *inside* this change. The image changed — that
is the point — and those constants are sandbox scene authoring, not engine
behaviour. Keeping them separate is what makes each half attributable.

**Follow-up, 31 Aug 2026.** The retune was then done as its own commit, and it
mostly did not work — worth recording, because the reason is not obvious.
Dropping the sun from 4.8/4.4/3.8 to 2.0/1.85/1.6 and ambient from 0.16 to
0.085 moved mean frame luminance from 212/255 to 206/255. `world.sun.color`
only drives the *direct punctual* term. The bulk of this scene's light is the
baked sky cubemap and the split-sum IBL derived from it, both of which get
their absolute magnitude from `sky_radiance()` in `renderer/src/ibl.cpp` and
neither of which any sandbox constant can reach.

So the scene has three independent light magnitudes and no single knob, which
is the argument for exposure control (Renderer #29) rather than more tuning.
Narkowicz's note asks for an exposure multiply *and* the gamma correction; this
work did the second and the first is still missing. The retuned constants were
kept because they are more sensible for a correct pipeline, not because they
solved the exposure.

Do not create a data texture as `RGBA8_UNORM_SRGB`. The rule is colour gets
`_SRGB`, data gets `UNORM` — metallic-roughness, normal maps, masks and LUTs are
data. No such texture is uploaded today (the glTF loader parses their URIs but
the sandbox never uploads them), so nothing enforces this yet; the next person
to add one has to get it right by reading.

The remaining known gap: the gate asserts the mip builder's output, not the
bytes actually uploaded to the GPU. Closing that needs a texture → buffer
readback path in the RHI, which does not exist. Separate work.

---

## Renderer #27 / RHI #14 — instanced draws (done)

**Why:** Scene #1 raised the instance cap to 512 and named the next ceiling in
its own Do-not: the 1 MiB frame constant ring at ~816 drawn instances, failing
by *silently dropping draws*. The cost was per drawn instance because every
object re-uploaded its own `model`, `prev_model` and material to three passes
and issued its own `draw_indexed`. 512 identical huskies cost 512 draw calls and
1,280 bytes of ring each, to say the same thing 512 times.

**Choice:** per-instance data in one `StructuredBuffer` behind a **root SRV**,
not a descriptor table and not a cbuffer array.

- *Root SRV, not a table.* A table costs a descriptor per frame per bind and
  another indirection. A root SRV is a raw GPU virtual address in the root
  signature - one `SetGraphicsRootShaderResourceView` per pass. `space1` keeps
  it clear of the material SRVs the passes already bind in `space0`.
- *StructuredBuffer, not a cbuffer array.* cbuffer arrays pack to 16-byte
  registers and cap at 64 KiB; a structured buffer packs tight (144 bytes per
  instance, no padding) and is bounded only by the ring.
- *`first_instance` in the constants, not `StartInstanceLocation`.* This is the
  portability trap. `DrawIndexedInstanced`'s `StartInstanceLocation` is **not**
  visible to `SV_InstanceID` on D3D - but Vulkan folds the equivalent into
  `gl_InstanceIndex` and Metal exposes it as a separate `[[base_instance]]`
  input. A shader written against any one of those three reads a different
  index on the other two, and the failure is objects rendering as each other,
  not a validation error. Passing the base explicitly in the pass constants
  means `sol_instance(id, instance_base)` means the same thing on every backend
  the RHI will ever grow.

Batching is **group-by-key, not run-length and not sorted**. Run-length was
tried first and bought nothing: the demo alternates albedo between neighbours,
so 33 draws produced 33 batches. Sorting was rejected because the key is made of
pointers - ordering them would make batch composition depend on allocator
addresses and any gate asserting a batch count would be flaky. Pointers are only
compared for equality; batch order is first-appearance order, which is scene
order, which is stable.

Three things moved with it:

- **One batch list for all three passes**, built in extract. Per-pass batching
  would let shadow, forward and motion group differently, and motion draws with
  `DepthTest::Equal` - geometry that does not rasterize identically to forward
  writes nothing, silently.
- **The instance array uploads once per frame**, in `RenderGraph::execute`, not
  once per `record_draws`. The ring hands out frame-lifetime memory, so three
  passes reading identical bytes should pay for them once; uploading per pass
  cost 3x144 bytes per instance and would have given back most of the ceiling.
- **Constants shrank** now that they are per batch, not per draw:
  `FrameConstants` 400 -> 336, `ShadowConstants` 128 -> 80,
  `motion::Constants` 272 -> 160.

**Gate (met):** `Instancing gate: drawn=7 batches=3 sizes=3/2/2
split_on_texture=yes coverage=yes shader_mapping=yes (pass)` - it builds a scene
that *must* split (same mesh, different albedo), checks the split happened,
checks every drawn instance appears in exactly one batch's slice, and checks
`first_instance + SV_InstanceID` addresses the right row. Plus
`Frustum gate: ... drawn=33 batches=5` and `Instance capacity gate: cap=512
drawn=512 ... arena_overflow=no`, both unchanged in what they assert.

Verified with `ENGINE_GPU_DEBUG=1`: **0 messages, 0 errors, 0 warnings**. That
number is new. The debug layer was only ever dumped when some *other* call had
already failed, so "the debug layer is silent" had never actually been measured
- and it was not silent: 314 warnings per `--gates` run, all
`Ignoring InitialState D3D12_RESOURCE_STATE_COPY_DEST`, because D3D12 ignores
the initial state of a DEFAULT-heap buffer. `~D3D12Device` now counts the info
queue by severity and logs the total whether or not anything failed.

**Do not:** this is batching, not a GPU-driven pipeline. There is no per-batch
culling (the cull is still per instance, before batching), no indirect draw, no
persistent instance buffer across frames, and no sorting - front-to-back or by
state. Batch membership depends on what survived the cull, so it cannot be
cached across frames as written. The next real ceiling is the O(n x batches)
linear scan in extract, which is invisible at 512 instances and a handful of
keys and would want a hash the moment either grows an order of magnitude.

---

## Foundation #6 — file logger (done)

**Why:** `StdoutLogger` was the only `ILogger` in the tree and `set_logger()`
was never called anywhere — the seam existed with zero consumers. Three facts
compounded into a shipped game that recorded nothing: `game.exe` is a
console-subsystem binary whose window closes with the process; `assert_fail`
logged only when handed a message, so the 56 bare `ENGINE_ASSERT` sites of 76
wrote nothing at all; and `std::abort()` discards a buffered stream. The most
common hard failure left no evidence. Found as S4 and S5 in the 31 Aug audit.

**Choice:** A `FileLogger` in `core` — portable C++, so no platform backend and
no new package edge. `<exe_dir>/logs/log.txt`, rotating the previous run to
`log.prev.txt`: bounded at two files with no pruning logic, and it survives the
case that matters, where the player relaunches after a crash and would
otherwise overwrite the evidence. Tees to stderr, so console behaviour is
unchanged. **Flushes every line** — the record worth keeping is the one written
immediately before `abort()`, and logging here is startup- and event-driven, so
the cost is nil. Header carries wall-clock UTC once; each line carries monotonic
seconds, which answers "how far did it get".

`install_file_logger()` owns the sink in a function-local static rather than
handing it back: `main()` is the process's one exception boundary and its
`catch` handlers log *after* `run_app()` returns, so a caller-owned sink would
be logged through after destruction.

Installed right after `create_platform()` — the earliest point the executable
directory is known — and the start banner moved after it so the banner is the
file's first record. **Not installed under `--gates`**: two gate runs would push
a real crash log out of both files, and gates run constantly during
development. An unwritable directory logs a warning and continues on stderr,
which is what a `C:\Program Files` install hits.

**Gate (met):** `File log gate: created=yes header=yes lines=yes rotated=yes
prev_intact=yes fresh=yes unwritable_rejected=yes (pass)`. Because gates mode
installs no sink, the gate drives `create_file_logger` directly against a temp
directory and reads every field back off disk — the same shape as
`run_pak_gate`'s synthetic pak. Verified beyond the gate by killing a live
session with no clean shutdown and confirming the log survived intact, which is
the per-line flush claim tested directly.

**Do not (still):** `%LOCALAPPDATA%` (Build #7 — `default_log_directory()` is
the single function that moves), minidumps (Foundation #7), level filtering,
per-channel files, async writing.

---

## Developer setup — the formatter settled, and one build definition (done)

**Why:** The 31 Aug audit's D1 said the tree did not conform to the
`.clang-format` it ships and nothing checked it. That was closed by relabelling
the config `DESCRIPTIVE, NOT ENFORCED` — but no editor reads comments. Visual
Studio enables ClangFormat support by default and invokes `clang-format.exe`
as you type whenever a `.clang-format` is present, and the VS Code C/C++
extension does the same on save, so the failure the finding described was still
live: one save could rewrite 92 of 123 C++ files across 1,961 sites. A research
pass on 1 Sep also measured four things the audit said it had not looked at — a
clean machine, timings, IDE integration, and CI beyond reading it — and found
five more defects.

**Choice:** `DisableFormat: true` at the root, which makes clang-format return
its input unchanged and therefore disarms both IDEs, since they drive that same
binary. Verified against clang-format 20.1.8 (the copy VS 18 ships) and 22.1.1,
byte-identical over all 142 source files, with a control proving each binary
still reformats when given an explicit style. The house style moved to
`tools/house-style.clang-format`, deliberately not named `.clang-format` so
nothing discovers it.

A bulk reformat was rejected on measurement, not taste: across seven candidate
configs, none describes this tree. The best still left 92 files and 1,945 sites
divergent, and `AlignConsecutiveShortCaseStatements` — the option most likely to
rescue the aligned `case` returns — moved 16 sites.

What *is* enforceable is `.editorconfig`, which the tree already obeyed on five
of six properties. 97 lines over the 100-column limit were wrapped by hand and
`format-hygiene` now holds all six on every push. That is the CI formatting
check Godot and bgfx have and D1's comparison said Sol lacked — over rules that
are true here, rather than over a config that cannot describe the tree.

`CMakePresets.json` replaces three configure commands that had already diverged.
`vs2026` stays the documented default; `ci-build` sets no generator, preserving
the reason the old CI comment gave. `ninja` is secondary and exists because
`CMAKE_EXPORT_COMPILE_COMMANDS` had been set since the beginning and produced
nothing — the VS generator ignores it, so clangd had no compile database.

**Gate (met):** No gate — nothing here changes runtime behaviour, and the gate
count stays at 74 in both configurations precisely because it should. The
equivalent is invariant **#14, `format-hygiene`**, whose ten rules were each
watched failing on an injected violation in a real tracked file before being
trusted, then restored byte-for-byte. `tools/probe-formatter.ps1` re-proves the
no-op on demand. Both build presets produce a `sandbox.exe` reporting 74
`(pass)` / 0 `FAIL`; `build-ninja/compile_commands.json` has 147 entries. The
`MAX_PATH` guard warns at 158 characters and is silent at 46 — its first version
sat after `project()` and never printed, because the try-compile it warns about
*is* `project()`'s compiler check, and its control test caught that.

**Do not (still):** do not bulk-reformat, and do not add `clang-format` to CI —
enforcing a config that cannot describe the tree is the trap, not the fix. Do
not make the Ninja preset the documented default; it needs an MSVC environment
the VS preset does not. Do not duplicate `.editorconfig`'s rules into
`.vscode/settings.json` — two copies of a contract is how the first one went
stale. Do not claim the 5.1 CI job covers the execution-policy failure: hosted
runners are permissive and no CI job can reproduce it.

---

## Build #14 — `CMakePresets.json` (done)

**Why:** Three configure commands existed and had already diverged. README said
`-G "Visual Studio 18 2026" -A x64`, CI's build job said `-A x64`, and the
options matrix spelled each `-D<OPT>=OFF` out again in YAML. Nothing bound them,
so a change to one was a change to one.

**Choice:** One file the CLI, Visual Studio, VS Code and CI all read. `vs2026` is
the documented default and reproduces the old README command exactly. `ci-build`
deliberately sets **no generator** — the dev box pins VS 18 2026, the runner
image ships whatever it ships, and letting CMake choose is what keeps CI working
across image updates. That reasoning predates the presets and is preserved in
both the preset's own `description` and `ci.yml`'s comment. Four option-off
presets replace the matrix's hand-written flags, so which option each switches
off is stated once.

`ninja` is a second, secondary preset, and it exists for a defect rather than for
speed: `CMAKE_EXPORT_COMPILE_COMMANDS` had been set since the beginning and
produced nothing, because the Visual Studio generator ignores it. There was no
`compile_commands.json` anywhere and clangd had nothing to read. Ninja
Multi-Config writes one with 147 entries and builds Debug in about a quarter of
the time. It needs `cl.exe` on `PATH`, which an IDE or a Developer Command Prompt
supplies and a plain shell does not — so it stays secondary and README says why.

**Gate (met):** No gate; nothing changes at runtime. Verified end to end instead:
both presets configure, both produce a `sandbox.exe` whose `--gates` reports
**74 `(pass)` / 0 `FAIL`**, all four option-off presets configure clean, and
`build-ninja/compile_commands.json` has 147 entries. `.gitignore`'s `build/`
widened to `build*/` to cover the two new binary dirs.

**Do not (still):** do not make the Ninja preset the documented default — it
needs an MSVC environment the VS preset does not. Do not give `ci-build` a
generator. Do not add a `CMakeUserPresets.json` to the repository; that file is
for a developer's own machine and is not shared.

---

## Build #16 — a setup check that names the prerequisite (done)

**Why:** Two prerequisites already failed well — a missing Windows SDK is a
`FATAL_ERROR` naming DXC, and an over-long source path warns before `project()`.
The rest failed as CMake errors about CMake. No Visual Studio gives "No
CMAKE_CXX_COMPILER could be found"; CMake under 4.2 gives "Could not create
named generator Visual Studio 18 2026". Neither names what to install.

**Choice:** `tools/check-prereqs.ps1`, run before the first configure. One line
per prerequisite — Windows build, shell, git, CMake against both its floors, the
VS 18 generator, Visual Studio's C++ workload via `vswhere`, the Windows SDK DXC
pair, source path length — found with a version, or missing with a name and a
URL. The DXC line asks the build's own search through
`cmake -P tools/report-dxc.cmake` rather than copying the path logic: a second
copy would drift, and a drifted check is worse than none, because it reports
success on a machine where configure then fails.

Not an invariant, and it cannot be one — it inspects a machine, and CI runners
have everything. CI runs it under both shells anyway, which catches
compatibility regressions: the first version threw under Windows PowerShell 5.1
alone, where a native command's stderr becomes an error record that
`$ErrorActionPreference = 'Stop'` raises, and `cmake -P` writes its result there.

**Gate (met):** No gate — it inspects a machine, not the engine. Three cases
watched failing instead: the SDK absent, CMake below 3.24, and a CMake with no
VS 18 generator. Each named the right prerequisite and exited 1, with the
baseline re-verified green afterwards.

**Do not (still):** do not split `project()` into `LANGUAGES NONE` plus
`enable_language(CXX)` to intercept "no C++ compiler" inside CMake. That moves
when `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` is set, and `engine_locate_dxc`
reads it — risking DXC discovery to duplicate a message this script already
gives. Do not duplicate the DXC search.

---

## Build #8 / #15 — a zip, and a tag that publishes it (done)

**Why:** `game.exe` imported `MSVCP140.dll`, `VCRUNTIME140.dll`,
`VCRUNTIME140_1.dll` and eight `api-ms-win-crt-*` entries. The latter are the
Universal CRT and inbox on Windows 10+; the first three are the Visual C++
redistributable and are not on a machine that has never had Visual Studio. That
is exactly what Build #8's "(no Visual Studio on the player machine)" meant, and
there was no way to hand anyone a build in any case.

**Choice:** Remove the requirement rather than ship it.
`CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded` drops all eleven imports for 0.3 MB;
`game.exe` now depends only on `ole32`, `VERSION`, `USER32`, `XINPUT1_4`,
`d3d12`, `dxgi`, `KERNEL32` and `dxcompiler.dll`. This honours Build #9's
`Do not: app-local vcruntime` rather than reversing it — no vcruntime travels at
all. Safe because the engine ships no DLLs of its own, and `dxcompiler.dll` is
reached through COM, so no CRT object crosses the boundary.

CPack wraps the layout `cmake --install` already produced and restates nothing
about what ships — the `install()` rules are the single definition. ZIP only.
`release.yml` on a `v*` tag configures with `ci-build`, builds, runs the
invariants, packs, checks the archive and publishes with `gh release create`;
`workflow_dispatch` does everything except publish, so the path can be rehearsed
without spending a tag.

**Gate (met):** No gate — packaging changes nothing at runtime.
`tools/check-shipped-zip.ps1` is the equivalent and runs before every upload: one
top-level directory, `game.exe` / `dxcompiler.dll` / `dxil.dll` /
`content.pak` / `LICENSE` / `content/` / `debug/` all present, no
`.pdb`/`.lib`/`.ilk`/`.exp`, and no redistributable import — read out of the
exe's bytes, since a PE stores imported module names as ASCII and that needs no
Visual Studio on the runner. Watched failing on a real `/MD` build and on an
archive with `dxcompiler.dll` removed. Proven the way a player meets it: the
CI-built zip, downloaded and unzipped into a fresh directory, ran
`game.exe --gates` to **74 (pass) / 0 FAIL**.

**Do not (still):** no NSIS or WiX installer while the game is a folder you
unzip — Build #19 delta patching (Far) is the row that would need one. Do not
add files to CPack instead of to an `install()` rule. Do not claim CI verifies
the gates: a hosted runner has no hardware D3D12 adapter, so a release is only as
gate-verified as the last local run on that commit.

---

## Build #13 — the tree compiles without Windows (done)

**Why:** ARCHITECTURE.md says only the backend packages touch a platform API,
and a source scan agrees — 7 of 142 files, each inside its own package. Nothing
made that true of the compiler rather than of the text.

**Choice:** Four fixes, all small. `packages/game` gated the DXC install on the
**option** `ENGINE_RHI_D3D12`, which defaults ON everywhere, instead of the
**target** `engine::rhi-d3d12`, which exists only under
`if(ENGINE_RHI_D3D12 AND WIN32)` — off Windows the `FATAL_ERROR` killed configure
before anything else ran. The `content.pak` install rule was unguarded while the
copy rule beside it was already guarded. Three includes were unguarded, and
three call sites behind them. Hot reload simply degrades (the poll site already
null-checks the watcher); the PNG decode refuses with a named error rather than
uploading a black albedo that reads as a lighting bug; the shader compiler and
everything downstream sit in one `ENGINE_HAS_D3D12` region whose `#else` logs
Fatal and returns 1 — deliberately not a gate failure, because a backend-less
build is a configuration, not a defect.

**Gate (met):** A CI job on `ubuntu-latest` with gcc. It compiled **and linked**
both `sandbox` and `game` on its first run — no source iteration was needed,
which is the strongest evidence the layering claim was already true. Eight
`-Wunused-function` warnings from functions reachable only inside the
`ENGINE_HAS_D3D12` region were closed with `[[maybe_unused]]` rather than
tolerated.

**Do not (still):** do not read a green Linux job as Linux support. Nothing runs
there — no platform backend, no RHI. Build #17 non-Windows packaging is a
separate row, and Platform #9 is now Ready because this one landed. Do not add
`-Werror` to that job in the same breath as making it pass.

---

## Build #10 — quality presets (done)

**Why:** Fullscreen was already there — `WindowMode`, `set_mode`, the `window.*`
cvars and F11. What was missing was one knob that moves several, and there were
only two real quality knobs to move: `r.aa`, and a shadow size that was a struct
field rather than a cvar.

**Choice:** `r.shadow_size` (a power of two, 256–4096) plus `r.quality` over
`custom | low | medium | high`. **`custom` is the default and means no preset**,
so the per-knob defaults stand and an existing `config.cfg` behaves exactly as
before — none of the three presets reproduces today's Off + 1024 pairing, so
making one of them the default would have changed behaviour while claiming not
to. A preset is a default, not an override: `resolve_quality` reads whether a
knob was *set* rather than writing through `Cvar::set()`, so an explicit `r.aa`
or `r.shadow_size` wins.

`run_aa_gate` went red the moment `r.quality` could move the AA mode, because it
re-derived its own expectation. Both it and the setup path now call one
`resolve_quality_from_cvars`, so the gate cannot drift from the behaviour it
checks, and its message reports startup/wanted/factory instead of a hard-coded
`default=off`.

**Gate (met):** `Quality preset gate: low=off/512 medium=fxaa/1024
high=taa/2048 custom=yes unknown=rejected explicit_aa_wins=yes
explicit_shadow_wins=yes bounds=yes (pass)` — 75 gates now. Watched failing
first by swapping two rows of the preset table: `low=BAD/2048
explicit_shadow_wins=no (FAIL)`, exit 1. The renderer's ready line now carries
the shadow size, because until it did there was no way to observe which size the
real graph used — the shadow gate runs against its own probe. Verified across
`low`, `high`, an explicit override, an unknown name and a bad size.

**Do not (still):** startup only — do not make these live without first solving
the render-graph transient rebuild. Do not persist them: `config.cfg` is read and
never written, and the cvar writer is Foundation #17. Do not add a preset knob
for something the renderer does not actually expose.

---

## RHI #24 — Vulkan parity: the whole gate suite on the second backend (done)

**Why:** RHI #12 proved the contract survives a second API, but only for the
surface one triangle needs. Every virtual past that called `not_implemented`,
which meant the interesting question — does the contract survive a *frame* —
was still unanswered. A second backend that cannot run the engine is a
demonstration, not a backend.

**Gate:** `--rhi vulkan --gates` is **96 pass / 0 FAIL / 1 skip** with the
validation layer armed and **zero messages from it**; D3D12 is unchanged at
**97 pass / 0 FAIL / 0 skip** with the debug layer reporting 0/0/0, and the
Release `game.exe` at 97/0/0. The one skip is honest and is described below.
`solengine.bat gates-vk` runs it.

And a configure with **`-DENGINE_RHI_D3D12=OFF -DENGINE_RHI_VULKAN=ON`** now
builds and runs the engine on its own: **89 pass / 0 FAIL / 1 skip**, exit 0,
validation silent. The seven fewer gates are the D3D12 halves of the parity
pairs, compiled out. That configuration was not a goal of this row and is what
found the last two defects below.

**Choice:** the counting was the point. A gate suite that runs on one backend
and not the other is not a parity claim, so the first thing built was a way to
compare the two runs gate-by-gate rather than by total.

That comparison is what found the real defect. The first Vulkan run reported
**45 pass / 2 FAIL** and looked like two small problems. It was not: the sandbox
compiled every shader to DXIL unconditionally, so all three pipeline setups
rejected the bytecode, `setup_forward_demo` returned early, and **fifty gates
never ran at all**. Nothing failed. Fifty gates were simply absent, and the pass
count does not show absence — only a diff of the two runs' gate *names* does.
Every fix below was found that way or by the validation layer, never by a red
line in the expected place.

**`IDevice::api()`, and the app maps it.** `shaders` and `rhi` are siblings —
neither may include the other — so nothing below the app can turn a
`GraphicsAPI` into a `ShaderTarget`. The mapping lives in exactly one function,
`shader_target_for(device)` in `sandbox_common.cpp`, and `debug-draw`'s two
`init` calls take the target as a parameter rather than deriving it a second
time. The alternative, threading a `ShaderTarget` through five signatures, was
rejected because every one of those sites already holds a device.

**Eight defects the second backend found, every one in code already shipped:**

*1. `mip_levels == 0` means a full chain, and the Vulkan backend read it as 1.*
Worse, it stored the unresolved `0` on the texture, so `upload_to_image` ran its
`mip < mips` loop zero times, staged zero bytes, and returned false with nothing
logged — a 2048×2048 albedo that came back null. The resolved counts now go into
the desc the texture keeps, because everything downstream reads them back off it.

*2. The gates acquire a swapchain image they never present.* `begin_frame`
acquired unconditionally, and the gates drive `begin_frame`/`submit`/`wait_idle`
dozens of times without a present — so the process held more images than the
swapchain owns (`VUID-vkAcquireNextImageKHR-surface-07783`) and every submit
waited on a semaphore nothing would signal (`VUID-vkQueueSubmit-pWaitSemaphores-03238`).
The acquire is now paired with the *present*, not with `begin_frame`: it happens
on first use of `swapchain_color()`, which only a presenting frame reaches, and
`submit()` wires up the swapchain semaphores only when the frame holds an image.

*3. Render-finished semaphores belong to the image, not the frame slot.* A frame
slot's fence says the submit finished; it says nothing about whether the present
that waited on the semaphore has. Indexed by slot, the third frame signalled a
semaphore the first frame's present had not consumed
(`VUID-vkQueueSubmit-pSignalSemaphores-00067`).

*4. volk allows one Vulkan instance per process, and this had three.*
`volkLoadInstance` rebinds *global* function pointers, so a second instance
rebinds every instance-level entry point — `vkDestroyDebugUtilsMessengerEXT`
among them — and destroying it leaves the first device's teardown calling
through a pointer into an instance that no longer exists. That was an access
violation at shutdown, in the last device destroyed, and **only** in a process
with more than one Vulkan device — which is exactly what the parity gates make.
The instance is now acquired and released by reference count. Nothing about the
crash pointed at volk: it was found by breadcrumbing the destructor, because
each step it survived narrowed the fault to the one call after it.

*5. `run_ship_gate` asserted a D3D feature level on a Vulkan device* — while
`run_vulkan_device_gate` asserted, correctly, that the same field is 0. Two
gates contradicting each other on the same backend. The feature level is now
asserted where it means something and reported `n/a` where it does not, rather
than quietly dropped: a gate that stops checking without saying so is worse than
one that fails.

*6. `run_mesh_reload_gate` passed vacuously.* Its comparison reduced to
`0 <= 0 + 8 MiB` on any device that does not report VRAM usage, and it printed
`local VRAM 0 -> 0 bytes (pass)`. An earlier audit went on to credit it for
asserting byte-identical VRAM it was not looking at. It now counts its 300
allocations — without that it cannot tell "nothing leaked" from "nothing was
allocated" — and reports a **skip** by name when the figure is unavailable.
That is the one skip in the Vulkan run.

*7. The DXC runtime copy sat in the wrong block.* `shaders-dxc` is linked by
both backends — DXC emits DXIL *and* SPIR-V from the same HLSL — but
`engine_copy_dxc_runtime` was called only from the D3D12 half of
`EngineRuntimeApp.cmake`. So a Vulkan-only build linked `dxcompiler.lib`, put no
`dxcompiler.dll` next to the exe, compiled without a warning and would not
start: `STATUS_DLL_NOT_FOUND`, `0xC0000135`, before `main()` and therefore
before any log line. The copy now follows the link. Nothing on the D3D12 path
can see this, because there the copy always happens.

*8. `--rhi` defaulted to a backend that need not exist.* Hard-coded `"d3d12"`,
so the Vulkan-only build refused to start on its only available backend with a
message that was correct and useless: *`--rhi d3d12` was not compiled into this
build. This build has: vulkan.* The default is now D3D12 where it exists and
whatever the build has otherwise. Naming a backend explicitly that this build
lacks is still fatal, and still not a fallback.

**Two gates were measuring the sandbox, not the backend.** `run_dxc_gate`
asserted `desc.target == Dxil`, which is a restatement of the sandbox's own
hard-coded choice and therefore could not fail; `run_shader_cache_gate` asserted
the cached blob was DXIL. Both now assert the bytecode matches whatever was
*asked for*, which fails in both directions and is the stronger check.

**`run_msaa_gate` and `run_storage_texture_gate` had default arguments** of
`Dxil` and `"d3d12"`, and the live-device call sites took them — so on a Vulkan
session the gate fed DXIL to a Vulkan device and reported the failure as
`[d3d12]`. The defaults are gone. A default that is right for one backend is a
trap on the second, and the depth gate printed `convention=standard` the same
way a day earlier.

**A seventeenth invariant, `shader-target`.** Every default-constructed
`ShaderCompileDesc` must name its target; a copy inherits one and is exempt. The
two offenders it would have caught were found by hand, twice, and each cost the
whole forward pass. Watched failing before it was kept.

**The GPU gate block's `#ifdef ENGINE_HAS_D3D12` was mislabelling.** Nothing in
it needs `rhi-d3d12` — `shaders-dxc` is linked from both backend blocks
precisely because DXC emits DXIL *and* SPIR-V — so a Vulkan-only configure built
a working sandbox that logged "No GPU backend compiled in" and returned 1
without ever using the device it had just created. It now names what it needs.

**Do not:** do not point volk at `vulkan_core.h` — it guards its Win32 entry
points on `VK_KHR_win32_surface`, which only `vulkan_win32.h` defines, and the
symptom is one unresolved identifier that reads like a missing extension.
`src/vulkan_headers_sol.h` gathers the platform headers so volk.h and volk.c see
the same set. Do not create a second `VkInstance`; `acquire_vulkan_instance` is
reference-counted for the reason above. Do not index the backbuffer array or the
render-finished semaphores by frame slot. Do not acquire in `begin_frame`. Do not
give a per-backend gate parameter a default. Do not let a gate report `pass` on a
figure the device does not provide — `skip`, by name, with the reason.

**Still missing, and recorded rather than hidden:** `last_gpu_time_ms()` is 0 on
Vulkan (RHI #10's blocker is a consumer, not a backend);
`gpu_memory_stats().local_usage_bytes` needs `VK_EXT_memory_budget`; and what a
Vulkan-only *player* build would ship for shaders is unsettled — the SDK's
SPIR-V `dxcompiler.dll` is not redistributable, so it wants cooked SPIR-V. That
is written down in `docs/GPU_BASELINE.md`, not asserted by `run_ship_gate`.

---

## Shaders #5 / RHI #12 — a second GPU backend (done)

**Why:** The question was whether `rhi-vulkan` could start. The 1 Sep contract
pass (#15 reversed-Z, #9 storage textures, #18 MSAA) existed to clear the way
for it — those three change the contract and are API-neutral, so each cost one
implementation then and would cost two now. With them in, the answer was yes,
and RHI #12's *Finish first* was corrected to name Shaders #5 alone: Vulkan runs
on Windows, so a non-Windows platform package was never a prerequisite.

**Choice:** Offscreen first, and five things measured before any of it was
designed. Each measurement would otherwise have been a guess that shaped the
work.

*1. The DXC this engine ships cannot emit SPIR-V.* A probe against the exact
`dxcompiler.dll` the build copies next to `sandbox.exe` answered
`SPIR-V CodeGen not available. Please recompile with -DENABLE_SPIRV_CODEGEN=ON`.
The DLL's *strings* say otherwise — `fvk-` and `SPIR-V` are both in it, because
the option table is compiled in whether the backend is or not — so the compiler
had to be asked rather than the binary. The Vulkan SDK ships a second build with
DirectX codegen disabled instead; they are two different binaries with the same
name.

*2. Two same-named DXC builds coexist in one process.* The worry was the
loader's base-name dedup silently returning the already-loaded module, which
would hand back a working compiler that quietly cannot do SPIR-V — the worst
possible shape. Measured false: Windows dedups by *resolved path*, so a
full-path `LoadLibraryExW` gets a distinct module and both compile. No rename,
no copy step.

*3. Every shader in the tree already compiles to SPIR-V.* All 23 real entry
points, first try, with disjoint register shifts. This is where a second backend
usually discovers its HLSL is not portable; here it was not a risk at all, and
knowing that before designing around it was worth the probe.

*4. Coordinate systems cost almost nothing.* Depth is [0,1] in both APIs, so
reversed-Z transferred untouched — RHI #15 needed no adjustment anywhere in the
Vulkan pipeline code. Y flips, and a negative viewport height (core since Vulkan
1.1) fixes it in the backend, so the shaders stay byte-identical.
`-fvk-invert-y` is deliberately unused: it would put the difference in the
shaders, which is the property worth protecting.

*5. The SDK ships volk and VMA*, plus the validation layer and the SPIR-V DXC.

**Offscreen, not a triangle in a window.** `--rhi vulkan` would put the sandbox
on the Vulkan device, and the sandbox immediately runs 82 gates and a render
graph needing samplers, compute, structured buffers, mips and cube maps. So a
windowed slice produces a wall of *not implemented* and cannot run the engine —
presentation buys nothing usable until parity. Worse, a gate behind a flag rots.
So the Vulkan device is stood up **inside the ordinary `--gates` run**, on the
D3D12 build, and asserts the same numbers.

**Two contract gaps filled first, on D3D12**, so the reference pixels were
established through the new API before a second backend existed to disagree with
them. `window_handle == nullptr` was an undefined state; it now means an
offscreen device, and `swapchain()` on one asserts by name rather than returning
a null object whose `present()` quietly does nothing. And `read_texture` is the
twin `read_buffer` never had — its absence is why the MSAA gate reads its target
back through a compute pass and a storage buffer, which is a lot of machinery
for four numbers.

**Vendored, not depended on.** volk plus the minimal Vulkan C headers, ~2 MB,
under the package's own `third_party/` following the `cgltf.h` precedent. volk
resolves the API at runtime from the driver's loader, so the build needs no SDK
and **CI compiles the second backend on every push**. That is the entire reason
for vendoring rather than `find_package(Vulkan)`. `vulkan.hpp` (16 MB of C++
bindings) and `vk_enum_string_helper.h` (817 KB) are not vendored.

**Gate (met):** **87 (pass) / 0 FAIL** in Debug and Release, D3D12 debug layer
**0/0/0**, Vulkan validation layer **silent**, 16/16 invariants, 27 packages.

One gate function, one shader source, two devices:

```
Backend parity gate [d3d12]:  inside=(51,153,204,255) outside=(0,0,0,255) lit=2016
Backend parity gate [vulkan]: inside=(51,153,204,255) outside=(0,0,0,255) lit=2016
Backend agreement gate: d3d12_lit=2016 vulkan_lit=2016 spread=0
```

The falsifications are the more interesting half. Removing the negative viewport
height — the Y flip, and the only place it happens — leaves `lit` at **exactly
2016**, the correct value, while both probes read the clear colour. Flipping the
shader's triangle to the other half on D3D12 moved it to 2,080, still inside the
gate's geometric window. So a coverage count is blind to a vertically inverted
image on *both* backends, and only the two mirror-probe texels catch it. That is
now measured twice, and it is why the probes are the assertion and the count is
only reported.

**What the second backend found about the contract.** This is what the pass was
for, and the counts binding model was kept precisely so the answer would be
evidence rather than prediction. It held: `uniform_buffer_count` translated into
a `VkDescriptorSetLayout` without argument, and both asymmetries the contract
calls out — storage buffers visible to every stage, samplers immutable —
survived as written. Four strains, all recorded in code:

- **`read_texture` needs the texture in `CopySrc`.** One backend tracks a
  per-image layout the copy requires; the other does not care. From the
  permissive one alone the requirement is invisible. Now stated on the
  interface.
- **A render target needs an extra creation flag to be readable back**
  (`TRANSFER_SRC`). The other backend needs none for the same operation.
- **`GpuBaseline` is D3D-shaped.** `shader_model` has an honest Vulkan answer —
  SM 6.0, what DXC compiles the SPIR-V from — but `feature_level` has no
  equivalent at all, so it reports 0 and the gate asserts that rather than
  accepting any number.
- **`gpu_memory_stats` splits.** Budget is available; usage needs
  `VK_EXT_memory_budget`, so it reports 0 rather than an estimate. A made-up
  usage figure is worse than an obviously absent one.

Two invariants were extended before the code was written, and both watched
failing: `graphics-api-isolation` now fences `vulkan/` and `volk.h` to
`rhi-vulkan`, and `rhi-vocabulary` now bans Vulkan vocabulary from the public
`rhi` headers as well as D3D12's — a header kept neutral in only one direction
drifts toward whichever backend was written second. That second check caught two
of this pass's own comments.

**Do not (still):** do not add a second way to select depth direction *or* Y
direction. Depth is `DeviceDesc::depth_convention` and nothing else; Y is the
viewport sign in `begin_render_pass` and nothing else, and `-fvk-invert-y` would
put a second one in the shaders. Do not let the register shifts in
`shaders-dxc` and the binding bases in `device_vulkan.hpp` drift — they are the
same numbers in two places, a mismatch is a shader reading the wrong descriptor
with nothing logged, and both sites carry a comment naming the other. Do not
implement a Vulkan virtual by returning silently; `not_implemented` exists so a
deliberately partial backend cannot be mistaken for a working one. Do not run
`--gates` on a hosted runner for either backend — both skip software devices on
purpose. Do not add a target check to `shader_cache_dxc.cpp`: `cache_key`
already folds `desc.target`, and a second rejection there is what made the
SPIR-V path look unimplemented after it was implemented.

**What is left, as RHI #24 (Ready):** the surface and swapchain, and every
virtual the offscreen slice leaves calling `not_implemented` — vertex and index
buffers, sampled textures and samplers, compute, structured buffers, mip chains,
cube maps, and `RenderPassInfo::resolve`. Then `--rhi vulkan` and the whole gate
suite against it. VMA is worth vendoring at that resource count; it is not at
this one. A2's bind-group model stays open, now with evidence behind the
decision rather than an expectation.

---

## RHI #15 / #9 / #18 — the contract pass before a second backend (done)

**Why:** The question was whether `rhi-vulkan` could start now. The answer was
yes, but three rows would be cheaper to do first — not because Vulkan is far
off, but because each one *changes the contract* and is API-neutral. Reversed-Z,
UAV textures and MSAA all add vocabulary to `rhi`'s public headers. Adding
vocabulary to a one-backend contract costs one implementation; adding it to a
two-backend contract costs two, plus the argument about which backend's shape
wins. So they went first, together, as one pass.

**Choice:** Three rows, in dependency order, each one an enum or a field rather
than a mechanism.

*#15 — reversed-Z.* One flag on `DeviceDesc`, not a per-pipeline knob. A depth
direction that can differ between two pipelines in the same frame is a bug
generator, and the near-plane precision argument applies to the whole device or
to none of it. The flag flips three things that must agree — the compare
function, the clear value, and the sign of the slope-scaled bias — so the
contract exposes `depth_closer()`, `depth_closer_or_equal()`,
`depth_bias_for()` and `shadow_comparison_sampler()` rather than leaving four
call sites to remember. `DepthTest` gained `Greater` and `GreaterEqual` so the
reversed comparisons are sayable at all. The trap this pass actually removed was
the baked one: D3D12 stores a clear value at *creation*, and three sites still
hard-coded `1.0f`. A half-applied reversed-Z is not a wrong picture, it is a
black one — clear to 1, compare `Greater`, and every fragment is rejected in
silence.

*#9 — UAV textures.* `TextureUsage::StorageShaderResource`, `ResourceState`'s
`UnorderedAccess` renamed to `Storage` (the D3D12 word for a state every API
has), `set_unordered_access(u32, ITexture&)`, and `TransientDesc::storage` so
the graph can create one. This is the row that finished `PassKind::Compute`:
before it, a compute pass could be *ordered* against a graph resource but could
not write one, so `Access` had no storage mapping and the enumerator carried
half a feature.

*#18 — MSAA.* `sample_count` on `TextureDesc` and on `GraphicsPipelineDesc`, and
`RenderPassInfo::resolve`. The resolve is where the two APIs genuinely disagree:
D3D12 issues `ResolveSubresource` after the pass, Vulkan hangs
`pResolveAttachments` off the subpass and never issues a call. So the contract
declares *what* — this multisampled target lands in that single-sample one when
the pass ends — and each backend picks *how*. The D3D12 side does its own
barriers around the resolve and leaves both textures in the state the graph
believes they are in, so the graph never learns the resolve happened. A
pipeline whose sample count disagrees with the target it is bound against is a
draw the runtime drops with a debug-layer line naming neither; the backend now
says both numbers at bind time instead.

Two things fell out of building the gate rather than being planned. Compute
could not read an SRV — `set_shader_resource` only ever bound through the
graphics root, which silently binds nothing from a dispatch — so the compute
twin was added alongside the UAV path #9 had already opened. And
`create_color_shader_resource_texture` passed a null clear value, which costs
the driver its fast clear and warns on every clear; it now bakes the same
opaque black the plain render-target path does, which `Color4`'s default alpha
of 1 agrees with by construction.

**Gate (met):** **82 (pass) / 0 FAIL** in Debug and Release, debug layer
**0/0/0**, 16/16 invariants. Three gates, each watched failing first or
falsified after the fact. `run_msaa_gate` asserts four things: a 4× target
reports 4 and its resolve target reports 1; a 1× pipeline bound inside a 4×
pass is diagnosed by name; the resolve lands at the destination's extent
(2,016 lit texels of 4,096, the triangle's half); and the resolved diagonal
carries **64** partial-coverage texels where the single-sample one carries
**0** — one per row the edge crosses, which is the difference multisampling
exists to make. Deleting the `ResolveSubresource` call turns it red at
`resolved=(blend=0 lit=0)`, measured.

**Do not (still):** do not add a second way to select depth direction — it is
`DeviceDesc::depth_convention` and nothing else, and a per-pipeline override
would let two passes in one frame disagree about which way is nearer. Do not
switch the standard frame to MSAA without a reason TAA cannot cover: TAA and
SMAA both ship, MSAA costs memory in proportion to the sample count, and it
does nothing for the shading aliasing the temporal path is there for. Do not
let `TextureUsage` grow into a flags enum without checking every `switch` over
it — the backend dispatches creation on that single value, and a bitmask turns
six exhaustive switches into six wrong ones. Do not read
`RenderPassInfo::resolve` as "the backend will fix my states": the D3D12 path
assumes both textures arrive as render targets, which is what a resolve
destination is in a real frame.

**Left for the second backend on purpose:** RHI #17 (DRED breadcrumbs) and #16
(PSO disk cache) are both backend-shaped — DRED is a D3D12 feature with a
Vulkan counterpart of a different shape (`VK_NV_device_diagnostic_checkpoints`),
and a PSO cache keyed on the pipeline desc has to serialise a blob whose format
each API owns. Doing either with one backend present is how the interface ends
up describing D3D12 in neutral words. The same goes for A2's binding model:
Dawn chose the Vulkan bind-group model because Vulkan → D3D12 is the cheap
translation direction, and that is the expected answer — but it stays a comment
in `resources.hpp` until there is a second backend to validate it against.

---

## Architecture — gates by domain, compute in the graph, a neutral RHI (done)

**Why:** The 31 Aug audit graded Architecture B+ and named A1 — the eight-file
pass-addition path — as "the single criterion separating this dimension from
Exemplary". A1 shipped the same day. What remained were A4 (`main.cpp` at 26% of
the engine, and growing), A2 (a D3D12 root-parameter model in a
backend-agnostic header) and A3 (compute could not be a graph pass).

**Choice:** Three, in the order their dependencies allowed.

*A4.* The plan said "change 72 gate signatures, then move them". That could not
start: `main.cpp` was one `namespace {` from line 105 to 5737, and an anonymous
namespace is translation-unit local. So the shared surface came out first
(`sandbox_common.hpp/.cpp`, 25 constants, 17 cvars, the three app types, the
pipeline-desc family), then the gates moved to `gates/gates_<domain>.cpp` by
name — assignment by name, not line range, so a private helper lands with its
callers as a decision rather than an accident. Six helpers were measured as used
from outside the region and handled individually. main.cpp: **6,189 → 1,351**.

Then a registry rather than 72 signature changes: `kGates` classifies each gate
`Cpu` or `Gpu`, with Cpu entries carrying a lambda that adapts the gate's own
signature. Same guarantee, a fraction of the churn, and the stability plan's
headless run filters on that kind instead of maintaining a second sequence.

*A3.* `PassKind::Compute`. The dependency model needed nothing — the reads and
writes arrays were already kind-agnostic — so ordering, missing-producer
detection and cycle detection covered compute the moment the enumerator existed.
Only `execute` needed a branch, which skips `begin_render_pass` and transitions
declared writes as well as reads, so the ordering model and the state model
cannot disagree.

*A2.* The counts renamed to what they mean, and a binding contract at the top of
`resources.hpp` saying what each one costs a backend. Not a bind-group redesign:
Dawn chose the Vulkan model because Vulkan → D3D12 is the cheap direction, and
that is the expected answer — but `rhi-vulkan` is Far, and designing against the
one backend that exists is how abstractions get the wrong seams.

**Gate (met):** **76 (pass) / 0 FAIL** in Debug and Release, debug layer 0/0/0.
Three new invariants' worth of machine-checking, each watched failing:
**gate-registry** (15) on five ways a gate can fall out of a sequence, and
**rhi-vocabulary** (16) on a `register space` put back in a comment. The compute
gate's first version was worthless and the fail-watch caught it — it asserted
`compile()` returned true, which was true whether or not compute participated;
it now asserts the pass is **rejected** when inserted before its producer.

**Do not (still):** do not put gates back in `main.cpp`; add a file under
`gates/` and a `kGates` entry, or invariant 15 fails. Do not raise `kMaxRefs`
without a pass that needs it — the highest in use is 3 and `add_pass` clamps.
Do not redesign the binding model to bind groups until `rhi-vulkan` exists to
validate it. Do not read "compute participates" as "compute can write a graph
texture": a declared write is ordering-only until RHI #9, which is why bloom is
still fullscreen triangles.

---

## Stability — sanitizers, fuzzing, and a crash reporter (done)

**Why:** All four of the 31 Aug audit's stability findings were already closed.
What held the dimension was one paragraph: *"Where Sol is behind every
reference: there is no sanitizer job, no fuzzing, and no crash reporter. Unity,
Unreal and Godot all run ASan/UBSan or equivalent in CI. 71 hand-written
assertions on one developer's GPU is a narrower net than any of them."* All
three, closed.

**Choice:** The Linux CI job compiled this tree and then stopped, so nothing had
ever *executed* off Windows and a sanitizer job would have instrumented code
that never ran. `--gates-cpu` runs the 37 gates `kGates` marks `Cpu`, before
`create_platform()` — the branch below it is where a build with no platform
gives up. A `std::filesystem` `IFileSystem` lives in the sandbox because it is
scaffolding, not an engine capability.

ASan and UBSan over that run, with `halt_on_error=1`: a sanitizer that reports
and carries on turns a red build green. Neither found anything in 37 gates'
worth of core, math, scene, physics-cpu, assets and the renderer's CPU maths —
which is only worth saying because the job has been watched failing on an
injected heap-buffer-overflow, named by file, line and function.

The fuzz gate is a seeded mutation loop rather than libFuzzer: no framework, it
runs everywhere the engine runs, a failure reproduces from its seed, and being
CPU-only it goes through the sanitizer job for free. A counting `ILogger` makes
"did this rejection say anything" an assertion, which is the hole the audit
named as uncovered.

The crash reporter needed a hook: `assert_fail` is in `core`,
`MiniDumpWriteDump` is a Windows API, and dependencies only point downward. Both
an access violation and a failed `ENGINE_ASSERT` now leave a dump beside the log
— the assert path matters more, because there is no `NDEBUG` guard and 76 assert
sites are live in Release.

**Gate (met):** **79 (pass) / 0 FAIL** in Debug and Release, 37 headless, 16/16
invariants. Every new check watched failing: the ASan job on a real overflow,
the fuzz gate on a reject reverted to a bare `return false` (`scene=20`), the
minidump gate on a stubbed `MiniDumpWriteDump`. And once by hand, outside any
gate: a deliberate assert in a live session left `crash-assert.dmp`, 128,478
bytes.

Three things the first attempt got wrong, all caught by running rather than
reasoning. The first headless run on Linux went red on a **build race** — a
single-config generator puts `sandbox` and `game` in the same `bin/`, so their
POST_BUILD content copies fought under `--parallel` — and on a **gate asserting
a Windows-only escape vector**, where `C:/Windows` is an ordinary directory
name and the engine was right. The fuzz gate's first version asserted pak at
zero and failed against a correct engine, 239 of 3,058: a lookup miss is a
question with a legitimate negative answer, not a refusal.

**Do not (still):** do not suppress a sanitizer finding without a written
reason. Do not let the fuzz gate become non-deterministic, and quote the seed
when it finds something. Do not read the Linux sanitizer job as covering GPU
code — none is compiled there. Do not install the crash handler under `--gates`.
Do not treat a shipped dump as readable: that needs Build #18, symbol archiving,
which this row unblocked and did not do.

---

## After 14 — engine map (next, pick with a gate)

Still one at a time. Still modular. Still `--gates`.

**Full list (21 categories, implementation order inside each):**
[ENGINE_MAP.md](ENGINE_MAP.md). Later rows include **Finish first** (what
blocks them).

Short version of what that file contains: renderer (IBL, PCF, World sky,
Bloom, cheap AA, motion vectors, and Karis TAA are done; PCSS only if
contacts still look like stamps), assets cooker (`SOLC`). 2D audio and
rigid-body physics with a Y-up capsule, sensor enter/exit, and closest-hit
rays are **done**. Ready rows are open (UI quads, skins, additive worlds,
alpha, CPU particles, 3D audio, navmesh, terrain, actions, logger,
installer). Foundation #8 cvars is **done**.
Gameplay #4 walk/jump, Gameplay #3 follow/orbit/FPS, Platform #3
XInput pad, and Assets #4 glTF extras are **done**.
Named GPU markers
are **done**. No in-engine inspector — an
editor is a separate app, far. `game.exe` Release install layout is **done**.
GPU baseline and DXC DLLs next to the exe are **done**.

**Still not “copy the org chart”:** fiber job stealing, ECS as a second scene
truth, bindless SM 6.6 before material count exists. Use arrays until types
explode. Use one worker until many systems hitch.

---

## Foundation log (phases 5–11, done)

Kept so the gates stay auditable. Do not re-open these unless a regression
shows up.

### Phase 5 — See the world (done)

**Gate (met):** `--gates` logs a non-zero husky AABB; F4 draws world boxes that
move with Z/X on husky 0; a checker floor sits under the huskies; projection
scale is `1/(aspect*tan(fov/2))` from swapchain width/height.

### Phase 6 — Light (done)

**Gate (met):** `World` holds sun + ambient + four point slots; extract copies
them onto `RenderSnapshot.lighting`; the renderer never includes `World`.
Forward HLSL at this gate was Lambert + a small Blinn term (Renderer #8 later
replaced specular with GGX Cook-Torrance). `--gates` logs a unit sun, nonzero
ambient, and at least one point light (`FrameConstants` was 384 bytes then;
400 after phase 13).

### Phase 7 — Shadow (done)

**Gate (met):** 1024² `D32` shadow map is a graph transient; depth-only sun pass
from the scene AABB; forward samples with a comparison sampler. `--gates` logs
a non-identity sun view-proj and a live shadow pipeline. GPU debug clean.

### Phase 8 — HDR (done)

**Gate (met):** `scene_color` is an RGBA16 graph transient; forward writes it;
ACES tonemap presents to the swapchain. Sun is intense (`~4.8`); `--gates`
logs a live tonemap pipeline. F3/F4 still draw on the LDR backbuffer. GPU
debug clean.

### Phase 9 — Real DXC (done)

**Gate (met):** `IDxcCompiler3` produces DXIL at SM 6.0. `--gates` logs
`dxil=yes`. Second launch is a disk-cache hit. `dxcompiler.dll` / `dxil.dll`
copy next to `sandbox.exe`. GPU debug clean.

### Phase 10 — Content (done)

**Gate (met):** Albedo uploads with `mip_levels = 0` generate a full box-filter
chain (`2048²` → 12 levels). Husky mesh is `/content/meshes/cartoon_husky.gltf`
(7132 verts, 37506 indices, albedo URI). `--gates` logs both. GPU debug clean.

### Phase 11 — Scale (done)

**Gate (met):** Hot-reload compiles on a worker thread; `--gates` logs
`Async compile gate` with poll under 16 ms while Busy, then Reloaded. World
holds 64 instances (63 huskies + floor); extract skips AABBs outside the
camera frustum (`visible=33 skipped=31` on the default camera). GPU debug
clean. One worker thread, not a fiber job graph.

---

## Agent / session checklist

After finishing a phase (or any roadmap edit):

1. Add the feature's Why / Choice / Gate (met) / Do-not entry in this file.
2. Flip the row to **Done** in [ENGINE_MAP.md](ENGINE_MAP.md), and promote any
   **Later** row whose only remaining *Finish first* was that row.
3. Recount lines: `packages` `*.cpp *.hpp *.h *.hlsl` (exclude `build/` and
   `third_party/`). Recount with the command; do not adjust by hand.
4. Set the design spec's `Status:` to `implemented`.
5. Run `sandbox --gates` with `ENGINE_GPU_DEBUG=1` when GPU code changed.
6. Do not start the next row until the gate is written as done here.

`/ship-feature` performs all six.
