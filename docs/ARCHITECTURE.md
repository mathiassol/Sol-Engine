# Architecture

Modular C++ game engine. Each package has one responsibility, communicates through
interfaces, and can be replaced independently. See [packageRules.md](packageRules.md).

## Dependency graph

Read as `package → what it links`. Derived from `target_link_libraries` in each
`packages/*/CMakeLists.txt`; keep it that way rather than drawing it by hand.

```
Layer 0  core            → (nothing)
         math            → core
         reflect         → core

Layer 1  platform        → core          rhi       → core
         shaders         → core          assets    → core, math
         audio           → core, math    physics   → core, math

Layer 2  platform-win32  → platform          rhi-d3d12  → rhi, math
         shaders-dxc     → shaders           rhi-vulkan → rhi, math
         audio-xaudio2   → audio
         physics-cpu     → physics           assets-gpu → assets, rhi
         assets-filesystem → assets, platform
         assets-obj / assets-gltf / assets-png-wic → assets

Layer 3  renderer        → rhi, core, math
         debug-draw      → core, math, rhi, shaders
         scene           → core, math, assets
         gameplay        → core, math, physics

Layer 4  engine          → core, platform, rhi, renderer, assets, audio, physics
         scene-render    → scene, renderer, debug-draw, assets-gpu, math

Tool     cook            → core, math, assets, assets-obj, assets-gltf, assets-png-wic

Apps     sandbox / game  → engine, scene, scene-render, gameplay, debug-draw, math,
                           assets-*, and the Layer 2 backends (guarded by
                           ENGINE_* options). Same sources; game adds the
                           install layout and identity resources.
```

**Rule:** dependencies only point downward. Verified: no cycles, no upward
edges, and no package reaching around an interface to a concrete backend.

Two facts the shape is load-bearing on:

- `engine` does **not** depend on `scene`, `gameplay`, or `debug-draw` — the
  **apps** link those. That is *why* the renderer never sees the scene: the
  `scene → RenderSnapshot` bridge is its own Layer 4 package, `scene-render`
  (`packages/scene-render/src/extract.cpp`), sitting above all three so it can
  see them and linked by the apps rather than by `engine`.
- `rhi` depends on `core` only — **not** `math`. `rhi-d3d12` links `rhi` and
  `math` — the latter only for `build_rgba8_mip_chain`.

## Packages

| Package | Layer | Type | Responsibility |
|---------|-------|------|----------------|
| `core` | 0 | lib | Clock, frame timer, log, arena, profile scopes, cvars |
| `math` | 0 | lib | Vec3, Mat4, AABB, Frustum, ortho, column-major RH Y-up |
| `reflect` | 0 | lib | Describes a POD struct's fields, so text serialisation, the command layer and an editor inspector are one mechanism rather than three. Today: `FieldType`, the byte width and alignment of each type it names, and `FieldDesc`/`TypeDesc` — a field's name, byte offset and width, built with `ENGINE_REFLECT_FIELD` so the three cannot disagree with the member they describe. Depends on `core` only — deliberately **not** `math`, so the vector widths are stated here rather than derived. The reflect gate cross-checks every stated width against the real `sizeof`. `validate()` is `constexpr`, so a type can `static_assert` that its descriptors match the struct |
| `platform` | 1 | interface | `IPlatform`, `IWindow`, `IInput` (keys, mouse, four `GamepadState` slots), `IFileSystem` |
| `rhi` | 1 | interface | `IRHI`, `IDevice`, buffers, graphics + compute pipelines, commands, `SamplerDesc`, **instanced `draw_indexed`** + `set_structured_buffer` |
| `assets` | 1 | lib | `IAssetLoader`, mesh/image types, `SOLC` cooker, `SOLP` pak |
| `audio` | 1 | interface | `IAudio`: create PCM sound, play 2D one-shot, tick |
| `physics` | 1 | interface | `IPhysics`: overlap queries, translation-only rigid bodies, `step(dt)` |
| `shaders` | 1 | interface | `IShaderCompiler`, hot-reload interface |
| `platform-win32` | 2 | lib | Win32 platform; XInput 1.4 gamepads (four slots, Xbox layout) |
| `assets-filesystem` | 2 | lib | Filesystem `IAssetLoader` |
| `assets-obj` | 2 | lib | Wavefront OBJ mesh loader |
| `assets-gltf` | 2 | lib | glTF mesh loader (all triangle primitives; cgltf; metal/rough factors + MR/normal URIs). Walks the **node graph**, not `meshes` — world transforms are composed through the parent chain and baked into vertices (normals via the cofactor matrix; mirrored nodes flip winding). `cgltf_validate` runs before any unpack. Falls back to a flat mesh walk for files with no nodes |
| `assets-png-wic` | 2 | lib | PNG/JPG/BMP decode via Windows WIC. Named for its backend, not its format: unlike `assets-obj` and `assets-gltf` it is Windows-only, and the name is the warning |
| `assets-gpu` | 2 | lib | CPU mesh → GPU buffers, and `GpuTextureStore`: CPU images → GPU textures keyed on the resolved content path *and* colour space, so a texture many materials share costs one upload while an sRGB and a linear use of the same file stay separate |
| `audio-xaudio2` | 2 | lib | XAudio2 backend (`create_audio()`); 16-bit PCM one-shots |
| `physics-cpu` | 2 | lib | Dynamic AABB tree + sequential-impulse solver; AABB / sphere / Y-up capsule; sensor enter/exit; closest-hit raycasts (`create_physics()`) |
| `shaders-dxc` | 2 | lib | HLSL compile via Windows SDK `dxcompiler` (SM 6.0 / DXIL) **and SPIR-V** through a second, SPIR-V-capable DXC located from the Vulkan SDK (the Windows SDK build ships without `-DENABLE_SPIRV_CODEGEN`); `-fvk-*-shift` binding shifts applied for the Vulkan register-space collision; `ShaderTarget` enum; disk cache keyed per target; worker-thread hot-reload |
| `rhi-d3d12` | 2 | lib | D3D12 backend (`create_rhi()` is the public header; device types stay in `src/`). Inbox OS D3D12 (FL 11_0 + SM 6.0, no Agility). PIX ANSI `BeginEvent`/`EndEvent`/`SetMarker` on the command list. SamplerDesc static samplers, `create_sampler` (object only — `set_sampler` is a deliberate stub), compute PSO + dispatch + buffer UAV readback, per-SRV tables, device shader-visible SRV heap, color+SRV transients, RGBA8 mip generation, **root SRVs** (`space1`) for per-instance structured buffers |
| `rhi-vulkan` | 2 | lib | Vulkan backend (`engine::rhi::vulkan::create_rhi()`). Loaded through vendored **volk**; Vulkan and volk headers are vendored under `third_party/` at SDK 1.4.357.0 and must be bumped together. Surface + swapchain, per-frame-slot command pools and fences, depth targets with reversed-Z and end-of-pass resolve, compute and storage resources, sampled textures with mips and cube maps, sampler objects, vertex input and indexed draws with staged uploads. Selected at runtime with `--rhi vulkan`. Presents with IMMEDIATE and warns: FIFO loses the device on the second present (RHI #25) |
| `renderer` | 3 | lib | Render graph, **standard frame** (shadow → forward → motion → sky → bloom → TAA → tonemap → AA → debug → overlay), per-pass GPU debug events, frustum extract, **GGX PBR** (albedo + packed MR + derivative TBN normals), **16-tap Vogel PCF**, **split-sum IBL**, **Karis bloom**, **Karis TAA**, **SMAA 1x / FXAA**, **instanced draws** (extract batches by material/mesh key; one `draw_indexed` and one constant upload per batch) |
| `debug-draw` | 3 | lib | Frame stats, F3 overlay, F4 world AABBs, F5 AA mode |
| `scene` | 3 | lib | Flat instance list (**512**) with interned names, parent indices, `solscene` files (`load_world` reads one from a mounted virtual path through `IAssetLoader`), and prefab extract/instantiate (prefix + root transform); world = parent * local; materials (16) that name their own albedo, normal and metal-rough textures by `assets::TextureHandle` — the handles are **not** serialized, see decision S5 — camera, sun + point lights (`World`); no ECS |
| `scene-render` | 4 | lib | The `scene → RenderSnapshot` bridge: copies `scene::World` into `ExtractInstance`, extracts lighting into `renderer::Lighting`, computes the world's mesh bounds, resolves each material's albedo, normal and metal-rough handles against a `GpuTextureStore` (falling back to a built-in default per map), and picks a material's pipeline from its opacity. Rank 4 rather than 3 because a bridge has to sit *above* the packages it bridges — `dependency-direction` rejects a dependency on a package at the same rank |
| `gameplay` | 3 | lib | Kinematic `CharacterController` (walk, jump, step, slope; analog wish) and `GameCamera` (follow / orbit / FPS, stick look) on `IPhysics` + math; no input map |
| `engine` | 4 | lib | Phased loop, module injection, repo vs install content layout, `config.cfg` load |
| `sandbox` | app | exe | Dev harness (`--gates`, repo mounts, Debug + `ENGINE_GPU_DEBUG`) |
| `cook` | app | exe | Offline `SOLC`/`SOLP` cooker; writes `content.pak` |
| `game` | app | exe | Player binary (Release, identity + content + `content.pak` + DXC DLLs next to exe; OS D3D12, no Agility) |

Engine map: [ENGINE_MAP.md](ENGINE_MAP.md). Roadmap: [ROADMAP.md](ROADMAP.md).
GPU / DLL baseline: [GPU_BASELINE.md](GPU_BASELINE.md).

## Interface / implementation pattern

| Interface | Implementation(s) |
|-----------|-------------------|
| `platform` | `platform-win32` |
| `rhi` | `rhi-d3d12`, `rhi-vulkan` |
| `assets` (`IAssetLoader`) | `assets-filesystem`, plus the in-tree `pak` loader |
| `assets` (`IMeshLoader`) | `assets-obj` |
| `audio` | `audio-xaudio2` |
| `physics` | `physics-cpu` |
| `shaders` | `shaders-dxc` |

Applications link implementations. `engine` and `renderer` only see interfaces.

Two `assets-*` packages sit outside that pattern and it is worth knowing which:
`assets-gltf` exposes its own `IGltfLoader` (returns a glTF-specific result
with texture URIs, so it is not substitutable for `IMeshLoader`), and
`assets-png-wic` has no interface at all — free functions over WIC. `assets-gpu` is
not a loader either; it uploads CPU meshes to GPU buffers.

## Swap test — where a new pass goes

**New engine pass = `add_pass` in `packages/renderer/src/standard_frame.cpp`**
(`setup_standard_frame`). The sandbox must not register shadow/forward/tonemap/debug/overlay.

Frustum cull and sun bounds live in `renderer::extract_visible`. The copy of
`scene::World` into `ExtractInstance` — including metal/rough from
`scene::Material` — is **no longer app code**: it is `scene-render`
(`packages/scene-render/src/extract.cpp`), an engine package the apps link.
A bridge every app would otherwise reimplement is engine capability, and so are
the two `static_assert`s in it that couple `scene`'s instance and light caps to
the renderer's. The renderer still does not include `scene`.

Extract also *batches* the survivors, grouping by pipeline + buffers + textures +
index count. One batch list serves shadow, forward and motion: building it per
pass would let them disagree about grouping, and the motion pass draws with
`DepthTest::Equal`, so geometry that does not rasterize identically to forward
writes nothing and says nothing. Per-instance transforms go to the GPU in one
`StructuredBuffer` (root SRV, `space1`) uploaded once per frame; a batch's
`first_instance` rides in the pass constants because `StartInstanceLocation` is
not portable — D3D excludes it from `SV_InstanceID`, Vulkan folds it into
`gl_InstanceIndex`, Metal splits it out as `[[base_instance]]`.

Overlay/debug *drawing* stays in `debug-draw`; the graph calls them through
function pointers on `StandardFrameDesc`.

## Two backends, and what the contract cost

`rhi-vulkan` exists, passes the whole gate suite, and renders a live frame. It
is a **package**, not a fork: `--rhi vulkan` selects it at runtime, and a parity
gate proves both backends return byte-identical readback. D3D12 remains the
shipped player backend — `game.exe` links it and `GPU_BASELINE.md` is written
against it.

What that second implementation actually cost, now that it is written, because
these are the places a **third** backend will pay again:

- **SPIR-V** was the cheapest part and needed no shader edits — `-spirv` plus
  the `-fvk-*-shift` binding shifts, because D3D's separate `b`/`t`/`s` register
  spaces all collide at binding 0 in Vulkan. The catch was not the flags: the
  Windows SDK's `dxcompiler.dll` is built without `-DENABLE_SPIRV_CODEGEN` and
  answers "SPIR-V CodeGen not available", so `shaders-dxc` locates a second DXC
  from the Vulkan SDK and keeps both alive.
- **Descriptor sets.** `set_constant_buffer` / `set_shader_resource` are a flat
  per-slot model with no set concept, and the Vulkan backend synthesises one.
  **The binding contract at the top of
  `packages/rhi/include/engine/rhi/resources.hpp` is the table** — what each of
  the four counts means, what D3D12 makes of it, and what Vulkan synthesises —
  and the `rhi-vocabulary` invariant fails the build if backend terms return to
  those headers. Counts rather than an explicit layout is the standing debt;
  bind groups are the expected answer, and there is now a second backend to
  validate one against.
- **NDC Y** was the one clip-space convention that was not already portable.
  Depth range, matrix order and texture origin agree across D3D12, Vulkan and
  Metal. All the Y-dependent helpers live in
  `packages/sandbox/content/shaders/common.hlsli`, so the flip was one edit —
  read the note there about varyings before changing where it applies.
- **Barriers.** `transition()` takes the old state explicitly, D3D12-style. That
  works because `RenderGraph` centralizes the tracking — exactly one
  `transition()` call escapes it, in a compute gate. The standing gap is that
  `ResourceState::ShaderRead` does not say *which* stage reads, so the Vulkan
  backend over-synchronizes until a stage scope is added.
- **Present.** `ISwapchain::present()` returns `void`, so there is still no
  channel for `VK_ERROR_OUT_OF_DATE_KHR`. Resize is driven only by window
  events, which is not sufficient on Wayland. This is unfinished contract work,
  not a Vulkan implementation detail.

Two D3D12-isms were removed before that author could trip over them:
`IDevice::frame_slot()` (the backbuffer index is not a frame-in-flight slot on
Vulkan) and `ICommandList::set_sampler` (a dead virtual whose only
implementation logged an error).

A **third** backend's blocker is not the RHI contract — a second backend has now
taken that all the way through a frame. It is Platform #9, a non-Windows
platform package. See ENGINE_MAP.md category 3.

## Build

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe   # from repo root

cmake --build build --config Release --target game
.\build\bin\Release\game.exe    # content + content.pak next to the exe
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `ENGINE_PLATFORM_WIN32` | ON | Win32 platform backend |
| `ENGINE_RHI_D3D12` | ON | D3D12 RHI backend |
| `ENGINE_RHI_VULKAN` | ON | Vulkan RHI backend |
| `ENGINE_BUILD_SANDBOX` | ON | Build sandbox application |
| `ENGINE_BUILD_GAME` | ON | Build player `game.exe` (install layout) |

## Conventions

- Headers: `include/engine/<package>/<header>.hpp`
- Namespace: `engine::`, sub-namespaces per package
- Ownership: `std::unique_ptr` for modules, injected at startup
- C++20, RAII, explicit context passing ([FOUNDATION.md](FOUNDATION.md))
