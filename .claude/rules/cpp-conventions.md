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

- **Never run `clang-format` over an existing file, and never bulk-reformat.**
  The formatting is hand-tuned per site (aligned `case` returns, breaks chosen
  for readability, grouped initialiser lists), so a formatter destroys
  information rather than normalising it — measured 1 Sep 2026, 92 of 123 C++
  files and 1,961 sites diverge even with the config tuned toward house style.
  Match the lines around the ones you touch, by hand.
  - The root `.clang-format` is `DisableFormat: true`, so clang-format is a
    verified no-op and Visual Studio's format-as-you-type cannot rewrite the
    tree. That is a mechanism, not a request: a comment saying "descriptive
    only" was ignored by both IDEs, which is why this file says what it says.
  - The house style for a **new** file is `tools/house-style.clang-format`,
    applied explicitly:
    `clang-format --style=file:tools/house-style.clang-format -i <newfile>`.
  - What *is* enforced is `.editorconfig` — indent, 100-column limit, final
    newline, no trailing whitespace, LF (CRLF for `.ps1`/`.bat`/`.cmd`) —
    checked by the `format-hygiene` invariant on every push.
- RAII everywhere. No manual cleanup that a destructor could do.
- Prefer composition over inheritance; prefer data over objects.
- Ownership is explicit: `std::unique_ptr` for modules, injected at startup.
  No hidden global state; pass context explicitly instead of reaching for a
  singleton. Five deliberate exceptions: the logger (`g_logger` in
  `core/src/log.cpp`, set once at startup via `set_logger()`), the profiler
  (`g_profiler` and `g_frame_profiler` in `core/src/profile.cpp`, set once at
  startup via `set_profiler()` — the same shape as the logger), the cvar
  registry (a function-local static, so static-initialisation order cannot
  bite), `install_file_logger`'s install-once statics (`core/src/log_file.cpp`,
  so the sink outlives every caller), and the warn-once latches — a `bool` per
  `CapacityKind` in `warn_physics_capacity`, and one for the profiler's full
  scope table — each firing once per process.
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
