---
paths:
  - "packages/**/*.cpp"
  - "packages/**/*.hpp"
  - "packages/**/*.h"
---

# C++ conventions

From [Philosophy.md](../../Philosophy.md)'s Code section and
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)'s conventions — applies to
every package.

- RAII everywhere. No manual cleanup that a destructor could do.
- Prefer composition over inheritance; prefer data over objects.
- Ownership is explicit: `std::unique_ptr` for modules, injected at startup.
  No hidden global state; pass context explicitly instead of reaching for a
  singleton.
- Minimize dynamic allocation; favor immutable data when practical.
- Headers live at `include/engine/<package>/<header>.hpp`. Implementation
  details live only in `src/`, never in a public header.
- Namespace `engine::`, with a sub-namespace per package.
- One factory function per implementation package (e.g. `create_platform()`,
  `create_rhi()`). Prefer forward declarations; include only what a header
  needs.
- A package with swappable backends splits into `foo` (interfaces/types
  only) and `foo-bar` (one implementation). `foo` must not depend on any
  `foo-bar`.
