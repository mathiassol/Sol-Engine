# Architecture

Modular C++ game engine. Each package has one responsibility, communicates through
interfaces, and can be replaced independently. See [packageRules.md](packageRules.md).

## Dependency graph

```
                         ┌──────────┐
                         │ sandbox  │  (application — links implementations)
                         └────┬─────┘
                              │
                         ┌────▼─────┐
                         │  engine  │  (runtime, module wiring)
                         └────┬─────┘
                ┌────────────┼────────────┐
                │            │            │
           ┌────▼────┐  ┌─────▼────┐  ┌───▼──────┐
           │renderer │  │  assets  │  │ platform │  (interfaces)
           │debug-draw│ └─────┬────┘  └────┬─────┘
           └────┬────┘        │            │
                │        ┌────▼────────────┼──────────────┐
                │        │ assets-fs     │              │
                │        │ assets-obj    │       ┌──────▼──────────┐
                │        │ assets-gpu    │       │  platform-win32 │
                │        └───────────────┘       └─────────────────┘
           ┌────▼────┐
           │   rhi   │
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
| `core` | 0 | lib | Clock, frame timer, log, arena, profile scopes |
| `math` | 0 | lib | Vec3, Mat4, column-major RH Y-up |
| `platform` | 1 | interface | `IPlatform`, `IWindow`, `IInput`, `IFileSystem` |
| `rhi` | 1 | interface | `IRHI`, `IDevice`, buffers, pipelines, commands |
| `assets` | 1 | interface | `IAssetLoader`, mesh types |
| `shaders` | 1 | interface | `IShaderCompiler`, hot-reload interface |
| `platform-win32` | 2 | lib | Win32 platform |
| `assets-filesystem` | 2 | lib | Filesystem `IAssetLoader` |
| `assets-obj` | 2 | lib | Wavefront OBJ mesh loader |
| `assets-gpu` | 2 | lib | CPU mesh → GPU buffers |
| `shaders-dxc` | 2 | lib | HLSL compile via `D3DCompile` (FXC; rename later) |
| `rhi-d3d12` | 2 | lib | D3D12 backend |
| `renderer` | 3 | lib | Pass list / render graph hook |
| `debug-draw` | 3 | lib | Frame stats, screen overlay |
| `engine` | 4 | lib | Phased loop, module injection |
| `sandbox` | app | exe | Dev harness |

Deferred: [TODO_LATER.md](TODO_LATER.md). Roadmap: [WHATS_NEXT.md](WHATS_NEXT.md).

## Interface / implementation pattern

| Interface | Implementation(s) |
|-----------|-------------------|
| `platform` | `platform-win32` |
| `rhi` | `rhi-d3d12` |
| `assets` | `assets-filesystem`, `assets-obj`, `assets-gpu` |
| `shaders` | `shaders-dxc` |

Applications link implementations. `engine` and `renderer` only see interfaces.

## Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\bin\Debug\sandbox.exe   # from repo root
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `ENGINE_PLATFORM_WIN32` | ON | Win32 platform backend |
| `ENGINE_RHI_D3D12` | ON | D3D12 RHI backend |
| `ENGINE_BUILD_SANDBOX` | ON | Build sandbox application |

## Conventions

- Headers: `include/engine/<package>/<header>.hpp`
- Namespace: `engine::`, sub-namespaces per package
- Ownership: `std::unique_ptr` for modules, injected at startup
- C++20, RAII, explicit context passing ([FOUNDATION.md](FOUNDATION.md))
