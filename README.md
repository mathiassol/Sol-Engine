# Engine

General-purpose C++ game engine (Unity / Godot / Unreal category) — interface
and implementation packages, dependencies only downward, stability as the
method so systems stay swappable.

The sandbox is the proving ground, not the product.

## New machine setup

**Requirements**

- Windows 10/11 x64
- Direct3D 12 GPU (Feature Level 11_0, Shader Model 6.0). See [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md).
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++**
- [CMake 3.24+](https://cmake.org/download/) (or VS bundled CMake)
- Git

**Clone and build**

```powershell
git clone <your-remote-url> Engine
cd Engine

cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

**Run sandbox** — content mounts are resolved from the repo root (works from any CWD):

```powershell
.\build\bin\Debug\sandbox.exe
```

**Ship / play `game.exe`** — Release, no GPU debug layer, content,
`content.pak`, and DXC DLLs (`dxcompiler.dll`, `dxil.dll`) next to the exe.
D3D12 comes from Windows; Agility is not shipped.

```powershell
cmake --build build --config Release --target game
.\build\bin\Release\game.exe
```

Optional install layout (same files, chosen prefix):

```powershell
cmake --install build --config Release --prefix dist
.\dist\game.exe
```

Or via CPM scripts (`cpm run` if CPM CLI installed):

```powershell
cpm configure
cpm build
cpm run
```

**Controls:** WASD + Q/E move, right-mouse look, **Tab** / **Start** walk mode (WASD or left stick / D-pad camera-relative, **Space** / **A** jump, **Enter** / **Y** cycle follow / orbit / FPS, right stick look; husky 0 is the capsule), **Z/X** nudge the walker on world X, **Space** 2D beep when not walking, **F3** stats overlay (`P` poll / `X` extract / `E` execute / `G` GPU ms, plus AA mode), **F4** instance AABBs, **F5** cycle AA (Off / FXAA / SMAA; default SMAA 1x after ACES), **Esc** quit. Sixty-three huskies plus a checker floor live in `scene::World` (64 instances, frustum-culled at extract), lit by a directional sun (with a floor shadow), split-sum IBL, and one rim point light. A source cubemap sky fills leftover pixels in HDR before ACES. Scene color is RGBA16; ACES writes LDR, then SMAA (or FXAA) before the swapchain. Shaders compile with Windows SDK DXC (SM 6.0 / DXIL) on a worker thread for hot-reload. The husky mesh is glTF (`/content/meshes/cartoon_husky.gltf`); the cube stays OBJ. Albedos upload a full mip chain. Huskies sample `/content/textures/husky/Cartoon_Husky_Albedo1.png` … `Albedo4.png`.

**Gates (no interactive loop):**

```powershell
.\build\bin\Debug\sandbox.exe --gates
.\build\bin\Release\game.exe --gates
```

Exits `0` on pass, `1` on fail.

**Optional:** in **Debug**, `ENGINE_GPU_DEBUG=1` enables the D3D12 debug layer and shader debug flags. Release `game.exe` ignores it.

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
  sandbox                 Dev harness
  game                    Player exe (Release install layout)
docs/                     Architecture, roadmap, rules
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `ENGINE_PLATFORM_WIN32` | ON | Win32 platform backend |
| `ENGINE_RHI_D3D12` | ON | D3D12 RHI backend |
| `ENGINE_BUILD_SANDBOX` | ON | Build sandbox app |
| `ENGINE_BUILD_GAME` | ON | Build player `game.exe` |

## Docs

- [Philosophy.md](Philosophy.md) — design principles
- [docs/packageRules.md](docs/packageRules.md) — package rules
- [docs/FOUNDATION.md](docs/FOUNDATION.md) — core/math/platform API
- [docs/ROADMAP.md](docs/ROADMAP.md) — live numbered sequence (after 14: pick from the engine map)
- [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — full engine map by category (implementation order)
- [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md) — player GPU / OS / DLL baseline
- [docs/WHATS_NEXT.md](docs/WHATS_NEXT.md) — archive of the 2026 foundation list
- [docs/TODO_LATER.md](docs/TODO_LATER.md) — how to pick the next gate from the map
