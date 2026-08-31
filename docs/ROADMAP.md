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

Measured 31 Aug 2026: **23,349 lines** of C++/HLSL in **140 files**, **26
packages** (engine sources; vendored `cgltf.h` not counted). `sandbox` is 6,220
(its `content/shaders/*.hlsl` counts here too); `renderer` is 2,626;
`rhi-d3d12` is 2,589 — 11% of the engine, down from 13% since the mip builder
moved to `math` in Renderer #28. `physics-cpu` is 1,292; `core` is 971; `math`
is 558. `game.exe` reuses sandbox sources (install layout, no extra .cpp).

The per-package figures above are **not** machine-checked — only the total,
file count and package count are, by the `roadmap-audit` invariant. The
previous set in this slot had drifted well past rounding (it claimed
`rhi-d3d12` 3,014 and `renderer` 2,867), so recount rather than trust them.

Roughly 3,000 of `sandbox`'s lines are the gate suite itself, which is compiled
into `game.exe` too — the player binary carries the tests.

The previous figure in this slot (16,078 / 124) had gone stale by more than one
row — it predated several shipped features, not just cvars. Recount with the
command in the `ship-feature` skill rather than adjusting it by hand.

| Layer | What is real | What is missing for a general engine |
|-------|----------------|--------------------------------------|
| Loop | Phased `Engine::run`, frame arena, F3, `--gates`, async DXC worker, **cvars** (`config.cfg` + `--set`) | No gameplay beyond fly camera + Z/X; no file logger |
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
Picking rules: [TODO_LATER.md](TODO_LATER.md).

---

## How to use this file

Phases 0–14 are all **Done**. The numbered-phase model is finished; work is now
picked from the map.

1. Pick **one Ready row** from [ENGINE_MAP.md](ENGINE_MAP.md). Picking rules:
   [TODO_LATER.md](TODO_LATER.md).
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
