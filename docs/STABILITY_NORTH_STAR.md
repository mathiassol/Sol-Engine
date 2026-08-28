# Stability north star

How this engine stays **correct and swappable** while it grows into a
general-purpose runtime (Unity / Godot / Unreal category). Stability is the
method. The product is still an engine — see [ROADMAP.md](ROADMAP.md).

This is not a ban on SSAO, Vulkan, an editor, or PBR. Those are on the engine
map. What *is* banned is copying Unreal or Frostbite’s **org chart** onto an
unfinished tree (ECS + fibers + deferred+SSAO+editor with no gates). That is
how previous engines became unpredictable and impossible to rip.

**Rule of the document:** every “amazing” idea must earn a trigger **and** land
behind an interface. If the trigger is imaginary, the idea is a liability. If
the idea has no package boundary, it will be un-swappable later.

Related: [Philosophy.md](../Philosophy.md), [packageRules.md](packageRules.md),
[ROADMAP.md](ROADMAP.md), [TODO_LATER.md](TODO_LATER.md),
[ARCHITECTURE.md](ARCHITECTURE.md), [FOUNDATION.md](FOUNDATION.md).

Executable sequence is [ROADMAP.md](ROADMAP.md). This file is the **why
stability and modularity**, not a “never build Unreal” essay.

---

## 1. What “amazing” means here

For this project, excellence is a **general-purpose engine that you can still
debug and replace**:

1. **Correctness that survives time.** Shutdown, resize, hot-reload, and a failed compile do not leak GPU objects, freeze the swapchain, or trip the debug layer.
2. **Obvious data flow.** Game state → extract snapshot → renderer/graph → RHI. No hidden globals, no renderer reading fly-camera, no RHI named after a demo pass.
3. **Boring, small APIs.** One responsibility per package. Dependencies only downward. Invalid states hard to represent (handles with generation, compile-time graph checks). **Ripping a package is a design requirement.**
4. **Stable frame time before peak FPS.** Eliminate stalls and wait-for-idle in the hot path; measure CPU and GPU separately; optimize only what PIX / timestamps prove.
5. **Debuggability as a product.** Named D3D12 objects, a log that does not lie, CPU+GPU ms on the overlay, asserts on programmer errors.

That matches the written philosophy: architecture before features, simplicity over cleverness, measure before optimizing, every abstraction must earn its place.

Jason Gregory’s *Game Engine Architecture* frames the runtime the same way: a **heartbeat loop** that updates subsystems, then presents. Tools and cookers are not the runtime. Memory and lifetime dominate every later decision. Concurrency exists to stop *blocking*, not to look modern.

---

## 2. The worst instability: over-engineering

Mike Acton (Insomniac / “Three Big Lies”, CppCon data-oriented design): software is a **data transform**. Code and UML have no intrinsic value. Speculative generality — abstractions for needs that are not real yet — produces systems that are slow *and* hard to debug.

Practical translation for this engine:

| Temptation | Why it feels “architectural” | Why it is instability |
|------------|------------------------------|------------------------|
| ECS now | Unreal / Unity have one | Arrays and a snapshot already feed the renderer. ECS is a second source of truth. |
| Fiber job system now | Naughty Dog GDC 2015 | They had ~800–1000 jobs/frame and SPUs. We have one cube and a hitch-free load. Fibers plus D3D12 is a heisenbug factory until log, device, and graph are explicitly main-thread. |
| Unreal-style game / render / RHI threads | “AAA architecture” | Three timelines for a window + cube. Extract snapshot on one thread is enough until recording is the bottleneck. |
| Bindless + SM 6.6 ResourceDescriptorHeap | MJP, NVIDIA: simpler at *scale* | Needs DXC, a shader-visible heap, and content volume. Per-draw root CBV is the NVIDIA-recommended *fast* path for constants today. |
| Transient aliasing / placed heaps | Frostbite FrameGraph | Pays off with many overlapping full-screen targets. One optional transient is not that problem. |
| Full asset database | O3DE / editor pipelines | Mounts + generational handles + a disk shader cache already iterate. A DB without a cooker is ceremony. |
| Second GPU API before the contract exists | “Portable from day one” | Scaffold already isolates the RHI. A second **impl** before D3D12 is production-hard doubles every bug. Grow `IDevice` / `ICommandList` **now** so Vulkan is a package later. |
| SSAO / deferred / shadows as the *first* architecture | “Looks like an engine” | Zero leverage on lifetime if the graph still lives in the sandbox. Shadows/HDR are already in; more graphics wait on renderer-owned passes. |

**Over-engineering is not caution. It is adding states you cannot test.**

The Our Machinery blogs are useful *and* dangerous: their render graph and thin RHI are the right *shape*; their plugin/ECS/fiber stack is for a product with many authors. Steal the shape, not the org chart.

---

## 3. Research notes (industry, kept small)

Sources are public talks and docs. Nothing here requires copying a studio’s internals.

### 3.1 Render graph (Frostbite 2017, Unreal RDG, Our Machinery)

Yuriy O’Donnell, GDC 2017 (*FrameGraph: Extensible Rendering Architecture in Frostbite*):

- The frame is a **DAG of passes and resources**, compiled with full knowledge of the frame.
- **Transient** resources live at most one frame. The graph allocates, transitions, and (when it matters) **aliases** memory whose lifetimes do not overlap.
- Goals: modular features, automatic barriers, simpler async compute, **visualization**.
- Aliasing needs **aliasing barriers** and metadata init (DCC/HTILE). That is a later D3D12 skill, not a first graph.

Unreal RDG (public docs / community writeups): setup → compile (cull unused, lifetimes, barriers) → execute. Game thread owns world; **proxies / views** feed the renderer. The graph does not know what an entity is.

Riccardo Loggini (2021) and later D3D12 writeups: transients are **placed resources** on a heap; aliasing is optional; compile-time lifetime is the actual invention.

**What we already have:** declared reads/writes, missing-producer and cycle compile, optional transients, auto barriers, snapshot draws, graph as the only swapchain presenter.

**What we should not add yet:** pass reordering scheduler, async compute queues, ESRAM/placed-heap aliasing, graph visualization UI, Unreal’s three-thread split.

**What we should add when the graph has three real offscreen targets:** lifetime-based allocation (create at first use, drop after last use) — still without aliasing if VRAM is fine.

### 3.2 RHI vs renderer vs extract

Industry consensus:

- **RHI:** buffers, textures, pipelines, command lists, present. No “forward pass” names.
- **Renderer / graph:** *what* the GPU does this frame.
- **Extract / snapshot / proxy:** *what* the game wants drawn. Pointers or handles valid for this frame only.

This tree already does that (`on_extract` → `RenderSnapshot` → `record_opaque_draws`). Keep it. Do not let the graph grow a scene graph.

Scene hierarchy (if it ever exists) is **CPU data**. Render graph is **GPU work**. Mixing them is the classic 2000s mistake.

### 3.3 D3D12: correctness first, then fewer barriers

Microsoft: the app owns resource states; consecutive barriers must agree; UAV/aliasing/transition are different tools; **fences** order queues and CPU/GPU, barriers do not replace fences.

NVIDIA *Advanced API Performance: Barriers*:

- Minimize barriers and fences; they kill parallelism.
- Batch `ResourceBarrier` calls; use the **minimum** state flags.
- Split barriers later; do not sprinkle wait-for-idle.
- Root **CBV** is cheaper than a CBV descriptor table for per-draw constants.

NVIDIA *Descriptors*: prefer bindless **when** you have a large, stable heap and SM 6.6. Until then: **do not** create/copy descriptors every draw; keep them persistent; one shader-visible heap you almost never switch (switching stalls the GPU).

MJP, *Ten Years of D3D12*: bindless/SM 6.6 is a *codebase* simplification at scale, not a FPS cheat code for 10 textures.

Chuck Walbourn / DirectXTK12 `LinearAllocator`: dynamic constants need a **ring** of upload memory, fenced per frame — not `Map` of one buffer the GPU might still read.

**What we already have:** 3-frame flight, staging upload to default heap, deferred `retire_resource`, debug names, `ENGINE_GPU_DEBUG` debug layer.

**Gaps that actually matter:**

- Per-texture descriptor heaps (fine for a handful of views; becomes a heap-switch tax).
- CPU barrier tracking can desync; the debug layer is the real compiler.

### 3.4 Jobs and threads

Naughty Dog (Gyrling, GDC 2015) and Our Machinery fibers: pin workers, yield on wait, thousands of jobs. That is an *endgame* scheduler.

Until then, Gregory’s rule is enough: **do not block the heartbeat**. A hitch is: sync DXC of a huge shader, sync mesh parse of a 50 MB OBJ, or `wait_idle` every upload.

**Trigger we already wrote:** visible hitch. Implementation when triggered: a **simple** pool (function pointer + counter), work finished on main thread for GPU upload. Not fibers. Not “everything is a job.” Log, D3D12 device, and graph stay main-thread until proven otherwise.

### 3.5 Assets without a database

Bevy Asset V2 / Asset Cooker / O3DE (public): **hash + mtime + optional cook folder**. Hot reload is “file changed → replace GPU object behind a stable handle.” A transactional DB is for editors with hundreds of thousands of files.

**What we already have:** mounts, `content_root`, generational `MeshHandle` + `unload()`, shader disk cache v3 (quoted `#include` hashed, DXC/SM 6.0 key), shader file watch.

**Sensible next cook, when textures exist:** source PNG/TGA → engine format (dimensions, mips, DXGI format) in `.cache/`, keyed by content hash. Still no SQL.

### 3.6 Tests and validation

Explicit APIs do not check your work. D3D12 debug layer and GPU-based validation (optional, slow) are the equivalent of Vulkan validation layers. AMD/Khronos: **debug builds on, ship off.**

CI for a Windows D3D12 engine is awkward (no GPU on many runners). Practical ladder:

1. **CPU gates** that need no window (graph compile, handle generation, hash) — already the style of sandbox probes.
2. **Optional local** `ENGINE_GPU_DEBUG=1` smoke: open, resize, quit, no debug-layer breakpoint.
3. Screenshot/CI ML compare is overkill until you ship content.

Do **not** wait for Google Test + WARP + golden images before writing more engine. Do **extract** the existing sandbox probes into a `sandbox` or `tests` target that can run `-gate` and exit 0/1.

### 3.7 Profiling

Gregory: in-game profiler and memory stats are *tools for debugging*, same class as debug draw. CPU scopes that are no-ops teach nothing. GPU timestamps are how you stop guessing whether the cube is CPU- or GPU-bound.

PIX command-list events + named objects + overlay GPU ms (Phase 6) is the
observability stack we need for years.

---

## 4. Honest map onto this codebase (Aug 2026)

**Already in the “amazing shape” column**

- Package DAG, interface/impl split, no graphics headers above `rhi-d3d12`.
- Phased loop, frame arena, focused input, content mounts.
- Generic `GraphicsPipelineDesc`; sandbox owns demo state.
- Graph: compile (producer, copy format, cycle), optional transients, always present, shutdown clears GPU objects before the device dies.
- Extract snapshot; per-draw model; unique CBV assert; overlay `should_execute`.
- Staging + deferred free + PIX names + 3-frame flight.
- Mutex log; `cpu_frame_slot` vs `IDevice::frame_slot()`.

**Real remaining instability (do these; they are not features)**

| Area | Why it bites later |
|------|-------------------|
| ~~Raw COM on `D3D12Device` members~~ | **Done** (Horizon A1) — `ComPtr` throughout. |
| ~~Profiler no-op~~ | **Done** (Horizon A2) — real RAII scopes, F3 shows `P`/`X`/`E` ms. |
| ~~Upload `wait_idle`~~ | **Done** (Horizon B2) — fenced upload ring. |
| ~~No GPU timestamps~~ | **Done** (Horizon A3) — F3 shows `G` GPU ms. |
| Gates only inside `sandbox.exe` / `game.exe` | Nothing runs them automatically — no CI, no git hook. Easy to ship a broken compile check. |
| `DrawItem` raw GPU pointers | Fine for one frame; dangling if owners reset mid-frame. Document + assert, don’t invent an ID indirection yet. |
| FXC (`D3DCompile`) in a package named dxc | Done in Phase 9 (`IDxcCompiler3`, SM 6.0). Bindless SM 6.6 still waits on material count. |

**Not remaining instability (leave them)**

Scene package, jobs, albedo, SSAO, shadows, bindless, Vulkan, editor, ECS, transient aliasing, render thread.

---

## 5. North-star architecture (the thing we are growing toward)

A single picture, still small:

```
sandbox / game
    → engine loop (poll, fixed, update)
        → extract: arrays of DrawItem + camera  (no ECS required)
            → renderer compile/execute graph
                → rhi (D3D12 only for a long time)
```

Invariants that must still be true in five years:

1. Renderer never includes `d3d12.h`.
2. Graph is the only presenter.
3. GPU resources have a fence-dated retire path; CPU never `Release`s in-flight objects.
4. Hot path is arrays and IDs; virtuals stay at package boundaries (device, window, compiler).
5. New packages appear only when an existing package is doing two jobs (see [packageRules.md](packageRules.md)).
6. A feature that does not change data flow (extract → graph → RHI) is a **leaf** and can wait.

Fast, in this architecture, means:

- No `wait_idle` on the frame path.
- Dynamic constants from a **linear upload allocator** (DirectXTK pattern).
- Barriers batched, states exact.
- One (or two) descriptor heaps, not one heap per texture, **when** texture count hurts — measure in PIX first.
- GPU work that does not exist is culled (graph already can skip `should_execute`).

That is enough to be “architecturally amazing” without becoming a studio engine.

---

## 6. Plan to reach the stability goal

Horizon: **make the current loop production-hard**, then grow content, then grow systems. Each step has a gate. If the gate is not failing, do not start the next layer.

### Horizon A — Production-hard cube (core stability)

Do this before any new rendering look.

| Step | Work | Gate | Explicitly not |
|------|------|------|----------------|
| A1 | `ComPtr` (or equivalent) for device, queue, swapchain, allocators, lists, fence | ✅ Device COM is `ComPtr`; buffers/textures stay on retire | Rewriting the RHI API |
| A2 | CPU profiler that writes to overlay (even a 8-slot ring of named scopes) | ✅ F3 shows `P` poll / `X` extract / `E` execute ms | Tracy integration, Chrome tracing |
| A3 | GPU timestamp queries around the graph | ✅ F3 shows `G` GPU ms (`IDevice::last_gpu_time_ms`) | Async compute |
| A4 | Extract sandbox CPU probes to `sandbox --gates` (or a tiny `engine-gates` exe) that exits non-zero on fail | ✅ `sandbox --gates` runs probes and exits 0/1 | Cloud CI GPU |
| A5 | Habit: `ENGINE_GPU_DEBUG=1` while developing; treat layer errors as compile failures | Habit (no code) | GPU-based validation always-on |

**Done when:** you can leave the sandbox running, resize like a maniac, hot-reload the shader, toggle F3, quit, and trust the overlay numbers.

### Horizon B — Frame-time hygiene (fast without new features)

| Step | Work | Gate | Explicitly not |
|------|------|------|----------------|
| B1 | Upload heap **ring** for uniforms (and later for streaming) fenced by `IDevice::frame_slot()` | ✅ `alloc_frame_memory`; two DrawItems, distinct slices | Bindless |
| B2 | Staging copies without full-device idle (copy queue or fence the copy list) | ✅ Copy list waits `copy_fence_value_` only; mesh reload gate holds | Background thread yet |
| B3 | Optional second cube **only** as a DrawItem gate | ✅ Two cubes, identity + translate; no `scene` package | Hierarchy, lights, names |
| B4 | If PIX shows descriptor-heap churn: one CBV/SRV heap, persistent views | Skipped — no PIX evidence yet | SM 6.6 bindless |

**Done when:** adding a second instance does not require a new package or a new thread.

### Horizon C — Content that forces systems (still no boom)

Only when you have a texture, a second mesh, or a compile that hitchs.

| Step | Trigger | Work | Still not |
|------|---------|------|-----------|
| C1 | You want albedo | ✅ Sampled texture + SRV + static sampler; PNG via mounts (`assets-png` WIC); GGX PBR on metal/rough | Material graph |
| C2 | Shader compile hitch visible | Not triggered | Fibers, job stealing |
| C3 | OBJ load hitch visible | Not triggered | Async IO stack |
| C4 | Extract function is a mess of cameras/lights | ✅ Flat `scene::World` arrays; Z/X moves instance 0 | ECS |

Texture is a **content** trigger, not a prestige feature. The graph already has `ShaderRead`.

### Horizon D — Graph maturity (only with multiple offscreen targets)

| Step | Trigger | Work | Still not |
|------|---------|------|-----------|
| D1 | HDR / post / shadow map exists | Transient lifetime (alloc first use) | Placed-heap aliasing |
| D2 | VRAM of transients actually hurts | Aliasing + aliasing barriers, tested in PIX | Console ESRAM folklore |
| D3 | You cannot see why a pass runs | Dump graph as text (pass names, resources) | Full GUI editor |

Phase 6 debug lines/AABBs fit here when you have bounds to draw.

### Horizon E — Method, not a never-list

Keep the **method**: one gate, one package, debug-layer clean. The systems
themselves live on [TODO_LATER.md](TODO_LATER.md) and [ROADMAP.md](ROADMAP.md)
phases 12+:

- Physics, audio, net — behind interfaces, when triggered
- Editor — **separate app**, far; F3/F4/F5 debug viz is engine, not an inspector
- `rhi-vulkan` — after the RHI contract (phase 14), D3D12 stays daily driver
- Deferred + SSAO + TAA — after the renderer owns the graph
- Bindless SM 6.6 — after materials actually hurt
- Fiber job system — not before many hitches

---

## 7. How to choose work on a Monday

1. If the debug layer yells, that is the only task.
2. Else pick **one Ready row** from [ENGINE_MAP.md](ENGINE_MAP.md). (Phases
   0–14 are all done; the numbered-phase model this section once described is
   finished. [ROADMAP.md](ROADMAP.md) is now the decision log, not the queue.)
3. Do not skip a Ready row to “look better.” A PBR shader in `main.cpp` makes
   the renderer *harder* to rip.
4. If you want a new graphics paper feature, it is a **pass + material**, not
   a new architecture.

One package, one job, one gate — [ROADMAP.md](ROADMAP.md) is the sequence. This
file is the **why** for stability and swapability.

---

## 8. Source list (public)

- Yuriy O’Donnell, GDC 2017, *FrameGraph: Extensible Rendering Architecture in Frostbite*
- Unreal Engine documentation: Render Dependency Graph; mesh drawing / scene proxies
- Our Machinery blog archive: *A modern rendering architecture*; *High-level rendering using render graphs*; *ECS and rendering* (steal RHI/graph, not the full plugin stack)
- Microsoft Learn: *Using resource barriers to synchronize resource states in Direct3D 12*
- NVIDIA: *Advanced API Performance: Barriers*; *Advanced API Performance: Descriptors*
- MJP: *Ten Years of D3D12*
- DirectXTK12 `LinearAllocator` (upload ring)
- Mike Acton: *Three Big Lies*; CppCon data-oriented design
- Christian Gyrling, GDC 2015, *Parallelizing the Naughty Dog Engine Using Fibers* (what *not* to start with)
- Jason Gregory, *Game Engine Architecture* (loop, memory, tools vs runtime)
- Pavel Šmejkal: *Aliasing transient textures in DirectX 12* (when Horizon D2 is real)
- Bevy Asset V2 notes; Asset Cooker (cook without a database)

---

*Written 18 Aug 2026 against the tree after Phase 3.6. Revisit only when a Horizon trigger fires — not when a blog post is interesting.*
