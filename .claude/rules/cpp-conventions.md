---
paths:
  - "packages/**/*.cpp"
  - "packages/**/*.hpp"
  - "packages/**/*.h"
  - "packages/**/*.hlsl"
  - "packages/**/*.hlsli"
  # The dependency-direction and interface/impl rules below apply mainly when
  # editing a package's build file, so they have to load there.
  - "packages/**/CMakeLists.txt"
  - "cmake/**"
---

# C++ conventions

From [Philosophy.md](../../Philosophy.md)'s Code section and
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)'s conventions — applies to
every package.

- RAII everywhere. No manual cleanup that a destructor could do.
- Prefer composition over inheritance; prefer data over objects.
- Ownership is explicit: `std::unique_ptr` for modules, injected at startup.
  No hidden global state; pass context explicitly instead of reaching for a
  singleton. Three deliberate exceptions: the logger (`g_logger` in
  `core/src/log.cpp`, set once at startup via `set_logger()`), the cvar
  registry (a function-local static, so static-initialisation order cannot
  bite), and `warn_physics_capacity`'s per-kind latch array (one `bool` per
  `CapacityKind`, each firing once per process).
- Minimize dynamic allocation; favor immutable data when practical.
- Headers live at `include/engine/<package>/<header>.hpp`. Implementation
  details live only in `src/`, never in a public header.
- Namespace `engine::`, with a sub-namespace per package — `engine::rhi`,
  `engine::scene`, `engine::physics`. Backends nest under their interface:
  `engine::rhi::d3d12`, `engine::platform::win32`, `engine::audio::xaudio2`.
  Two deliberate exceptions: `core` is bare `engine::` (foundation types), and
  `debug-draw` is `engine::debug`.
- One factory function per implementation package (e.g. `create_platform()`,
  `create_rhi()`). Prefer forward declarations; include only what a header
  needs.
- A package with swappable backends splits into `foo` (interfaces/types
  only) and `foo-bar` (one implementation). `foo` must not depend on any
  `foo-bar`.
