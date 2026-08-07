# Engine

Modular C++ game engine — interface/impl packages, explicit dependencies downward, foundation-first.

## New machine setup

**Requirements**

- Windows 10/11 x64
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++**
- [CMake 3.24+](https://cmake.org/download/) (or VS bundled CMake)
- Git

**Clone and build**

```powershell
git clone <your-remote-url> Engine
cd Engine

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

**Run sandbox** — must be from **repo root** (content paths are relative):

```powershell
.\build\bin\Debug\sandbox.exe
```

Or via CPM scripts (`cpm run` if CPM CLI installed):

```powershell
cpm configure
cpm build
cpm run
```

**Controls:** WASD + Q/E move, right-mouse look, **F3** stats overlay, **Esc** quit.

**Optional:** `ENGINE_GPU_DEBUG=1` enables D3D12 debug layer and shader debug flags.

## Git on a new PC

This repo should have a single initial commit before moving machines:

```powershell
git add .
git status          # verify .pp/, build/, .cursor-tmp/ are NOT listed
git commit -m "Initial engine scaffold with D3D12 forward pass"
git remote add origin <url>
git push -u origin main
```

On the new PC: clone, configure, build — no extra secrets in repo (`.pp/` is gitignored).

## Project layout

```
packages/
  core, math              Layer 0 — foundation
  platform, rhi, assets     Layer 1 — interfaces
  *-win32, *-d3d12, ...    Layer 2 — implementations
  renderer, debug-draw    Layer 3 — systems
  engine                  Layer 4 — runtime loop
  sandbox                 Application
docs/                     Architecture, roadmap, rules
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/WHATS_NEXT.md](docs/WHATS_NEXT.md).

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `ENGINE_PLATFORM_WIN32` | ON | Win32 platform backend |
| `ENGINE_RHI_D3D12` | ON | D3D12 RHI backend |
| `ENGINE_BUILD_SANDBOX` | ON | Build sandbox app |

## Docs

- [Philosophy.md](Philosophy.md) — design principles
- [docs/packageRules.md](docs/packageRules.md) — package rules
- [docs/FOUNDATION.md](docs/FOUNDATION.md) — core/math/platform API
- [docs/WHATS_NEXT.md](docs/WHATS_NEXT.md) — roadmap (foundation-first)
- [docs/TODO_LATER.md](docs/TODO_LATER.md) — deferred systems (scene, jobs, …)
