# Engine

General-purpose C++ game engine (Unity / Godot / Unreal category) — interface
and implementation packages, dependencies only downward, stability as the
method so systems stay swappable.

The sandbox is the proving ground, not the product.

## New machine setup

**Requirements**

- Windows 10/11 x64
- Direct3D 12 GPU (Feature Level 11_0, Shader Model 6.0). See [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md).
- [Visual Studio 2026](https://visualstudio.microsoft.com/) with **Desktop development with C++**
- Windows 10/11 SDK (ships with that workload) — supplies `dxcompiler.dll` +
  `dxil.dll`. Configure fails with a `FATAL_ERROR` without it.
- [CMake 4.2+](https://cmake.org/download/) (or VS bundled CMake) — the
  `Visual Studio 18 2026` generator was added in CMake 4.2
- C++20 (`/std:c++20`, no compiler extensions) — set by the build
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

## Controls

| Key | Gamepad | Action |
|-----|---------|--------|
| `WASD` + `Q`/`E` | — | Fly-camera move |
| Right mouse | Right stick | Look |
| `Tab` | Start | Toggle walk mode (husky 0 is the capsule) |
| `WASD` | Left stick / D-pad | Walk, camera-relative |
| `Space` | A | Jump in walk mode — 2D beep otherwise |
| `Enter` | Y | Cycle game camera: follow / orbit / FPS |
| `Z` / `X` | — | Nudge the walker on world X |
| `F3` | — | Stats overlay: `P` poll / `X` extract / `E` execute / `G` GPU ms, plus AA mode |
| `F4` | — | Instance AABBs |
| `F5` | — | Cycle AA: **Off** (default) → FXAA → SMAA → TAA |
| `F11` | — | Toggle windowed / borderless |
| `Esc` | — | Quit |

Startup AA can also be set without a rebuild: `sandbox.exe --set r.aa=taa`
(`off` | `fxaa` | `smaa` | `taa`). See [docs/FOUNDATION.md](docs/FOUNDATION.md)
for the full cvar list.

**What the sandbox renders:** 63 huskies plus a checker floor in `scene::World`
(64 instances, frustum-culled at extract), lit by a directional sun with a
shadow, split-sum IBL, and one rim point light. A source cubemap sky fills
leftover pixels in HDR before ACES. Scene color is RGBA16; ACES writes LDR,
then the chosen AA pass before the swapchain. Shaders compile with Windows SDK
DXC (SM 6.0 / DXIL) on a worker thread for hot-reload. The husky mesh is glTF
(`/content/meshes/cartoon_husky.gltf`); the cube stays OBJ. Albedos upload a
full mip chain, sampling `/content/textures/husky/Cartoon_Husky_Albedo1.png` …
`Albedo4.png`.

**Gates (no interactive loop):**

```powershell
.\build\bin\Debug\sandbox.exe --gates
.\build\bin\Release\game.exe --gates
```

Exits `0` on pass, `1` on fail.

**Optional:** in **Debug**, `ENGINE_GPU_DEBUG=1` enables the D3D12 debug layer and shader debug flags. Release `game.exe` ignores it.

## Project layout

```
packages/
  core, math                          Layer 0 — foundation
  platform, rhi, shaders, assets,     Layer 1 — interfaces
  audio, physics
  *-win32, *-d3d12, *-dxc,            Layer 2 — implementations
  *-xaudio2, physics-cpu, assets-*
  renderer, debug-draw,               Layer 3 — systems
  scene, gameplay
  engine                              Layer 4 — runtime loop
  cook                                Tool — offline SOLC/SOLP cooker
  sandbox                             Dev harness
  game                                Player exe (Release install layout)
docs/                                 Architecture, roadmap, rules
```

`engine` depends on the interfaces plus `renderer` — **not** on `scene`,
`gameplay`, or `debug-draw`. The apps link those. That is why the renderer
never sees the scene.

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
- [docs/ROADMAP.md](docs/ROADMAP.md) — decision log: why each shipped feature was built the way it was
- [docs/ENGINE_MAP.md](docs/ENGINE_MAP.md) — the backlog. Pick one **Ready** row
- [docs/GPU_BASELINE.md](docs/GPU_BASELINE.md) — player GPU / OS / DLL baseline
- [docs/TODO_LATER.md](docs/TODO_LATER.md) — how to pick the next gate from the map

## License

MIT — see [LICENSE](LICENSE).

Third-party: [`cgltf`](https://github.com/jkuhlmann/cgltf) (MIT) is vendored at
`packages/assets-gltf/third_party/cgltf.h`; its licence notice is at the end of
that file. Everything else links only Windows system libraries.
