# Engine

[![CI](https://github.com/mathiassol/Sol-Engine/actions/workflows/ci.yml/badge.svg)](https://github.com/mathiassol/Sol-Engine/actions/workflows/ci.yml)

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
  `Visual Studio 18 2026` generator was added in CMake 4.2. The project's own
  floor is `cmake_minimum_required(VERSION 3.24)`, so an older CMake works with
  a different generator; 4.2 is what the *documented* build needs.
- C++20 (`/std:c++20`, no compiler extensions) — set by the build
- Git
- [PowerShell 7](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows)
  (`pwsh`) — not part of a base Windows install, and what the documented
  invariant command below uses. Installing it is also what makes that command
  *work*: PowerShell 7 sets the machine execution policy to `RemoteSigned`,
  while Windows PowerShell 5.1 leaves it `Undefined`, which resolves to
  `Restricted` on client Windows and refuses to run a local unsigned script at
  all. The script itself is 5.1-compatible — and faster there, 3.2 s against
  9.1 s — but under 5.1 it needs the policy spelled out:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-invariants.ps1`.

**Clone and build**

```powershell
git clone <your-remote-url> Engine
cd Engine

pwsh -NoProfile -File tools/check-prereqs.ps1
```

That prints one line per prerequisite — found with its version, or missing with
its name and where to get it — and exits non-zero if the documented build cannot
work yet. Run it first; it is faster than reading a CMake error about CMake.

```powershell
cmake --preset vs2026
cmake --build --preset debug
```

`CMakePresets.json` is the one build definition — the CLI, Visual Studio, VS
Code and CI all read it, so they cannot drift apart. `cmake --list-presets`
shows all of them. The raw equivalent still works if you would rather not use
presets:

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Clone somewhere short. A source path over about **140 characters** breaks
configure on Windows: the build generates paths up to 119 characters of its own,
and CMake's try-compile goes deeper still, so MSBuild hits the 260-character
`MAX_PATH` limit and fails with an error that names nothing about this project.
`cmake` warns at configure time if you are close. Enabling
[long paths](https://learn.microsoft.com/windows/win32/fileio/maximum-file-path-limitation)
also works.

**Cold clone to running**, measured 1 Sep 2026 on a Windows 11 desktop at a
42-character path: configure 4.7 s, Debug build 68 s, Release `game` 75 s,
no-op rebuild 8.6 s. `build/` reaches 279 MB with both configurations present;
the working tree is 13.6 MB.

**Ninja preset** (optional). `cmake --preset ninja` builds the same tree ~2.6×
faster — 26 s cold Debug against 68 s — and is the only preset that produces
`build-ninja/compile_commands.json`, which clangd and other compile-database
tools need; the Visual Studio generator does not write one. Ninja ships with
Visual Studio, so it is not an extra prerequisite, but it needs `cl.exe` on
`PATH`: Visual Studio and VS Code arrange that when they open the folder, and
from a terminal you need a Developer Command Prompt. The Visual Studio preset
stays the documented default because it needs none of that.

**Run sandbox** — content mounts are resolved next to the executable
(`build/bin/Debug`, layout `install`), which CMake refreshes on every build;
works from any CWD. Shader edits under `packages/sandbox/content/` therefore
need a rebuild to be picked up.

```powershell
.\build\bin\Debug\sandbox.exe
```

`run.bat` at the repo root does the same interactively — it prompts for GPU
debug and any extra arguments (`--gates`, `--set r.aa=taa`), checks the binary
exists and tells you how to build it if not, then prints the exit code.

**Ship / play `game.exe`** — Release, no GPU debug layer, content,
`content.pak`, and DXC DLLs (`dxcompiler.dll`, `dxil.dll`) next to the exe.
D3D12 comes from Windows; Agility is not shipped.

```powershell
cmake --build --preset release-game
.\build\bin\Release\game.exe
```

Optional install layout (same files, chosen prefix):

```powershell
cmake --install build --config Release --prefix dist
.\dist\game.exe
```

**Ship a build** — the same layout, zipped:

```powershell
cmake --build --preset release-game
cpack --config build/CPackConfig.cmake -C Release
```

`build/package/Sol-<version>-win64.zip`, about 11 MB. It unzips to one
directory containing `game.exe`, `content/`, `debug/`, `content.pak`,
`dxcompiler.dll`, `dxil.dll` and `LICENSE` — and nothing else: no `.pdb`, no
import libraries, and **no Visual C++ redistributable**, because the CRT is
linked statically. A player unzips it and runs `game.exe`.

**Cut a release** — push a `v*` tag and
[`release.yml`](.github/workflows/release.yml) configures, builds, runs the
invariants, packs, checks the archive is self-contained, and attaches it to a
GitHub Release:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

Rehearse first with **Run workflow** on the Actions tab — a `workflow_dispatch`
run does everything except publish, and leaves the zip as a run artifact. Tags
are permanent; a broken first release is awkward to withdraw.

Run `--gates` locally before you tag. CI cannot: a hosted runner has no hardware
D3D12 adapter, so a release is only as gate-verified as the last local run on
that commit.

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
| `F3` | — | Stats overlay: `P` poll / `X` extract / `E` execute / `G` GPU ms, `R` frame-ring peak as % of capacity, plus AA mode |
| `F4` | — | Instance AABBs |
| `F5` | — | Cycle AA: **Off** (default) → FXAA → SMAA → TAA |
| `F11` | — | Toggle windowed / borderless |
| `Esc` | — | Quit |

Startup AA can also be set without a rebuild: `sandbox.exe --set r.aa=taa`
(`off` | `fxaa` | `smaa` | `taa`). See [docs/FOUNDATION.md](docs/FOUNDATION.md)
for the full cvar list.

**What the sandbox renders:** 63 huskies plus a checker floor in `scene::World`
(the scene holds up to 512; all are frustum-culled at extract), lit by a directional sun with a
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

**Invariant checks** — no compiler or GPU needed, and what CI runs:

```powershell
pwsh -NoProfile -File tools/check-invariants.ps1
```

Without PowerShell 7, add the policy the shipped shell does not grant itself:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-invariants.ps1`.

CI compiles Debug and Release `game`, configures with each `ENGINE_*` option
off, and runs the invariant checks under both shells. It does **not** run
`--gates`: the D3D12 backend skips software adapters, and hosted runners have no
hardware one.
Run the gates locally before pushing.

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
- [docs/PICKING.md](docs/PICKING.md) — how to choose the next row from the map

## License

MIT — see [LICENSE](LICENSE).

Third-party: [`cgltf`](https://github.com/jkuhlmann/cgltf) (MIT) is vendored at
`packages/assets-gltf/third_party/cgltf.h`; its licence notice is at the end of
that file. Everything else links only Windows system libraries.
