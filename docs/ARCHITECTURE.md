# Architecture

Modular C++ game engine. Each package has one responsibility, communicates through
interfaces, and can be replaced independently. See [packageRules.md](packageRules.md).

## Dependency graph

```
                         ┌──────────┐   ┌──────────┐
                         │ sandbox  │   │   game   │  (apps — sandbox proves, game ships)
                         └────┬─────┘   └────┬─────┘
                              │              │
                         ┌────▼──────────────▼─────┐
                         │         engine          │  (runtime, module wiring)
                         └────────────┬────────────┘
          ┌───────────┬───────────┬───┴────┬───────────┐
          │           │           │        │           │
     ┌────▼────┐ ┌────▼────┐ ┌────▼────┐ ┌─▼──────┐ ┌─▼──────┐
     │renderer │ │  assets │ │platform │ │ audio  │ │physics │
     │debug-draw│ └────┬────┘ └────┬────┘ └───┬────┘ └───┬────┘
     └────┬────┘      │          │          │          │
          │      ┌────▼──────────┼──────────┼──────────┼─────────┐
          │      │ assets-fs     │          │          │         │
          │      │ assets-obj    │   ┌──────▼──────────┐         │
          │      │ assets-gltf   │   │  platform-win32 │         │
          │      │ assets-png    │   └─────────────────┘         │
          │      │ assets-gpu    │                               │
          │      └───────────────┘     ┌────────────┐  ┌─────────▼────────┐
     ┌────▼────┐                       │audio-xaudio2│  │   physics-cpu    │
     │   rhi   │                       └────────────┘  └──────────────────┘
     └────┬────┘
          │
     ┌────▼────────┐
     │  rhi-d3d12  │  (+ shaders-dxc)
     └──────┬──────┘
            │
     ┌──────▼──────┐
     │    math     │
     └──────┬──────┘
            │
     ┌──────▼──────┐
     │    core     │
     └─────────────┘
```

**Rule:** dependencies only point downward.

## Packages

| Package | Layer | Type | Responsibility |
|---------|-------|------|----------------|
| `core` | 0 | lib | Clock, frame timer, log, arena, profile scopes, cvars |
| `math` | 0 | lib | Vec3, Mat4, AABB, Frustum, ortho, column-major RH Y-up |
| `platform` | 1 | interface | `IPlatform`, `IWindow`, `IInput` (keys, mouse, four `GamepadState` slots), `IFileSystem` |
| `rhi` | 1 | interface | `IRHI`, `IDevice`, buffers, graphics + compute pipelines, commands, `SamplerDesc` |
| `assets` | 1 | lib | `IAssetLoader`, mesh/image types, `SOLC` cooker, `SOLP` pak |
| `audio` | 1 | interface | `IAudio`: create PCM sound, play 2D one-shot, tick |
| `physics` | 1 | interface | `IPhysics`: overlap queries, translation-only rigid bodies, `step(dt)` |
| `shaders` | 1 | interface | `IShaderCompiler`, hot-reload interface |
| `platform-win32` | 2 | lib | Win32 platform; XInput 1.4 gamepads (four slots, Xbox layout) |
| `assets-filesystem` | 2 | lib | Filesystem `IAssetLoader` |
| `assets-obj` | 2 | lib | Wavefront OBJ mesh loader |
| `assets-gltf` | 2 | lib | glTF mesh loader (all triangle primitives; cgltf; metal/rough factors + MR/normal URIs) |
| `assets-png` | 2 | lib | PNG albedo decode |
| `assets-gpu` | 2 | lib | CPU mesh → GPU buffers |
| `audio-xaudio2` | 2 | lib | XAudio2 backend (`create_audio()`); 16-bit PCM one-shots |
| `physics-cpu` | 2 | lib | Dynamic AABB tree + sequential-impulse solver; AABB / sphere / Y-up capsule; sensor enter/exit; closest-hit raycasts (`create_physics()`) |
| `shaders-dxc` | 2 | lib | HLSL compile via Windows SDK `dxcompiler` (SM 6.0 / DXIL); `ShaderTarget` enum (SPIR-V rejected); disk cache; worker-thread hot-reload |
| `rhi-d3d12` | 2 | lib | D3D12 backend (`create_rhi()` is the public header; device types stay in `src/`). Inbox OS D3D12 (FL 11_0 + SM 6.0, no Agility). PIX ANSI `BeginEvent`/`EndEvent`/`SetMarker` on the command list. SamplerDesc static samplers, `create_sampler`, compute stub, per-SRV tables, device shader-visible SRV heap, color+SRV transients, RGBA8 mip generation |
| `renderer` | 3 | lib | Render graph, **standard frame** (shadow → forward → motion → sky → bloom → TAA → tonemap → AA → debug → overlay), per-pass GPU debug events, frustum extract, **GGX PBR** (albedo + packed MR + derivative TBN normals), **16-tap Vogel PCF**, **split-sum IBL**, **Karis bloom**, **Karis TAA**, **SMAA 1x / FXAA** |
| `debug-draw` | 3 | lib | Frame stats, F3 overlay, F4 world AABBs, F5 AA mode |
| `scene` | 3 | lib | Flat instance list (64) with interned names, parent indices, `solscene` files, and prefab extract/instantiate (prefix + root transform); world = parent * local; materials (16), camera, sun + point lights (`World`); no ECS |
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
| `rhi` | `rhi-d3d12` |
| `assets` | `assets-filesystem`, `assets-obj`, `assets-gltf`, `assets-png`, `assets-gpu` |
| `audio` | `audio-xaudio2` |
| `physics` | `physics-cpu` |
| `shaders` | `shaders-dxc` |

Applications link implementations. `engine` and `renderer` only see interfaces.

## Swap test — where a new pass goes

**New engine pass = `add_pass` in `packages/renderer/src/standard_frame.cpp`**
(`setup_standard_frame`). The sandbox must not register shadow/forward/tonemap/debug/overlay.

Frustum cull and sun bounds live in `renderer::extract_visible`. The sandbox only
copies `scene::World` into `ExtractInstance` (`packages/sandbox/src/world_extract.cpp`),
including metal/rough from `scene::Material`. The renderer does not include `scene`.

Overlay/debug *drawing* stays in `debug-draw`; the graph calls them through
function pointers on `StandardFrameDesc`.

## Swap cost today

Phase 14 put samplers and compute on the RHI contract. D3D12 now implements
compute (PSO + dispatch + buffer UAV readback) and cube / array textures.
Remaining gaps for a second backend: SPIR-V. Do not implement `rhi-vulkan`
until you need a second daily driver; D3D12 stays the production backend.

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
| `ENGINE_BUILD_SANDBOX` | ON | Build sandbox application |
| `ENGINE_BUILD_GAME` | ON | Build player `game.exe` (install layout) |

## Conventions

- Headers: `include/engine/<package>/<header>.hpp`
- Namespace: `engine::`, sub-namespaces per package
- Ownership: `std::unique_ptr` for modules, injected at startup
- C++20, RAII, explicit context passing ([FOUNDATION.md](FOUNDATION.md))
